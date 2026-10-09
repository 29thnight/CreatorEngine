// PHASE 14 P3 — 녹화 UI.
//
// 옛 ProfilerWindow(557줄)는 걷었다. 그것은 전역 프로파일러의 vector 를 매
// 프레임 직접 읽어 타임라인을 그렸고, 읽는 동안에도 기록이 계속돼 화면과
// 자료가 어긋났다. 새 UI는 수집기가 공개한 immutable 캡처만 읽는다(§6.4).
//
// ★ 이 층에는 자료를 접는 코드가 없다. Hierarchy/Flat/레인 합계는 전부
//   ProfileAggregate 가 만들고, 선택과 Live Follow 는 ProfileReader 가 든다.
//   그래야 완료조건("Timeline 합계와 Hierarchy inclusive 가 일치", "pause 후
//   엔진이 돌아도 선택 자료가 변하지 않음")을 화면 없이 잰다 — 그리는 것만
//   으로는 살았는지 알 수 없다는 것을 P1·P2 에서 두 번 겪었다.
//
// ★ Space 단축키를 만들지 않는다. §7.1 이 "Space 전역 단축키는 제거하거나
//   Profiler 창 focus 일 때만 받는다" 고 적은 것은 옛 코어 얘기이고, 지금
//   에디터에는 그런 단축키가 없다. 여기서 새로 만들지 않는 것이 그 조건을
//   지키는 가장 싼 방법이다.
#include "ProfilerPresenter.h"
#include "ProfilerView.h"
#include "ProfilerViewerClient.h"
#include "ProfilerLiveDiagnostics.h"

#include <cinttypes>
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <exception>
#include <utility>
#include <atomic>
#include <cstdio>
#include <memory>
#include <string>
#include <stdexcept>
#include <string_view>
#include <vector>

#include "ImGui.h"
#include <imgui_internal.h>
#include "EditorIcons.h"
#include "EditorTheme.h"
#include "ProfileCaptureFile.h"

namespace editor::profiler_view
{
    namespace
    {
        enum class information_panel { session, file, integrity, counters };

        struct presenter_state
        {
            ce::preparation_dispatch dispatch;
            ce::preparation_dispatch diagnostics_dispatch;
            ce::profiler_viewer::client* source = nullptr;
            std::unique_ptr<ce::capture_reader> reader;
            std::uint64_t shown_live_epoch = 0;
            std::shared_ptr<const ce::profiler_viewer::client_snapshot> frame_source;
            information_panel information = information_panel::session;
            bool information_wide = true;
            bool information_expanded = true;
            bool information_requested = false;
            bool information_close_requested = false;
        };

        presenter_state state;
    }

    void initialize(ce::preparation_dispatch dispatch, ce::profiler_viewer::client& source,
                    ce::preparation_dispatch diagnostics_dispatch)
    {
        if (!dispatch || !diagnostics_dispatch || state.reader)
        {
            throw std::logic_error("Profiler presenter requires initialized asynchronous host executors");
        }
        state.dispatch = std::move(dispatch);
        state.diagnostics_dispatch = std::move(diagnostics_dispatch);
        state.source = &source;
        state.reader = std::make_unique<ce::capture_reader>(state.dispatch);
    }

    ce::capture_reader& reader()
    {
        return *state.reader;
    }

    ce::profiler_viewer::client& source()
    {
        return *state.source;
    }

    const ce::profiler_viewer::client_snapshot& frame_source()
    {
        return *state.frame_source;
    }

    ImGuiID information_popup_id()
    {
        // Menus and the body have different ID stacks. One explicit ID lets
        // both query actual popup visibility, including Escape/outside dismissal.
        return ImHashStr("ProfilerViewer.CompactInformation");
    }

    bool information_visible()
    {
        return state.information_wide ? state.information_expanded :
            !state.information_close_requested && (state.information_requested ||
                ImGui::IsPopupOpen(information_popup_id(), ImGuiPopupFlags_AnyPopupLevel));
    }

    void show_information(information_panel panel)
    {
        state.information = panel;
        state.information_expanded = true;
        state.information_requested = true;
        state.information_close_requested = false;
    }

    void dispatch_work(std::function<void()> work)
    {
        state.dispatch(std::move(work));
    }

    void dispatch_diagnostics(std::function<void()> work)
    {
        state.diagnostics_dispatch(std::move(work));
    }

    double ticks_to_milliseconds(ce::profile_tick ticks)
    {
        // ★ 환산은 **캡처가 뜬 기계의** 주파수로 한다(P6). 이 기계의 QPC 로
        //   나누면 남의 기계에서 뜬 .ceprof 의 모든 구간 길이가 두 주파수의
        //   비만큼 틀리고, 화면에는 그럴듯한 숫자가 그대로 나온다.
        //
        //   캡처가 없으면 환산할 것도 없다. 이 기계의 주파수로 물러나지
        //   **않는다** — 물러나는 순간 그 경로가 파일 캡처에서도 돌 수 있다.
        const ce::capture_session* capture = reader().capture();
        const double frequency = capture
            ? static_cast<double>(capture->environment().ticks_per_second)
            : 0.0;
        if (frequency <= 0.0)
        {
            return 0.0;
        }
        return static_cast<double>(ticks) * 1000.0 / frequency;
    }

    const char* thread_name(const ce::capture_session* capture, std::uint16_t slot)
    {
        if (capture)
        {
            for (const ce::thread_info& info : capture->threads())
            {
                if (info.slot == slot)
                {
                    return info.name.c_str();
                }
            }
        }

        // 이름표를 못 찾아도 빈 칸을 내지 않는다. 슬롯 번호만으로도 두 줄이
        // 같은 스레드인지 가릴 수 있다.
        static thread_local char fallback[32];
        std::snprintf(fallback, sizeof(fallback), "slot %u", static_cast<unsigned>(slot));
        return fallback;
    }

    const char* marker_name(const ce::capture_session* capture, ce::marker_id id)
    {
        if (capture)
        {
            const ce::capture_marker& info = capture->marker(id);
            if (!info.name.empty())
            {
                return info.name.c_str();
            }
        }

        // 표에 없는 id. 전역 registry 로 물러나지 **않는다** — 그러면 남의
        // 빌드에서 온 캡처가 이 프로세스의 엉뚱한 이름을 그린다. 대신 id 를
        // 그대로 보여 준다: 모른다는 것이 화면에 보여야 한다.
        static thread_local char fallback[32];
        std::snprintf(fallback, sizeof(fallback), "marker %u", static_cast<unsigned>(id));
        return fallback;
    }

    std::optional<ce::profile_tick> capture_stop_tick(const ce::capture_session& capture)
    {
        if (capture.frames().empty())
        {
            return std::nullopt;
        }
        const ce::frame_record& tail = capture.frames().back();
        for (const ce::profile_counter_sample& sample : tail.counters)
        {
            const ce::capture_counter* descriptor = ce::find_counter(capture.counter_descriptors(), sample.id);
            if (descriptor && descriptor->name == "Capture.StopDrainMilliseconds" &&
                std::isfinite(sample.value) && sample.value >= 0.0)
            {
                return tail.tick_end;
            }
        }
        return std::nullopt;
    }
}

namespace editor::profiler_view::capture_integrity
{
    struct stop_details
    {
        std::weak_ptr<const ce::capture_session> capture_;
        std::array<std::string, 6> counters_;
        std::optional<double> admitted_gpu_;
        std::vector<std::uint16_t> timed_out_threads_;
        std::vector<const ce::profile_event*> open_scopes_;
        std::vector<std::string> gpu_errors_;
        std::size_t other_truncated_ = 0;
        bool recorded_ = false;
    };

    const stop_details& details_for(const ce::capture_session_ptr& capture)
    {
        static stop_details cached;
        if (cached.capture_.lock() == capture)
        {
            return cached;
        }
        cached = {};
        cached.capture_ = capture;
        cached.counters_.fill("not recorded");
        if (!capture || capture->frames().empty())
        {
            return cached;
        }

        // Decode only the immutable final frame, once per capture. Never walk
        // the retained recording or resolve file IDs through the live registry.
        constexpr std::array<std::string_view, 6> names = {
            "Capture.UnackedCpuStreams", "Capture.OpenScopesAtStop", "Capture.PendingGpuSubmissions",
            "Capture.FailedGpuSubmissions", "Capture.StopDrainMilliseconds", "Capture.AdmittedGpuSubmissions"
        };
        const ce::frame_record& tail = capture->frames().back();
        for (const ce::profile_counter_sample& sample : tail.counters)
        {
            const ce::capture_counter* descriptor = ce::find_counter(capture->counter_descriptors(), sample.id);
            if (!descriptor || !std::isfinite(sample.value) || sample.value < 0.0)
            {
                continue;
            }
            for (std::size_t index = 0; index < names.size(); ++index)
            {
                if (descriptor->name == names[index])
                {
                    char value[64];
                    std::snprintf(value, sizeof(value), index == 4 ? "%.3f ms" : "%.0f", sample.value);
                    cached.counters_[index] = value;
                    if (index == 5)
                    {
                        cached.admitted_gpu_ = sample.value;
                    }
                    cached.recorded_ = true;
                    break;
                }
            }
        }
        if (!cached.recorded_)
        {
            return cached;
        }

        const auto stopTick = capture_stop_tick(*capture);
        for (const ce::profile_event& event : tail.events)
        {
            if (ce::has_flag(event.flags, ce::event_flags::instant))
            {
                const std::string& name = capture->marker(event.marker).name;
                if (name == "Capture.Incomplete.CpuProducerTimeout")
                {
                    if (std::find(cached.timed_out_threads_.begin(), cached.timed_out_threads_.end(),
                                  event.thread_slot) == cached.timed_out_threads_.end())
                    {
                        cached.timed_out_threads_.push_back(event.thread_slot);
                    }
                }
                else if (name.starts_with("Capture.Incomplete.Gpu: "))
                {
                    cached.gpu_errors_.push_back(name.substr(std::string_view("Capture.Incomplete.Gpu: ").size()));
                }
                continue;
            }
            const bool truncatedEnd = ce::has_flag(event.flags, ce::event_flags::truncated_end);
            if (stopTick && truncatedEnd && event.tick_end == *stopTick &&
                !ce::has_flag(event.flags, ce::event_flags::gpu_span))
            {
                cached.open_scopes_.push_back(&event);
            }
            else if (truncatedEnd || ce::has_flag(event.flags, ce::event_flags::truncated_begin))
            {
                ++cached.other_truncated_;
            }
        }
        return cached;
    }

