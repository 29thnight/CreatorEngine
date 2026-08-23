#pragma once
#include "../Utility_Framework/Core.Minimal.h"
#include "Component.h"
#include "IRenderable.h"
#include "CanvasScaleMode.h"
#include "CanvasRenderMode.h"
#include "EntityHandle.h"

class Canvas : public meta::identity<Canvas, Component>
{
   public:
   static consteval auto reflect()
   {
       return meta::schema<Self>(
           meta::field<&Self::ScaleMode>,
           meta::field<&Self::ReferenceResolution>,
           meta::field<&Self::MatchWidthOrHeight>,
           meta::field<&Self::ScaleFactor>,
           meta::field<&Self::RenderMode>,
           meta::field<&Self::PlaneDistance>,
           meta::field<&Self::CanvasOrder>,
           meta::field<&Self::CanvasName>);
   }
public:
	Canvas();
	~Canvas() = default;

	void OnUninitializing() override;

	// 레인 UI — 가상 Update 오버라이드를 걷어내고 UITickSystem이 부르는 비가상
	// 진입점으로 옮긴다(UITickSystem.h 상단 주석 참고). 등록/해지는 Awake/OnDestroy가
	// 아니라 6단계 씬 편입/이탈 훅에 건다 — DDOL 오브젝트가 씬을 건널 때 Awake가
	// 다시 불리지 않아 등록부에서 영구 이탈하는 결함을 피하기 위해서다(근거 동일).
	void OnAddedToScene() override;
	void OnRemovingFromScene() override;

	void AddUIObject(Entity* obj);

	// UI 컴포넌트가 파괴될 때 자기 오브젝트를 이 캔버스 목록에서 뺀다.
	void RemoveUIObject(Entity* obj);
	// 옛 Update(float tick)의 본문 그대로 — UITickSystem::Update가 가드를 통과시킨
	// 뒤 호출한다. 이름을 바꾼 이유는 Component::Update와 이름이 같으면
	// LifecycleRegistry::MaskOfType이 여전히 Bit_Update를 세워 암묵 구독이
	// 되살아나기 때문이다.
	void TickCanvasOrder(float tick);
	void SetCanvasOrder(int order) { CanvasOrder = order; }
	int GetCanvasOrder() const { return CanvasOrder; }
	CanvasRenderMode GetRenderMode() const { return RenderMode; }
	float GetPlaneDistance() const { return PlaneDistance; }

	void SetCanvasName(std::string_view name) { CanvasName = name.data(); }
	void OnDeserialized(); // CT6-d: UIManager 등록 + prevCanvasName 동기화(구 팩토리 분기)

	std::string GetCanvasName() const { return CanvasName; }
	Entity* GetFrontUIObject();

	// ── 캔버스 스케일러(PHASE 7-3) ──
	//
	// 화면 크기가 바뀔 때 UI를 어떻게 따라가게 할지 정한다. 7-1이 캔버스 rect를
	// 화면에 맞추는 것까지 했고, 여기서는 그 안의 내용물 크기를 정한다.
	//
	// 기본값이 ScaleWithScreenSize + (1920,1080)인 이유: 현재 콘텐츠가 전부
	// 1920x1080 기준으로 저작돼 있어 그 해상도에서 배율이 정확히 1이 된다.
	// 즉 기존 씬은 무수정으로 지금과 같은 결과가 나오고, 다른 해상도에서만
	// 비로소 따라 움직인다. ConstantPixelSize를 기본으로 두면 이 단계의 효과가
	// 아무 데서도 나타나지 않는다.
	float ComputeScaleFactor(const Mathf::Rect& screenRect) const;

	CanvasScaleMode ScaleMode = CanvasScaleMode::ScaleWithScreenSize;

	Mathf::Vector2 ReferenceResolution = { 1920.f, 1080.f };

	// 0이면 너비, 1이면 높이에 맞춘다. 그 사이는 로그 공간 보간(uGUI와 같다).
	float MatchWidthOrHeight = 0.5f;

	// ConstantPixelSize에서만 쓰는 고정 배율.
	float ScaleFactor = 1.f;

	// Overlay는 Game View의 최종 화면에, Camera는 게임 카메라 앞 평면에,
	// World는 Canvas 오브젝트의 Transform 평면에 그린다.
	CanvasRenderMode RenderMode = CanvasRenderMode::ScreenSpaceOverlay;

	// ScreenSpaceCamera에서 게임 카메라 앞에 놓을 거리.
	float PlaneDistance = 100.f;

	int PreCanvasOrder = 0;
	int CanvasOrder = 0;
	// 씬 소유 객체를 장기 소유하지 않는 세대 검증 캐시. DDOL 이송 때
	// OnRemovingFromScene에서 비우고, 새 씬의 OnAddedToScene 이후 UIManager가
	// 현재 sceneId/index/generation으로 다시 연결한다.
	std::vector<EntityHandle> UIObjs;
	std::string CanvasName = "Canvas";
	std::string prevCanvasName{};
};


