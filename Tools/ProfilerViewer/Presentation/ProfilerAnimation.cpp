#include "ProfilerView.h"

#include "ProfilerLiveDiagnostics.h"
#include "ImGui.h"

#include <algorithm>
#include <cstdint>
#include <string>

namespace editor::profiler_view
{
    namespace animation = ce::profiler_viewer::diagnostics;
    namespace
    {
        const char* task_name(animation::task_kind kind)
        {
            using animation::task_kind;
            switch (kind)
            {
            case task_kind::sample_clip: return "Sample clip";
            case task_kind::blend: return "Blend";
            case task_kind::materialize: return "Materialize";
            case task_kind::prepare_composite: return "Prepare layers";
            case task_kind::blend_masked: return "Blend mask";
            case task_kind::make_additive: return "Make additive";
            case task_kind::apply_additive: return "Apply additive";
            case task_kind::materialize_composite: return "Materialize layers";
            case task_kind::two_bone_ik: return "Two-bone IK";
            case task_kind::bone_transform: return "Bone correction";
            case task_kind::output: return "Output";
            }
            return "Unknown";
        }

        void draw_task_index(std::uint32_t index)
        {
            if (index == animation::invalid_task)
            {
                ImGui::TextDisabled("-");
            }
            else
            {
                ImGui::Text("%u", index);
            }
        }
    }