    void draw_stop_details()
    {
        ce::capture_reader& view = reader();
        const auto capture = view.capture_handle();
        const auto recording = view.recording();
        if (recording && recording->frame_count() > 0 &&
            (!capture || view.recording_first_ordinal() + capture->frame_count() < recording->frame_count()))
        {
            ImGui::TextDisabled("Detailed stop diagnostics are in the recording's last frame.");
            ImGui::BeginDisabled(view.preparation_pending());
            if (ImGui::SmallButton("Load stop diagnostics"))
            {
                view.request_recording_range(recording->frame_count() - 1, 1);
            }
            ImGui::EndDisabled();
            return;
        }
        if (!capture)
        {
            return;
        }
        const stop_details& details = details_for(capture);
        if (!details.recorded_)
        {
            if (recording || !capture->complete())
            {
                ImGui::TextDisabled("Detailed stop diagnostics were not recorded in this window; absence is not completeness.");
            }
            return;
        }
        ImGui::Text("Stop drain: %s | unacknowledged CPU: %s | open scopes: %s",
                    details.counters_[4].c_str(), details.counters_[0].c_str(), details.counters_[1].c_str());
        ImGui::Text("GPU submissions admitted: %s | pending / failed: %s / %s",
                    details.counters_[5].c_str(), details.counters_[2].c_str(), details.counters_[3].c_str());
        if (details.admitted_gpu_ && *details.admitted_gpu_ == 0.0)
        {
            ImGui::TextDisabled("GPU coverage unobserved: no GPU submissions were admitted during this capture.");
        }
        if (!ImGui::TreeNodeEx("Stop integrity details", capture->complete() ? 0 : ImGuiTreeNodeFlags_DefaultOpen))
        {
            return;
        }
        ImGui::TextDisabled("Capture-owned diagnostics from frame %u", capture->frames().back().engine_frame);
        for (const std::uint16_t slot : details.timed_out_threads_)
        {
            ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.25f, 1.0f), "CPU producer timed out: %s (slot %u)",
                               thread_name(capture.get(), slot), static_cast<unsigned>(slot));
        }
        if (capture->unacked_streams() > details.timed_out_threads_.size())
        {
            ImGui::TextDisabled("Some unacknowledged CPU thread identities are unavailable in this capture.");
        }
        for (const std::string& error : details.gpu_errors_)
        {
            ImGui::TextWrapped("GPU stop issue: %s", error.c_str());
        }
        ImGui::Text("Preserved open-at-stop spans: %zu | other truncated spans in final frame: %zu",
                    details.open_scopes_.size(), details.other_truncated_);
        ImGui::TextDisabled("Open-at-stop spans have a truncated end; their actual completion was not observed.");
        if (!details.open_scopes_.empty())
        {
            ImGui::BeginChild("##OpenScopesAtStop", ImVec2(0.0f, ImGui::GetTextLineHeightWithSpacing() * 6.0f), true,
                              ImGuiWindowFlags_HorizontalScrollbar);
            ImGui::PushTextWrapPos(-1.0f);
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(details.open_scopes_.size()));
            while (clipper.Step())
            {
                for (int index = clipper.DisplayStart; index < clipper.DisplayEnd; ++index)
                {
                    const ce::profile_event& event = *details.open_scopes_[static_cast<std::size_t>(index)];
                    ImGui::Text("%s (slot %u) | %s | depth %u | session %" PRIu64 " / tick %" PRIu64 " / task %" PRIu64,
                                thread_name(capture.get(), event.thread_slot), static_cast<unsigned>(event.thread_slot),
                                marker_name(capture.get(), event.marker), static_cast<unsigned>(event.depth),
                                event.cpu.session, event.cpu.tick, event.cpu.task);
                }
            }
            ImGui::PopTextWrapPos();
            ImGui::EndChild();
        }
        ImGui::TreePop();
    }
}

// 파일 검사와 복사는 스케줄러 워커가 맡고, UI는 완료된 결과만 받아 쓴다.
namespace editor::profiler_view::capture_file_view
{
    enum class operation_kind
    {
        open,
        save,
    };

    struct file_operation
    {
        std::atomic_bool done{ false };
        operation_kind kind = operation_kind::open;
        std::filesystem::path path;
        ce::capture_recording_ptr recording;
        ce::profiler_viewer::source_artifact_ptr artifact;
        std::atomic_bool canceled{ false };
        std::stop_source cancellation;
        std::string error = "File operation was not accepted by the worker scheduler";
    };

    // 스케줄러가 콜백을 실행하지 않고 버려도 진행 표시가 영원히 남지 않는다.
    struct file_admission
    {
        explicit file_admission(std::shared_ptr<file_operation> value) : operation(std::move(value)) {}

        ~file_admission()
        {
            if (!started.load(std::memory_order_acquire))
            {
                operation->done.store(true, std::memory_order_release);
            }
        }

        std::shared_ptr<file_operation> operation;
        std::atomic_bool started{ false };
    };

    struct file_view_state
    {
        std::shared_ptr<file_operation> operation;
        std::string message;
        std::uint32_t range_frames = ce::kDefaultRetainedFrames;
        bool clear_pending = false;
        std::uint64_t clear_revision = 0;
        std::uint64_t clear_command_revision = 0;
    };

    struct pinned_recording
    {
        ce::capture_recording_ptr recording;
        ce::profiler_viewer::source_artifact_ptr artifact;
    };

    file_view_state& file_state()
    {
        static file_view_state value;
        return value;
    }

    std::string utf8_file_name(const std::filesystem::path& path)
    {
        const std::u8string name = path.filename().u8string();
        return std::string(name.begin(), name.end());
    }

    bool file_busy()
    {
        return static_cast<bool>(file_state().operation);
    }

    void poll_file_operation()
    {
        file_view_state& state = file_state();
        const std::shared_ptr<file_operation> operation = state.operation;
        if (!operation || !operation->done.load(std::memory_order_acquire))
        {
            return;
        }
        state.operation.reset();
        if (operation->canceled.load(std::memory_order_acquire))
        {
            return;
        }
        if (!operation->error.empty())
        {
            state.message = operation->error;
            return;
        }
        if (operation->kind == operation_kind::open)
        {
            // Aliasing ownership keeps the source pin alive for the reader and
            // every accepted range job, then releases it when the file is no
            // longer used. Switching back to Live need not lock an old file.
            const auto owner = std::make_shared<pinned_recording>(
                pinned_recording{ operation->recording, operation->artifact });
            reader().open_file(ce::capture_recording_ptr(owner, owner->recording.get()));
            state.message.clear();
        }
        else
        {
            state.message = "Saved entire recording: " + utf8_file_name(operation->path);
            if (operation->recording && !operation->recording->complete())
            {
                state.message += " (contains capture losses / unacknowledged streams)";
            }
        }
    }

    void save_current_recording()
    {
        const auto remote = source().snapshot();
        const auto artifact = remote->recording_source;
        const std::filesystem::path path = artifact ? artifact->path() : std::filesystem::path{};
        if (path.empty() || remote->recording.state != ce::recording_state::finalized || file_busy())
        {
            return;
        }
        const std::filesystem::path destination = pick_capture_to_save();
        if (destination.empty())
        {
            return;
        }
        auto operation = std::make_shared<file_operation>();
        operation->kind = operation_kind::save;
        operation->path = destination;
        operation->artifact = artifact;
        file_state().operation = operation;
        file_state().message.clear();
        try
        {
            const auto admission = std::make_shared<file_admission>(operation);
            dispatch_work([operation, path, admission]
            {
                admission->started.store(true, std::memory_order_release);
                operation->error.clear();
                try
                {
                    const auto recording = ce::open_capture_recording(path, operation->cancellation.get_token());
                    if (!recording)
                    {
                        operation->error = std::string("Save failed: ") + ce::describe(recording.error());
                    }
                    else if (!(*recording)->finalized())
                    {
                        operation->error = "Save failed: recording is not finalized";
                    }
                    else
                    {
                        operation->recording = *recording;
                        if (const auto saved = ce::save_recording(**recording, operation->path, operation->cancellation.get_token()); !saved)
                        {
                            operation->error = std::string("Save failed: ") + ce::describe(saved.error());
                        }
                    }
                }
                catch (const std::exception& exception)
                {
                    operation->error = std::string("Save failed: ") + exception.what();
                }
                catch (...)
                {
                    operation->error = "Save failed: unexpected worker error";
                }
                operation->done.store(true, std::memory_order_release);
            });
        }
        catch (const std::exception& exception)
        {
            file_state().message = std::string("Save could not start: ") + exception.what();
            file_state().operation.reset();
        }
    }

