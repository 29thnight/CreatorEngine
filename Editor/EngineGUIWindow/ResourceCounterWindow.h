#pragma once
#include "ImGuiRegister.h"
#include "EngineResourceCensus.h"
#include <cstdint>

// 리소스 카운터 HUD.
//
// 에셋 캐시 / 렌더 프록시 / GPU 객체 수를 한 화면에 모아 보여준다.
// 목적은 "씬을 오갈 때 무엇이 회수되지 않는가"를 실행 중에 눈으로 확인하는 것이다.
// 기준선을 찍어두면 그 이후 증가분이 강조 표시되므로, 누수 재현 절차를 밟으며
// 어느 컨테이너가 자라는지 바로 좁힐 수 있다.
class ResourceCounterWindow
{
public:
	ResourceCounterWindow();
	~ResourceCounterWindow() = default;

private:
	// 한 시점의 카운터 모음. 기준선 비교에 사용한다.
	struct Snapshot
	{
		bool valid{ false };

		// 에셋 캐시 (DataSystem)
		size_t models{ 0 };
		size_t materials{ 0 };
		size_t textures{ 0 };
		size_t uiTextures{ 0 };
		size_t spriteSheets{ 0 };
		size_t spriteFonts{ 0 };
		size_t retainedAssets{ 0 };

		// 렌더 프록시 (RenderScene)
		size_t proxies{ 0 };
		size_t uiProxies{ 0 };
		size_t animators{ 0 };
		size_t animationPalettes{ 0 };
		size_t renderPassDatas{ 0 };

		// GPU
		uint64_t vramUsedMB{ 0 };
		uint64_t vramBudgetMB{ 0 };
		uint32_t liveGpuObjects{ 0 };   // 디버그 레이어 집계(종료 리포트에서만 유효)
		bool     liveGpuValid{ false };

		// 엔진이 직접 센 에셋 인스턴스 수. 원자적 읽기뿐이라 매 프레임 갱신해도 된다.
		Diagnostics::ResourceSnapshot engineResources{};

		// 관리 힙 (.NET GC) — PHASE 9-7.
		// CoreCLR 도입으로 힙이 둘이 됐다. 네이티브만 보면 "씬을 오갔을 때 제자리로
		// 돌아왔는가"의 절반만 보는 것이다.
		bool     gcValid{ false };
		int32_t  gcGen0{ 0 };
		int32_t  gcGen1{ 0 };
		int32_t  gcGen2{ 0 };
		int64_t  gcHeapBytes{ 0 };
		int64_t  gcFragmentedBytes{ 0 };
		int64_t  gcPausePercentX100{ 0 };
	};

	Snapshot Capture(bool includeGpuObjects) const;
	void DrawCountRow(const char* label, size_t current, size_t baseline) const;

	Snapshot m_baseline{};
	Snapshot m_lastGpuCensus{};   // 무거운 GPU 집계는 요청 시에만 갱신해 보관
};