    void draw_animation_budget()
    {
        static std::uint64_t selectedAnimatorId{};
        static std::uint64_t targetRevision{};
        static std::uint32_t sceneId{};
        const auto diagnostics = live_diagnostics();
        const auto revision = live_target_revision();
        if (revision != targetRevision || (diagnostics && diagnostics->scene_id != sceneId))
        {
            selectedAnimatorId = 0;
            targetRevision = revision;
            sceneId = diagnostics ? diagnostics->scene_id : 0;
        }
        request_animation_snapshot(selectedAnimatorId);
        const auto snapshot = diagnostics ? diagnostics->animation : nullptr;
        ImGui::TextDisabled("Live scene data; independent of the selected .ceprof capture.");
        if (const char* status = live_diagnostic_status(); status[0] != '\0')
        {
            ImGui::TextDisabled("%s", status);
        }
        if (!snapshot)
        {
            ImGui::TextDisabled("Waiting for an animation frame.");
            return;
        }

        ImGui::Text("Frame %llu   Registered %llu   Evaluated %llu   Degraded %llu",
            static_cast<unsigned long long>(snapshot->frame),
            static_cast<unsigned long long>(snapshot->registered),
            static_cast<unsigned long long>(snapshot->evaluated),
            static_cast<unsigned long long>(snapshot->degraded));
        const bool overBudget = snapshot->measuredUs > snapshot->budgetUs;
        if (overBudget)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, .45f, .3f, 1.f));
        }
        ImGui::Text("Pose CPU %.3f ms measured / %.3f ms budget   (predicted %.3f ms)",
            snapshot->measuredUs / 1000., snapshot->budgetUs / 1000.,
            snapshot->predictedUs / 1000.);
        if (overBudget)
        {
            ImGui::PopStyleColor();
        }
        const float budgetFraction = snapshot->budgetUs > 0.
            ? static_cast<float>((std::min)(snapshot->measuredUs / snapshot->budgetUs, 1.))
            : 0.f;
        ImGui::ProgressBar(budgetFraction, ImVec2(-1.f, 0.f));
        if (snapshot->animator_count > snapshot->animators.size() ||
            snapshot->task_count > snapshot->tasks.size() || snapshot->strings_truncated)
        {
            ImGui::TextWrapped("Bounded live snapshot: %zu / %llu animators, %zu / %llu tasks.%s",
                snapshot->animators.size(), static_cast<unsigned long long>(snapshot->animator_count),
                snapshot->tasks.size(), static_cast<unsigned long long>(snapshot->task_count),
                snapshot->strings_truncated ? " Some labels were shortened." : "");
        }

        if (ImGui::CollapsingHeader("Quality stages", ImGuiTreeNodeFlags_DefaultOpen))
        {
            for (std::size_t stage = 0; stage < snapshot->stages.size(); ++stage)
            {
                const float fraction = snapshot->animator_count == 0 ? 0.f
                    : static_cast<float>(snapshot->stages[stage])
                        / static_cast<float>(snapshot->animator_count);
                ImGui::Text("L%zu  %llu", stage,
                    static_cast<unsigned long long>(snapshot->stages[stage]));
                ImGui::SameLine(100.f);
                ImGui::PushID(static_cast<int>(stage));
                ImGui::ProgressBar(fraction, ImVec2(-1.f, 0.f), "");
                ImGui::PopID();
            }
        }

        ImGui::SeparatorText("Animators");
        const float rowHeight = ImGui::GetTextLineHeightWithSpacing();
        const float actorTableHeight = rowHeight
            * static_cast<float>((std::min)((std::max)(snapshot->animators.size(), std::size_t{1}), std::size_t{8}) + 1)
            + 8.f;
        if (ImGui::BeginTable("AnimationBudgetActors", 6,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollX
            | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit,
            ImVec2(0.f, actorTableHeight)))
        {
            ImGui::TableSetupColumn("Animator", ImGuiTableColumnFlags_WidthFixed, 250.f);
            ImGui::TableSetupColumn("Stage", ImGuiTableColumnFlags_WidthFixed, 80.f);
            ImGui::TableSetupColumn("Reason", ImGuiTableColumnFlags_WidthFixed, 400.f);
            ImGui::TableSetupColumn("Pred.", ImGuiTableColumnFlags_WidthFixed, 110.f);
            ImGui::TableSetupColumn("CPU ms", ImGuiTableColumnFlags_WidthFixed, 110.f);
            ImGui::TableSetupColumn("Result", ImGuiTableColumnFlags_WidthFixed, 140.f);
            ImGui::TableHeadersRow();
            for (const auto& actor : snapshot->animators)
            {
                ImGui::PushID(static_cast<int>(actor.id));
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                const bool selected = selectedAnimatorId == actor.id
                    || (selectedAnimatorId == 0 && snapshot->selectedAnimatorId == actor.id);
                if (ImGui::Selectable(actor.name.c_str(), selected,
                    ImGuiSelectableFlags_SpanAllColumns))
                {
                    selectedAnimatorId = actor.id;
                    request_animation_snapshot(selectedAnimatorId, true);
                }
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("L%d", static_cast<int>(actor.stage));
                ImGui::TableSetColumnIndex(2);
                ImGui::TextUnformatted(actor.reason.c_str());
                ImGui::TableSetColumnIndex(3);
                ImGui::Text("%.3f", actor.predictedUs / 1000.);
                ImGui::TableSetColumnIndex(4);
                ImGui::Text("%.3f", actor.measuredUs / 1000.);
                ImGui::TableSetColumnIndex(5);
                ImGui::TextUnformatted(!actor.evaluated ? "Skipped"
                    : actor.interpolated ? "Interpolated" : "Executed");
                ImGui::PopID();
            }
            ImGui::EndTable();
        }

        ImGui::SeparatorText("Recorded task execution");
        if (snapshot->selectedAnimatorId == 0)
        {
            ImGui::TextDisabled("Select an active Animator to inspect its tasks.");
            return;
        }
        ImGui::Text("Animator #%llu   Pose buffers: instance %llu, worker %llu",
            static_cast<unsigned long long>(snapshot->selectedAnimatorId),
            static_cast<unsigned long long>(snapshot->instancePoseBuffers),
            static_cast<unsigned long long>(snapshot->workerPoseBuffers));
        if (ImGui::TreeNode("Pose storage diagnostics"))
        {
            ImGui::TextDisabled("Target address labels only; no remote memory access.");
            ImGui::Text("Worker pool: 0x%llX", static_cast<unsigned long long>(snapshot->workerPosePool));
            ImGui::Text("Worker storage: 0x%llX", static_cast<unsigned long long>(snapshot->workerCurrentStorage));
            ImGui::Text("Instance pose: 0x%llX", static_cast<unsigned long long>(snapshot->instancePoseStorage));
            ImGui::TreePop();
        }
        ImGui::TextDisabled("Execution order is recorded by the executor; skipped tasks have no order.");
        const float taskTableHeight = rowHeight
            * static_cast<float>((std::min)((std::max)(snapshot->tasks.size(), std::size_t{1}), std::size_t{8}) + 1)
            + 8.f;
        if (ImGui::BeginTable("AnimationTaskSnapshot", 7,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollX
            | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit,
            ImVec2(0.f, taskTableHeight)))
        {
            ImGui::TableSetupColumn("Index", ImGuiTableColumnFlags_WidthFixed, 65.f);
            ImGui::TableSetupColumn("Task", ImGuiTableColumnFlags_WidthFixed, 180.f);
            ImGui::TableSetupColumn("Deps", ImGuiTableColumnFlags_WidthFixed, 90.f);
            ImGui::TableSetupColumn("Clip", ImGuiTableColumnFlags_WidthFixed, 55.f);
            ImGui::TableSetupColumn("Reach", ImGuiTableColumnFlags_WidthFixed, 95.f);
            ImGui::TableSetupColumn("Order", ImGuiTableColumnFlags_WidthFixed, 120.f);
            ImGui::TableSetupColumn("Buffer owner", ImGuiTableColumnFlags_WidthFixed, 500.f);
            ImGui::TableHeadersRow();
            for (const auto& task : snapshot->tasks)
            {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%u", task.index);
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(task_name(task.kind));
                ImGui::TableSetColumnIndex(2);
                draw_task_index(task.dependencyA);
                ImGui::SameLine(0.f, 2.f);
                ImGui::TextUnformatted("/");
                ImGui::SameLine(0.f, 2.f);
                draw_task_index(task.dependencyB);
                ImGui::TableSetColumnIndex(3);
                if (task.clipIndex >= 0)
                {
                    ImGui::Text("%d", task.clipIndex);
                }
                else
                {
                    ImGui::TextDisabled("-");
                }
                ImGui::TableSetColumnIndex(4);
                ImGui::TextUnformatted(task.reachable ? "Yes" : "No");
                ImGui::TableSetColumnIndex(5);
                if (task.executionOrder != animation::invalid_task)
                {
                    ImGui::Text("#%u", task.executionOrder);
                }
                else
                {
                    ImGui::TextDisabled("Skipped");
                }
                ImGui::TableSetColumnIndex(6);
                ImGui::TextUnformatted(task.bufferOwner.c_str());
            }
            ImGui::EndTable();
        }
    }
}
