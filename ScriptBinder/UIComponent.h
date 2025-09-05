#pragma once
#include "../Utility_Framework/Core.Minimal.h"
#include "Component.h"
#include "IRenderable.h"
#include "Canvas.h"
#include "UIComponent.generated.h"

extern float MaxOreder;

//아직안씀 
enum class UItype
{
	Image,
	Text,
	None,
};

enum class Direction
{
	Up,
	Down,
	Left,
	Right
};

class UIComponent : public Component
{
public:
   ReflectUIComponent
    [[Serializable(Inheritance:Component)]]
	GENERATED_BODY(UIComponent)

	void SetCanvas(Canvas* canvas) { ownerCanvas = canvas; }
	Canvas* GetOwnerCanvas() { return ownerCanvas; }
	void SetOrder(int index) { _layerorder = index; }
	int GetLayerOrder() const { return _layerorder; }
	void SetNavi(Direction dir, const std::shared_ptr<GameObject>& otherUI);
	GameObject* GetNextNavi(Direction dir);

	Mathf::Vector3 pos{ 960, 540, 0 };
	Mathf::Vector2 scale{ 1, 1 };
	UItype type = UItype::None;

	static bool CompareLayerOrder(UIComponent* a, UIComponent* b)
	{
		if (a->_layerorder != b->_layerorder)
			return a->_layerorder < b->_layerorder;
		auto aCanvas = a->GetOwnerCanvas();
		auto bCanvas = b->GetOwnerCanvas();
		int aOrder = aCanvas ? aCanvas->GetCanvasOrder() : 0;
		int bOrder = bCanvas ? bCanvas->GetCanvasOrder() : 0;
		return aOrder < bOrder;
	}

private:
	std::unordered_map<Direction, std::weak_ptr<GameObject>> navigation;
	Canvas* ownerCanvas = nullptr;

protected:
    [[Property]]
	int _layerorder{};
};

