#pragma once
#include "../Utility_Framework/Core.Minimal.h"
#include "Component.h"
#include "IRegistableEvent.h"
#include "UIManager.h"
#include "UIComponent.h"
#include "UIButton.generated.h"

//UI 처리용
enum class UIColliderType : uint8_t
{
	Box,
	Circle,
	Capsule,
};
AUTO_REGISTER_ENUM(UIColliderType);

class UIButton : public UIComponent, public RegistableEvent<UIButton>
{
public:
   ReflectUIButton
    [[Serializable(Inheritance:UIComponent)]]
	GENERATED_BODY(UIButton)
	
	void Update(float deltaSecond) override;

	void SetClickFunction(std::function<void()> func)
	{
		m_clickFunction = func;
	}
	void UpdateCollider();
	bool CheckClick(Mathf::Vector2 _mousePos);
	void SetFunction(std::string& funName,float key,std::function<void()> func) { m_clickFunction = func;}
    [[Method]]
	void Click();

private:
	DirectX::BoundingOrientedBox obBox;
	std::function<void()> m_clickFunction;
	UIColliderType type = UIColliderType::Box;

public:
	bool isClick = false;
};

