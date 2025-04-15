#pragma once
#include "../Utility_Framework/Core.Minimal.h"
#include "Component.h"
#include "IUpdatable.h"
#include "UIManager.h"

//UI Ŭ��ó�������θ� �� ��
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
	
	//�� ĸ���� �ʿ�� �߰�
	DirectX::BoundingOrientedBox obBox;
	bool isClick = false;
	
};

//UI�Ŵ��� �ؼ� ���� �������� UI��� �� ������
