#pragma once
#include "../Utility_Framework/Core.Minimal.h"
#include "Component.h"
#include "IUpdatable.h"
#include "UIManager.h"
#include "UIComponent.h"
#include "UIButton.generated.h"

//UI 처리용
enum class UIColliderType
{
	Box,
	Circle,
	Capsule,
};
AUTO_REGISTER_ENUM(UIColliderType);

class UIButton : public UIComponent, public IUpdatable
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
	void Click() { m_clickFunction(); }

	bool isClick = false;
private:
	DirectX::BoundingOrientedBox obBox;
	UIColliderType type = UIColliderType::Box;
	std::function<void()> m_clickFunction;
};

