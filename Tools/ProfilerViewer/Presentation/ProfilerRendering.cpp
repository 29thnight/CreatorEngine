// Rendering - Live presentation belongs only to the ordinary-privilege viewer.
// Engine data arrives through the immutable, bounded diagnostics snapshot; this
// translation unit deliberately has no renderer, RHI, scene, or editor registry.
#include "ProfilerLiveDiagnostics.h"
#include "ProfilerRenderingDiagnostics.h"
#include "ProfilerView.h"
#include "EditorIcons.h"
#include "ImGui.h"

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <vector>

namespace editor::profiler_view
{
    namespace rendering_ui
    {
        using namespace ce::profiler_viewer::diagnostics;

        constexpr ImVec4 kOkColor{ 0.49f, 0.88f, 0.72f, 1.0f };
        constexpr ImVec4 kWarnColor{ 0.96f, 0.78f, 0.36f, 1.0f };
        constexpr ImVec4 kErrorColor{ 0.94f, 0.44f, 0.47f, 1.0f };
        constexpr ImVec4 kDimColor{ 0.55f, 0.58f, 0.65f, 1.0f };

        void LabeledValue(const char* label, const char* value, const ImVec4& color)
        {
            ImGui::TextColored(kDimColor, "%s", label);
            ImGui::SameLine(125.f * ImGui::GetStyle().FontScaleMain * ImGui::GetStyle().FontScaleDpi);
            ImGui::TextColored(color, "%s", value);
        }

        void LabeledValue(const char* label, const char* value)
        {
            LabeledValue(label, value, ImGui::GetStyleColorVec4(ImGuiCol_Text));
        }

        void DrawShadowStats(const rendering_shadow_stats& stats)
        {
            if (!stats.valid)
            {
                ImGui::TextColored(kDimColor, "This view has not been prepared yet.");
                return;
            }
            if (!stats.hasDirectionalLight)
            {
                ImGui::TextColored(kWarnColor, "No directional light: this view renders without shadows.");
                return;
            }
            ImGui::Text("Light #%u  direction (%.3f, %.3f, %.3f)", stats.lightIndex,
                stats.lightDirection[0], stats.lightDirection[1], stats.lightDirection[2]);
            ImGui::Text("Distance %.2f  slope scale %.2f  non-graph casters %u",
                stats.shadowDistance, stats.slopeScale, stats.casterCandidates);

            if (stats.gpuVisibilityActive)
            {
                ImGui::Text("GPU shadow visibility: %llu submitted candidates / %llu bins",
                    static_cast<unsigned long long>(stats.gpuSubmittedCandidates),
                    static_cast<unsigned long long>(stats.gpuSubmittedBins));
                ImGui::TextColored(kDimColor, "Visible/culled counts are not read back from the GPU.");
            }

            constexpr ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg
                | ImGuiTableFlags_SizingFixedFit;
            if (!ImGui::BeginTable("ShadowCascades", 8, flags))
            {
                return;
            }
            ImGui::TableSetupColumn("Cascade");
            ImGui::TableSetupColumn("Ends at");
            ImGui::TableSetupColumn("Radius");
            ImGui::TableSetupColumn("Texel");
            ImGui::TableSetupColumn("Depth span");
            ImGui::TableSetupColumn("Bias");
            ImGui::TableSetupColumn("Bias (steep)");
            ImGui::TableSetupColumn("Graph casters");
            ImGui::TableHeadersRow();
            for (std::uint32_t index = 0; index < stats.cascades.size(); ++index)
            {
                const rendering_shadow_cascade& cascade = stats.cascades[index];
                // 셰이더는 tan 을 8 로 자른다 — 가장 비스듬한 면의 편향이다.
                const float steepBias = cascade.constantBias * (1.f + stats.slopeScale * 8.f);
                ImGui::TableNextRow();
                ImGui::TableNextColumn(); ImGui::Text("%u", index);
                ImGui::TableNextColumn(); ImGui::Text("%.2f", cascade.splitDepth);
                ImGui::TableNextColumn(); ImGui::Text("%.2f", cascade.radius);
                ImGui::TableNextColumn(); ImGui::Text("%.4f", cascade.worldTexel);
                ImGui::TableNextColumn(); ImGui::Text("%.2f", cascade.depthSpan);
                ImGui::TableNextColumn(); ImGui::Text("%.4f", cascade.constantBias);
                ImGui::TableNextColumn(); ImGui::Text("%.4f", steepBias);
                ImGui::TableNextColumn(); ImGui::Text("%u", cascade.graphCasters);
            }
            ImGui::EndTable();
            ImGui::TextColored(kDimColor, "Lengths are world units. Bias is along the light; steep = slope term at its cap.");
        }

