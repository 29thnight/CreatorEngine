#pragma once
#include "../Utility_Framework/Core.Minimal.h"
#include "Component.h"
#include "IUpdatable.h"
#include "UIManager.h"
#include "UIComponent.h"

//UI ó����
enum class UIColliderType
{
	Box,
	Circle,
	Capsule,
};

class UIButton : public UIComponent, public IUpdatable, public Meta::IReflectable<UIButton>
{
public:
	GENERATED_BODY(UIButton)
	
	void Update(float deltaSecond) override;

	void SetClickFunction(std::function<void()> func)
	{
		m_clickFunction = func;
	}
	void UpdateCollider();
	bool CheckClick(Mathf::Vector2 _mousePos);
	void SetFunction(std::function<void()> func){ m_clickFunction = func;}
	void Click(){ m_clickFunction(); }

	ReflectionField(UIButton)
	{
		MethodField
		({
			meta_method(Click)
		});

		FieldEnd(UIButton, MethodOnly)
	};

	bool isClick = false;
private:
	DirectX::BoundingOrientedBox obBox;
	UIColliderType type = UIColliderType::Box;
	std::function<void()> m_clickFunction;
};

