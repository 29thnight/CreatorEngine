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
	virtual void Update(float tick) override;
	void SetCanvasOrder(int order) { CanvasOrder = order; }
	int GetCanvasOrder() const { return CanvasOrder; }

	void SetCanvasName(std::string_view name) { CanvasName = name.data(); }
	std::string GetCanvasName() const { return CanvasName; }

	int PreCanvasOrder = 0;
    [[Property]]
	int CanvasOrder = 0;
	std::vector<std::weak_ptr<GameObject>> UIObjs;
	[[Property]]
	std::string CanvasName = "Canvas";

	//현재 선택중인 UI
	std::weak_ptr<GameObject> SelectUI;
};

