#pragma once
#include "../Utility_Framework/Core.Minimal.h"
#include "Component.h"
#include "IRenderable.h"
#include "Canvas.h"

//¾ÆÁ÷¾È¾¸ 
enum class UItype
{
	Image,
	Text,
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
	UIComponent();
	~UIComponent() = default;

	virtual std::string ToString() const override
	{
		return std::string("UIComponent");
	}

	void SetCanvas(Canvas* canvas) { ownerCanvas = canvas; }
	Canvas* GetOwnerCanvas() { return ownerCanvas; }

	void SetNavi(Direction dir, GameObject* other);
	GameObject* GetNextNavi(Direction dir);


	Mathf::Vector3 pos{ 960,540,0 };
	Mathf::Vector3 scale{ 1,1,1 };
private:
	std::unordered_map<Direction, GameObject*> navigation;
	Canvas* ownerCanvas = nullptr;

	//UIComponent* parent;
	//int layerorder;
};