    void open_recording_path(const std::filesystem::path& path,
                             ce::profiler_viewer::source_artifact_ptr artifact = {})
    {
        if (file_busy())
        {
            return;
        }
        if (path.empty())
        {
            return;
        }
        auto operation = std::make_shared<file_operation>();
        operation->path = path;
        operation->artifact = std::move(artifact);
        file_state().operation = operation;
        file_state().message.clear();
        try
        {
            const auto admission = std::make_shared<file_admission>(operation);
            dispatch_work([operation, admission]
            {
                admission->started.store(true, std::memory_order_release);
                operation->error.clear();
                try
                {
                    if (!operation->artifact)
                    {
                        auto artifact = ce::profiler_viewer::open_source_artifact(operation->path);
                        if (!artifact)
                        {
                            operation->error = "Open failed: " + artifact.error();
                            operation->done.store(true, std::memory_order_release);
                            return;
                        }
                        operation->artifact = *artifact;
                    }
                    const auto loaded = ce::open_capture_recording(operation->artifact->path(), operation->cancellation.get_token());
                    if (loaded)
                    {
                        operation->recording = *loaded;
                    }
                    else
                    {
                        operation->error = std::string("Open failed: ") + ce::describe(loaded.error());
                    }
                }
                catch (const std::exception& exception)
                {
                    operation->error = std::string("Open failed: ") + exception.what();
                }
                catch (...)
                {
                    operation->error = "Open failed: unexpected worker error";
                }
                operation->done.store(true, std::memory_order_release);
            });
        }
        catch (const std::exception& exception)
        {
            file_state().message = std::string("Open could not start: ") + exception.what();
            file_state().operation.reset();
        }
    }

    void open_capture_file()
    {
        if (!file_busy())
        {
            open_recording_path(pick_capture_to_open());
        }
    }

    void draw_file_line()
    {
        const ce::capture_recording_ptr recording = reader().recording();
        if (recording)
        {
            const ce::recording_status status = recording->status();
            ImGui::TextColored(ImVec4(0.55f, 0.80f, 1.0f, 1.0f),
                               "File: %s | %" PRIu64 " frames | %s%s",
                               utf8_file_name(recording->path()).c_str(), recording->frame_count(),
                               recording->finalized() ? "Finalized" : "Recovered prefix",
                               recording->complete() ? "" : " | Incomplete capture");
            const auto metadata = recording->metadata();
            const double frequency = metadata ? static_cast<double>(metadata->environment().ticks_per_second) : 0.0;
            const double duration = frequency > 0.0 && status.last_tick >= status.first_tick
                ? static_cast<double>(status.last_tick - status.first_tick) / frequency : 0.0;
            ImGui::Text("Recorded span: %.3f s | submitted %" PRIu64 " / available %" PRIu64 " frames%s",
                        duration, status.submitted_frames, recording->frame_count(),
                        recording->recovered() ? " | recovered prefix only" : "");
            if (status.dropped_frames || status.dropped_events || status.dropped_counters ||
                status.source_dropped_counters || status.source_losses.dropped_events ||
                status.source_losses.dropped_frame_boundaries || status.source_losses.late_events ||
                status.source_losses.late_gpu_spans)
            {
                ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.25f, 1.0f),
                                   "File losses: writer frames/events/counters %" PRIu64 "/%" PRIu64 "/%" PRIu64
                                   " | source events/frames/counters %" PRIu64 "/%" PRIu64 "/%" PRIu64
                                   " | late CPU/GPU %" PRIu64 "/%" PRIu64,
                                   status.dropped_frames, status.dropped_events, status.dropped_counters,
                                   status.source_losses.dropped_events, status.source_losses.dropped_frame_boundaries,
                                   status.source_dropped_counters, status.source_losses.late_events,
                                   status.source_losses.late_gpu_spans);
            }
        }
        if (file_busy())
        {
            ImGui::TextDisabled("%s", file_state().operation->kind == operation_kind::open
                                         ? "Opening recording index..." : "Saving entire recording...");
            if (ImGui::SmallButton("Cancel file operation"))
            {
                file_state().operation->canceled.store(true, std::memory_order_release);
                file_state().operation->cancellation.request_stop();
                file_state().operation.reset();
                file_state().message = "Cancellation requested; in-flight I/O finishes at a safe boundary";
            }
        }
        if (!file_state().message.empty())
        {
            ImGui::TextUnformatted(file_state().message.c_str());
        }
        if (file_state().clear_pending)
        {
            ImGui::TextDisabled("Clearing live capture...");
        }
        if (reader().preparation_pending())
        {
            ImGui::TextDisabled("Loading selected range / preparing view...");
        }
        if (reader().preparation_failed())
        {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.3f, 1.0f),
                               "Selected range could not be prepared. Try a smaller window or reopen the file.");
        }
    }

    void same_line_if_fits(const char* nextLabel, float extraWidth = 0.0f)
    {
        const float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
        const float nextWidth = ImGui::CalcTextSize(nextLabel).x + ImGui::GetStyle().FramePadding.x * 2.0f + extraWidth;
        if (ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + nextWidth <= right)
        {
            ImGui::SameLine();
        }
    }

    void draw_recording_overview()
    {
        ce::capture_reader& view = reader();
        const ce::capture_recording_ptr recording = view.recording();
        if (!recording || recording->frame_count() == 0)
        {
            return;
        }
        const auto bins = recording->overview();
        const std::uint64_t total = recording->frame_count();
        const auto metadata = recording->metadata();
        const double frequency = metadata ? static_cast<double>(metadata->environment().ticks_per_second) : 0.0;
        double peak = 0.0;
        for (const ce::recording_overview_bin& bin : bins)
        {
            peak = (std::max)(peak, static_cast<double>(bin.maximum_duration));
        }
        peak = (std::max)(peak, 1.0);
        ImGui::Text("Entire recording | %" PRIu64 " frames | %zu overview bins", total, bins.size());
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const ImVec2 size((std::max)(ImGui::GetContentRegionAvail().x, 1.0f),
                          ImGui::GetTextLineHeightWithSpacing() * 3.0f);
        ImGui::InvisibleButton("##EntireRecordingOverview", size);
        ImDrawList* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y), IM_COL32(24, 26, 30, 255));
        const std::uint64_t first = view.recording_first_ordinal();
        const std::uint64_t count = view.capture() ? view.capture()->frame_count() : 0;
        for (const ce::recording_overview_bin& bin : bins)
        {
            const float x0 = origin.x + static_cast<float>(static_cast<double>(bin.first_ordinal) / total) * size.x;
            const float x1 = origin.x + static_cast<float>(static_cast<double>(bin.first_ordinal + bin.frame_count) / total) * size.x;
            const float height = static_cast<float>(static_cast<double>(bin.maximum_duration) / peak) * size.y;
            const bool selected = bin.first_ordinal < first + count && bin.first_ordinal + bin.frame_count > first;
            draw->AddRectFilled(ImVec2(x0, origin.y + size.y - height),
                                ImVec2((std::max)(x1, x0 + 1.0f), origin.y + size.y),
                                selected ? IM_COL32(120, 200, 255, 255) : IM_COL32(110, 130, 150, 255));
        }
        const auto ordinal_at = [&](float x)
        {
            const double ratio = (std::clamp)(static_cast<double>((x - origin.x) / size.x), 0.0, 1.0);
            return (std::min)(static_cast<std::uint64_t>(ratio * static_cast<double>(total)), total - 1);
        };
        if (ImGui::IsItemHovered())
        {
            const std::uint64_t ordinal = ordinal_at(ImGui::GetIO().MousePos.x);
            for (const ce::recording_overview_bin& bin : bins)
            {
                if (ordinal >= bin.first_ordinal && ordinal - bin.first_ordinal < bin.frame_count)
                {
                    ImGui::SetTooltip("frames %u..%u | %" PRIu64 " frames\nPeak %.3f ms | events %" PRIu64
                                      "\nClick or drag to load up to 600 frames", bin.first_engine_frame,
                                      bin.last_engine_frame, bin.frame_count,
                                      frequency > 0.0 ? static_cast<double>(bin.maximum_duration) * 1000.0 / frequency : 0.0,
                                      bin.event_count);
                    break;
                }
            }
        }
        if (ImGui::IsItemDeactivated())
        {
            const std::uint64_t clicked = ordinal_at(ImGui::GetIO().MouseClickedPos[0].x);
            const std::uint64_t released = ordinal_at(ImGui::GetIO().MousePos.x);
            std::uint64_t begin = clicked;
            std::uint32_t requested = file_state().range_frames;
            if (ImGui::IsMouseDragPastThreshold(ImGuiMouseButton_Left))
            {
                begin = (std::min)(clicked, released);
                requested = static_cast<std::uint32_t>((std::min)(
                    (std::max)(clicked, released) - begin + 1, static_cast<std::uint64_t>(requested)));
            }
            else
            {
                begin = clicked > requested / 2 ? clicked - requested / 2 : 0;
            }
            begin = (std::min)(begin, total > requested ? total - requested : 0);
            view.request_recording_range(begin, requested);
        }
        int rangeFrames = static_cast<int>(file_state().range_frames);
        ImGui::SetNextItemWidth(ThemePixels(120.0f));
        if (ImGui::InputInt("Frames per window", &rangeFrames, 1, 60))
        {
            file_state().range_frames = static_cast<std::uint32_t>((std::clamp)(rangeFrames, 1, 600));
        }
        same_line_if_fits("First");
        if (ImGui::SmallButton("First"))
        {
            view.request_recording_range(0, file_state().range_frames);
        }
        same_line_if_fits("Previous");
        if (ImGui::SmallButton("Previous"))
        {
            view.request_recording_range(first > file_state().range_frames ? first - file_state().range_frames : 0,
                                         file_state().range_frames);
        }
        same_line_if_fits("Next");
        if (ImGui::SmallButton("Next"))
        {
            const std::uint64_t lastStart = total > file_state().range_frames ? total - file_state().range_frames : 0;
            view.request_recording_range((std::min)(first + file_state().range_frames, lastStart),
                                         file_state().range_frames);
        }
        same_line_if_fits("Recent 600");
        if (ImGui::SmallButton("Recent 600"))
        {
            file_state().range_frames = 600;
            view.request_recording_range(total > 600 ? total - 600 : 0, 600);
        }
        ImGui::TextDisabled("Loaded ordinal %" PRIu64 "..%" PRIu64 " (%" PRIu64 ") | 128 MiB window budget",
                            first, count ? first + count - 1 : first, count);
    }
}

