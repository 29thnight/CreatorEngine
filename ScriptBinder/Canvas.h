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

	void AddUIObject(GameObject* obj);
	virtual void Update(float tick) override;

	int PreCanvasOrder = 0;
    [[Property]]
	int CanvasOrder = 0;
	std::vector<GameObject*> UIObjs;

	//현재 선택중인 UI
	GameObject* SelectUI = nullptr;
};

