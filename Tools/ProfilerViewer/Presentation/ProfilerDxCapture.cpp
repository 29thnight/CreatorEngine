#include "ProfilerView.h"
#include "DxCaptureFile.h"
#include "ProfilerViewerClient.h"
#include "ImGui.h"

#include <algorithm>
#include <cinttypes>
#include <cmath>
#include <limits>
#include <atomic>
#include <exception>
#include <memory>
#include <utility>

namespace editor::profiler_view
{
    namespace
    {
        const char* association_label(ce::dx_capture::association_state state)
        {
            switch (state)
            {
            case ce::dx_capture::association_state::unique: return "Unique submit interval";
            case ce::dx_capture::association_state::ambiguous: return "Ambiguous submit";
            case ce::dx_capture::association_state::unmatched: return "Unmatched submit";
            }
            return "Unknown";
        }

        void draw_work_tracks(const ce::dx_capture::recording_snapshot& capture)
        {
            using namespace ce::dx_capture;
            const auto records = capture.records();
            const auto works = capture.work_links();
            const auto& summary = capture.summary();
            if (works.empty() || capture.session().qpc_frequency == 0)
            {
                ImGui::TextDisabled("No decoded GPU work in this capture. Missing data is not zero GPU activity.");
                return;
            }
            const double frequency = static_cast<double>(capture.session().qpc_frequency);
            const std::uint64_t origin = summary.first_qpc;
            const double duration = static_cast<double>(summary.last_qpc - origin) / frequency;
            static float start_fraction = 0.0f;
            static float visible_seconds = 0.1f;
            ImGui::SliderFloat("Position", &start_fraction, 0.0f, 1.0f, "%.3f");
            ImGui::SliderFloat("Visible seconds", &visible_seconds, 0.001f,
                              static_cast<float>((std::max)(duration, 0.001)), "%.3f",
                              ImGuiSliderFlags_Logarithmic);
            const double span = (std::max)(static_cast<double>(visible_seconds), 0.001);
            const double begin = (std::max)(duration - span, 0.0) * start_fraction;
            ImGui::Text("Axis: %.3f .. %.3f ms from capture data start | gray: submit wait | blue: GPU work",
                        begin * 1000.0, (begin + span) * 1000.0);
            ImGui::TextDisabled("ETW-calibrated GPU intervals, not an exclusive GPU busy sum. Rows may overlap.");

            if (!ImGui::BeginTable("DxGpuWork", 4, ImGuiTableFlags_BordersInnerV |
                                  ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
                                  ImGuiTableFlags_Resizable, ImVec2(0.0f, 0.0f)))
            {
                return;
            }
            ImGui::TableSetupColumn("ETW queue / execution", ImGuiTableColumnFlags_WidthFixed, 175.0f);
            ImGui::TableSetupColumn("Engine frame / submission", ImGuiTableColumnFlags_WidthFixed, 190.0f);
            ImGui::TableSetupColumn("Submit to execution", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Work ms", ImGuiTableColumnFlags_WidthFixed, 85.0f);
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableHeadersRow();
            const float row_height = ImGui::GetTextLineHeightWithSpacing() + 3.0f;
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(works.size()), row_height);
            while (clipper.Step())
            {
                for (int index = clipper.DisplayStart; index < clipper.DisplayEnd; ++index)
                {
                    const work_link& link = works[static_cast<std::size_t>(index)];
                    const record& work = records[link.work_record];
                    const bool calibrated = (work.flags & record_flags::calibrated_qpc) != 0 &&
                                            work.qpc_begin != 0 && work.qpc_end >= work.qpc_begin;
                    const record* submission = link.submission_record == no_record
                        ? nullptr : &records[link.submission_record];
                    const record* execution = link.execution_index == no_record ? nullptr
                        : &records[capture.execution_links()[link.execution_index].begin_record];
                    ImGui::PushID(index);
                    ImGui::TableNextRow(ImGuiTableRowFlags_None, row_height);
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("Q%" PRIu64 " / E%" PRIu64, work.api_queue_id, work.execution_id);
                    ImGui::TableSetColumnIndex(1);
                    if (submission && (submission->flags & record_flags::engine_correlated) != 0)
                    {
                        ImGui::Text("F%" PRIu64 " / S%" PRIu64,
                                    submission->engine_frame_id, submission->submission_id);
                    }
                    else if (submission)
                    {
                        ImGui::TextDisabled("Native submit; query ID unknown");
                    }
                    else
                    {
                        ImGui::TextDisabled("%s", association_label(link.association));
                    }
                    ImGui::TableSetColumnIndex(2);
                    const ImVec2 pos = ImGui::GetCursorScreenPos();
                    const float width = (std::max)(ImGui::GetContentRegionAvail().x, 20.0f);
                    ImGui::InvisibleButton("##WorkInterval", ImVec2(width, row_height - 3.0f));
                    ImDrawList* draw = ImGui::GetWindowDrawList();
                    const auto seconds = [&](std::uint64_t ticks)
                    {
                        return ticks >= origin ? static_cast<double>(ticks - origin) / frequency
                                               : -static_cast<double>(origin - ticks) / frequency;
                    };
                    const auto bar = [&](std::uint64_t first, std::uint64_t last, ImU32 color, float inset)
                    {
                        if (last < first || seconds(last) < begin || seconds(first) > begin + span)
                        {
                            return;
                        }
                        const float x0 = pos.x + static_cast<float>((std::clamp)((seconds(first) - begin) / span, 0.0, 1.0)) * width;
                        const float x1 = pos.x + static_cast<float>((std::clamp)((seconds(last) - begin) / span, 0.0, 1.0)) * width;
                        draw->AddRectFilled(ImVec2(x0, pos.y + inset),
                            ImVec2((std::min)(pos.x + width, (std::max)(x1, x0 + 1.0f)),
                                   pos.y + row_height - 4.0f - inset), color, 2.0f);
                    };
                    draw->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + row_height - 4.0f),
                                        IM_COL32(28, 30, 35, 255));
                    if (calibrated && execution && (execution->flags & record_flags::has_cpu_submit) != 0 &&
                        execution->cpu_submit_qpc <= work.qpc_begin)
                    {
                        bar(execution->cpu_submit_qpc, work.qpc_begin, IM_COL32(135, 135, 145, 255), 5.0f);
                    }
                    if (calibrated)
                    {
                        bar(work.qpc_begin, work.qpc_end, IM_COL32(85, 175, 230, 255), 1.0f);
                    }
                    else
                    {
                        draw->AddText(pos, IM_COL32(195, 175, 135, 255), "QPC conversion unavailable");
                    }
                    if (ImGui::IsItemHovered())
                    {
                        ImGui::BeginTooltip();
                        ImGui::Text("%s", association_label(link.association));
                        ImGui::Text("HW queue %" PRIu64 " | list token %u | raw API marker %" PRIu64,
                                    work.hardware_queue_id, work.command_list_index, work.api_marker_id);
                        ImGui::Text("QPC interval %" PRIu64 " .. %" PRIu64, work.qpc_begin, work.qpc_end);
                        ImGui::Text("Source ns %" PRIu64 " .. %" PRIu64, work.source_begin_ns, work.source_end_ns);
                        if (execution)
                        {
                            ImGui::Text("CPU submit QPC %" PRIu64 " | PID %u TID %u",
                                        execution->cpu_submit_qpc, execution->process_id, execution->thread_id);
                        }
                        if (submission)
                        {
                            ImGui::Text("Engine queue pointer 0x%" PRIx64 " | view %" PRIu64,
                                        submission->api_queue_id, submission->render_view_id);
                        }
                        ImGui::TextDisabled("PIX marker text/scopes are not decoded in this version.");
                        ImGui::EndTooltip();
                    }
                    ImGui::TableSetColumnIndex(3);
                    if (calibrated)
                    {
                        ImGui::Text("%.4f", static_cast<double>(work.qpc_end - work.qpc_begin) * 1000.0 / frequency);
                    }
                    else
                    {
                        ImGui::TextDisabled("Unavailable");
                    }
                    ImGui::PopID();
                }
            }
            ImGui::EndTable();
        }
    }

    namespace
    {
        struct dx_operation
        {
            std::atomic_bool done{ false };
            std::atomic_bool canceled{ false };
            ce::profiler_viewer::source_artifact_ptr artifact;
            ce::dx_capture::recording_snapshot_ptr capture;
            std::filesystem::path path;
            std::filesystem::path destination;
            bool show = true;
            std::string error = "DX file operation was not accepted by the worker";
        };

        struct dx_admission
        {
            explicit dx_admission(std::shared_ptr<dx_operation> operation) : operation(std::move(operation)) {}
            ~dx_admission()
            {
                if (!started.load(std::memory_order_acquire))
                {
                    operation->done.store(true, std::memory_order_release);
                }
            }
            std::shared_ptr<dx_operation> operation;
            std::atomic_bool started{ false };
        };

        struct dx_view_state
        {
            std::shared_ptr<dx_operation> operation;
            ce::dx_capture::recording_snapshot_ptr capture;
            ce::profiler_viewer::source_artifact_ptr seen_source;
            std::filesystem::path path;
            std::filesystem::path destination;
            bool requested = false;
            std::uint64_t start_command_revision = 0;
            bool start_acknowledged = false;
            bool show_remote = false;
            std::string message;
        };

        dx_view_state view;

        void begin_open(const std::filesystem::path& path,
                        ce::profiler_viewer::source_artifact_ptr artifact = {},
                        const std::filesystem::path& destination = {}, bool show = true)
        {
            if (view.operation || path.empty())
            {
                return;
            }
            auto operation = std::make_shared<dx_operation>();
            operation->path = path;
            operation->destination = destination;
            operation->artifact = std::move(artifact);
            operation->show = show;
            view.operation = operation;
            view.message.clear();
            try
            {
                const auto admission = std::make_shared<dx_admission>(operation);
                dispatch_work([operation, admission]
                {
                    admission->started.store(true, std::memory_order_release);
                    operation->error.clear();
                    try
                    {
                        if (!operation->artifact)
                        {
                            const auto artifact = ce::profiler_viewer::open_source_artifact(operation->path);
                            if (!artifact)
                            {
                                operation->error = "Open failed: " + artifact.error();
                                operation->done.store(true, std::memory_order_release);
                                return;
                            }
                            operation->artifact = *artifact;
                        }
                        const auto loaded = ce::dx_capture::read_recording(operation->artifact->path());
                        if (!loaded)
                        {
                            operation->error = std::string("Open failed: ") + ce::dx_capture::describe(loaded.error());
                        }
                        else
                        {
                            operation->capture = *loaded;
                            if (!operation->destination.empty() && !operation->canceled.load(std::memory_order_acquire))
                            {
                                copy_dx_capture_file(operation->artifact->path(), operation->destination, operation->error);
                            }
                        }
                    }
                    catch (const std::exception& error)
                    {
                        operation->error = error.what();
                    }
                    catch (...)
                    {
                        operation->error = "Unexpected DX file worker error";
                    }
                    operation->done.store(true, std::memory_order_release);
                });
            }
            catch (const std::exception& error)
            {
                view.message = error.what();
                view.operation.reset();
            }
        }
    }

    void open_dx_capture(const std::filesystem::path& path)
    {
        if (path.empty() || view.operation)
        {
            return;
        }
        view.show_remote = false;
        begin_open(path);
    }

    void shutdown_dx_capture()
    {
        if (view.operation)
        {
            view.operation->canceled.store(true, std::memory_order_release);
        }
        view = {};
    }

    void poll_dx_capture()
    {
        if (view.operation && view.operation->done.load(std::memory_order_acquire))
        {
            const auto operation = std::exchange(view.operation, {});
            if (!operation->canceled.load(std::memory_order_acquire))
            {
                view.message = operation->error;
                if (operation->capture && operation->show)
                {
                    view.capture = operation->capture;
                    view.path = operation->destination.empty() ? operation->path : operation->destination;
                }
                if (operation->error.empty())
                {
                    view.message = operation->destination.empty() ? "Capture opened" : "DX capture exported";
                }
            }
        }
        const auto remote = source().snapshot();
        if (remote->dx_source && remote->dx_source != view.seen_source && !view.operation)
        {
            view.seen_source = remote->dx_source;
            const auto destination = view.requested ? view.destination : std::filesystem::path{};
            const bool show = view.requested && view.show_remote;
            view.requested = false;
            view.destination.clear();
            // A newly attached viewer can inspect the target's last completed DX
            // capture, but a user's explicit file selection is never overwritten.
            if (show || !destination.empty() || !view.capture)
            {
                begin_open(remote->dx_source->path(), remote->dx_source, destination, show || !view.capture);
            }
        }
        if (view.requested && !view.start_acknowledged &&
            remote->command_revision > view.start_command_revision &&
            remote->last_command == ce::profiler_viewer::command::start_dx)
        {
            view.start_acknowledged = true;
            if (!remote->command_accepted)
            {
                view.requested = false;
                view.destination.clear();
                view.message = remote->command_message.empty() ? "The target rejected DX capture" : remote->command_message;
            }
        }
        if (view.requested && view.start_acknowledged && !remote->dx_status.busy &&
            !remote->dx_status.recording.finalized && remote->dx_status.recording.record_count == 0 &&
            remote->dx_source == view.seen_source)
        {
            view.requested = false;
            view.destination.clear();
            view.message = remote->dx_status.message.empty() ? "DX capture completed without an artifact" : remote->dx_status.message;
        }
        if (!remote->connected && view.requested)
        {
            view.requested = false;
            view.message = "Target disconnected before a completed DX artifact arrived; no export was made";
            view.destination.clear();
        }
    }

    void draw_dx_capture()
    {
        using namespace ce::dx_capture;
        const auto remote = source().snapshot();
        const capture_status& status = remote->dx_status;
        ImGui::TextWrapped("DX12 deep capture uses a headless ETW helper. Record for up to 60 seconds into a new .cedx file.");
        ImGui::TextWrapped("This version only runs an ordinary-privilege helper. Automatic and manual elevation are blocked pending decoder security validation.");
        ImGui::TextDisabled("If existing ETW permissions are insufficient, deep capture reports unavailable.");
        ImGui::BeginDisabled(!remote->connected || remote->command_pending || !remote->dx_available || status.busy || view.operation || view.requested);
        if (ImGui::Button("Start DX12 Deep Capture"))
        {
            const auto path = pick_dx_capture_to_save();
            if (!path.empty())
            {
                if (source().request(ce::profiler_viewer::command::start_dx))
                {
                    view.destination = path;
                    view.requested = true;
                    view.start_acknowledged = false;
                    view.start_command_revision = remote->command_revision;
                    view.show_remote = true;
                    view.seen_source = remote->dx_source;
                    view.message = "Waiting for target capture; the viewer will export the completed artifact";
                }
                else
                {
                    view.message = "Start was not sent: target disconnected or command queue full";
                }
            }
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(static_cast<bool>(view.operation));
        if (ImGui::Button("Open .cedx"))
        {
            open_dx_capture(pick_dx_capture_to_open());
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!remote->connected || remote->command_pending || !status.busy || status.opening);
        if (ImGui::Button("Stop Deep Capture"))
        {
            if (!source().request(ce::profiler_viewer::command::stop_dx))
            {
                view.message = "Stop was not sent: target disconnected or command queue full";
            }
        }
        ImGui::EndDisabled();
        ImGui::TextWrapped("%s", status.message.c_str());
        if (!view.message.empty())
        {
            ImGui::TextWrapped("%s", view.message.c_str());
        }
        if (view.operation)
        {
            ImGui::TextDisabled("Opening / exporting DX capture...");
        }
        const auto filename = view.path.filename().u8string();
        const std::string path_text(filename.begin(), filename.end());
        ImGui::Text("File: %s | records %u", path_text.c_str(),
                    view.capture ? view.capture->summary().record_count : 0);
        if (remote->target.connection.target_pid)
        {
            ImGui::TextDisabled("Target helper: error %u | transport drops %" PRIu64,
                status.process.win32_error, status.process.dropped_records);
        }
        ImGui::TextDisabled("PIX scope decoding, PSO/residency, CPU context switches and Vulkan are outside this capture mode.");
        const recording_snapshot_ptr capture = view.capture;
        if (!capture)
        {
            return;
        }
        const auto& summary = capture->summary();
        ImGui::Separator();
        ImGui::Text("%s | work %u | submissions %u | unmatched %u | ambiguous %u",
                    summary.complete ? "Finalized" : "Incomplete / recovered prefix", summary.work_count,
                    summary.submission_count, summary.unmatched_executions, summary.ambiguous_executions);
        if (!summary.complete || summary.reported_loss_count || summary.dropped_records)
        {
            ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.25f, 1.0f),
                "Capture issues 0x%x | reported loss %" PRIu64 " | writer drops %" PRIu64,
                summary.issues, summary.reported_loss_count, summary.dropped_records);
        }
        ImGui::Text("ETW events lost %" PRIu64 " | ETW buffers lost %" PRIu64,
                    summary.etw_events_lost, summary.etw_buffers_lost);
        draw_work_tracks(*capture);
    }
}
