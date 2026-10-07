#include "DxTimingCollector.h"
#include "DxCaptureTransport.h"

#include <Windows.h>
#include <evntrace.h>
#include <evntcons.h>
#include <intrin.h>

#include <DxTimingCaptureLibrary/DxTimingCaptureEventHandler.h>
#include <DxTimingCaptureLibrary/EtwException.h>
#include <DxTimingCaptureLibrary/EtwProviders.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>

namespace ce::dx_capture
{
    namespace
    {
        constexpr std::uint64_t nanoseconds_per_second = 1'000'000'000;
        constexpr ULONGLONG maximum_capture_ms = 60'000;
        constexpr ULONG provider_timeout_ms = 500;
        constexpr std::size_t maximum_executions = 4096;
        constexpr std::uint64_t target_execution_bit = std::uint64_t{1} << 63;
        constexpr std::uint64_t maximum_decoded_events = 1'000'000;
        constexpr std::uint64_t maximum_decoded_bytes = 128 * 1024 * 1024;
        constexpr SIZE_T maximum_process_bytes = 256 * 1024 * 1024;

        bool scale_clock(std::uint64_t value, std::uint64_t multiplier, std::uint64_t divisor,
                         std::uint64_t& result, std::uint64_t& remainder) noexcept
        {
            std::uint64_t high = 0;
            const std::uint64_t low = _umul128(value, multiplier, &high);
            if (divisor == 0 || high >= divisor)
            {
                return false;
            }
            result = _udiv128(high, low, divisor, &remainder);
            return true;
        }

        // ETW가 버퍼를 비운 뒤에도 이 저장소만 사용한다. 콜백은 pipe/lock/할당을 기다리지 않는다.
        class record_queue
        {
        public:
            bool push(const record& value) noexcept
            {
                const auto write = write_.load(std::memory_order_relaxed);
                const auto read = read_.load(std::memory_order_acquire);
                if (write - read >= values_.size())
                {
                    return false;
                }
                values_[write % values_.size()] = value;
                write_.store(write + 1, std::memory_order_release);
                return true;
            }

            bool pop(record& value) noexcept
            {
                const auto read = read_.load(std::memory_order_relaxed);
                const auto write = write_.load(std::memory_order_acquire);
                if (read == write)
                {
                    return false;
                }
                value = values_[read % values_.size()];
                read_.store(read + 1, std::memory_order_release);
                return true;
            }

            std::uint64_t pending() const noexcept
            {
                return write_.load(std::memory_order_acquire) - read_.load(std::memory_order_acquire);
            }

        private:
            std::array<record, maximum_queued_records> values_{};
            alignas(64) std::atomic<std::uint64_t> write_{0};
            alignas(64) std::atomic<std::uint64_t> read_{0};
        };

        struct trace_properties
        {
            EVENT_TRACE_PROPERTIES properties{};
            wchar_t name[256]{};
        };
    }

