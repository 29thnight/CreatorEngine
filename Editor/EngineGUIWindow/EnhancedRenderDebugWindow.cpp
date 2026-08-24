#include "EnhancedRenderDebugWindow.h"
#include "Render/Scene/EnhancedSceneRenderer.h"
#include "IconsFontAwesome6.h"
#include "fa.h"

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <vector>

// 익명 네임스페이스가 아니라 이름을 준다. 이 프로젝트는 유니티 빌드라
// (EnableUnitySupport) 여러 .cpp가 한 TU로 합쳐지는데, ResourceCounterWindow.cpp도
// 익명 네임스페이스에 kRefreshIntervalSeconds·kDimColor를 두고 있어 같은 묶음에
// 들어가면 재정의로 깨진다.
namespace EnhancedRenderDebugUi
{
	// 스냅샷은 값 복사라 싸지만, 패스 시간은 프레임마다 출렁여서 매 프레임
	// 갱신하면 숫자가 읽히지 않는다. 눈이 따라갈 수 있는 주기를 둔다.
	constexpr double kRefreshIntervalSeconds = 0.25;

	constexpr ImVec4 kOkColor{ 0.49f, 0.88f, 0.72f, 1.0f };
	constexpr ImVec4 kWarnColor{ 0.96f, 0.78f, 0.36f, 1.0f };
	constexpr ImVec4 kErrorColor{ 0.94f, 0.44f, 0.47f, 1.0f };
	constexpr ImVec4 kDimColor{ 0.55f, 0.58f, 0.65f, 1.0f };

	void LabeledValue(const char* label, const char* value, const ImVec4& color)
	{
		ImGui::TextColored(kDimColor, "%s", label);
		ImGui::SameLine(180.0f);
		ImGui::TextColored(color, "%s", value);
	}

	void LabeledValue(const char* label, const char* value)
	{
		LabeledValue(label, value, ImGui::GetStyleColorVec4(ImGuiCol_Text));
	}
}

using namespace EnhancedRenderDebugUi;