namespace
{
    const char* state_label(ce::recorder_state state)
    {
        switch (state)
        {
        case ce::recorder_state::starting:  return "Starting";
        case ce::recorder_state::recording: return "Recording";
        case ce::recorder_state::frozen:    return "Frozen";
        case ce::recorder_state::pausing:   return "Draining CPU / GPU";
        default:                            return "Stopped";
        }
    }

    void draw_row(const char* label, const char* value)
    {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted(label);
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted(value);
    }

    struct control_availability
    {
        bool recording;
        bool transitioning;
        bool file_busy;
        bool clearing;
        bool record_enabled;
        bool clear_enabled;
        bool finalized_enabled;
    };

    control_availability controls()
    {
        using namespace editor::profiler_view;
        const auto& remote = frame_source();
        const bool hasRecording = static_cast<bool>(remote.recording_source);
        control_availability result{};
        result.recording = remote.summary.state == ce::recorder_state::recording;
        result.transitioning = remote.summary.state == ce::recorder_state::starting ||
            remote.summary.state == ce::recorder_state::pausing ||
            (hasRecording && (remote.recording.state == ce::recording_state::starting ||
                              remote.recording.state == ce::recording_state::flushing));
        result.file_busy = capture_file_view::file_busy();
        result.clearing = capture_file_view::file_state().clear_pending || remote.clear_pending;
        result.record_enabled = remote.connected && !remote.command_pending && !result.transitioning &&
            !result.file_busy && !result.clearing;
        result.clear_enabled = result.record_enabled && !result.recording;
        result.finalized_enabled = !result.recording && !result.transitioning && !result.file_busy &&
            !result.clearing && hasRecording && remote.recording.state == ce::recording_state::finalized;
        return result;
    }

