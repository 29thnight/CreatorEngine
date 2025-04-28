#pragma once
#include "../physics/Physx.h"
#include "../physics/ICollider.h"
#include "GameObject.h"
#include "Component.h"

class ICollider;
class Component;
class GameObject;
class PhysicsManager
{
	//todo : 
	// - �������� �ʱ�ȭ �� ������Ʈ
	// - �������� ����
	// - �������� �� ����
	// - Object�� ��ȸ�ϸ� ����������Ʈ�� ã�� ���� �� ������Ʈ �� ����
	// - �������� �ݸ��� �̺�Ʈ�� ã�Ƽ� �ݹ��Լ� ȣ��
	// - �������� ������Ʈ�� �����͸� ������� ����� ���� ��ο�
	using ColliderID = unsigned int;
	struct ColliderInfo
	{
		uint32_t id;
		Component* component;
		GameObject* gameObject;
		ICollider* collider;
		bool bIsDestroyed = false;
		bool bIsRemoveBody = false;
	};

	struct CollisionCallbackInfo {
		CollisionData data;
		ECollisionEventType Type;
	};

public:
	PhysicsManager() = default;
	~PhysicsManager() = default;

	// �������� �ʱ�ȭ �� ������Ʈ
	void Initialize();
	void Update(float fixedDeltaTime);

	// �������� ����
	void Shutdown();

	// �������� �� ����
	void ChangeScene();

	//�� �ε� -> ���� ������ �� ������ü�� �����ϰ� ���� ���Ӿ��� ��ü�� ���� �������� ��ü�� ������ ���
	void OnLoadScene();

	//�� ��ε�
	void OnUnloadScene();



	//����� ���� ��ο� //[[maybe_unused]] todo : DebugSystem ����
	[[maybe_unused]] 
	void DrawDebugInfo();

private:
	// �ʱ�ȭ ����
	bool m_bIsInitialized{ false };

	// �������� �ùķ���Ʈ ����
	bool m_bPlay{ false };

	//����� ��ο� ����
	bool m_bDebugDraw{ false };

	//pre update  GameObject data -> pxScene data
	void SetPhysicData();

	//post update pxScene data -> GameObject data
	void GetPhysicData();

	unsigned int m_lastColliderID{ 0 };
	
	//�������� ��ü
	std::unordered_map<ColliderID, ColliderInfo> m_colliderContainer;

	//�ݸ��� �ݹ� 
	std::vector<CollisionCallbackInfo> m_callbacks;
};

