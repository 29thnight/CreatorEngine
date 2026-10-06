#include "EnhancedRenderDebugWindow.h"
#include "EditorWindowNames.h"
#include "Windows/EditorStandardWindows.h"
#include "Render/Scene/EnhancedSceneRenderer.h"
#include "EditorIcons.h"
#include "ProfilerHUD.h"
#include "EditorWindowRegistry.h"
#include "EditorSettingsStore.h"
#include "RuntimeSettings.h"
#include "Windows/EditorToolboxWindows.h"

#include <iterator>

// 익명 네임스페이스가 아니라 이름을 준다. 이 프로젝트는 유니티 빌드라
// (EnableUnitySupport) 여러 .cpp가 한 TU로 합쳐지는데, ResourceCounterWindow.cpp도
// 익명 네임스페이스에 kRefreshIntervalSeconds·kDimColor를 두고 있어 같은 묶음에
// 들어가면 재정의로 깨진다.
namespace EnhancedRenderDebugUi
{
	// 스냅샷은 값 복사라 싸지만, 패스 시간은 프레임마다 출렁여서 매 프레임
	// 갱신하면 숫자가 읽히지 않는다. 눈이 따라갈 수 있는 주기를 둔다.
	constexpr double kRefreshIntervalSeconds = 0.25;

	constexpr ImVec4 kWarnColor{ 0.96f, 0.78f, 0.36f, 1.0f };
	constexpr ImVec4 kDimColor{ 0.55f, 0.58f, 0.65f, 1.0f };

	// EnhancedShadowDebugView 순서 그대로다(셰이더의 CASCADED_SHADOW_DEBUG_* 와도 같다).
	constexpr const char* kShadowDebugViews[]{
		"Off", "Cascade index", "Texel grid", "Shadow term only",
		"Depth delta (bias)", "Missing samples", "Shadow contrast" };

	// EnhancedShadowFilter 순서 그대로다(셰이더의 CASCADED_SHADOW_FILTER_* 와도 같다).
	constexpr const char* kShadowFilters[]{
		"Hardware 2x2 (1 tap)", "Tent 3x3 (4 taps)", "Tent 5x5 (9 taps)", "Tent 7x7 (16 taps)" };

	// 고른 보기의 색 뜻. 색만 칠하고 읽는 법을 안 적으면 다음 사람이 다시 쫓는다.
	const char* ShadowDebugViewLegend(int view)
	{
		switch (view)
		{
		case 1: return "Red/green/blue = cascade 0/1/2, blended near splits. Gray = beyond shadow distance, magenta = outside the light box.";
		case 2: return "Each checker cell is one shadow-map texel. Cells many pixels wide mean blocky edges; moire means the texel is finer than a pixel.";
		case 3: return "Visibility of the shadowing light only (1 lit, 0 occluded), without any lighting.";
		case 4: return "Red = occluded beyond the bias. Yellow = only the bias keeps it lit (acne without it; with too much, contact shadows detach). Green = lit.";
		case 5: return "Gray = beyond shadow distance, magenta = outside the light box. Both silently sample as lit.";
		case 6: return "How dark a full shadow could make each pixel: blue = invisible (ambient dominates the light), yellow = black.";
		default: return "";
		}
	}