    void draw_recording_status()
    {
        using namespace editor::profiler_view;
        const auto* remote = &frame_source();
        const ce::live_summary& summary = remote->summary;
        const ce::recorder_state state = summary.state;
        const ce::recording_status& disk = remote->recording;
        const bool hasRecording = static_cast<bool>(remote->recording_source);
        const bool transitioning = controls().transitioning;
        ImGui::Text("%s  |  frame %u", state_label(state), summary.engine_frame);
        if (state == ce::recorder_state::pausing)
        {
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.35f, 1.0f),
                               "Stopping: waiting for %u CPU producers and %" PRIu64 " GPU submissions | %.3f ms elapsed",
                               summary.pause_unacked_streams, summary.pause_pending_gpu_submissions,
                               summary.pause_drain_ms);
            ImGui::Text("Open CPU scopes: pending final count | failed GPU submissions: %" PRIu64,
                        summary.pause_failed_gpu_submissions);
            ImGui::TextDisabled("CPU tails and submitted GPU work are still draining. This capture is not finalized yet.");
        }
        else if (remote->command_pending)
        {
            ImGui::TextDisabled("Waiting for the target to acknowledge the control request...");
        }
        if (hasRecording || state == ce::recorder_state::starting || disk.error)
        {
            const char* diskState = disk.state == ce::recording_state::starting ? "Starting" :
                disk.state == ce::recording_state::recording ? "Writing" :
                disk.state == ce::recording_state::flushing ? "Flushing" :
                disk.state == ce::recording_state::finalized ? "Finalized" : "Failed";
            ImGui::Text("Session writer: %s | %" PRIu64 "/%" PRIu64 " frames | %.2f MiB written / %.2f MiB flushed",
                        diskState, disk.written_frames, disk.submitted_frames,
                        static_cast<double>(disk.written_bytes) / (1024.0 * 1024.0),
                        static_cast<double>(disk.flushed_bytes) / (1024.0 * 1024.0));
            ImGui::Text("Writer backlog: %" PRIu64 " batches / %.2f MiB | dropped frames/events/counters %" PRIu64
                        "/%" PRIu64 "/%" PRIu64, disk.queued_batches,
                        static_cast<double>(disk.queued_bytes) / (1024.0 * 1024.0),
                        disk.dropped_frames, disk.dropped_events, disk.dropped_counters);
            if (disk.dropped_frames || disk.dropped_events || disk.dropped_counters ||
                disk.source_dropped_counters || disk.source_losses.dropped_events ||
                disk.source_losses.dropped_frame_boundaries || disk.source_losses.late_events ||
                disk.source_losses.late_gpu_spans)
            {
                ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.25f, 1.0f),
                                   "Recording has losses | source events/frames/counters %" PRIu64 "/%" PRIu64 "/%" PRIu64
                                   " | late CPU/GPU %" PRIu64 "/%" PRIu64,
                                   disk.source_losses.dropped_events, disk.source_losses.dropped_frame_boundaries,
                                   disk.source_dropped_counters, disk.source_losses.late_events,
                                   disk.source_losses.late_gpu_spans);
            }
            if (disk.error)
            {
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "Writer error: %s", ce::describe(*disk.error));
                if (hasRecording)
                {
                    const auto path = remote->recording_source->path().u8string();
                    const std::string utf8Path(path.begin(), path.end());
                    ImGui::TextWrapped("Recoverable spool: %s", utf8Path.c_str());
                }
            }
            if (transitioning && state != ce::recorder_state::pausing)
            {
                ImGui::TextDisabled("Waiting for the session writer. Record, Clear and Save are disabled.");
            }
        }
    }

    void draw_counter_modules()
    {
        using namespace editor::profiler_view;
        const auto* remote = &frame_source();
        ImGui::BeginDisabled(!remote->connected || remote->command_pending || controls().clearing);
        const auto toggle = [&](const char* label, ce::counter_category category)
        {
            const ce::counter_mask bit = ce::counter_bit(category);
            bool enabled = (remote->counters & bit) != 0;
            if (ImGui::Checkbox(label, &enabled))
            {
                const ce::counter_mask before = remote->counters;
                if (!source().request(ce::profiler_viewer::command::counter_mask, enabled ? before | bit : before & ~bit))
                {
                    capture_file_view::file_state().message = "Counter module change was not sent";
                    show_information(information_panel::file);
                }
            }
        };
        toggle("Process CPU/RAM", ce::counter_category::process);
        toggle("GPU VRAM", ce::counter_category::gpu);
        toggle("Render", ce::counter_category::render);
        toggle("Managed GC", ce::counter_category::managed);
        toggle("Resources", ce::counter_category::resources);
        toggle("Physics", ce::counter_category::physics);
        toggle("Audio", ce::counter_category::audio);
        ImGui::TextDisabled("Resources는 기본 꺼짐 · 켜면 0.5초마다 소유 프레임에서 집계합니다");
        ImGui::EndDisabled();
    }

    void draw_capture_integrity()
    {
        using namespace editor::profiler_view;

        // ★ 녹화 중 화면은 **한 박자 뒤처진다.** 코어가 정한 간격으로만
        //   스냅샷을 내고, 늦게 오는 GPU 구간은 닫힌 프레임에 나중에 들어간다
        //   (실측 제출→수집 최대 94 ms). 그 사실을 적어 두지 않으면 "최신
        //   프레임에 GPU 막대가 없다" 를 결함으로 읽는다.
        if (frame_source().summary.state == ce::recorder_state::recording)
        {
            ImGui::TextDisabled("녹화 중 - 화면은 마지막 스냅샷이다 (GPU 구간은 몇 프레임 뒤에 채워진다)");
        }

        // ★ 온전하지 않은 캡처를 **말없이** 그리지 않는다. 잠든 워커는 봉인
        //   요청에 응답하지 못해 그 꼬리가 여기 없는데, 아무 말이 없으면
        //   "그 스레드가 조용했다" 로 읽힌다.
        //
        // ★ **보고 있는 캡처**의 표식을 읽는다. 라이브 서비스의 요약을 읽으면
        //   파일을 보는 동안 남의 캡처 이야기를 한다 — 캡처가 제 온전함을 들고
        //   다니는 이유가 그것이다(ProfileCapture.h).
        if (const ce::capture_session* shown = reader().capture();
            shown && !shown->complete())
        {
            ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.25f, 1.0f),
                               "캡처 미완전 - CPU/GPU 수집 미완료 또는 손실 (미응답 CPU %u)",
                               shown->unacked_streams());
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("미응답 CPU 스트림의 꼬리 또는 수집되지 않은 GPU 작업이 빠질 수 있다.\n"
                                  "세션 writer와 파일의 손실 계수를 함께 확인한다.");
            }
        }
        if (const ce::capture_session* shown = reader().capture();
            shown && shown->dropped_counters() > 0)
        {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.3f, 1.0f),
                               "텔레메트리 표본 손실: %llu",
                               static_cast<unsigned long long>(shown->dropped_counters()));
        }
        capture_integrity::draw_stop_details();
    }

    void draw_summary(const ce::live_summary& summary)
    {
        if (!ImGui::BeginTable("ProfilerSummary", 2, ImGuiTableFlags_SizingStretchProp))
        {
            return;
        }

        char buffer[128];

        std::snprintf(buffer, sizeof(buffer), "%u", summary.retained_frames);
        draw_row("Retained frames", buffer);

        std::snprintf(buffer, sizeof(buffer), "%u (peak %u)",
                      summary.last_frame_events, summary.peak_frame_events);
        draw_row("Events / frame", buffer);

        std::snprintf(buffer, sizeof(buffer), "%u", summary.thread_count);
        draw_row("Threads", buffer);

        std::snprintf(buffer, sizeof(buffer), "%u", summary.registered_markers);
        draw_row("Markers", buffer);

        std::snprintf(buffer, sizeof(buffer), "%.2f MiB / %.0f MiB",
                      static_cast<double>(summary.memory_bytes) / (1024.0 * 1024.0),
                      static_cast<double>(summary.memory_budget) / (1024.0 * 1024.0));
        draw_row("Capture memory", buffer);

        std::snprintf(buffer, sizeof(buffer), "%u / %u", summary.free_chunks, summary.chunk_count);
        draw_row("Free chunks", buffer);
        std::snprintf(buffer, sizeof(buffer), "%.2f MiB",
                      static_cast<double>(summary.page_pool_bytes) / (1024.0 * 1024.0));
        draw_row("Page pool memory", buffer);

        // 잃은 것과 어긋난 것은 0 이 아니면 눈에 띄어야 한다. 프로파일러가
        // 스스로 잃은 수를 감추면 그 수치를 근거로 내리는 판단이 전부 틀어진다.
        std::snprintf(buffer, sizeof(buffer), "%" PRIu64, summary.dropped_events);
        draw_row("Dropped events", buffer);
        std::snprintf(buffer, sizeof(buffer), "%" PRIu64, summary.dropped_counters);
        draw_row("Dropped telemetry samples", buffer);

        std::snprintf(buffer, sizeof(buffer), "%" PRIu64, summary.unbalanced_scopes);
        draw_row("Unbalanced scopes", buffer);

        // 늦게 온 것의 장부. placed 는 제 프레임 칸으로 돌아간 수, dropped 는
        // 그 칸이 이미 링 밖이라 갈 곳이 없던 수다. stale 은 지운 세대의 것.
        std::snprintf(buffer, sizeof(buffer), "%" PRIu64 " / %" PRIu64,
                      summary.late_events_placed, summary.late_events_dropped);
        draw_row("Late CPU placed / dropped", buffer);

        std::snprintf(buffer, sizeof(buffer), "%" PRIu64, summary.stale_chunks_dropped);
        draw_row("Stale (pre-Clear) dropped", buffer);
        std::snprintf(buffer, sizeof(buffer), "%" PRIu64, summary.malformed_pages);
        draw_row("Malformed pages", buffer);
        std::snprintf(buffer, sizeof(buffer), "%" PRIu64, summary.ingested_pages);
        draw_row("Ingested pages", buffer);

        std::snprintf(buffer, sizeof(buffer), "%u", summary.pause_unacked_streams);
        draw_row("Stop: unacknowledged CPU producers", buffer);
        if (summary.state == ce::recorder_state::pausing)
        {
            draw_row("Stop: open CPU scopes", "pending final count");
        }
        else
        {
            std::snprintf(buffer, sizeof(buffer), "%" PRIu64, summary.pause_open_scopes);
            draw_row("Stop: open CPU scopes", buffer);
        }
        std::snprintf(buffer, sizeof(buffer), "%" PRIu64 " / %" PRIu64,
                      summary.pause_pending_gpu_submissions, summary.pause_failed_gpu_submissions);
        draw_row("Stop: pending / failed GPU submissions", buffer);
        std::snprintf(buffer, sizeof(buffer), "%.3f ms", summary.pause_drain_ms);
        draw_row("Stop drain duration", buffer);

        // 소유 경계. 셋 다 0 이어야 한다.
        std::snprintf(buffer, sizeof(buffer), "%" PRIu64 " / %" PRIu64,
                      summary.foreign_stream_touches, summary.abandoned_streams);
        draw_row("Foreign touches / abandoned", buffer);

        std::snprintf(buffer, sizeof(buffer), "%" PRIu64, summary.control_requests_deferred);
        draw_row("Control deferred", buffer);

        std::snprintf(buffer, sizeof(buffer), "%u / %" PRIu64,
                      summary.collector_queued_frames, summary.collector_dropped_frames);
        draw_row("Collector queued / dropped frames", buffer);
        std::snprintf(buffer, sizeof(buffer), "%" PRIu64 " / %" PRIu64,
                      summary.gpu_query_overflow_passes, summary.gpu_collect_failures);
        draw_row("GPU query lost / collect failures", buffer);
        if (summary.gpu_issue_last_frame != 0)
        {
            std::snprintf(buffer, sizeof(buffer), "%u", summary.gpu_issue_last_frame);
            draw_row("Last GPU issue frame", buffer);
        }
        const double collectorFrequency = static_cast<double>(editor::profiler_view::frame_source().target.qpc_frequency);
        auto collector_ms = [collectorFrequency](ce::profile_tick ticks)
        {
            return collectorFrequency > 0.0
                ? static_cast<double>(ticks) * 1000.0 / collectorFrequency : 0.0;
        };
        std::snprintf(buffer, sizeof(buffer), "%.3f ms / %" PRIu64 " batches",
                      collector_ms(summary.collector.page_ingest_ticks),
                      summary.collector.ingest_batches);
        draw_row("Page ingest + attribution", buffer);
        std::snprintf(buffer, sizeof(buffer), "%.3f ms / %" PRIu64 " frames",
                      collector_ms(summary.collector.frame_close_ticks),
                      summary.collector.frames_closed);
        draw_row("Frame close + retention", buffer);
        std::snprintf(buffer, sizeof(buffer), "%.3f ms / %" PRIu64 " captures",
                      collector_ms(summary.collector.snapshot_ticks),
                      summary.collector.snapshots_built);
        draw_row("Capture publish", buffer);
        std::snprintf(buffer, sizeof(buffer), "%.3f / %.3f ms",
                      collector_ms(summary.collector.wait_ticks),
                      collector_ms(summary.collector.queue_delay_ticks));
        draw_row("Signal wait / queue delay", buffer);
        std::snprintf(buffer, sizeof(buffer), "%.3f ms",
                      collector_ms(summary.collector.replenish_ticks));
        draw_row("Page replenish", buffer);
        std::snprintf(buffer, sizeof(buffer), "%u", summary.collector_os_thread_id);
        draw_row("Collector OS thread", buffer);

        ImGui::EndTable();
    }

    // 선택 구간의 집계 요약. ★ 두 합이 어긋나면 그 자리에서 드러나야 한다 —
    // 코어 프로브가 잡는 것과 같은 불변식이고, 화면에서도 보이는 편이 낫다.
    bool draw_selection_summary()
    {
        using namespace editor::profiler_view;

        const ce::frame_aggregate& aggregate = reader().aggregate();
        if (aggregate.frame_count() == 0)
        {
            if (reader().preparation_failed())
            {
                ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.3f, 1.0f), "Selected range preparation failed");
            }
            else
            {
                ImGui::TextDisabled("Preparing selected range...");
            }
            return false;
        }
        ImGui::Text("이벤트 %llu  ·  구간 %.3f ms  ·  레인 %zu",
                    static_cast<unsigned long long>(aggregate.event_count()),
                    ticks_to_milliseconds(aggregate.tick_end() - aggregate.tick_begin()),
                    aggregate.threads().size());

        if (aggregate.timeline_total_ticks() != aggregate.hierarchy_total_ticks())
        {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f),
                               "레인 합계와 트리 합계가 어긋난다 (%.3f ms vs %.3f ms) - 집계를 믿지 말 것",
                               ticks_to_milliseconds(aggregate.timeline_total_ticks()),
                               ticks_to_milliseconds(aggregate.hierarchy_total_ticks()));
        }
        if (aggregate.truncated_events() > 0)
        {
            ImGui::TextDisabled("잘린 구간 %llu 개 - 그 줄의 길이는 실제보다 짧다",
                                static_cast<unsigned long long>(aggregate.truncated_events()));
        }
        if (aggregate.dropped_events() > 0)
        {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.3f, 1.0f),
                               "이 구간에서 %llu 개를 잃었다 - 합계를 그대로 믿지 말 것",
                               static_cast<unsigned long long>(aggregate.dropped_events()));
        }
        return true;
    }
}

