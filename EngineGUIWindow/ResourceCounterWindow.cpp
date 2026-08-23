#include "ResourceCounterWindow.h"
#include "RenderScene.h"
#include "SceneManager.h"
#include "DataSystem.h"
#include "RHI/IRHIDeviceResources.h"
#include "ClrHost.h"
#include "IconsFontAwesome6.h"
#include "fa.h"

namespace
{
	// 카운터를 매 프레임 읽으면 에셋 로딩 스레드와 락 경합이 생긴다.
	// HUD는 추세만 보이면 충분하므로 갱신 주기를 둔다.
	constexpr double kRefreshIntervalSeconds = 0.5;

	constexpr ImVec4 kIncreaseColor{ 0.94f, 0.44f, 0.47f, 1.0f };  // 증가: 회수되지 않았다는 신호
	constexpr ImVec4 kDecreaseColor{ 0.49f, 0.88f, 0.72f, 1.0f };  // 감소: 정상 회수
	constexpr ImVec4 kDimColor{ 0.55f, 0.58f, 0.65f, 1.0f };
}

ResourceCounterWindow::ResourceCounterWindow()
{
	ImGui::ContextRegister("Resource Counter", true, [&]()
	{
		static Snapshot displayed{};
		static double lastRefreshTime = -1.0;

		const double now = ImGui::GetTime();
		if (!displayed.valid || (now - lastRefreshTime) >= kRefreshIntervalSeconds)
		{
			displayed = Capture(false);
			lastRefreshTime = now;
		}

		// 무거운 GPU 객체 집계는 별도 보관값을 재사용한다.
		displayed.liveGpuObjects = m_lastGpuCensus.liveGpuObjects;
		displayed.liveGpuValid = m_lastGpuCensus.liveGpuValid;

		// --- 기준선 조작 ---
		if (ImGui::Button(ICON_FA_FLAG " 현재를 기준선으로"))
		{
			m_baseline = displayed;
			m_baseline.valid = true;
		}
		ImGui::SameLine();
		if (ImGui::Button(ICON_FA_XMARK " 기준선 해제"))
		{
			m_baseline = Snapshot{};
		}

		if (m_baseline.valid)
		{
			ImGui::TextColored(kDimColor, "기준선 대비 증감을 표시합니다. 붉은 값은 회수되지 않은 항목입니다.");
		}
		else
		{
			ImGui::TextColored(kDimColor, "기준선을 찍은 뒤 씬을 오가면 무엇이 남는지 드러납니다.");
		}

		ImGui::Separator();

		// --- 에셋 캐시 ---
		if (ImGui::CollapsingHeader("에셋 캐시 (DataSystem)", ImGuiTreeNodeFlags_DefaultOpen))
		{
			if (ImGui::BeginTable("##assetCounts", 3, ImGuiTableFlags_SizingFixedFit))
			{
				DrawCountRow("Models", displayed.models, m_baseline.models);
				DrawCountRow("Materials", displayed.materials, m_baseline.materials);
				DrawCountRow("Textures", displayed.textures, m_baseline.textures);
				DrawCountRow("UITextures", displayed.uiTextures, m_baseline.uiTextures);
				DrawCountRow("SpriteSheets", displayed.spriteSheets, m_baseline.spriteSheets);
				DrawCountRow("SpriteFonts", displayed.spriteFonts, m_baseline.spriteFonts);
				DrawCountRow("Retained(보존 표시)", displayed.retainedAssets, m_baseline.retainedAssets);
				ImGui::EndTable();
			}
			ImGui::TextColored(kDimColor,
				"참고: 현재 UnloadUnusedAssets는 호출되지 않아 캐시는 증가만 합니다.");
		}

		// --- 렌더 프록시 ---
		if (ImGui::CollapsingHeader("렌더 프록시 (RenderScene)", ImGuiTreeNodeFlags_DefaultOpen))
		{
			if (ImGui::BeginTable("##proxyCounts", 3, ImGuiTableFlags_SizingFixedFit))
			{
				DrawCountRow("Proxies", displayed.proxies, m_baseline.proxies);
				DrawCountRow("UI Proxies", displayed.uiProxies, m_baseline.uiProxies);
				DrawCountRow("Animators", displayed.animators, m_baseline.animators);
				DrawCountRow("Animation Palettes", displayed.animationPalettes, m_baseline.animationPalettes);
				DrawCountRow("RenderPassData", displayed.renderPassDatas, m_baseline.renderPassDatas);
				ImGui::EndTable();
			}
			if (displayed.animators != displayed.animationPalettes)
			{
				// 두 맵은 항상 짝을 이뤄야 한다. 어긋나면 팔레트 버퍼가 새거나 조기 해제된 것이다.
				ImGui::TextColored(kIncreaseColor,
					ICON_FA_TRIANGLE_EXCLAMATION " Animator와 Palette 수가 다릅니다 (%zu vs %zu)",
					displayed.animators, displayed.animationPalettes);
			}
		}

		// --- GPU ---
		if (ImGui::CollapsingHeader("GPU", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::Text("VRAM  %llu MB / %llu MB",
				static_cast<unsigned long long>(displayed.vramUsedMB),
				static_cast<unsigned long long>(displayed.vramBudgetMB));

			if (m_baseline.valid)
			{
				const int64_t delta =
					static_cast<int64_t>(displayed.vramUsedMB) - static_cast<int64_t>(m_baseline.vramUsedMB);
				if (delta != 0)
				{
					ImGui::SameLine();
					ImGui::TextColored(delta > 0 ? kIncreaseColor : kDecreaseColor, "(%+lld MB)",
						static_cast<long long>(delta));
				}
			}

			if (displayed.vramBudgetMB > 0)
			{
				const float ratio = static_cast<float>(displayed.vramUsedMB) /
					static_cast<float>(displayed.vramBudgetMB);
				ImGui::ProgressBar(ratio, ImVec2(-1.0f, 0.0f));
			}

			ImGui::Separator();

			// 엔진이 직접 센 에셋 인스턴스. 디버그 레이어의 전수 열거는 실행 중에
			// 부르면 이후 렌더가 죽으므로(IRHIDeviceResources.h 참고) 쓰지 않는다.
			// 대신 이 수치가 씬을 오갔을 때 제자리로 돌아오는지를 본다.
			ImGui::Text("엔진 에셋");
			for (size_t i = 0; i < displayed.engineResources.counts.size(); ++i)
			{
				const int64_t current = displayed.engineResources.counts[i];
				if (current == 0 && !m_baseline.valid) continue;

				ImGui::Text("  %-10s %lld",
					std::string(Diagnostics::kEngineResourceNames[i]).c_str(),
					static_cast<long long>(current));

				if (m_baseline.valid)
				{
					const int64_t delta = current - m_baseline.engineResources.counts[i];
					if (delta != 0)
					{
						ImGui::SameLine();
						ImGui::TextColored(delta > 0 ? kIncreaseColor : kDecreaseColor, "(%+lld)",
							static_cast<long long>(delta));
					}
				}
			}

			ImGui::TextColored(kDimColor, "D3D 객체 전수 집계는 종료 리포트(로그)에서 확인");
		}

		// --- 관리 힙 (.NET GC) --- PHASE 9-7
		//
		// 네이티브 카운터만 보면 평탄성의 절반만 보는 것이다. 스크립트가 C#으로 간
		// 뒤로는 씬이 잡고 있던 것의 상당수가 관리 힙에 있고, 그쪽이 자라면
		// 네이티브 수치가 아무리 제자리여도 프로세스는 커진다.
		if (ImGui::CollapsingHeader("관리 힙 (.NET GC)", ImGuiTreeNodeFlags_DefaultOpen))
		{
			if (!displayed.gcValid)
			{
				ImGui::TextColored(kDimColor, "스크립트 계층 비활성 — 관리 힙 지표 없음");
			}
			else
			{
				auto drawDelta = [&](int64_t current, int64_t baseline, const char* suffix)
				{
					if (!m_baseline.valid || !m_baseline.gcValid) return;
					const int64_t delta = current - baseline;
					if (0 == delta) return;
					ImGui::SameLine();
					ImGui::TextColored(delta > 0 ? kIncreaseColor : kDecreaseColor,
						"(%+lld%s)", static_cast<long long>(delta), suffix);
				};

				// 수집 횟수는 단조 증가라 증감의 뜻이 다르다 — 여기서는 '기준선 이후
				// 몇 번 돌았나'이고, 씬 전환마다 gen2가 정확히 늘어나는지가 9-6의 확인점이다.
				ImGui::Text("수집 횟수  gen0 %d", displayed.gcGen0);
				drawDelta(displayed.gcGen0, m_baseline.gcGen0, "");
				ImGui::SameLine(); ImGui::Text(" · gen1 %d", displayed.gcGen1);
				drawDelta(displayed.gcGen1, m_baseline.gcGen1, "");
				ImGui::SameLine(); ImGui::Text(" · gen2 %d", displayed.gcGen2);
				drawDelta(displayed.gcGen2, m_baseline.gcGen2, "");

				constexpr double kBytesPerMB = 1024.0 * 1024.0;
				ImGui::Text("힙 크기    %.1f MB", displayed.gcHeapBytes / kBytesPerMB);
				drawDelta((displayed.gcHeapBytes - m_baseline.gcHeapBytes) / 1048576, 0, " MB");

				ImGui::Text("단편화     %.1f MB", displayed.gcFragmentedBytes / kBytesPerMB);
				ImGui::Text("GC 점유    %.2f %%", displayed.gcPausePercentX100 / 100.0);

				ImGui::TextColored(kDimColor,
					"씬을 오간 뒤 힙 크기가 기준선으로 돌아와야 한다. 계속 자라면 관리 측 참조가 남은 것이다.");
			}
		}
	}, ImGuiWindowFlags_AlwaysVerticalScrollbar);

	ImGui::GetContext("Resource Counter").Close();
}

void ResourceCounterWindow::DrawCountRow(const char* label, size_t current, size_t baseline) const
{
	ImGui::TableNextRow();

	ImGui::TableNextColumn();
	ImGui::TextUnformatted(label);

	ImGui::TableNextColumn();
	ImGui::Text("%zu", current);

	ImGui::TableNextColumn();
	if (!m_baseline.valid)
	{
		return;
	}

	const int64_t delta = static_cast<int64_t>(current) - static_cast<int64_t>(baseline);
	if (delta == 0)
	{
		ImGui::TextColored(kDimColor, "-");
	}
	else
	{
		ImGui::TextColored(delta > 0 ? kIncreaseColor : kDecreaseColor, "%+lld",
			static_cast<long long>(delta));
	}
}

ResourceCounterWindow::Snapshot ResourceCounterWindow::Capture(bool includeGpuObjects) const
{
	Snapshot snapshot{};
	snapshot.valid = true;

	// --- 에셋 캐시 ---
	// 전용 뮤텍스가 있는 컨테이너는 규약을 지켜 읽는다.
	// UITextures/SpriteSheets/SFonts는 아직 전용 락이 없어(리팩토링 1-8 대상)
	// 근사치로 읽는다. HUD 용도에서는 추세만 보이면 충분하다.
	if (auto* dataSystem = DataSystem::GetInstance())
	{
		{
			std::lock_guard<std::mutex> lock(dataSystem->m_modelMutex);
			snapshot.models = dataSystem->Models.size();
		}
		{
			std::lock_guard<std::mutex> lock(dataSystem->m_materialMutex);
			snapshot.materials = dataSystem->Materials.size();
		}
		{
			std::lock_guard<std::mutex> lock(dataSystem->m_textureMutex);
			snapshot.textures = dataSystem->Textures.size();
		}

		snapshot.uiTextures = dataSystem->UITextures.size();
		snapshot.spriteSheets = dataSystem->SpriteSheets.size();
		snapshot.spriteFonts = 0; // 폰트 컨테이너는 D4에서 은퇴, SDF 계통에서 복원

		for (const auto& [type, names] : dataSystem->m_retainedAssets)
		{
			snapshot.retainedAssets += names.size();
		}
	}

	// --- 렌더 프록시 ---
	RenderScene* renderScene = SceneManagers->GetRenderScene();
	if (renderScene != nullptr)
	{
		const RenderScene::ResourceCounts counts = renderScene->GetResourceCounts();
		snapshot.proxies = counts.proxies;
		snapshot.uiProxies = counts.uiProxies;
		snapshot.animators = counts.animators;
		snapshot.animationPalettes = counts.animationPalettes;
		snapshot.renderPassDatas = counts.renderPassDatas;
	}

	// --- GPU ---
	if (auto* resources = GetDiagnosticsDeviceResources())
	{
		snapshot.engineResources = Diagnostics::CaptureResourceSnapshot();

		if (includeGpuObjects)
		{
			// 실행 중에는 타입별 집계를 얻을 수 없다(디버그 레이어 순회가 이후 렌더를
			// 망가뜨린다). VRAM만 채워지고 liveGpuValid는 false로 남는다 —
			// allowDeviceEnumeration=false가 그 약속이다.
			const RHIGpuObjectCensus census = resources->CaptureLiveObjectCensus(false);
			snapshot.vramUsedMB = census.vramUsedMB;
			snapshot.vramBudgetMB = census.vramBudgetMB;
			snapshot.liveGpuObjects = census.totalObjects;
			snapshot.liveGpuValid = census.available;
		}
		else
		{
			// VRAM 조회는 가벼우므로 주기 갱신에 포함한다.
			const RHIVideoMemoryInfo memory = resources->QueryVideoMemory();
			snapshot.vramUsedMB = memory.usedMB;
			snapshot.vramBudgetMB = memory.budgetMB;
		}
	}

	// --- 관리 힙 (PHASE 9-7) ---
	//
	// HUD는 게임 스레드의 ImGui 패스에서 그려지므로 여기서 경계를 넘어도 규약을 지킨다
	// (관리 코드 호출은 게임 스레드 전용). 하는 일은 카운터 읽기뿐이고 HUD 자체가
	// 0.5초 주기라 틱마다 넘지도 않는다.
	{
		ClrHost::ScriptGcStats gc{};
		if (ClrHost::Get().GetManagedGcStats(gc))
		{
			snapshot.gcValid = true;
			snapshot.gcGen0 = gc.gen0Collections;
			snapshot.gcGen1 = gc.gen1Collections;
			snapshot.gcGen2 = gc.gen2Collections;
			snapshot.gcHeapBytes = gc.heapSizeBytes;
			snapshot.gcFragmentedBytes = gc.fragmentedBytes;
			snapshot.gcPausePercentX100 = gc.pauseTimePercentageX100;
		}
	}

	return snapshot;
}