	// 캐스케이드 수치 표. 텍셀 폭을 장면의 물체 크기와 대 보라는 표다 —
	// 1.8 짜리 캐릭터에 텍셀이 0.25 면 그 그림자는 일곱 칸짜리 덩어리다.
	void DrawShadowStats(const EnhancedLiveShadowStats& stats)
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
		if (!ImGui::BeginTable("ShadowCascades", 8, flags)) return;
		ImGui::TableSetupColumn("Cascade");
		ImGui::TableSetupColumn("Ends at");
		ImGui::TableSetupColumn("Radius");
		ImGui::TableSetupColumn("Texel");
		ImGui::TableSetupColumn("Depth span");
		ImGui::TableSetupColumn("Bias");
		ImGui::TableSetupColumn("Bias (steep)");
		ImGui::TableSetupColumn("Graph casters");
		ImGui::TableHeadersRow();
		for (uint32_t index = 0; index < stats.cascades.size(); ++index)
		{
			const EnhancedLiveShadowCascade& cascade = stats.cascades[index];
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

    const char* GraphStateName(RHIResourceState state)
    {
        switch (state)
        {
        case RHIResourceState::Common: return "Common";
        case RHIResourceState::RenderTarget: return "RenderTarget";
        case RHIResourceState::DepthWrite: return "DepthWrite";
        case RHIResourceState::DepthRead: return "DepthRead";
        case RHIResourceState::ShaderResource: return "ShaderResource";
        case RHIResourceState::PixelShaderResource: return "PixelShaderResource";
        case RHIResourceState::DepthReadShaderResource: return "DepthReadShaderResource";
        case RHIResourceState::UnorderedAccess: return "UnorderedAccess";
        case RHIResourceState::CopySource: return "CopySource";
        case RHIResourceState::CopyDest: return "CopyDest";
        case RHIResourceState::IndexBuffer: return "IndexBuffer";
        case RHIResourceState::IndirectArgument: return "IndirectArgument";
        case RHIResourceState::VertexAndShaderResource: return "VertexAndShaderResource";
        default: return "Unknown";
        }
    }

    const char* GraphAccessName(RGAccessMode access)
    {
        switch (access)
        {
        case RGAccessMode::Read: return "Read";
        case RGAccessMode::Write: return "Write";
        case RGAccessMode::ReadWrite: return "Modify";
        default: return "LegacyState";
        }
    }

    const char* GraphEdgeReason(EnhancedRenderGraph::DiagnosticSnapshot::VersionEdge::Reason reason)
    {
        using Reason = EnhancedRenderGraph::DiagnosticSnapshot::VersionEdge::Reason;
        switch (reason)
        {
        case Reason::RAW: return "RAW";
        case Reason::WAR: return "WAR";
        case Reason::WAW: return "WAW";
        default: return "Unknown";
        }
    }

    void DrawCompiledGraph(const EnhancedRenderGraph::DiagnosticSnapshot& snapshot,
        const ImGuiTextFilter& filter)
    {
        const auto resourceName = [&snapshot](uint32_t index)
        {
            return index < snapshot.resources.size() ? snapshot.resources[index].name.c_str() : "Invalid resource";
        };
        if (!ImGui::BeginTabBar("CompiledGraphDetails"))
        {
            return;
        }
        if (ImGui::BeginTabItem("Passes"))
        {
            for (const auto& pass : snapshot.passes)
            {
                if (!filter.PassFilter(pass.name.c_str()))
                {
                    continue;
                }
                ImGui::PushID(static_cast<int>(pass.authoredIndex));
                const int wave = pass.authoredIndex < snapshot.dependencyWaves.size()
                    ? snapshot.dependencyWaves[pass.authoredIndex] : -1;
                if (ImGui::TreeNode("Pass", "#%u %s | compiled %d | wave %d%s",
                    pass.authoredIndex, pass.name.c_str(), pass.compiledIndex, wave,
                    pass.culled ? " | culled" : ""))
                {
                    ImGui::Text("Side effect: %s | record cost %u | max slices %u",
                        pass.sideEffect ? "yes" : "no", pass.recordCost, pass.maxSlices);
                    for (const auto& usage : pass.usages)
                    {
                        ImGui::BulletText("%s r%u v%u %s | %s", GraphAccessName(usage.access),
                            usage.resource, static_cast<unsigned int>(usage.version),
                            resourceName(usage.resource), GraphStateName(usage.state));
                    }
                    if (!pass.barriers.empty())
                    {
                        ImGui::TextUnformatted("Pass barriers");
                        for (const auto& barrier : pass.barriers)
                        {
                            ImGui::BulletText("%s r%u %s: %s -> %s%s",
                                barrier.afterPass ? "After" : "Before", barrier.resource,
                                resourceName(barrier.resource), GraphStateName(barrier.before),
                                GraphStateName(barrier.after), barrier.uav ? " (UAV ordering)" : "");
                        }
                    }
                    if (!pass.phases.empty())
                    {
                        ImGui::Text("%u serial iterations / %zu phases / one unsplit recording unit",
                            pass.repeatCount, pass.phases.size());
                        for (uint32_t phaseIndex = 0; phaseIndex < pass.phases.size(); ++phaseIndex)
                        {
                            const auto& phase = pass.phases[phaseIndex];
                            ImGui::PushID(static_cast<int>(phaseIndex));
                            if (ImGui::TreeNode("Phase", "Phase %u: %s", phaseIndex, phase.name.c_str()))
                            {
                                for (const auto& usage : phase.usages)
                                {
                                    ImGui::BulletText("%s r%u v%u %s | %s", GraphAccessName(usage.access),
                                        usage.resource, static_cast<unsigned int>(usage.version),
                                        resourceName(usage.resource), GraphStateName(usage.state));
                                }
                                for (uint32_t iterationTemplate = 0; iterationTemplate < 2; ++iterationTemplate)
                                {
                                    ImGui::TextUnformatted(iterationTemplate == 0
                                        ? "First iteration barriers" : "Subsequent iteration barriers");
                                    const auto& barriers = iterationTemplate == 0
                                        ? phase.firstBarriers : phase.repeatBarriers;
                                    for (const auto& barrier : barriers)
                                    {
                                        ImGui::BulletText("r%u %s: %s -> %s%s", barrier.resource,
                                            resourceName(barrier.resource), GraphStateName(barrier.before),
                                            GraphStateName(barrier.after), barrier.uav ? " (UAV ordering)" : "");
                                    }
                                }
                                ImGui::TreePop();
                            }
                            ImGui::PopID();
                        }
                    }
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Resources"))
        {
            for (uint32_t index = 0; index < snapshot.resources.size(); ++index)
            {
                const auto& resource = snapshot.resources[index];
                if (!filter.PassFilter(resource.name.c_str()))
                {
                    continue;
                }
                ImGui::PushID(static_cast<int>(index));
                if (ImGui::TreeNode("Resource", "r%u %s | %s | %s%s", index, resource.name.c_str(),
                    resource.buffer ? "buffer" : "texture", resource.imported ? "imported" : "transient",
                    resource.used ? "" : " | unused"))
                {
                    ImGui::Text("Versions: %u | state %s -> %s", resource.versionCount,
                        GraphStateName(resource.initialState), GraphStateName(resource.finalState));
                    if (resource.used)
                    {
                        ImGui::Text("Compiled lifetime: %u to %u", resource.firstUse, resource.lastUse);
                    }
                    for (const auto& pass : snapshot.passes)
                    {
                        for (const auto& usage : pass.usages)
                        {
                            if (usage.resource == index)
                            {
                                ImGui::BulletText("v%u %s: #%u %s%s", static_cast<unsigned int>(usage.version),
                                    GraphAccessName(usage.access), pass.authoredIndex, pass.name.c_str(),
                                    pass.culled ? " (culled)" : "");
                            }
                        }
                    }
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Dependencies"))
        {
            ImGui::TextUnformatted("Compiled submission order");
            for (uint32_t index = 0; index < snapshot.executeOrder.size(); ++index)
            {
                const auto authored = snapshot.executeOrder[index];
                if (authored < snapshot.passes.size())
                {
                    ImGui::Text("%u: #%u %s", index, static_cast<unsigned int>(authored),
                        snapshot.passes[authored].name.c_str());
                }
            }
            ImGui::Separator();
            ImGui::TextUnformatted("Retained version edges");
            for (const auto& edge : snapshot.versionEdges)
            {
                if (edge.producer >= snapshot.passes.size() || edge.consumer >= snapshot.passes.size())
                {
                    continue;
                }
                const auto& producer = snapshot.passes[edge.producer];
                const auto& consumer = snapshot.passes[edge.consumer];
                if (!filter.PassFilter(producer.name.c_str()) && !filter.PassFilter(consumer.name.c_str()) &&
                    !filter.PassFilter(resourceName(edge.resource)))
                {
                    continue;
                }
                ImGui::BulletText("#%u %s -> r%u v%u %s [%s] -> #%u %s",
                    edge.producer, producer.name.c_str(), edge.resource, static_cast<unsigned int>(edge.version),
                    resourceName(edge.resource), GraphEdgeReason(edge.reason), edge.consumer, consumer.name.c_str());
            }
            ImGui::Separator();
            ImGui::TextUnformatted("Critical path (pass count, not GPU duration)");
            for (const auto authored : snapshot.criticalPath)
            {
                if (authored < snapshot.passes.size())
                {
                    ImGui::BulletText("#%u %s", static_cast<unsigned int>(authored),
                        snapshot.passes[authored].name.c_str());
                }
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

using namespace EnhancedRenderDebugUi;

namespace
{
	// 창 상태의 유일한 자리(PHASE 21 W3). 정렬 깃발과 편집 버퍼 넷뿐이다.
	EnhancedRenderDebugWindow& render_pass_state()
	{
		static EnhancedRenderDebugWindow value;
		return value;
	}
}

void editor::windows::draw_render_pass()
{
	render_pass_state().Draw();
}

void EnhancedRenderDebugWindow::DrawPassSettings()
{
	if (!m_editingLoaded)
	{
		m_editing = EnhancedSceneRenderer::GetLiveTuning();
		m_editingLoaded = true;
	}

	if (!ImGui::CollapsingHeader(EditorIcon::Label<EditorIcon::Inspector, " Pass settings">, ImGuiTreeNodeFlags_DefaultOpen))
	{
		return;
	}

	bool changed = false;

	if (ImGui::TreeNodeEx("Shadow", ImGuiTreeNodeFlags_DefaultOpen))
	{
		EnhancedLiveTuning::Shadow& shadow = m_editing.shadow;
		changed |= ImGui::SliderFloat("Bias (texels)##shadow", &shadow.biasTexels, 0.f, 8.f, "%.2f");
		changed |= ImGui::SliderFloat("Slope scale##shadow", &shadow.slopeScale, 0.f, 8.f, "%.2f");
		changed |= ImGui::SliderFloat("Cascade blend##shadow", &shadow.cascadeBlendBand, 0.f, 0.5f, "%.2f");
		changed |= ImGui::SliderFloat("Distance##shadow", &shadow.shadowDistance, 1.f, 1000.f, "%.1f",
			ImGuiSliderFlags_Logarithmic);
		changed |= ImGui::Combo("Edge filter##shadow", &shadow.filter, kShadowFilters,
			static_cast<int>(std::size(kShadowFilters)));
		if (ImGui::IsItemHovered())
		{
			// 반그림자 폭은 텍셀 단위라 먼 캐스케이드일수록 월드 폭이 넓다.
			ImGui::SetTooltip("Wider filters remove shimmering and stair-stepped edges.\n"
				"The penumbra is measured in shadow-map texels, so it widens in far cascades.");
		}
		changed |= ImGui::Combo("Debug view##shadow", &shadow.debugView, kShadowDebugViews,
			static_cast<int>(std::size(kShadowDebugViews)));
		if (0 != shadow.debugView)
		{
			ImGui::TextWrapped("%s", ShadowDebugViewLegend(shadow.debugView));
			// 디버그 색은 HDR 조명 타깃에 들어가 노출·톤 매핑을 거친다.
			ImGui::TextColored(kDimColor, "Post chain (exposure / tone map) tints these colors.");
		}

		// 조절하며 바로 보도록 씬 뷰의 수치를 여기에도 둔다. 다른 뷰는
		// Profiler > Rendering - Live 의 View 에서 고른다.
		static EnhancedLiveDebugSnapshot shadowSnapshot{};
		static double shadowRefreshTime = -1.0;
		const double now = ImGui::GetTime();
		if (shadowRefreshTime < 0.0 || (now - shadowRefreshTime) >= kRefreshIntervalSeconds)
		{
			shadowSnapshot = EnhancedSceneRenderer::GetLiveDebugSnapshot();
			shadowRefreshTime = now;
		}
		if (ImGui::TreeNodeEx("Scene view cascades##shadow"))
		{
			DrawShadowStats(shadowSnapshot.shadow[static_cast<uint32_t>(EnhancedLiveDisplayTarget::Editor)]);
			ImGui::TreePop();
		}
		ImGui::TreePop();
	}

	if (ImGui::TreeNodeEx("SSAO", ImGuiTreeNodeFlags_DefaultOpen))
	{
		EnhancedLiveTuning::Ssao& ssao = m_editing.ssao;
		changed |= ImGui::SliderFloat("Radius##ssao", &ssao.radius, 0.01f, 4.f);
		changed |= ImGui::SliderFloat("Thickness##ssao", &ssao.thickness, 0.01f, 2.f);
		changed |= ImGui::SliderFloat("Intensity##ssao", &ssao.intensity, 0.f, 4.f);
		changed |= ImGui::SliderFloat("Filter depth sigma##ssao",
			&ssao.filterDepthSigma, 0.0001f, 1.f, "%.4f");
		ImGui::TreePop();
	}

	if (ImGui::TreeNodeEx("SSGI"))
	{
		EnhancedLiveTuning::Ssgi& ssgi = m_editing.ssgi;
		changed |= ImGui::SliderFloat("Trace distance##ssgi", &ssgi.traceDistance, 0.5f, 64.f);
		changed |= ImGui::SliderFloat("Trace thickness##ssgi", &ssgi.traceThickness, 0.0001f, 1.f, "%.4f");
		// 두께는 현재 셰이더가 클립 깊이와 비교해 사실상 무시된다(패스 헤더의
		// 실측 주석). 조작은 열어 두되 그 사실을 옆에 적어 헛다리를 막는다.
		ImGui::SameLine();
		ImGui::TextColored(kDimColor, "(no effect yet)");
		changed |= ImGui::SliderFloat("Accum depth tolerance##ssgi",
			&ssgi.accumDepthTolerance, 0.0001f, 0.5f, "%.4f");
		changed |= ImGui::SliderFloat("Filter depth sigma##ssgi",
			&ssgi.filterDepthSigma, 0.0001f, 1.f, "%.4f");
		changed |= ImGui::SliderFloat("Filter normal power##ssgi",
			&ssgi.filterNormalPower, 1.f, 64.f);
		changed |= ImGui::SliderFloat("Composite depth sigma##ssgi",
			&ssgi.compositeDepthSigma, 0.0001f, 1.f, "%.4f");
		changed |= ImGui::SliderFloat("Intensity##ssgi", &ssgi.intensity, 0.f, 4.f);
		ImGui::TreePop();
	}

	if (ImGui::TreeNodeEx("SSS"))
	{
		EnhancedLiveTuning::Sss& sss = m_editing.sss;

		changed |= ImGui::Checkbox("Enabled##sss", &sss.enabled);
		// 재질 마스크가 없다 — 켜면 화면 전체가 번진다. DX11도 그랬고
		// 그것이 기본을 꺼짐으로 둔 이유다. 옆에 적어 두지 않으면
		// "왜 켜니까 전체가 뿌옇지"를 나중에 다시 쫓게 된다.
		ImGui::SameLine();
		ImGui::TextColored(kDimColor, "(no material mask: blurs the whole screen)");

		changed |= ImGui::SliderFloat("Strength##sss", &sss.strength, 0.f, 4.f);
		changed |= ImGui::SliderFloat("Width##sss", &sss.width, 0.f, 0.1f, "%.4f");
		ImGui::TreePop();
	}

	if (ImGui::TreeNodeEx("SSR"))
	{
		EnhancedLiveTuning::Ssr& ssr = m_editing.ssr;

		changed |= ImGui::Checkbox("Enabled##ssr", &ssr.enabled);
		// DX11에서 넘어온 잔물결이다(패스 헤더의 발견 ①~④): 히스토리가
		// 죽어 있어 프레임마다 반사가 흔들리고, 거칠기와 가장자리 페이드가
		// 계산만 되고 쓰이지 않는다. 기준선 보존이라 그대로 옮겼다.
		ImGui::SameLine();
		ImGui::TextColored(kDimColor, "(DX11 parity: temporal off, roughness ignored)");

		changed |= ImGui::SliderFloat("Step size##ssr", &ssr.stepSize, 0.001f, 1.f, "%.3f");
		changed |= ImGui::SliderFloat("Max thickness##ssr",
			&ssr.maxThickness, 0.0001f, 0.1f, "%.5f");
		changed |= ImGui::SliderInt("Max ray count##ssr", &ssr.maxRayCount, 1, 128);
		ImGui::TreePop();
	}

	if (ImGui::TreeNodeEx("VolumetricFog"))
	{
		EnhancedLiveTuning::Fog& fog = m_editing.fog;

		changed |= ImGui::Checkbox("Enabled##fog", &fog.enabled);
		// 켜는 순간 뷰마다 프록셀 격자를 잡는다. 그 사실을 옆에 적어 두지
		// 않으면 "왜 켤 때 한 프레임 튀지"를 나중에 다시 쫓게 된다.
		ImGui::SameLine();
		ImGui::TextColored(kDimColor, "(first enable allocates ~127MB: proxel grids per view)");

		changed |= ImGui::SliderFloat("Anisotropy##fog", &fog.anisotropy, -0.99f, 0.99f);
		changed |= ImGui::SliderFloat("Density##fog", &fog.density, 0.f, 1.f, "%.3f");
		changed |= ImGui::SliderFloat("Strength##fog", &fog.strength, 0.f, 10.f);
		changed |= ImGui::SliderFloat("Thickness factor##fog",
			&fog.thicknessFactor, 0.f, 1.f, "%.3f");
		// DX11에서부터 죽어 있는 튜닝이다(패스 헤더의 발견 ④) — 슬라이더를
		// 움직여도 그림이 안 바뀐다. 지우지 않는 이유는 기준선이 DX11이라서다.
		ImGui::SameLine();
		ImGui::TextColored(kDimColor, "(dead in DX11 too)");
		changed |= ImGui::SliderFloat("Scene color blend##fog",
			&fog.blendingWithSceneColorFactor, 0.f, 1.f, "%.3f");
		changed |= ImGui::SliderFloat("Previous frame blend##fog",
			&fog.previousFrameBlendFactor, 0.f, 1.f, "%.3f");
		changed |= ImGui::SliderFloat("Near plane##fog", &fog.customNearPlane, 0.01f, 10.f);
		changed |= ImGui::SliderFloat("Far plane##fog", &fog.customFarPlane, 10.f, 5000.f);
		ImGui::TreePop();
	}

	if (ImGui::TreeNodeEx("PostChain"))
	{
		EnhancedLiveTuning::PostChain& post = m_editing.postChain;

		if (ImGui::TreeNodeEx("Bloom", ImGuiTreeNodeFlags_DefaultOpen))
		{
			changed |= ImGui::Checkbox("Enabled##bloom", &post.bloomEnabled);
			changed |= ImGui::SliderFloat("Threshold##bloom", &post.bloomThreshold, 0.f, 8.f);
			changed |= ImGui::SliderFloat("Knee##bloom", &post.bloomKnee, 0.f, 1.f);
			changed |= ImGui::SliderFloat("Intensity##bloom", &post.bloomIntensity, 0.f, 1.f, "%.3f");
			ImGui::TreePop();
		}

		if (ImGui::TreeNodeEx("Tone map", ImGuiTreeNodeFlags_DefaultOpen))
		{
			changed |= ImGui::Checkbox("Enabled##tonemap", &post.toneMapEnabled);
			// 0=ACES, 1=AgX — EnhancedPostChainPass::ToneMapper와 같은 순서다.
			changed |= ImGui::RadioButton("ACES", &post.toneMapper, 0);
			ImGui::SameLine();
			changed |= ImGui::RadioButton("AgX", &post.toneMapper, 1);
			changed |= ImGui::SliderFloat("Exposure##tonemap", &post.exposure, 0.f, 8.f);
			ImGui::TreePop();
		}

		if (ImGui::TreeNodeEx("Vignette"))
		{
			changed |= ImGui::Checkbox("Enabled##vignette", &post.vignetteEnabled);
			changed |= ImGui::SliderFloat("Radius##vignette", &post.vignetteRadius, 0.f, 2.f);
			changed |= ImGui::SliderFloat("Softness##vignette", &post.vignetteSoftness, 0.f, 2.f);
			changed |= ImGui::SliderFloat("Intensity##vignette", &post.vignetteIntensity, 0.f, 1.f);
			ImGui::TreePop();
		}

		if (ImGui::TreeNodeEx("Color grading"))
		{
			changed |= ImGui::Checkbox("Enabled##grading", &post.gradingEnabled);
			changed |= ImGui::SliderFloat("Saturation##grading", &post.saturation, 0.f, 4.f);
			changed |= ImGui::SliderFloat("Contrast##grading", &post.contrast, 0.f, 4.f);
			ImGui::TreePop();
		}

		if (ImGui::TreeNodeEx("FXAA"))
		{
			changed |= ImGui::Checkbox("Enabled##fxaa", &post.fxaaEnabled);
			changed |= ImGui::SliderFloat("Bias##fxaa", &post.fxaaBias, 0.f, 1.f, "%.3f");
			changed |= ImGui::SliderFloat("Bias min##fxaa", &post.fxaaBiasMin, 0.f, 0.5f, "%.3f");
			changed |= ImGui::SliderFloat("Span max##fxaa", &post.fxaaSpanMax, 1.f, 16.f);
			ImGui::TreePop();
		}

		ImGui::TreePop();
	}

	if (changed)
	{
		EnhancedSceneRenderer::SetLiveTuning(m_editing);
	}

	if (ImGui::Button(EditorIcon::Label<EditorIcon::Revert, " Revert to live values">))
	{
		m_editing = EnhancedSceneRenderer::GetLiveTuning();
	}

	// 조작할 것이 없는 패스를 침묵으로 두면 "왜 SSAO만 있지?"가 남는다.
	// 없는 이유가 두 가지로 갈리므로 그것을 적는다.
	ImGui::TextColored(kDimColor,
		"Other live passes expose no tunable parameters:");
	ImGui::TextColored(kDimColor,
		"  GBuffer, Deferred, Forward+, SkyBox, Grid, Gizmo, UI");
	ImGui::TextColored(kDimColor,
		"SSR / SSS have Tuning but are not wired into the live graph yet,");
	ImGui::TextColored(kDimColor,
		"so there is nothing to drive from here.");
}

void editor::OpenRenderLiveDiagnostics()
{
    request_profiler_viewer(true);
}

void EnhancedRenderDebugWindow::Draw()
{
    ImGui::TextUnformatted("RenderPass - compiled graph");
    constexpr const char* targets[]{"Scene", "Game", "Material Preview"};
    if (ImGui::Combo("View", &m_graphTarget, targets, 3))
    {
        m_graphSnapshot.reset();
        m_graphLastRefresh = -1.0;
    }
    const double now = ImGui::GetTime();
    if (m_graphLastRefresh < 0.0 || now - m_graphLastRefresh >= kRefreshIntervalSeconds)
    {
        m_graphSnapshot = EnhancedSceneRenderer::GetLiveGraphSnapshot(
            static_cast<EnhancedLiveDisplayTarget>(m_graphTarget));
        m_graphLastRefresh = now;
    }
    if (ImGui::Button("Graphics settings"))
    {
        editor::open_window(EditorWindowName::kProjectSettings);
    }
    ImGui::SameLine();
    if (ImGui::Button("Rendering - Live"))
    {
        editor::OpenRenderLiveDiagnostics();
    }
    ImGui::Separator();
    if (!m_graphSnapshot)
    {
        ImGui::TextDisabled("Waiting for a compiled frame for this view.");
        const auto debug = EnhancedSceneRenderer::GetLiveDebugSnapshot();
        if (!debug.lastError.empty())
        {
            ImGui::TextWrapped("Latest renderer error: %s", debug.lastError.c_str());
        }
        return;
    }
    const auto& snapshot = *m_graphSnapshot;
    ImGui::Text("Generation %llu | epoch %llu | frame %llu | view %llu | %u x %u",
        static_cast<unsigned long long>(snapshot.generation), static_cast<unsigned long long>(snapshot.graphEpoch),
        static_cast<unsigned long long>(snapshot.frameId), static_cast<unsigned long long>(snapshot.viewId),
        snapshot.width, snapshot.height);
    ImGui::Text("Dependency hash %016llx | %zu declared / %zu executed / %zu resources",
        static_cast<unsigned long long>(snapshot.dependencyHash), snapshot.passes.size(),
        snapshot.executeOrder.size(), snapshot.resources.size());
    ImGui::TextDisabled("Native C++ source | %s | %s | read-only compiled snapshot",
        snapshot.scheduling == RGSchedulingMode::ExplicitVersioned ? "versioned" : "legacy/single writer",
        snapshot.orderPolicy == RGOrderPolicy::DependencyOrder ? "dependency order" : "authored order");
    m_graphFilter.Draw("Filter passes/resources");
    DrawCompiledGraph(snapshot, m_graphFilter);
}

void editor::windows::draw_preferences()
{
    auto& preferences = EditorSettingsStore::Get().Preferences();
    bool changed = false;
    float scale = preferences.GetImGuiScale();
    if (ImGui::SliderFloat("UI scale", &scale, .8f, 1.5f))
    { preferences.SetImGuiScale(scale); changed = true; }
    float width = preferences.GetContentTreeWidth();
    if (ImGui::SliderFloat("Content Browser tree width", &width, 120.f, 600.f))
    { preferences.SetContentTreeWidth(width); changed = true; }

    ImGui::SeparatorText("Frame rate");
    int mode = static_cast<int>(preferences.GetFrameRateMode());
    if (ImGui::Combo("Editor frame rate", &mode, "Display refresh\0Unlimited\0Custom\0"))
    { preferences.SetFrameRateMode(static_cast<EditorFrameRateMode>(mode)); changed = true; }
    if (EditorFrameRateMode::Custom == preferences.GetFrameRateMode())
    {
        int rate = static_cast<int>(preferences.GetCustomFrameRate());
        if (ImGui::SliderInt("Custom fps", &rate,
                static_cast<int>(EditorPreferences::kMinCustomFrameRate),
                static_cast<int>(EditorPreferences::kMaxCustomFrameRate), "%d", ImGuiSliderFlags_AlwaysClamp))
        { preferences.SetCustomFrameRate(static_cast<std::uint32_t>(rate)); changed = true; }
    }
    int background = static_cast<int>(preferences.GetBackgroundFrameRate());
    if (ImGui::SliderInt("Background fps (0 = off)", &background, 0,
            static_cast<int>(EditorPreferences::kMaxBackgroundFrameRate), "%d", ImGuiSliderFlags_AlwaysClamp))
    { preferences.SetBackgroundFrameRate(static_cast<std::uint32_t>(background)); changed = true; }
    ImGui::TextDisabled("Play mode is uncapped. Background limit pauses while loading or compiling shaders.");

    if (changed) EditorSettingsStore::Get().Save();
    ImGui::TextWrapped("Manage workspace layouts from Window > Workspace.");
}

void editor::windows::draw_project_settings()
{
    if (ImGui::BeginTabBar("ProjectSettingsPages"))
    {
        if (ImGui::BeginTabItem("General / Build"))
        { draw_build_scene_setting(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Graphics"))
        {
            ImGui::TextUnformatted("Enhanced Scene Renderer");
            ImGui::TextWrapped("SceneRenderProfile owns Scene parameters. The controls below are temporary live tuning; they do not save a project profile.");
            auto settings = RuntimeSettings::Get().GetRenderPassSettings();
            if (ImGui::Checkbox("Show environment background", &settings.m_isSkyboxEnabled))
            { RuntimeSettings::Get().SetRenderPassSettings(settings); EditorSettingsStore::Get().Save(); }
            render_pass_state().DrawPassSettings();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Quality"))
        {
            ImGui::TextWrapped("Quality presets are not available yet. Edit a SceneRenderProfile in the Inspector to save Scene settings.");
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}