EnhancedRenderDebugWindow::EnhancedRenderDebugWindow()
{
	ImGui::ContextRegister("RenderPass", true, [&]()
	{
		static EnhancedLiveDebugSnapshot displayed{};
		static double lastRefreshTime = -1.0;

		const double now = ImGui::GetTime();
		if (lastRefreshTime < 0.0 || (now - lastRefreshTime) >= kRefreshIntervalSeconds)
		{
			displayed = EnhancedSceneRenderer::GetLiveDebugSnapshot();
			lastRefreshTime = now;
		}

		// ── 패스 세부 설정 ──
		//
		// 이 창의 본래 이름이 Pipeline Setting이다. 계측만 있고 설정이 없으면
		// 이름이 거짓말이 된다.
		DrawPassSettings();

		// ── 러너 상태 ──
		//
		// 화면이 비었을 때 원인이 여기서 갈린다: 러너가 꺼졌는가,
		// 파이프라인이 못 섰는가, 서긴 했는데 드로우가 0인가.
		if (ImGui::CollapsingHeader(ICON_FA_MICROCHIP " Runtime", ImGuiTreeNodeFlags_DefaultOpen))
		{
			// UI 문자열은 영문으로 쓴다. 기본 폰트가 Verdana(라틴 전용)이고
			// 한글 글리프는 MenuBarWindow가 두 번째 폰트로 올려 둔 malgun을
			// PushFont로 꺼내 써야만 나온다 — 그 폰트는 그 창의 private 멤버다.
			const char* backendName = EnhancedLiveBackend::Vulkan == displayed.backend
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
			std::snprintf(buffer, sizeof(buffer), "%zu", displayed.graveyardCount);
			LabeledValue("Graveyard", buffer,
				0u == displayed.graveyardCount ? kDimColor : kWarnColor);
		}

		// LivePipelineDesc의 단일 목록을 그대로 보여 준다. 별도 CLI 덤프를
		// 늘리지 않고도 실제 활성 backend가 쓰는 순서·활성 상태·슬롯 흐름을
		// 확인할 수 있으며, 문자열은 파이프라인 변경 때만 다시 만들어진다.
		if (ImGui::CollapsingHeader(ICON_FA_LAYER_GROUP " Pipeline topology"))
		{
			LabeledValue("Descriptor",
				displayed.pipelineDescriptionValid ? "valid" : "invalid",
				displayed.pipelineDescriptionValid ? kOkColor : kErrorColor);

			if (displayed.pipelineDescription.empty())
			{
				ImGui::TextColored(kDimColor, "No pipeline description available.");
			}
			else
			{
				ImGui::BeginChild("PipelineTopology", ImVec2(0, 260.0f), true,
					ImGuiWindowFlags_HorizontalScrollbar);
				ImGui::TextUnformatted(displayed.pipelineDescription.c_str());
				ImGui::EndChild();
			}
		}

		// ── 프레임 비용 ──
		if (ImGui::CollapsingHeader(ICON_FA_STOPWATCH " Frame cost", ImGuiTreeNodeFlags_DefaultOpen))
		{
			char buffer[64]{};
			std::snprintf(buffer, sizeof(buffer), "%.3f ms", displayed.cpuMs);
			LabeledValue("CPU (record+submit)", buffer);
			std::snprintf(buffer, sizeof(buffer), "%.3f ms", displayed.gpuMs);
			LabeledValue("GPU (queue total)", buffer);
		}

		// ── 패스별 GPU 시간 ──
		if (ImGui::CollapsingHeader(ICON_FA_LAYER_GROUP " Pass timings", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::Checkbox("Sort by duration", &m_sortByDuration);
			ImGui::SameLine();
			ImGui::TextColored(kDimColor, "(%zu passes)", displayed.passTimings.size());

			if (displayed.passTimings.empty())
			{
				ImGui::TextColored(kDimColor,
					"No timings collected. The runner has not completed a frame yet,");
				ImGui::TextColored(kDimColor,
					"or profiler initialization failed.");
			}
			else
			{
				std::vector<EnhancedLivePassTiming> ordered = displayed.passTimings;
				if (m_sortByDuration)
				{
					std::stable_sort(ordered.begin(), ordered.end(),
						[](const EnhancedLivePassTiming& lhs, const EnhancedLivePassTiming& rhs)
						{
							return lhs.milliseconds > rhs.milliseconds;
						});
				}

				// 막대는 합계가 아니라 최대 패스를 기준으로 정규화한다.
				// 합계 기준이면 패스가 늘수록 전부 납작해져 비교가 안 된다.
				double slowest = 0.0;
				for (const EnhancedLivePassTiming& timing : ordered)
				{
					slowest = (std::max)(slowest, timing.milliseconds);
				}
				if (slowest <= 0.0) slowest = 1.0;

				if (ImGui::BeginTable("PassTimings", 3,
					ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
					ImGuiTableFlags_SizingStretchProp))
				{
					ImGui::TableSetupColumn("Pass", ImGuiTableColumnFlags_WidthStretch, 0.45f);
					ImGui::TableSetupColumn("ms", ImGuiTableColumnFlags_WidthStretch, 0.15f);
					ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch, 0.40f);
					ImGui::TableHeadersRow();

					for (const EnhancedLivePassTiming& timing : ordered)
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
			EnhancedLiveBackend::Vulkan == displayed.backend ? "Vulkan" : "D3D12";
		char validationHeader[64]{};
		std::snprintf(validationHeader, sizeof(validationHeader),
			ICON_FA_TRIANGLE_EXCLAMATION " %s validation", validationBackendName);
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
			ImGui::TextColored(kErrorColor, ICON_FA_CIRCLE_EXCLAMATION " Last error");
			ImGui::TextWrapped("%s", displayed.lastError.c_str());
		}
	}, ImGuiWindowFlags_AlwaysVerticalScrollbar);

	ImGui::GetContext("RenderPass").Close();
}

void EnhancedRenderDebugWindow::DrawPassSettings()
{
	if (!m_editingLoaded)
	{
		m_editing = EnhancedSceneRenderer::GetLiveTuning();
		m_editingLoaded = true;
	}

	if (!ImGui::CollapsingHeader(ICON_FA_SLIDERS " Pass settings", ImGuiTreeNodeFlags_DefaultOpen))
	{
		return;
	}

	bool changed = false;

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

	if (ImGui::Button(ICON_FA_ROTATE_LEFT " Revert to live values"))
	{
		m_editing = EnhancedSceneRenderer::GetLiveTuning();
	}

	// 조작할 것이 없는 패스를 침묵으로 두면 "왜 SSAO만 있지?"가 남는다.
	// 없는 이유가 두 가지로 갈리므로 그것을 적는다.
	ImGui::TextColored(kDimColor,
		"Other live passes expose no tunable parameters:");
	ImGui::TextColored(kDimColor,
		"  GBuffer, Shadow, Deferred, Forward+, SkyBox, Grid, Gizmo, UI");
	ImGui::TextColored(kDimColor,
		"SSR / SSS have Tuning but are not wired into the live graph yet,");
	ImGui::TextColored(kDimColor,
		"so there is nothing to drive from here.");
}