    struct timing_collector::state final : DirectX::Etw::TimestampConverter,
                                          DirectX::Etw::GpuTimingsCallbacks,
                                          DirectX::Etw::DxgkObjectCallbacks,
                                          DirectX::Etw::DiagnosticsSink
    {
        record_queue queue_;
        std::array<record, maximum_executions> executions_{};
        std::unique_ptr<DirectX::Etw::DxTimingCaptureEventHandler> handler_;
        std::thread consumer_;
        trace_properties properties_{};
        EVENT_TRACE_LOGFILEW logfile_{};
        TRACEHANDLE session_ = 0;
        TRACEHANDLE trace_ = INVALID_PROCESSTRACE_HANDLE;
        HANDLE memory_job_ = nullptr;
        bool owns_session_ = false;
        bool capture_started_ = false;
        std::atomic<bool> consumer_done_{false};
        std::atomic<std::uint32_t> fatal_error_{ERROR_SUCCESS};
        std::atomic<std::uint64_t> dropped_{0};
        std::atomic<std::uint64_t> etw_events_lost_{0};
        std::atomic<std::uint64_t> etw_buffers_lost_{0};
        std::atomic<bool> incomplete_data_{false};
        std::uint32_t target_process_id_ = 0;
        std::uint64_t frequency_ = 0;
        std::uint64_t capture_begin_ = 0;
        ULONGLONG capture_deadline_ = 0;
        std::uint64_t next_id_ = 0;
        INT64 last_timestamp_ns_ = 0;
        std::uint64_t decoded_events_ = 0;
        std::uint64_t decoded_bytes_ = 0;

        ~state()
        {
            stop_session();
            // 예외/전송 단절 경로에서도 ETW의 마지막 콜백보다 상태가 먼저 파괴되지 않는다.
            close_consumer();
            if (consumer_.joinable())
            {
                consumer_.join();
            }
            if (memory_job_ != nullptr)
            {
                CloseHandle(memory_job_);
            }
        }

        INT64 GetLastEventTimeStamp() const override { return last_timestamp_ns_; }
        INT64 GetHighPerformanceFrequency() const override { return static_cast<INT64>(frequency_); }

        INT64 ConvertClockToTimeStamp(INT64 count) const override
        {
            std::uint64_t result = 0;
            std::uint64_t remainder = 0;
            if (count < 0 || !scale_clock(static_cast<std::uint64_t>(count), nanoseconds_per_second, frequency_,
                                          result, remainder) ||
                result > static_cast<std::uint64_t>(std::numeric_limits<INT64>::max()))
            {
                throw std::overflow_error("QPC to nanoseconds overflow");
            }
            return static_cast<INT64>(result);
        }

        bool to_qpc(INT64 timestamp, bool cpuTimestamp, std::uint64_t& result) const noexcept
        {
            std::uint64_t remainder = 0;
            if (timestamp < 0 || !scale_clock(static_cast<std::uint64_t>(timestamp), frequency_, nanoseconds_per_second,
                                              result, remainder))
            {
                return false;
            }
            // upstream은 GPU 보정값에 ns 차이를 더하므로 identity converter를 쓰면 안 된다.
            // f <= 1 GHz일 때 ceil(floor(qpc * 1e9 / f) * f / 1e9)는 원래 CPU QPC다.
            // GPU는 ns 원본을 함께 남기고 가장 가까운 QPC tick으로 보정한다.
            const bool increment = cpuTimestamp ? remainder != 0 : remainder >= nanoseconds_per_second / 2;
            if (increment && result == std::numeric_limits<std::uint64_t>::max())
            {
                return false;
            }
            result += increment ? 1 : 0;
            return true;
        }

        void set_failure(std::uint32_t error) noexcept
        {
            std::uint32_t expected = ERROR_SUCCESS;
            fatal_error_.compare_exchange_strong(expected, error, std::memory_order_relaxed);
        }

        bool emit(const record& value) noexcept
        {
            if (queue_.push(value))
            {
                return true;
            }
            dropped_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }

        UINT64 allocate_id()
        {
            if (++next_id_ >= target_execution_bit)
            {
                throw std::overflow_error("Capture identifier overflow");
            }
            return next_id_;
        }

        record* find_execution(UINT64 executionId) noexcept
        {
            if ((executionId & target_execution_bit) == 0)
            {
                return nullptr;
            }
            auto& execution = executions_[executionId % executions_.size()];
            if (execution.execution_id != executionId)
            {
                dropped_.fetch_add(1, std::memory_order_relaxed);
                return nullptr;
            }
            return &execution;
        }

        HRESULT OnGpuExecutionBegin(UINT32 processId, UINT32 threadId, UINT64 apiCommandQueueId,
                                    INT64 cpuSubmitTime, UINT64 presentToken, UINT64* gpuExecutionId) override
        {
            // 버린 PID도 고유 ID를 받는다. 후속 work가 대상 PID의 실행으로 섞이지 않는다.
            *gpuExecutionId = allocate_id();
            if (processId != target_process_id_)
            {
                return S_OK;
            }
            *gpuExecutionId |= target_execution_bit;
            auto& execution = executions_[*gpuExecutionId % executions_.size()];
            if (execution.execution_id != 0)
            {
                dropped_.fetch_add(1, std::memory_order_relaxed);
                return S_OK;
            }
            execution = {};
            execution.kind = record_kind::gpu_execution_begin;
            execution.process_id = processId;
            execution.thread_id = threadId;
            execution.execution_id = *gpuExecutionId;
            execution.api_queue_id = apiCommandQueueId;
            execution.present_token = presentToken;
            if (cpuSubmitTime > 0)
            {
                if (!to_qpc(cpuSubmitTime, true, execution.cpu_submit_qpc))
                {
                    return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
                }
                execution.source_cpu_submit_ns = static_cast<std::uint64_t>(cpuSubmitTime);
                execution.flags |= has_cpu_submit;
            }
            else
            {
                execution.flags |= incomplete;
            }
            if (!emit(execution))
            {
                execution.flags |= incomplete | transport_loss;
            }
            return S_OK;
        }

        HRESULT OnGpuWork(UINT64 hardwareCommandQueueId, UINT64 gpuExecutionId, UINT32 commandListIndexInExecution,
                          UINT64 apiMarkerId, INT64 beginTimestamp, INT64 endTimestamp) override
        {
            record* execution = find_execution(gpuExecutionId);
            if (!execution)
            {
                return S_OK;
            }
            record work = *execution;
            work.kind = record_kind::gpu_work;
            work.hardware_queue_id = hardwareCommandQueueId;
            work.command_list_index = commandListIndexInExecution;
            work.api_marker_id = apiMarkerId;
            work.flags |= raw_api_marker_ids | pix_markers_unavailable | calibrated_qpc;
            if (endTimestamp < beginTimestamp || !to_qpc(beginTimestamp, false, work.qpc_begin) ||
                !to_qpc(endTimestamp, false, work.qpc_end))
            {
                return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
            }
            work.source_begin_ns = static_cast<std::uint64_t>(beginTimestamp);
            work.source_end_ns = static_cast<std::uint64_t>(endTimestamp);
            const bool firstWork = (execution->flags & calibrated_qpc) == 0;
            if (firstWork || work.qpc_begin < execution->qpc_begin)
            {
                execution->qpc_begin = work.qpc_begin;
            }
            if (firstWork || work.source_begin_ns < execution->source_begin_ns)
            {
                execution->source_begin_ns = work.source_begin_ns;
            }
            if (firstWork || work.qpc_end > execution->qpc_end)
            {
                execution->qpc_end = work.qpc_end;
            }
            if (firstWork || work.source_end_ns > execution->source_end_ns)
            {
                execution->source_end_ns = work.source_end_ns;
            }
            execution->flags |= calibrated_qpc;
            if (!emit(work))
            {
                execution->flags |= incomplete | transport_loss;
            }
            return S_OK;
        }

        HRESULT OnGpuExecutionComplete(UINT64 gpuExecutionId) override
        {
            if (record* execution = find_execution(gpuExecutionId))
            {
                execution->kind = record_kind::gpu_execution_end;
                execution->flags |= calibrated_qpc;
                if (execution->qpc_begin == 0)
                {
                    execution->flags |= incomplete;
                }
                emit(*execution);
                *execution = {};
            }
            return S_OK;
        }

        HRESULT OnApiMarker(UINT32, UINT32, PCWSTR, INT64, UINT64* apiMarkerId) override
        {
            *apiMarkerId = allocate_id();
            return S_OK;
        }

        HRESULT OnCommandListName(UINT64, UINT32, PCWSTR) override { return S_OK; }
        HRESULT OnHardwareAdapter(UINT64* adapterId) override
        {
            *adapterId = allocate_id();
            return S_OK;
        }
        HRESULT OnHardwareAdapterName(UINT64, PCWSTR) override { return S_OK; }
        HRESULT OnHardwareCommandQueue(UINT64, UINT64* hardwareCommandQueueId) override
        {
            *hardwareCommandQueueId = allocate_id();
            return S_OK;
        }
        HRESULT OnHardwareCommandQueueName(UINT64, PCWSTR) override { return S_OK; }
        HRESULT OnApiCommandQueue(UINT32, UINT64, PCWSTR, INT64, UINT64* apiCommandQueueId) override
        {
            // upstream callback ID는 세션 로컬 식별자다. 엔진의 네이티브 queue 주소가 아니다.
            *apiCommandQueueId = allocate_id();
            return S_OK;
        }
        HRESULT OnApiCommandQueueName(UINT64, PCWSTR) override { return S_OK; }
        HRESULT OnApiCommandQueueEnd(UINT64, INT64) override { return S_OK; }

        void OnDiagnostic(DirectX::Etw::DiagnosticSeverity, DirectX::Etw::DiagnosticCode code,
                          std::wstring_view) override
        {
            incomplete_data_.store(true, std::memory_order_relaxed);
            record diagnostic{};
            diagnostic.process_id = target_process_id_;
            diagnostic.flags = incomplete;
            if (code == DirectX::Etw::DiagnosticCode::EventsLost || code == DirectX::Etw::DiagnosticCode::BuffersLost)
            {
                diagnostic.status = status_code::events_lost;
                diagnostic.flags |= etw_loss;
                diagnostic.etw_events_lost = etw_events_lost_.load(std::memory_order_relaxed);
                diagnostic.etw_buffers_lost = etw_buffers_lost_.load(std::memory_order_relaxed);
            }
            else
            {
                diagnostic.status = status_code::malformed_event;
            }
            emit(diagnostic);
        }

        static void WINAPI receive_record(PEVENT_RECORD value) noexcept
        {
            auto* self = static_cast<state*>(value->UserContext);
            if (!self || self->fatal_error_.load(std::memory_order_relaxed) != ERROR_SUCCESS)
            {
                return;
            }
            const GUID& provider = value->EventHeader.ProviderId;
            if (provider != Direct3D12EtwProviderGuid && provider != DxgkControlGuid)
            {
                // PnP 이름·PIX·DirectStorage는 이 캡처의 범위가 아니다. 불필요한
                // 문자열 decoder 경로까지 상승 프로세스의 입력 표면으로 열지 않는다.
                return;
            }
            if (provider == Direct3D12EtwProviderGuid && value->EventHeader.ProcessId != self->target_process_id_)
            {
                return;
            }
            try
            {
                // 시간 상한만으로는 upstream의 미완료 실행 map/vector 크기를 제한하지 못한다.
                const std::uint64_t bytes = sizeof(EVENT_RECORD) + value->UserDataLength +
                    static_cast<std::uint64_t>(value->ExtendedDataCount) * sizeof(EVENT_HEADER_EXTENDED_DATA_ITEM);
                if (++self->decoded_events_ > maximum_decoded_events ||
                    bytes > maximum_decoded_bytes - self->decoded_bytes_)
                {
                    self->dropped_.fetch_add(1, std::memory_order_relaxed);
                    self->set_failure(ERROR_NOT_ENOUGH_QUOTA);
                    return;
                }
                self->decoded_bytes_ += bytes;
                const INT64 timestamp = self->ConvertClockToTimeStamp(value->EventHeader.TimeStamp.QuadPart);
                self->last_timestamp_ns_ = std::max(self->last_timestamp_ns_, timestamp);
                // DXGK 헤더의 PID는 커널 작업일 수 있다. 상관에 필요한 전역 메타데이터는
                // decoder에 주고, OnGpuExecutionBegin에서 결과의 실제 PID를 다시 거른다.
                self->handler_->HandleEventRecord(value);
            }
            catch (const DirectX::Etw::EtwException& error)
            {
                self->set_failure(static_cast<std::uint32_t>(error.GetErrorCode()));
            }
            catch (const std::bad_alloc&)
            {
                self->set_failure(ERROR_NOT_ENOUGH_MEMORY);
            }
            catch (...)
            {
                self->set_failure(ERROR_INVALID_DATA);
            }
        }

        void report_statistics(const EVENT_TRACE_LOGFILEW& value)
        {
            etw_events_lost_.store(std::max(etw_events_lost_.load(std::memory_order_relaxed),
                                           static_cast<std::uint64_t>(value.LogfileHeader.EventsLost)),
                                   std::memory_order_relaxed);
            etw_buffers_lost_.store(std::max(etw_buffers_lost_.load(std::memory_order_relaxed),
                                            static_cast<std::uint64_t>(value.LogfileHeader.BuffersLost)),
                                    std::memory_order_relaxed);
            handler_->ReportTraceStatistics(value);
        }

        static ULONG WINAPI receive_buffer(PEVENT_TRACE_LOGFILEW value) noexcept
        {
            auto* self = static_cast<state*>(value->Context);
            if (!self)
            {
                return FALSE;
            }
            try
            {
                self->report_statistics(*value);
            }
            catch (...)
            {
                self->set_failure(ERROR_INVALID_DATA);
            }
            return self->fatal_error_.load(std::memory_order_relaxed) == ERROR_SUCCESS ? TRUE : FALSE;
        }

        ULONG startup_command(transport& channel)
        {
            const command request = channel.try_read_command();
            if (request == command::disconnected || !channel.parent_alive())
            {
                return ERROR_BROKEN_PIPE;
            }
            if (request == command::stop)
            {
                return ERROR_CANCELLED;
            }
            if (request == command::start)
            {
                return ERROR_INVALID_DATA;
            }
            return fatal_error_.load(std::memory_order_relaxed);
        }

        ULONG enable_provider(transport& channel, const GUID& provider, UCHAR level, ULONGLONG keywords)
        {
            ULONG result = startup_command(channel);
            if (result != ERROR_SUCCESS)
            {
                return result;
            }
            ENABLE_TRACE_PARAMETERS parameters{};
            parameters.Version = ENABLE_TRACE_PARAMETERS_VERSION_2;
            result = EnableTraceEx2(session_, &provider, EVENT_CONTROL_CODE_ENABLE_PROVIDER, level, keywords,
                                    0, provider_timeout_ms, &parameters);
            if (result == ERROR_SUCCESS)
            {
                result = startup_command(channel);
            }
            if (result == ERROR_SUCCESS)
            {
                result = EnableTraceEx2(session_, &provider, EVENT_CONTROL_CODE_CAPTURE_STATE, TRACE_LEVEL_VERBOSE,
                                        0, 0, provider_timeout_ms, nullptr);
            }
            return result;
        }

        ULONG start(transport& channel)
        {
            LARGE_INTEGER frequency{};
            LARGE_INTEGER begin{};
            if (!QueryPerformanceFrequency(&frequency) || !QueryPerformanceCounter(&begin) || frequency.QuadPart <= 0 ||
                frequency.QuadPart > static_cast<INT64>(nanoseconds_per_second) || begin.QuadPart <= 0)
            {
                return ERROR_NOT_SUPPORTED;
            }
            frequency_ = static_cast<std::uint64_t>(frequency.QuadPart);
            capture_begin_ = static_cast<std::uint64_t>(begin.QuadPart);
            capture_deadline_ = GetTickCount64() + maximum_capture_ms;
            target_process_id_ = channel.target_process_id();

            // 이미 다른 job에 속한 경우에도 breakaway를 요청하지 않는다. 이 상한을
            // 적용할 수 없으면 캡처를 거절하여 외부 이벤트 parser의 무제한 할당을 피한다.
            memory_job_ = CreateJobObjectW(nullptr, nullptr);
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
            limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_PROCESS_MEMORY;
            limits.ProcessMemoryLimit = maximum_process_bytes;
            if (memory_job_ == nullptr ||
                !SetInformationJobObject(memory_job_, JobObjectExtendedLimitInformation, &limits, sizeof(limits)) ||
                !AssignProcessToJobObject(memory_job_, GetCurrentProcess()))
            {
                return ERROR_NOT_SUPPORTED;
            }

            DirectX::Etw::DxTimingCaptureLibraryOptions options{};
            options.TrackGpuTiming = true;
            options.TrackApiObjects = false;
            options.TrackGpuEngineActivity = false;
            DirectX::Etw::DxTimingCaptureEventCallbacks callbacks{};
            callbacks.GpuTimingsCallbacks = this;
            callbacks.DxgkObjectCallbacks = this;
            callbacks.DiagnosticsSink = this;
            handler_ = DirectX::Etw::DxTimingCaptureEventHandler::Create(target_process_id_, options, callbacks, *this);

            const std::wstring name = channel.session_name();
            if (name.empty() || name.size() >= std::size(properties_.name))
            {
                return ERROR_INVALID_PARAMETER;
            }
            auto& properties = properties_.properties;
            properties.Wnode.BufferSize = sizeof(properties_);
            properties.Wnode.Flags = WNODE_FLAG_TRACED_GUID;
            properties.Wnode.ClientContext = 1;
            properties.BufferSize = 64;
            properties.MinimumBuffers = 16;
            properties.MaximumBuffers = 64;
            properties.FlushTimer = 1;
            properties.LogFileMode = EVENT_TRACE_REAL_TIME_MODE | EVENT_TRACE_NO_PER_PROCESSOR_BUFFERING;
            properties.LoggerNameOffset = offsetof(trace_properties, name);
            std::memcpy(properties_.name, name.c_str(), (name.size() + 1) * sizeof(wchar_t));
            ULONG result = StartTraceW(&session_, properties_.name, &properties);
            if (result != ERROR_SUCCESS)
            {
                // ERROR_ALREADY_EXISTS는 소유권이 아니다. 기존 세션을 열거나 중지하지 않는다.
                session_ = 0;
                return result;
            }
            owns_session_ = true;

            logfile_.LoggerName = properties_.name;
            logfile_.ProcessTraceMode = PROCESS_TRACE_MODE_REAL_TIME | PROCESS_TRACE_MODE_EVENT_RECORD |
                                       PROCESS_TRACE_MODE_RAW_TIMESTAMP;
            logfile_.EventRecordCallback = &receive_record;
            logfile_.BufferCallback = &receive_buffer;
            logfile_.Context = this;
            trace_ = OpenTraceW(&logfile_);
            if (trace_ == INVALID_PROCESSTRACE_HANDLE)
            {
                return GetLastError();
            }

            const TRACEHANDLE consumerHandle = trace_;
            consumer_ = std::thread([this, consumerHandle]()
            {
                TRACEHANDLE handle = consumerHandle;
                const ULONG result = ProcessTrace(&handle, 1, nullptr, nullptr);
                if (result != ERROR_SUCCESS && result != ERROR_CANCELLED)
                {
                    set_failure(result);
                }
                try
                {
                    report_statistics(logfile_);
                }
                catch (...)
                {
                    set_failure(ERROR_INVALID_DATA);
                }
                try
                {
                    handler_->OnDataComplete();
                }
                catch (const DirectX::Etw::EtwException& error)
                {
                    set_failure(static_cast<std::uint32_t>(error.GetErrorCode()));
                }
                catch (...)
                {
                    set_failure(ERROR_INVALID_DATA);
                }
                consumer_done_.store(true, std::memory_order_release);
            });

            constexpr ULONGLONG d3d12Keywords = D3D12_ETW_LOG_FLAGS_RENDER_OPERATION_MARKERS |
                D3D12_ETW_LOG_FLAGS_DRIVER_CUSTOM_MARKERS | D3D12_ETW_LOG_FLAGS_BUNDLE_MARKERS |
                D3D12_ETW_LOG_FLAGS_NAMES | D3D12_ETW_LOG_FLAGS_DEVICES | D3D12_ETW_LOG_FLAGS_OBJECT_LIFETIME |
                D3D12_ETW_LOG_FLAGS_JOURNAL_ENTRIES;
            constexpr ULONGLONG dxgkKeywords = DXGK_KEYWORD_LOG_FLAGS_LONG_HAUL | DXGK_KEYWORD_LOG_FLAGS_BASE |
                DXGK_KEYWORD_LOG_FLAGS_PROFILER | DXGK_KEYWORD_LOG_FLAGS_HISTORY_BUFFER |
                DXGK_KEYWORD_LOG_FLAGS_ALLOCATIONS_REFERENCES;
            result = enable_provider(channel, Direct3D12EtwProviderGuid, TRACE_LEVEL_RESERVED6, d3d12Keywords);
            if (result == ERROR_SUCCESS)
            {
                result = enable_provider(channel, DxgkControlGuid, TRACE_LEVEL_VERBOSE, dxgkKeywords);
            }
            if (result == ERROR_SUCCESS)
            {
                result = startup_command(channel);
            }
            if (result == ERROR_SUCCESS && consumer_done_.load(std::memory_order_acquire))
            {
                result = ERROR_OPERATION_ABORTED;
            }
            capture_started_ = result == ERROR_SUCCESS;
            return result;
        }

        ULONG stop_session() noexcept
        {
            if (!owns_session_)
            {
                return ERROR_SUCCESS;
            }
            // 핸들로만 자기 세션을 중지한다. 이름으로 다른 세션을 찾는 fallback은 없다.
            properties_.properties.Wnode.BufferSize = sizeof(properties_);
            properties_.properties.Wnode.Flags = WNODE_FLAG_TRACED_GUID;
            properties_.properties.LogFileNameOffset = 0;
            const ULONG result = ControlTraceW(session_, nullptr, &properties_.properties, EVENT_TRACE_CONTROL_STOP);
            if (result == ERROR_SUCCESS || result == ERROR_WMI_INSTANCE_NOT_FOUND ||
                result == ERROR_MORE_DATA || result == ERROR_ACTIVE_CONNECTIONS)
            {
                // MORE_DATA는 이미 중지됨, ACTIVE_CONNECTIONS는 중지 진행 중이다.
                // 이 핸들로 다시 STOP하면 소유권을 잃은 세션을 건드릴 수 있다.
                owns_session_ = false;
                session_ = 0;
            }
            return result;
        }

        void close_consumer() noexcept
        {
            if (trace_ != INVALID_PROCESSTRACE_HANDLE)
            {
                CloseTrace(trace_);
                trace_ = INVALID_PROCESSTRACE_HANDLE;
            }
        }

        record status_record(const transport& channel, status_code status, std::uint32_t error = ERROR_SUCCESS) const
        {
            record value{};
            value.status = status;
            value.win32_error = error;
            value.process_id = channel.target_process_id();
            value.process_creation_time = channel.target_creation_time();
            value.windows_session_id = channel.windows_session_id();
            value.qpc_frequency = frequency_;
            value.flags = raw_api_marker_ids | pix_markers_unavailable;
            if (incomplete_data_.load(std::memory_order_relaxed))
            {
                value.flags |= incomplete;
            }
            value.loss_count = dropped_.load(std::memory_order_relaxed);
            value.etw_events_lost = etw_events_lost_.load(std::memory_order_relaxed);
            value.etw_buffers_lost = etw_buffers_lost_.load(std::memory_order_relaxed);
            if (value.loss_count != 0)
            {
                value.flags |= incomplete | transport_loss;
            }
            if (value.etw_events_lost != 0 || value.etw_buffers_lost != 0)
            {
                value.flags |= incomplete | etw_loss;
            }
            return value;
        }

        ULONG run(transport& channel)
        {
            const ULONG startResult = start(channel);
            bool connected = channel.parent_alive();
            if (startResult == ERROR_SUCCESS)
            {
                record session = status_record(channel, status_code::capturing);
                session.kind = record_kind::session;
                session.qpc_begin = capture_begin_;
                connected = connected && channel.send_record(session);
                if (connected)
                {
                    connected = channel.send_record(status_record(channel, status_code::capturing));
                }
            }

            while (startResult == ERROR_SUCCESS && connected && GetTickCount64() < capture_deadline_ &&
                   !consumer_done_.load(std::memory_order_acquire) &&
                   fatal_error_.load(std::memory_order_relaxed) == ERROR_SUCCESS)
            {
                const command request = channel.try_read_command();
                connected = request != command::disconnected && channel.parent_alive();
                if (!connected || request == command::stop)
                {
                    break;
                }
                if (request == command::start)
                {
                    set_failure(ERROR_INVALID_DATA);
                    break;
                }
                record value{};
                if (queue_.pop(value))
                {
                    connected = channel.send_record(value);
                }
                else
                {
                    Sleep(2);
                }
            }

            const ULONG stopResult = stop_session();
            if (stopResult != ERROR_SUCCESS)
            {
                incomplete_data_.store(true, std::memory_order_relaxed);
            }
            if (stopResult != ERROR_SUCCESS && stopResult != ERROR_WMI_INSTANCE_NOT_FOUND)
            {
                set_failure(stopResult);
            }
            const ULONGLONG drainDeadline = GetTickCount64() + shutdown_timeout_ms;
            while (consumer_.joinable() && !consumer_done_.load(std::memory_order_acquire) &&
                   GetTickCount64() < drainDeadline)
            {
                if (connected && capture_started_)
                {
                    connected = channel.try_read_command() != command::disconnected && channel.parent_alive();
                    record value{};
                    if (connected && queue_.pop(value))
                    {
                        connected = channel.send_record(value);
                    }
                    else
                    {
                        Sleep(2);
                    }
                }
                else
                {
                    Sleep(2);
                }
            }
            if (consumer_.joinable() && !consumer_done_.load(std::memory_order_acquire))
            {
                set_failure(ERROR_TIMEOUT);
                close_consumer();
            }
            if (consumer_.joinable())
            {
                consumer_.join();
            }
            close_consumer();

            // STOP의 누락 카운터는 consumer가 끝난 뒤에 합쳐 동일 handler의 동시 호출을 피한다.
            if (handler_ && capture_started_)
            {
                try
                {
                    EVENT_TRACE_LOGFILEW finalStatistics{};
                    finalStatistics.LogfileHeader.EventsLost = properties_.properties.EventsLost;
                    finalStatistics.LogfileHeader.BuffersLost = properties_.properties.RealTimeBuffersLost;
                    report_statistics(finalStatistics);
                }
                catch (...)
                {
                    set_failure(ERROR_INVALID_DATA);
                }
            }
            record value{};
            while (capture_started_ && connected && GetTickCount64() < drainDeadline && queue_.pop(value))
            {
                connected = channel.try_read_command() != command::disconnected && channel.parent_alive();
                if (connected)
                {
                    connected = channel.send_record(value);
                }
            }
            dropped_.fetch_add(queue_.pending(), std::memory_order_relaxed);

            ULONG result = startResult != ERROR_SUCCESS ? startResult : fatal_error_.load(std::memory_order_relaxed);
            if (result == ERROR_CANCELLED)
            {
                result = ERROR_SUCCESS;
            }
            status_code status = status_code::stopped;
            if (result == ERROR_ACCESS_DENIED || result == ERROR_PRIVILEGE_NOT_HELD)
            {
                status = status_code::permission_denied;
            }
            else if (result == ERROR_NOT_SUPPORTED)
            {
                status = status_code::unsupported;
            }
            else if (result != ERROR_SUCCESS)
            {
                status = startResult == ERROR_SUCCESS ? status_code::malformed_event : status_code::provider_failure;
            }
            record finalStatus = status_record(channel, status, result);
            if (result != ERROR_SUCCESS)
            {
                finalStatus.flags |= incomplete;
            }
            if (connected)
            {
                channel.send_record(finalStatus);
            }
            if (status == status_code::permission_denied)
            {
                return permission_denied_exit_code;
            }
            return connected ? result : ERROR_BROKEN_PIPE;
        }
    };

    timing_collector::timing_collector() : state_(std::make_unique<state>()) {}
    timing_collector::~timing_collector() = default;

    std::uint32_t timing_collector::run(transport& channel)
    {
        return state_->run(channel);
    }
}
