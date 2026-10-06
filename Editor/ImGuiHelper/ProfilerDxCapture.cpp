#include "ProfilerView.h"
#include "DxCaptureService.h"
#include "ImGui.h"

#include <algorithm>
#include <cinttypes>
#include <cmath>
#include <limits>

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

    void draw_dx_capture()
    {
        using namespace ce::dx_capture;
        capture_service& service = deep_capture();
        const capture_status status = service.status();
        ImGui::TextWrapped("DX12 deep capture uses a headless ETW helper. Record for up to 60 seconds into a new .cedx file.");
        ImGui::TextWrapped("This version only runs an ordinary-privilege helper. Automatic and manual elevation are blocked pending decoder security validation.");
        ImGui::BeginDisabled(status.busy);
        ImGui::TextDisabled("If existing ETW permissions are insufficient, deep capture reports unavailable.");
#if CE_DX_TIMING_CAPTURE && !CE_SHIPPING
        if (ImGui::Button("Start DX12 Deep Capture"))
        {
            auto path = pick_dx_capture_to_save();
            if (!path.empty())
            {
                if (path.extension().empty())
                {
                    path.replace_extension(L".cedx");
                }
                service.start(path, false);
            }
        }
#else
        ImGui::TextDisabled("Live capture is disabled in this build (EngineDxDeepCapture). Offline viewing is available.");
#endif
        ImGui::SameLine();
        if (ImGui::Button("Open .cedx"))
        {
            const auto path = pick_dx_capture_to_open();
            if (!path.empty())
            {
                service.open(path);
            }
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!status.busy || status.opening);
        if (ImGui::Button("Stop Deep Capture"))
        {
            service.request_stop();
        }
        ImGui::EndDisabled();
        ImGui::TextWrapped("%s", status.message.c_str());
        const auto filename = status.path.filename().u8string();
        const std::string path_text(filename.begin(), filename.end());
        ImGui::Text("File: %s | records %u | helper error %u | transport drops %" PRIu64,
                    path_text.c_str(), status.recording.record_count, status.process.win32_error,
                    status.process.dropped_records);
        ImGui::TextDisabled("PIX scope decoding, PSO/residency, CPU context switches and Vulkan are outside this capture mode.");
        const recording_snapshot_ptr capture = service.snapshot();
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