        void DrawRenderRuntime(const rendering_snapshot& displayed)
        {
            const char* backendName = rendering_backend::vulkan == displayed.backend
                ? "Vulkan" : "DX12";
            char backendLabel[64]{};
            std::snprintf(backendLabel, sizeof(backendLabel),
                "EnhancedRenderer / %s", backendName);
            LabeledValue("Backend", backendLabel, kOkColor);
            LabeledValue("Live runner", displayed.enabled ? "on" : "off",
                displayed.enabled ? kOkColor : kErrorColor);
            LabeledValue("Pipeline", displayed.pipelineReady ? "ready" : "none",
                displayed.pipelineReady ? kOkColor : kErrorColor);

            char buffer[128]{};
            std::snprintf(buffer, sizeof(buffer), "%u x %u", displayed.width, displayed.height);
            LabeledValue("Render target", buffer);

            // 드로우 0은 파이프라인이 멀쩡해도 화면이 비는 유일한 조건이라
            // 따로 색을 준다 — 여기서 멈춰야 할 신호다.
            std::snprintf(buffer, sizeof(buffer), "%u draws / %u batches",
                displayed.drawCount, displayed.batchCount);
            LabeledValue("GBuffer", buffer, 0u == displayed.drawCount ? kWarnColor : kOkColor);
            std::snprintf(buffer, sizeof(buffer), "%u prepared batches (no GPU readback)",
                displayed.preparedMeshletBatchCount);
            LabeledValue("Mesh shader", buffer);
            std::snprintf(buffer, sizeof(buffer), "%llu compacted / %llu preserved bins",
                static_cast<unsigned long long>(displayed.preparedGpuCompactedBins),
                static_cast<unsigned long long>(displayed.preparedGpuPreservedBins));
            LabeledValue("Prepared indirect routes", buffer);
            LabeledValue("Indirect capabilities",
                displayed.indexedIndirectSupported
                    ? (displayed.nonIndexedIndirectSupported ? "Indexed + procedural" : "Indexed only")
                    : (displayed.nonIndexedIndirectSupported ? "Procedural only" : "Unavailable: hardware direct path"));
            std::snprintf(buffer, sizeof(buffer), "%llu / %llu candidates keep visible",
                static_cast<unsigned long long>(displayed.preparedGpuConservativeCandidates),
                static_cast<unsigned long long>(displayed.preparedGpuCandidates));
            LabeledValue("Conservative inputs", buffer);
            ImGui::TextColored(kDimColor, "Prepared input categories, not GPU-visible counts or culling measurements.");
            LabeledValue("Current-frame HZB", displayed.currentFrameOcclusion ? "Enabled" : "Frustum fallback");
            if (!displayed.occlusionFallback.empty())
            {
                LabeledValue("Occlusion fallback", displayed.occlusionFallback.c_str(), kWarnColor);
            }
            if (!displayed.skinningFallback.empty())
            {
                LabeledValue("Skin bounds fallback", displayed.skinningFallback.c_str(), kWarnColor);
            }
            if (!displayed.meshletFallback.empty())
            {
                LabeledValue("Indexed fallback", displayed.meshletFallback.c_str(), kWarnColor);
            }


            // 데칼은 없는 씬이 정상이라 0을 경고로 칠하지 않는다. 볼 것은
            // 둘의 관계다 — 데칼이 있는데 배치가 0이면 텍스처 운반이 실패한
            // 것이고, 그건 화면만 봐서는 '데칼이 원래 없나'와 구분되지 않는다.
            std::snprintf(buffer, sizeof(buffer), "%u decals / %u batches",
                displayed.decalCount, displayed.decalBatchCount);
            LabeledValue("Decal", buffer,
                (0u != displayed.decalCount && 0u == displayed.decalBatchCount)
                    ? kWarnColor : kDimColor);

            std::snprintf(buffer, sizeof(buffer), "%llu rendered / %llu idle / %llu in-flight skip",
                static_cast<unsigned long long>(displayed.framesRendered),
                static_cast<unsigned long long>(displayed.framesIdle),
                static_cast<unsigned long long>(displayed.framesInFlight));
            LabeledValue("Frames", buffer);

            // 묘지가 자라기만 하면 DisableLive가 놓은 공유 객체를 아무도
            // 회수하지 않고 있다는 뜻이다(ShutdownLive까지 남는다).
            std::snprintf(buffer, sizeof(buffer), "%llu",
                static_cast<unsigned long long>(displayed.graveyardCount));
            LabeledValue("Graveyard", buffer,
                0u == displayed.graveyardCount ? kDimColor : kWarnColor);
        }
    }