namespace editor::profiler_view
{
    enum class page { frames, timeline, cpu, memory, gpu, network, animation, hierarchy, flat, threads, collector, renderingLive, physics, audio, dxCapture };
    page selectedPage = page::timeline;
    std::atomic_bool renderingLiveRequested{false};
    void select_rendering_live() { renderingLiveRequested = true; }
}

void editor::profiler_view::begin_frame()
{
    // Resolve responsiveness before menus, not a frame after they are drawn.
    // draw() reconciles this with the actual bounded body width below.
    state.information_wide = ImGui::GetMainViewport()->Size.x - navigation_width() >= ThemePixels(900.0f);
    if (source().take_page_request() == 1 || renderingLiveRequested.exchange(false))
    {
        selectedPage = page::renderingLive;
    }

    // Only the authenticated engine publishes captures. The viewer has no
    // ProfilerService singleton or scene startup of its own.
    state.frame_source = source().snapshot();
    const auto& remote = state.frame_source;
    capture_file_view::poll_file_operation();
    poll_dx_capture();
    reader().poll_preparation();
    if (capture_file_view::file_state().clear_pending)
    {
        if (!remote->connected)
        {
            capture_file_view::file_state().clear_pending = false;
            capture_file_view::file_state().message = "Disconnected before Clear completion was observed";
        }
        else if (remote->command_revision > capture_file_view::file_state().clear_command_revision &&
            remote->last_command == ce::profiler_viewer::command::clear && !remote->command_accepted)
        {
            capture_file_view::file_state().clear_pending = false;
            capture_file_view::file_state().message = remote->command_message;
        }
        else if (remote->clear_revision != capture_file_view::file_state().clear_revision)
        {
            capture_file_view::file_state().clear_pending = false;
            reader().reset();
            state.shown_live_epoch = 0;
        }
    }
    else if (selectedPage != page::renderingLive)
    {
        if (reader().sync(remote->capture))
        {
            state.shown_live_epoch = remote->capture_epoch;
        }
    }
}

namespace
{
    void draw_connection_status()
    {
        using namespace editor::profiler_view;
        const auto* remote = &frame_source();
        if (!remote->command_message.empty() && !remote->command_accepted)
        {
            ImGui::TextWrapped("%s", remote->command_message.c_str());
        }
        if (!remote->message.empty())
        {
            ImGui::TextWrapped("%s", remote->message.c_str());
        }
        if (remote->target.connection.target_pid)
        {
            ImGui::TextDisabled("Target PID %u | session %llu | %s | skipped live windows %llu",
                remote->target.connection.target_pid,
                static_cast<unsigned long long>(remote->target.session_generation),
                remote->connected ? "Connected" : "Disconnected; last received data",
                static_cast<unsigned long long>(remote->skipped_captures));
            if (!reader().recording() && state.shown_live_epoch != 0 &&
                state.shown_live_epoch != remote->capture_generation)
            {
                ImGui::TextDisabled("Showing capture epoch %llu; target is now epoch %llu",
                    static_cast<unsigned long long>(state.shown_live_epoch),
                    static_cast<unsigned long long>(remote->capture_generation));
            }
        }
        else
        {
            ImGui::TextDisabled("Offline viewer - open a recording from File");
        }
    }
}

float editor::profiler_view::navigation_width()
{
    return ThemePixels(40.0f);
}

float editor::profiler_view::draw_menus(bool compact)
{
    const auto& remote = frame_source();
    const control_availability available = controls();
    const auto fileMenu = [&]
    {
        if (ImGui::BeginMenu("File"))
        {
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                                ImVec2(ThemePixels(EditorThemeTokens::ControlPaddingX), ThemePixels(2.0f)));
            if (ImGui::MenuItem(EditorIcon::Label<EditorIcon::ContentBrowser, " Open...">, nullptr, false,
                                !available.file_busy && !available.clearing))
            {
                capture_file_view::open_capture_file();
                show_information(information_panel::file);
            }
            if (ImGui::MenuItem(EditorIcon::Label<EditorIcon::Save, " Save entire recording...">,
                                nullptr, false, available.finalized_enabled))
            {
                capture_file_view::save_current_recording();
                show_information(information_panel::file);
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            {
                ImGui::SetTooltip("Exports the entire stopped recording, including frames outside this view.\n"
                                  "Available after the session writer has finalized.");
            }
            ImGui::Separator();
            if (ImGui::MenuItem(EditorIcon::Label<EditorIcon::Delete, " Clear live capture">,
                                nullptr, false, available.clear_enabled))
            {
                if (source().request(ce::profiler_viewer::command::clear))
                {
                    capture_file_view::file_state().clear_pending = true;
                    capture_file_view::file_state().clear_revision = remote.clear_revision;
                    capture_file_view::file_state().clear_command_revision = remote.command_revision;
                    capture_file_view::file_state().message.clear();
                }
                else
                {
                    capture_file_view::file_state().message = "Clear was not sent: target disconnected or command queue full";
                }
                show_information(information_panel::file);
            }
            ImGui::PopStyleVar();
            ImGui::EndMenu();
        }
    };
    const auto viewMenu = [&]
    {
        if (ImGui::BeginMenu("View"))
        {
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                                ImVec2(ThemePixels(EditorThemeTokens::ControlPaddingX), ThemePixels(2.0f)));
            bool follow = reader().live_follow();
            if (ImGui::MenuItem(EditorIcon::Label<EditorIcon::Forward, " Live Follow">, nullptr, &follow,
                                !available.file_busy && !available.clearing))
            {
                reader().set_live_follow(follow);
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            {
                ImGui::SetTooltip("Shows the latest live window. Turning it off preserves this view.\n"
                                  "Recording and Save continue to refer to the entire session.");
            }
            if (ImGui::MenuItem("View entire session", nullptr, false, available.finalized_enabled))
            {
                capture_file_view::open_recording_path(remote.recording_source->path(), remote.recording_source);
                show_information(information_panel::file);
            }
            ImGui::Separator();
            bool informationVisible = information_visible();
            if (ImGui::MenuItem("Information sidebar", nullptr, &informationVisible))
            {
                if (state.information_wide)
                {
                    state.information_expanded = informationVisible;
                }
                else if (informationVisible)
                {
                    state.information_requested = true;
                    state.information_close_requested = false;
                }
                else
                {
                    state.information_requested = false;
                    state.information_close_requested = true;
                }
            }
            ImGui::PopStyleVar();
            ImGui::EndMenu();
        }
    };

    // The host's title row has no padding. Menus keep the shared compact menu
    // spacing, including their popups, rather than inheriting the full row height.
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                        ImVec2(ThemePixels(EditorThemeTokens::MenuPaddingX), ThemePixels(EditorThemeTokens::MenuPaddingY)));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
                        ImVec2(ThemePixels(EditorThemeTokens::MenuGapX), ThemePixels(EditorThemeTokens::MenuGapY)));
    if (compact)
    {
        if (ImGui::BeginMenu("Menu"))
        {
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                                ImVec2(ThemePixels(EditorThemeTokens::ControlPaddingX), ThemePixels(2.0f)));
            fileMenu();
            viewMenu();
            ImGui::PopStyleVar();
            ImGui::EndMenu();
        }
    }
    else
    {
        fileMenu();
        viewMenu();
    }
    ImGui::PopStyleVar(2);
    return ImGui::GetCursorScreenPos().x;
}

void editor::profiler_view::draw_record_control(const ImVec2& size)
{
    const auto& remote = frame_source();
    const control_availability available = controls();
    const bool active = available.recording;
    const bool pending = remote.command_pending || available.transitioning;
    const float scale = (std::max)(ThemePixels(1.0f), 0.01f);
    const float glyphSize = (std::min)(EditorThemeTokens::IconFontSize,
        (std::max)(1.0f, size.y - ThemePixels(6.0f)) / scale);
    ImGui::PushFont(nullptr, glyphSize);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_Button, ThemeColorValue(active ? ThemeColor::PanelRaised : ThemeColor::Chrome));
    ImGui::PushStyleColor(ImGuiCol_Text,
        ThemeColorValue(pending ? ThemeColor::Warning : active ? ThemeColor::Error : ThemeColor::Text));
    ImGui::BeginDisabled(!available.record_enabled);
    const char* icon = pending ? EditorIcon::Label<EditorIcon::Timing, "###ProfilerRecord"> :
        active ? EditorIcon::Label<EditorIcon::Stop, "###ProfilerRecord"> :
                 EditorIcon::Label<EditorIcon::Play, "###ProfilerRecord">;
    if (ImGui::Button(icon, size))
    {
        // Only enqueue the existing command. Stop never waits for producers,
        // GPU work or disk completion on the UI thread.
        if (!source().request(available.recording ? ce::profiler_viewer::command::stop :
                                                   ce::profiler_viewer::command::record))
        {
            capture_file_view::file_state().message = available.recording
                ? "Stop was not sent: target disconnected or command queue full"
                : "Record was not sent: target disconnected or command queue full";
            show_information(information_panel::file);
        }
        else
        {
            state.information = information_panel::session;
        }
    }
    ImGui::EndDisabled();
    const bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar();
    ImGui::PopFont();
    if (hovered)
    {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(pending ? "Recording control pending" : active ? "Stop recording" : "Start recording");
        ImGui::Text("Target state: %s", state_label(remote.summary.state));
        ImGui::TextUnformatted("Record starts a new session. Stop flushes the entire session before Save becomes available.");
        if (!available.record_enabled)
        {
            ImGui::TextDisabled("%s", !remote.connected ? "Target disconnected" :
                remote.command_pending ? "Waiting for the target to acknowledge the control request" :
                available.transitioning ? "Waiting for CPU/GPU drain or the session writer" :
                available.file_busy ? "A file operation is in progress" : "Clearing live capture");
        }
        ImGui::EndTooltip();
    }
}

void editor::profiler_view::draw_navigation(const ImTextureRef& brand_icon)
{
    // Brand stays at the very top even when the pages below need to scroll.
    // The same embedded artwork supplies the native window/taskbar icon.
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    const float extent = (std::max)(1.0f, (std::min)(ThemePixels(32.0f), width - ThemePixels(8.0f)));
    const ImVec2 topLeft{ origin.x + (width - extent) * .5f, origin.y + ThemePixels(4.0f) };
    ImGui::GetWindowDrawList()->AddImage(brand_icon, topLeft,
        ImVec2(topLeft.x + extent, topLeft.y + extent));
    ImGui::Dummy(ImVec2(width, extent + ThemePixels(8.0f)));
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("CreatorEngine Profiler");
    }

    // Fifteen 26px rows with 2px gaps replace the former 36px + 4px rows.
    // Keep the 16px shared icon/text face and full-size tooltips. Small windows
    // and large accessibility scales still get independent wheel/key scrolling.
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ThemeColorValue(ThemeColor::Chrome));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ThemePixels(4.0f), ThemePixels(4.0f)));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, ThemePixels(2.0f)));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ThemePixels(2.0f), ThemePixels(2.0f)));
    ImGui::Separator();
    ImGui::BeginChild("##ProfilerNavigationPages", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_NoScrollbar);
    const float buttonWidth = (std::max)(1.0f, ImGui::GetContentRegionAvail().x);
    const auto nav = [&](page target, const char* icon, const char* title, const char* description)
    {
        ImGui::PushID(static_cast<int>(target));
        const bool active = selectedPage == target;
        const ImVec4 activeColor = ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive);
        if (active)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, activeColor);
        }
        if (ImGui::Button(icon, ImVec2(buttonWidth, ThemePixels(26.0f))))
        {
            selectedPage = target;
        }
        if (active)
        {
            ImGui::PopStyleColor();
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted(title);
            ImGui::TextDisabled("%s", description);
            ImGui::EndTooltip();
        }
        ImGui::PopID();
    };
    nav(page::frames, EditorIcon::Profiler, "프레임 그래프", "전체 프레임과 CPU·메모리·GPU 추이");
    nav(page::timeline, EditorIcon::Layers, "타임라인", "스레드·GPU 구간을 시간축에서 탐색");
    ImGui::Separator();
    nav(page::cpu, EditorIcon::Timing, "CPU", "프로세스 사용률과 CPU Self 상위 마커");
    nav(page::memory, EditorIcon::Runtime, "메모리", "프로세스 RAM 작업 집합");
    nav(page::gpu, EditorIcon::Game, "GPU", "Graphics 구간 시간과 VRAM");
    nav(page::dxCapture, EditorIcon::Timing, "DX12 Deep Capture", "별도 ETW 수집 프로세스와 제출→실행 기록");
    nav(page::renderingLive, EditorIcon::Scene, "Rendering - Live", "Live renderer diagnostics without Record");
    nav(page::physics, EditorIcon::Timing, "Physics", "씬별 물리 틱 카운터와 손실 진단");
    nav(page::audio, EditorIcon::Audio, "Audio", "논리 재생·보이스·오디오 callback 진단");
    nav(page::network, EditorIcon::World, "네트워크", "엔진 송수신량");
    nav(page::animation, EditorIcon::AvatarMask, "Animation", "실시간 CPU 예산과 태스크 실행 기록");
    ImGui::Separator();
    nav(page::hierarchy, EditorIcon::Hierarchy, "Hierarchy", "부모·자식 호출 관계와 구간 통계");
    nav(page::flat, EditorIcon::Menu, "Flat", "호출 위치를 합친 마커별 통계");
    nav(page::threads, EditorIcon::Grid, "Threads", "스레드별 구간 요약");
    nav(page::collector, EditorIcon::Settings, "Collector", "수집 상태와 손실 계상");
    ImGui::EndChild();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor();
}

namespace
{
    void draw_selected_page()
    {
        using namespace editor::profiler_view;
        const ce::live_summary& summary = frame_source().summary;
        static bool timelineFlame = false;
        const char* pageTitle = selectedPage == page::frames ? "프레임 그래프" :
            selectedPage == page::timeline ? "타임라인" :
            selectedPage == page::cpu ? "CPU" :
            selectedPage == page::memory ? "메모리" :
            selectedPage == page::gpu ? "GPU" :
            selectedPage == page::dxCapture ? "DX12 Deep Capture" :
            selectedPage == page::physics ? "Physics" :
            selectedPage == page::audio ? "Audio" :
            selectedPage == page::network ? "네트워크" :
            selectedPage == page::animation ? "Animation Budget" :
            selectedPage == page::renderingLive ? "Rendering - Live" :
            selectedPage == page::hierarchy ? "Hierarchy" :
            selectedPage == page::flat ? "Flat" :
            selectedPage == page::threads ? "Threads" : "Collector";
        ImGui::TextUnformatted(pageTitle);
        ImGui::Separator();
        ImGui::BeginDisabled(capture_file_view::file_busy() || capture_file_view::file_state().clear_pending);
        if (selectedPage != page::renderingLive)
        {
            capture_file_view::draw_recording_overview();
        }
        switch (selectedPage)
        {
        case page::frames:
            draw_frame_overview();
            ImGui::Separator();
            draw_telemetry_dashboard();
            break;
        case page::timeline:
            draw_frame_overview();
            if (reader().has_capture())
            {
                draw_selection_summary();
                ImGui::Separator();
                if (ImGui::RadioButton("시간순 레인", !timelineFlame))
                {
                    timelineFlame = false;
                }
                capture_file_view::same_line_if_fits("CPU 호출 계층", ImGui::GetFrameHeight());
                if (ImGui::RadioButton("CPU 호출 계층", timelineFlame))
                {
                    timelineFlame = true;
                }
                ImGui::Separator();
                if (timelineFlame)
                {
                    draw_flame_graph();
                }
                else
                {
                    draw_timeline();
                }
            }
            break;
        case page::cpu:
            draw_telemetry(telemetry_page::cpu);
            if (reader().has_capture())
            {
                ImGui::Separator();
                draw_flame_graph();
            }
            break;
        case page::memory: draw_memory_profiler(); break;
        case page::gpu: draw_telemetry(telemetry_page::gpu); break;
        case page::dxCapture: draw_dx_capture(); break;
        case page::network: draw_telemetry(telemetry_page::network); break;
        case page::physics: draw_physics_telemetry(); break;
        case page::audio: draw_audio_telemetry(); break;
        case page::animation:
        {
            draw_animation_budget();
            break;
        }
        case page::renderingLive:
        {
            draw_rendering_live();
            break;
        }
        case page::hierarchy:
        case page::flat:
            if (reader().has_capture())
            {
                if (!draw_selection_summary())
                {
                    break;
                }
                ImGui::Separator();
                if (selectedPage == page::hierarchy)
                {
                    draw_hierarchy_table();
                }
                else
                {
                    draw_flat_table();
                }
            }
            else
            {
                ImGui::TextDisabled("아직 캡처가 없다 - Record 를 켤 것");
            }
            break;
        case page::threads:
            if (reader().has_capture())
            {
                if (draw_selection_summary())
                {
                    draw_thread_table();
                }
            }
            else
            {
                ImGui::TextDisabled("아직 캡처가 없다 - Record 를 켤 것");
            }
            break;
        case page::collector:
            draw_summary(summary);
            if (!summary.gpu_issue_last_error.empty())
            {
                ImGui::TextWrapped("Last GPU issue: %s", summary.gpu_issue_last_error.c_str());
            }
            if (summary.dropped_events > 0 || summary.dropped_counters > 0 || summary.unbalanced_scopes > 0
                || summary.late_events_dropped > 0 || summary.stale_chunks_dropped > 0
                || summary.foreign_stream_touches > 0 || summary.abandoned_streams > 0
                || summary.collector_dropped_frames > 0 || summary.malformed_pages > 0
                || summary.gpu_query_overflow_passes > 0 || summary.gpu_collect_failures > 0)
            {
                ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.3f, 1.0f),
                                   "수집에 구멍이 있다 - 이 캡처의 합계를 그대로 믿지 말 것");
            }
            if (!summary.capture_complete)
            {
                ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.25f, 1.0f),
                                   "캡처 미확정 - 미응답 스트림 %u", summary.capture_unacked_streams);
            }
            break;
        }
        ImGui::EndDisabled();
    }
}

namespace
{
    bool capture_needs_attention()
    {
        const ce::capture_session* shown = editor::profiler_view::reader().capture();
        return shown && (!shown->complete() || shown->dropped_counters() > 0);
    }

    const char* information_title(editor::profiler_view::information_panel panel)
    {
        using editor::profiler_view::information_panel;
        switch (panel)
        {
        case information_panel::session: return "Session";
        case information_panel::file: return "Recording file";
        case information_panel::integrity: return "Capture integrity";
        case information_panel::counters: return "Counter modules";
        }
        return "Information";
    }

