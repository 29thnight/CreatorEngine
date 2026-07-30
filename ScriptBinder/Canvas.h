#pragma once
#include "../Utility_Framework/Core.Minimal.h"
#include "Component.h"
#include "IRenderable.h"
#include "Canvas.generated.h"
#include "IRegistableEvent.h"

class Canvas : public Component, public RegistableEvent<Canvas>
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

	int PreCanvasOrder = 0;
    [[Property]]
	int CanvasOrder = 0;
	std::vector<std::weak_ptr<GameObject>> UIObjs;
	[[Property]]
	std::string CanvasName = "Canvas";
	std::string prevCanvasName{};
};


