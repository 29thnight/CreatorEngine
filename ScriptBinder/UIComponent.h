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
	void SetNavi(Direction dir, GameObject* otherUI);
	GameObject* GetNextNavi(Direction dir);

    [[Property]]
	int _layerorder{};
	Mathf::Vector3 pos{ 960,540,0 };
	Mathf::Vector2 scale{ 1,1};
	UItype type = UItype::None;

private:
	std::unordered_map<Direction, GameObject*> navigation;
	Canvas* ownerCanvas = nullptr;

	//UIComponent* parent;
	//int layerorder;
};