    void draw_rendering_live()
    {
        using namespace rendering_ui;
        using namespace ce::profiler_viewer::diagnostics;
        static bool m_sortByDuration = false;
        static int selectedTarget = 0;
        static std::uint64_t targetRevision = 0;
        const auto revision = live_target_revision();
        if (revision != targetRevision)
        {
            m_sortByDuration = false;
            selectedTarget = 0;
            targetRevision = revision;
        }
        const auto diagnostics = live_diagnostics();
        if (!diagnostics)
        {
            const char* status = live_diagnostic_status();
            ImGui::TextDisabled("%s", status[0] != '\0' ? status :
                "Waiting for live renderer diagnostics from the connected engine.");
            return;
        }
        const auto& displayed = diagnostics->rendering;

        ImGui::TextDisabled("Live renderer state - independent of Record and .ceprof selection");
        constexpr const char* targets[]{"Scene", "Game", "Material Preview"};
        ImGui::Combo("View", &selectedTarget, targets, 3);
        const auto& view = displayed.views[static_cast<std::size_t>(selectedTarget)];
        const bool gpuMatches = view.ready && view.viewId == displayed.lastGpuViewId &&
            displayed.lastGpuSubmissionId && !displayed.passTimings.empty();
        ImGui::Text("Completed frame %llu / view %llu / %u x %u / %s",
            static_cast<unsigned long long>(view.completedFrameId), static_cast<unsigned long long>(view.viewId),
            view.completedWidth, view.completedHeight, view.ready ? "ready" : "unavailable");
        ImGui::Text("Latest GPU sample: frame %llu / submission %llu / view %llu",
            static_cast<unsigned long long>(displayed.lastGpuFrameId),
            static_cast<unsigned long long>(displayed.lastGpuSubmissionId),
            static_cast<unsigned long long>(displayed.lastGpuViewId));
        if (displayed.lastGpuFrameId && displayed.consumedFrameId >= displayed.lastGpuFrameId)
        {
            ImGui::Text("Sample age: %llu engine frames", static_cast<unsigned long long>(displayed.consumedFrameId - displayed.lastGpuFrameId));
        }

        // ── 러너 상태 ──
        //
        // 화면이 비었을 때 원인이 여기서 갈린다: 러너가 꺼졌는가,
        // 파이프라인이 못 섰는가, 서긴 했는데 드로우가 0인가.
        if (ImGui::CollapsingHeader(EditorIcon::Label<EditorIcon::Runtime, " Runtime">, ImGuiTreeNodeFlags_DefaultOpen))
        {
            DrawRenderRuntime(displayed);
        }

        ImGui::BeginDisabled(!live_diagnostics_actions_enabled());
        if (ImGui::Button("Open RenderPass structure"))
        {
            request_rendering_command(rendering_command::open_render_pass);
        }
        ImGui::EndDisabled();
        if (const char* status = live_diagnostic_status(); status[0] != '\0')
        {
            ImGui::TextDisabled("%s", status);
        }

        // ── 그림자 캐스케이드 ── 위 View 에서 고른 뷰의 값이다.
        if (ImGui::CollapsingHeader("Shadow cascades"))
        {
            DrawShadowStats(displayed.shadow[static_cast<std::size_t>(selectedTarget)]);
        }

        // ── 프레임 비용 ──
        if (ImGui::CollapsingHeader(EditorIcon::Label<EditorIcon::Timing, " Frame cost">, ImGuiTreeNodeFlags_DefaultOpen))
        {
            char buffer[64]{};
            std::snprintf(buffer, sizeof(buffer), "%.3f ms", displayed.cpuMs);
            LabeledValue("CPU (all views)", buffer);
            std::snprintf(buffer, sizeof(buffer), "%.3f ms", displayed.gpuMs);
            LabeledValue("GPU (selected view)", gpuMatches ? buffer : "unavailable");
            ImGui::Text("GPU samples: %llu, rejected: %llu, query overflow: %llu",
                static_cast<unsigned long long>(displayed.gpuCollects),
                static_cast<unsigned long long>(displayed.gpuCollectMismatches),
                static_cast<unsigned long long>(displayed.gpuQueryOverflowPasses));
            if (!displayed.lastGpuCollectError.empty())
            {
                ImGui::TextWrapped("Last GPU collect error: %s", displayed.lastGpuCollectError.c_str());
            }
        }

        // ── 패스별 GPU 시간 ──
        if (ImGui::CollapsingHeader(EditorIcon::Label<EditorIcon::Layers, " Pass timings">, ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Checkbox("Sort by duration", &m_sortByDuration);
            ImGui::SameLine();
            ImGui::TextColored(kDimColor, "(%zu passes)", gpuMatches ? displayed.passTimings.size() : 0u);

            if (!gpuMatches)
            {
                ImGui::TextColored(kDimColor,
                    "No completed GPU sample for the selected view.");
                ImGui::TextColored(kDimColor,
                    "Samples from another view are never shown as this view.");
            }
            else
            {
                std::vector<rendering_pass_timing> ordered = displayed.passTimings;
                if (m_sortByDuration)
                {
                    std::stable_sort(ordered.begin(), ordered.end(),
                        [](const rendering_pass_timing& lhs, const rendering_pass_timing& rhs)
                        {
                            return lhs.milliseconds > rhs.milliseconds;
                        });
                }

                // 막대는 합계가 아니라 최대 패스를 기준으로 정규화한다.
                // 합계 기준이면 패스가 늘수록 전부 납작해져 비교가 안 된다.
                double slowest = 0.0;
                for (const rendering_pass_timing& timing : ordered)
                {
                    slowest = (std::max)(slowest, timing.milliseconds);
                }
                if (slowest <= 0.0)
                {
                    slowest = 1.0;
                }

                if (ImGui::BeginTable("PassTimings", 3,
                    ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                    ImGuiTableFlags_SizingStretchProp))
                {
                    ImGui::TableSetupColumn("Pass", ImGuiTableColumnFlags_WidthStretch, 0.45f);
                    ImGui::TableSetupColumn("ms", ImGuiTableColumnFlags_WidthStretch, 0.15f);
                    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch, 0.40f);
                    ImGui::TableHeadersRow();

                    for (const rendering_pass_timing& timing : ordered)
                    {
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::TextUnformatted(timing.name.c_str());

                        ImGui::TableSetColumnIndex(1);
                        ImGui::Text("%.3f", timing.milliseconds);

                        ImGui::TableSetColumnIndex(2);
                        const float fraction =
                            static_cast<float>(timing.milliseconds / slowest);
                        ImGui::ProgressBar(fraction, ImVec2(-FLT_MIN, 0.0f), "");
                    }
                    ImGui::EndTable();
                }

                // 큐 시간이라는 사실을 창에 남긴다 — 이 수치를 순수 GPU 작업
                // 시간으로 읽으면 앞 패스의 대기를 이 패스 비용으로 오해한다.
                ImGui::TextColored(kDimColor,
                    "Timestamps are queue time: a preceding pass's wait is included.");
                ImGui::TextColored(kDimColor,
                    "Read relative change under identical conditions, not absolutes.");
            }
        }

        // ── 검증 레이어 ──
        //
        // 콘솔에 한 번 찍히고 흘러가 버리던 것을 창에 모은다. 이 목록이
        // 비어 있지 않으면 화면이 멀쩡해 보여도 배선이 틀린 것이다.
        const char* validationBackendName =
            rendering_backend::vulkan == displayed.backend ? "Vulkan" : "D3D12";
        char validationHeader[64]{};
        std::snprintf(validationHeader, sizeof(validationHeader),
            EditorIcon::Label<EditorIcon::Warning, " %s validation">, validationBackendName);
        if (ImGui::CollapsingHeader(validationHeader,
            ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (displayed.validationMessages.empty())
            {
                ImGui::TextColored(kOkColor, "No messages observed");
            }
            else
            {
                ImGui::TextColored(kErrorColor, "%zu (first occurrence only)",
                    displayed.validationMessages.size());
                ImGui::BeginChild("ValidationMessages", ImVec2(0, 140.0f), true);
                for (const std::string& message : displayed.validationMessages)
                {
                    ImGui::TextWrapped("%s", message.c_str());
                    ImGui::Separator();
                }
                ImGui::EndChild();
            }
        }

        if (!displayed.lastError.empty())
        {
            ImGui::Separator();
            ImGui::TextColored(kErrorColor, EditorIcon::Label<EditorIcon::Error, " Last error">);
            ImGui::TextWrapped("%s", displayed.lastError.c_str());
        }
        if (displayed.omittedPassTimings || displayed.omittedValidationMessages || displayed.textTruncated)
        {
            ImGui::Separator();
            ImGui::TextColored(kWarnColor,
                "Snapshot limit: %llu pass timings / %llu validation messages omitted%s",
                static_cast<unsigned long long>(displayed.omittedPassTimings),
                static_cast<unsigned long long>(displayed.omittedValidationMessages),
                displayed.textTruncated ? "; long text shortened" : "");
        }

    }
}
