#pragma once
#include "../Utility_Framework/Core.Minimal.h"
#include "Component.h"
#include "IUpdatable.h"
#include "UIManager.h"

//UI Ã³¸®¿ë
enum class UIColliderType
{
	Box,
	Circle,
	Capsule,
};

class UIButton : public Component, public IUpdatable<UIButton>, public Meta::IReflectable<UIButton>
{
public:
	UIButton(std::function<void()> func);
	~UIButton() = default;
	
	std::string ToString() const override
	{
		return std::string("UIButton");
	}
	void Update(float deltaSecond) override;

	void UpdateCollider();
	bool CheckClick(Mathf::Vector2 _mousePos);
	void SetFunction(std::function<void()> func){ m_clickFunction = func;}
	void Click(){ m_clickFunction();}
	void SetCanvas(Canvas* canvas) { ownerCanvas = canvas; }
	ReflectionField(UIButton, MethodOnly)
	{

		MethodField
		({
			meta_method(Click)
			});
		ReturnReflectionMethodOnly(UIButton)
	};

	bool isClick = false;
private:
	Canvas* ownerCanvas = nullptr;
	DirectX::BoundingOrientedBox obBox;
	UIColliderType type = UIColliderType::Box;
	std::function<void()> m_clickFunction;
};

