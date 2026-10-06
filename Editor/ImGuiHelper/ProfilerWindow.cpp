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
#include "ProfilerHUD.h"
#include "ProfilerView.h"
#include "EnhancedRenderDebugWindow.h"

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
#include <string_view>
#include <vector>

#include "ImGui.h"
#include "EditorIcons.h"
#include "ProfileCaptureFile.h"
#include "ProfileScope.h"
#include "JobScheduler.h"

namespace editor::profiler_view
{
    // 창이 소유하는 reader. 함수 지역 static 이라 창을 닫아도 살아 있다 —
    // 녹화 상태는 서비스가, 선택은 이 reader 가 들고 있으므로 창을 여닫아도
    // 둘 다 유지된다(완료조건).
    ce::capture_reader& reader()
    {
        static ce::capture_reader instance{ [](std::function<void()> work)
        {
            (void)ce::get_job_scheduler().submit(std::move(work));
        } };
        return instance;
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
            ImGui::SameLine();
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
        std::uint64_t clear_ticket = 0;
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
        if (!operation->error.empty())
        {
            state.message = operation->error;
            return;
        }
        if (operation->kind == operation_kind::open)
        {
            reader().open_file(operation->recording);
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
        ce::profiler_service& service = ce::profiler();
        const std::filesystem::path source = service.recording_path();
        if (source.empty() || service.recording_status().state != ce::recording_state::finalized || file_busy())
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
        file_state().operation = operation;
        file_state().message.clear();
        try
        {
            const auto admission = std::make_shared<file_admission>(operation);
            (void)ce::get_job_scheduler().submit([operation, source, admission]
            {
                admission->started.store(true, std::memory_order_release);
                operation->error.clear();
                try
                {
                    const auto recording = ce::open_capture_recording(source);
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
                        if (const auto saved = ce::save_recording(**recording, operation->path); !saved)
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

    void open_recording_path(const std::filesystem::path& path)
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
        file_state().operation = operation;
        file_state().message.clear();
        try
        {
            const auto admission = std::make_shared<file_admission>(operation);
            (void)ce::get_job_scheduler().submit([operation, admission]
            {
                admission->started.store(true, std::memory_order_release);
                operation->error.clear();
                try
                {
                    const auto loaded = ce::open_capture_recording(operation->path);
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
        }
        if (!file_state().message.empty())
        {
            ImGui::TextUnformatted(file_state().message.c_str());
        }
        if (file_state().clear_ticket)
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
        const ImVec2 size((std::max)(ImGui::GetContentRegionAvail().x, 64.0f),
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
        ImGui::SetNextItemWidth(120.0f);
        if (ImGui::InputInt("Frames per window", &rangeFrames, 1, 60))
        {
            file_state().range_frames = static_cast<std::uint32_t>((std::clamp)(rangeFrames, 1, 600));
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("First"))
        {
            view.request_recording_range(0, file_state().range_frames);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Previous"))
        {
            view.request_recording_range(first > file_state().range_frames ? first - file_state().range_frames : 0,
                                         file_state().range_frames);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Next"))
        {
            const std::uint64_t lastStart = total > file_state().range_frames ? total - file_state().range_frames : 0;
            view.request_recording_range((std::min)(first + file_state().range_frames, lastStart),
                                         file_state().range_frames);
        }
        ImGui::SameLine();
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

    // 툴바. 녹화 제어와 Live Follow.
    void draw_toolbar(const ce::live_summary& summary)
    {
        using namespace editor::profiler_view;

        ce::profiler_service& service = ce::profiler();
        const ce::recorder_state state = service.state();
        const ce::recording_status disk = service.recording_status();
        const bool hasRecording = !service.recording_path().empty();
        const bool recording = state == ce::recorder_state::recording;
        const bool transitioning = state == ce::recorder_state::starting || state == ce::recorder_state::pausing ||
            (hasRecording && (disk.state == ce::recording_state::starting || disk.state == ce::recording_state::flushing));
        const bool fileBusy = capture_file_view::file_busy();
        const bool clearing = capture_file_view::file_state().clear_ticket != 0;

        bool record = recording || state == ce::recorder_state::starting;
        ImGui::BeginDisabled(transitioning || fileBusy || clearing);
        if (ImGui::Checkbox("Record", &record))
        {
            if (recording)
            {
                // 요청만 보낸다. 이 자리에서 생산자·수집기나 디스크 완료를 기다리지 않는다.
                service.pause();
            }
            else
            {
                service.record(summary.engine_frame);
            }
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Record starts a new session. Stop flushes the entire session before Save becomes available.");
        }

        ImGui::SameLine();
        ImGui::BeginDisabled(recording || transitioning || fileBusy || clearing);
        if (ImGui::Button(EditorIcon::Label<EditorIcon::Delete, " Clear">))
        {
            capture_file_view::file_state().clear_ticket = service.clear();
            reader().reset();
            capture_file_view::file_state().message.clear();
        }
        ImGui::EndDisabled();

        ImGui::SameLine();
        ImGui::BeginDisabled(recording || transitioning || fileBusy || clearing || !hasRecording ||
                             disk.state != ce::recording_state::finalized);
        if (ImGui::Button("Save"))
        {
            capture_file_view::save_current_recording();
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Save exports the entire current stopped recording, including frames outside this view.");
        }

        ImGui::SameLine();
        ImGui::BeginDisabled(recording || transitioning || fileBusy || clearing || !hasRecording ||
                             disk.state != ce::recording_state::finalized);
        if (ImGui::Button("View entire session"))
        {
            capture_file_view::open_recording_path(service.recording_path());
        }
        ImGui::EndDisabled();

        ImGui::SameLine();
        ImGui::BeginDisabled(fileBusy || clearing);
        if (ImGui::Button(EditorIcon::Label<EditorIcon::ContentBrowser, " Open">))
        {
            capture_file_view::open_capture_file();
        }
        ImGui::EndDisabled();

        ImGui::SameLine();
        ImGui::BeginDisabled(fileBusy || clearing);
        bool follow = reader().live_follow();
        if (ImGui::Checkbox(EditorIcon::Label<EditorIcon::Forward, " Live Follow">, &follow))
        {
            reader().set_live_follow(follow);
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Live Follow shows the latest live window. Turning it off preserves this view.\n"
                              "Recording and Save continue to refer to the entire session.");
        }

        ImGui::SameLine();
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
                    const auto path = service.recording_path().u8string();
                    const std::string utf8Path(path.begin(), path.end());
                    ImGui::TextWrapped("Recoverable spool: %s", utf8Path.c_str());
                }
            }
            if (transitioning && state != ce::recorder_state::pausing)
            {
                ImGui::TextDisabled("Waiting for the session writer. Record, Clear and Save are disabled.");
            }
        }

        if (ImGui::TreeNode("Counter modules"))
        {
            const auto toggle = [&](const char* label, ce::counter_category category)
            {
                const ce::counter_mask bit = ce::counter_bit(category);
                bool enabled = (service.get_counter_mask() & bit) != 0;
                if (ImGui::Checkbox(label, &enabled))
                {
                    const ce::counter_mask before = service.get_counter_mask();
                    service.set_counter_mask(enabled ? before | bit : before & ~bit);
                }
            };
            toggle("Process CPU/RAM", ce::counter_category::process);
            ImGui::SameLine(); toggle("GPU VRAM", ce::counter_category::gpu);
            ImGui::SameLine(); toggle("Render", ce::counter_category::render);
            ImGui::SameLine(); toggle("Managed GC", ce::counter_category::managed);
            ImGui::SameLine(); toggle("Resources", ce::counter_category::resources);
            ImGui::SameLine(); toggle("Physics", ce::counter_category::physics);
            ImGui::SameLine(); toggle("Audio", ce::counter_category::audio);
            ImGui::TextDisabled("Resources는 기본 꺼짐 · 켜면 0.5초마다 소유 프레임에서 집계합니다");
            ImGui::TreePop();
        }

        // ★ 녹화 중 화면은 **한 박자 뒤처진다.** 코어가 정한 간격으로만
        //   스냅샷을 내고, 늦게 오는 GPU 구간은 닫힌 프레임에 나중에 들어간다
        //   (실측 제출→수집 최대 94 ms). 그 사실을 적어 두지 않으면 "최신
        //   프레임에 GPU 막대가 없다" 를 결함으로 읽는다.
        if (recording)
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
        const double collectorFrequency = static_cast<double>(ce::profiler_service::ticks_per_second());
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
    enum class page { frames, timeline, cpu, memory, gpu, network, animation, hierarchy, flat, threads, collector, renderingLive, physics, audio };
    page selectedPage = page::timeline;
    std::atomic_bool renderingLiveRequested{false};
    void select_rendering_live() { renderingLiveRequested = true; }
}

void DrawProfilerHUD(std::mutex& sceneStructureMutex)
{
    using namespace editor::profiler_view;
    if (renderingLiveRequested.exchange(false))
    {
        selectedPage = page::renderingLive;
    }

    // ★ 창이 **그려졌다** 는 증거를 프로파일러 자신이 낸다. 창이 열린 것과
    //   본문이 도는 것은 다르다 — 도크 탭으로 겹친 창은 선택돼야 본문이
    //   돌고, 그러지 않으면 `editor.window ... open` 이 성공해도 여기까지
    //   오지 않는다. 이 마커가 캡처에 나타나는지로 게이트가 판정한다.
    //
    //   자기 UI 비용을 자기가 재는 것은 §7 이 말하는 profiler overhead 이기도
    //   하다. 창이 열려 있으면 녹화 중에도 최신 immutable 스냅샷을 그린다.
    ce::profile_scope _profile{ ce::marker<"ProfilerWindow">() };

    ce::profiler_service& service = ce::profiler();

    // ★ 녹화 중에도 프레임을 보여 준다(§6.4 개정). 창이 떠 있는 동안만
    //   청하므로, 창을 닫으면 코어는 스냅샷을 한 번도 만들지 않는다.
    //   간격은 코어가 정한다 — 화면이 부르는 대로 다 내주면 링을 통째로
    //   복사하는 비용이 재려는 대상을 흔든다.
    if (selectedPage != page::renderingLive && !capture_file_view::file_state().clear_ticket)
    {
        service.request_live_capture();
    }

    const ce::live_summary summary = service.summary();

    // 공개된 불변 캡처를 따라간다. 조회 때문에 진행 중인 녹화를 멈추지 않는다.
    //
    // ★ 언제 갈아타는가 는 이 줄이 아니라 reader 가 정한다. 그래야 그 규칙을
    //   화면 없이 재고 변이로 물 수 있다 — 여기 조건문을 두면 재는 수단이 눈뿐이다.
    capture_file_view::poll_file_operation();
    reader().poll_preparation();
    const ce::capture_session_ptr latest = service.capture();
    if (capture_file_view::file_state().clear_ticket)
    {
        if (service.control_applied(capture_file_view::file_state().clear_ticket))
        {
            capture_file_view::file_state().clear_ticket = 0;
            reader().reset();
        }
    }
    else if (selectedPage != page::renderingLive)
    {
        reader().sync(latest);
    }

    draw_toolbar(summary);
    capture_file_view::draw_file_line();
    if (selectedPage != page::renderingLive)
    {
        ImGui::BeginDisabled(capture_file_view::file_busy() || capture_file_view::file_state().clear_ticket != 0);
        capture_file_view::draw_recording_overview();
        ImGui::EndDisabled();
    }
    ImGui::Separator();
    // The left rail keeps every profiler view in one predictable location.
    // The selectedPage frame range belongs to the reader, not to an individual page.

    static bool timelineFlame = false;
    const float railWidth = ImGui::GetTextLineHeightWithSpacing() + 20.0f;
    ImGui::BeginChild("##ProfilerNavigation", ImVec2(railWidth, 0.0f), false,
                      ImGuiWindowFlags_NoScrollbar);
    const auto nav = [&](page target, const char* icon, const char* title, const char* description)
    {
        ImGui::PushID(static_cast<int>(target));
        const bool active = selectedPage == target;
        const ImVec4 activeColor = ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive);
        if (active)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, activeColor);
        }
        if (ImGui::Button(icon, ImVec2(railWidth - 12.0f, railWidth - 12.0f)))
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
    nav(page::renderingLive, EditorIcon::Scene, "Rendering - Live", "Live renderer diagnostics without Record");
    nav(page::physics, EditorIcon::Timing, "Physics", "씬별 물리 틱 카운터와 손실 진단");
    nav(page::audio, EditorIcon::Timing, "Audio", "논리 재생·보이스·오디오 callback 진단");
    nav(page::network, EditorIcon::World, "네트워크", "엔진 송수신량");
    nav(page::animation, EditorIcon::AvatarMask, "Animation", "실시간 CPU 예산과 태스크 실행 기록");
    ImGui::Separator();
    nav(page::hierarchy, EditorIcon::Hierarchy, "Hierarchy", "부모·자식 호출 관계와 구간 통계");
    nav(page::flat, EditorIcon::Menu, "Flat", "호출 위치를 합친 마커별 통계");
    nav(page::threads, EditorIcon::Grid, "Threads", "스레드별 구간 요약");
    nav(page::collector, EditorIcon::Settings, "Collector", "수집 상태와 손실 계상");
    ImGui::EndChild();
    ImGui::SameLine(0.0f, 0.0f);
    ImGui::BeginChild("##ProfilerPage", ImVec2(0.0f, 0.0f), false);
    const char* pageTitle = selectedPage == page::frames ? "프레임 그래프" :
        selectedPage == page::timeline ? "타임라인" :
        selectedPage == page::cpu ? "CPU" :
        selectedPage == page::memory ? "메모리" :
        selectedPage == page::gpu ? "GPU" :
        selectedPage == page::network ? "네트워크" :
        selectedPage == page::animation ? "Animation Budget" :
        selectedPage == page::renderingLive ? "Rendering - Live" :
        selectedPage == page::hierarchy ? "Hierarchy" :
        selectedPage == page::flat ? "Flat" :
        selectedPage == page::threads ? "Threads" : "Collector";
    ImGui::TextUnformatted(pageTitle);
    ImGui::Separator();
    ImGui::BeginDisabled(capture_file_view::file_busy() || capture_file_view::file_state().clear_ticket != 0);
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
            ImGui::SameLine();
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
    case page::network: draw_telemetry(telemetry_page::network); break;
    case page::physics: draw_physics_telemetry(); break;
    case page::audio: draw_audio_telemetry(); break;
    case page::animation:
    {
        std::lock_guard<std::mutex> sceneLock(sceneStructureMutex);
        draw_animation_budget();
        break;
    }
    case page::renderingLive:
    {
        std::lock_guard<std::mutex> sceneLock(sceneStructureMutex);
        editor::DrawRenderLiveDiagnostics();
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
    ImGui::EndChild();
}