    void draw_information_content()
    {
        using namespace editor::profiler_view;
        ImGui::PushTextWrapPos(0.0f);
        switch (state.information)
        {
        case information_panel::session:
            if (ImGui::CollapsingHeader("Connection", ImGuiTreeNodeFlags_DefaultOpen))
            {
                draw_connection_status();
                ImGui::TextDisabled("%s", reader().live_follow() ? "Live Follow enabled" : "Current view held");
            }
            if (ImGui::CollapsingHeader("Session writer", ImGuiTreeNodeFlags_DefaultOpen))
            {
                draw_recording_status();
            }
            break;
        case information_panel::file:
            if (!reader().recording() && !capture_file_view::file_busy())
            {
                ImGui::TextDisabled("Open a .ceprof recording from File, or use View > View entire session after stopping.");
            }
            capture_file_view::draw_file_line();
            break;
        case information_panel::integrity:
            ImGui::TextDisabled("Diagnostics belong to the displayed capture.");
            if (!reader().has_capture())
            {
                ImGui::TextDisabled("No capture is selected.");
            }
            else if (!capture_needs_attention())
            {
                ImGui::TextUnformatted("No incompleteness flag or telemetry sample loss in this capture window.");
            }
            draw_capture_integrity();
            break;
        case information_panel::counters:
            draw_counter_modules();
            break;
        }
        ImGui::PopTextWrapPos();
    }

    void draw_information_panel(bool popup)
    {
        using namespace editor::profiler_view;
        ImGui::PushStyleColor(ImGuiCol_ChildBg, editor::ThemeColorValue(editor::ThemeColor::Panel));
        ImGui::BeginChild("##ProfilerInformation", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AlwaysUseWindowPadding,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        const float closeSize = ImGui::GetFrameHeight();
        const float titleWidth = (std::max)(1.0f, ImGui::GetContentRegionAvail().x - closeSize - ImGui::GetStyle().ItemSpacing.x);
        ImGui::BeginChild("##InformationTitle", ImVec2(titleWidth, closeSize), ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(information_title(state.information));
        ImGui::EndChild();
        ImGui::SameLine();
        if (ImGui::Button(EditorIcon::Label<EditorIcon::Close, "##CloseInformation">, ImVec2(closeSize, closeSize)))
        {
            if (popup)
            {
                ImGui::CloseCurrentPopup();
            }
            else
            {
                state.information_expanded = false;
            }
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Close information sidebar");
        }
        ImGui::Separator();
        // Each section retains its own scroll position; its title and the right
        // icon strip stay fixed even for a very long list of stop diagnostics.
        ImGui::PushID(static_cast<int>(state.information));
        ImGui::BeginChild("##InformationScroll", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None);
        draw_information_content();
        ImGui::EndChild();
        ImGui::PopID();
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    void draw_information_icons(bool wide)
    {
        using namespace editor::profiler_view;
        const auto& remote = frame_source();
        const auto& disk = remote.recording;
        const auto recording = reader().recording();
        const bool sessionAttention = (!remote.connected && remote.target.connection.target_pid != 0) ||
            (!remote.command_message.empty() && !remote.command_accepted) ||
            disk.error || disk.dropped_frames || disk.dropped_events || disk.dropped_counters ||
            disk.source_dropped_counters || disk.source_losses.dropped_events ||
            disk.source_losses.dropped_frame_boundaries || disk.source_losses.late_events || disk.source_losses.late_gpu_spans;
        const bool fileAttention = capture_file_view::file_busy() || !capture_file_view::file_state().message.empty() ||
            capture_file_view::file_state().clear_pending || reader().preparation_pending() || reader().preparation_failed() ||
            (recording && (!recording->complete() || recording->recovered()));
        const auto button = [&](information_panel panel, const char* icon, const char* description, bool attention)
        {
            ImGui::PushID(static_cast<int>(panel));
            const bool selected = state.information == panel && information_visible();
            ImGui::PushStyleColor(ImGuiCol_Button,
                editor::ThemeColorValue(selected ? editor::ThemeColor::Selection : editor::ThemeColor::Chrome));
            ImGui::PushStyleColor(ImGuiCol_Text,
                editor::ThemeColorValue(attention ? editor::ThemeColor::Warning : editor::ThemeColor::Text));
            if (ImGui::Button(icon, ImVec2((std::max)(1.0f, ImGui::GetContentRegionAvail().x), editor::ThemePixels(32.0f))))
            {
                if (wide && selected)
                {
                    state.information_expanded = false;
                }
                else
                {
                    show_information(panel);
                }
            }
            ImGui::PopStyleColor(2);
            if (ImGui::IsItemHovered())
            {
                ImGui::BeginTooltip();
                ImGui::TextUnformatted(information_title(panel));
                ImGui::TextDisabled("%s", description);
                if (attention)
                {
                    ImGui::TextUnformatted("Status or diagnostics available");
                }
                ImGui::EndTooltip();
            }
            ImGui::PopID();
        };
        button(information_panel::session, EditorIcon::Info, "Connection, control status and session writer", sessionAttention);
        button(information_panel::file, EditorIcon::Save, "Recording metadata, file progress and cancellation", fileAttention);
        button(information_panel::integrity, EditorIcon::Shield, "Capture completeness and stop integrity details", capture_needs_attention());
        button(information_panel::counters, EditorIcon::Inspector, "Enable or disable counter modules", false);
    }
}

void editor::profiler_view::draw()
{
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 available((std::max)(1.0f, ImGui::GetContentRegionAvail().x),
                          (std::max)(1.0f, ImGui::GetContentRegionAvail().y));
    const float iconWidth = (std::min)(ThemePixels(40.0f), available.x * 0.25f);
    const bool wide = available.x >= ThemePixels(900.0f);
    state.information_wide = wide;
    const float informationWidth = wide && state.information_expanded
        ? (std::clamp)(available.x * 0.30f, ThemePixels(280.0f), ThemePixels(400.0f)) : 0.0f;
    const float pageWidth = (std::max)(1.0f, available.x - informationWidth - iconWidth);

    // Host chrome intentionally has zero padding/spacing. Restore shared body
    // metrics locally; page scrolling can never move the titlebar or either rail.
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ThemePixels(10.0f), ThemePixels(8.0f)));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
                        ImVec2(ThemePixels(EditorThemeTokens::ItemGapX), ThemePixels(EditorThemeTokens::ItemGapY)));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, ThemePixels(1.0f));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ThemeColorValue(ThemeColor::Canvas));
    ImGui::BeginChild("##ProfilerPage", ImVec2(pageWidth, available.y), ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_HorizontalScrollbar);
    draw_selected_page();
    ImGui::EndChild();
    ImGui::PopStyleColor();

    if (informationWidth > 0.0f)
    {
        ImGui::SetCursorScreenPos(ImVec2(origin.x + pageWidth, origin.y));
        ImGui::BeginChild("##InformationBounds", ImVec2(informationWidth, available.y), ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        draw_information_panel(false);
        ImGui::EndChild();
    }

    ImGui::SetCursorScreenPos(ImVec2(origin.x + available.x - iconWidth, origin.y));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ThemePixels(3.0f), ThemePixels(4.0f)));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, ThemePixels(4.0f)));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ThemeColorValue(ThemeColor::Chrome));
    ImGui::BeginChild("##InformationIcons", ImVec2(iconWidth, available.y), ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_NoScrollbar);
    draw_information_icons(wide);
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);

    if (state.information_requested)
    {
        if (!wide)
        {
            ImGui::OpenPopup(information_popup_id());
        }
        state.information_requested = false;
    }
    // At narrow widths a dismissible inspector overlays the page instead of
    // squeezing its chart to zero. Its bounds remain inside the body viewport.
    const float popupWidth = (std::max)(1.0f, (std::min)(ThemePixels(400.0f), available.x - iconWidth));
    ImGui::SetNextWindowPos(ImVec2(origin.x + available.x - iconWidth - popupWidth, origin.y));
    ImGui::SetNextWindowSize(ImVec2(popupWidth, available.y));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    if (ImGui::BeginPopupEx(information_popup_id(), ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                          ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
    {
        if (wide || state.information_close_requested)
        {
            ImGui::CloseCurrentPopup();
        }
        else
        {
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ThemePixels(10.0f), ThemePixels(8.0f)));
            draw_information_panel(true);
            ImGui::PopStyleVar();
        }
        ImGui::EndPopup();
    }
    state.information_close_requested = false;
    ImGui::PopStyleVar();
    ImGui::PopStyleVar(3);
}

void editor::profiler_view::open_path(const std::filesystem::path& path)
{
    auto extension = path.extension().wstring();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](wchar_t c)
    {
        return c >= L'A' && c <= L'Z' ? static_cast<wchar_t>(c + L'a' - L'A') : c;
    });
    if (extension == L".cedx")
    {
        selectedPage = page::dxCapture;
        open_dx_capture(path);
    }
    else
    {
        capture_file_view::open_recording_path(path);
    }
}

void editor::profiler_view::shutdown()
{
    if (capture_file_view::file_state().operation)
    {
        capture_file_view::file_state().operation->canceled.store(true, std::memory_order_release);
        capture_file_view::file_state().operation->cancellation.request_stop();
        capture_file_view::file_state().operation.reset();
    }
    shutdown_dx_capture();
    shutdown_live_diagnostics();
    state.reader.reset();
    state.frame_source.reset();
    state.dispatch = {};
    state.diagnostics_dispatch = {};
    state.source = nullptr;
}
