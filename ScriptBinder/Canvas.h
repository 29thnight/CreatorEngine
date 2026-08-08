#pragma once
#include "../Utility_Framework/Core.Minimal.h"
#include "Component.h"
#include "IRenderable.h"
#include "CanvasScaleMode.h"
#include "Canvas.generated.h"

class Canvas : public Component
{
public:
   ReflectCanvas
    [[Serializable(Inheritance:Component)]]
	Canvas();
	~Canvas() = default;

	void OnDestroy() override;

	void AddUIObject(std::shared_ptr<GameObject> obj);

	// UI 컴포넌트가 파괴될 때 자기 오브젝트를 이 캔버스 목록에서 뺀다.
	void RemoveUIObject(GameObject* obj);
	virtual void Update(float tick) override;
	void SetCanvasOrder(int order) { CanvasOrder = order; }
	int GetCanvasOrder() const { return CanvasOrder; }

	void SetCanvasName(std::string_view name) { CanvasName = name.data(); }
	std::string GetCanvasName() const { return CanvasName; }
	std::weak_ptr<GameObject> GetFrontUIObject();

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

	[[Property]]
	CanvasScaleMode ScaleMode = CanvasScaleMode::ScaleWithScreenSize;

	[[Property]]
	Mathf::Vector2 ReferenceResolution = { 1920.f, 1080.f };

	// 0이면 너비, 1이면 높이에 맞춘다. 그 사이는 로그 공간 보간(uGUI와 같다).
	[[Property]]
	float MatchWidthOrHeight = 0.5f;

	// ConstantPixelSize에서만 쓰는 고정 배율.
	[[Property]]
	float ScaleFactor = 1.f;

	int PreCanvasOrder = 0;
    [[Property]]
	int CanvasOrder = 0;
	std::vector<std::weak_ptr<GameObject>> UIObjs;
	[[Property]]
	std::string CanvasName = "Canvas";
	std::string prevCanvasName{};
};


