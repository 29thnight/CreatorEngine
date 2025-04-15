#pragma once
#include "../Utility_Framework/Core.Minimal.h"
#include "Component.h"
#include "IUpdatable.h"
#include "UIManager.h"

//UI 클릭처리용으로만 쓸 것
enum class UIColliderType
{
	Box,
	Circle,
	Capsule,
};
class UICollider : public Component, public IUpdatable<UICollider>//, public ICollision2D
{
public:
	UICollider();
	~UICollider() = default;

	std::string ToString() const override
	{
		return std::string("UICollider");
	}

	void SetCollider();
	bool CheckClick(Mathf::Vector2 _mousePos);

	void Update(float deltaSecond) override;

	UIColliderType type = UIColliderType::Box;
	
	//원 캡슐등 필요시 추가
	DirectX::BoundingOrientedBox obBox;
	bool isClick = false;
	
};

//UI매니저 해서 현재 고르는중인 UI목록 잘 나누기
