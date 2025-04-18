#pragma once
#include "../Utility_Framework/Core.Minimal.h"
#include "Component.h"
#include "IRenderable.h"
#include "Canvas.h"

extern float MaxOreder;

//¾ÆÁ÷¾È¾¸ 
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
class UIComponent : public Component, public IRenderable
{
public:
	UIComponent();
	~UIComponent() = default;

	virtual std::string ToString() const override
	{
		return std::string("UIComponent");
	}
	bool IsEnabled() const override
	{
		return m_IsEnabled;
	}

	void SetEnabled(bool able) override
	{
		m_IsEnabled = able;
	}
	void SetCanvas(Canvas* canvas) { ownerCanvas = canvas; }
	Canvas* GetOwnerCanvas() { return ownerCanvas; }
	void SetOrder(int index) { _layerorder = index; }
	void SetNavi(Direction dir, GameObject* otherUI);
	GameObject* GetNextNavi(Direction dir);

	
	int _layerorder;
	Mathf::Vector3 pos{ 960,540,0 };
	Mathf::Vector2 scale{ 1,1};
	UItype type = UItype::None;
protected:
	bool m_IsEnabled = true;
private:
	std::unordered_map<Direction, GameObject*> navigation;
	Canvas* ownerCanvas = nullptr;

	//UIComponent* parent;
	//int layerorder;
};

