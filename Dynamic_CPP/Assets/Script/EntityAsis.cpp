#include "EntityAsis.h"
#include "pch.h"
#include "EntityItem.h"
#include "MeshRenderer.h"
#include "Material.h"
#include "MaterialInfomation.h"
#include "SceneManager.h"
#include "InputActionManager.h"
#include "RigidBodyComponent.h"

#include "Player.h"

#include "GameManager.h"
using namespace Mathf;
void EntityAsis::Start()
{
	//auto playerMap = SceneManagers->GetInputActionManager()->AddActionMap("Test");
	//playerMap->AddButtonAction("Punch", 0, InputType::KeyBoard, KeyBoard::LeftControl, KeyState::Down, [this]() { Punch();});

	//playerMap->AddValueAction("Move", 0, InputValueType::Vector2, InputType::KeyBoard, { 'A', 'D', 'S', 'W'}, [this](Mathf::Vector2 _vector2) {Inputblabla(_vector2);});

	auto gameManager = GameObject::Find("GameManager");	
	if (gameManager)
	{
		auto gm = gameManager->GetComponent<GameManager>();
		if (gm)
		{
			gm->PushEntity(this);
		}
	}

	auto meshrenderer = GetOwner()->GetComponent<MeshRenderer>();
	if (meshrenderer)
	{
		auto material = meshrenderer->m_Material;
		if (material)
		{
			material->m_materialInfo.m_bitflag = 0;
		}
	}

	asisTail = GameObject::Find("AsisTail");
	asisHead = GameObject::Find("AsisHead");

	m_EntityItemQueue.resize(maxTailCapacity);

	m_fakeItemQueue.resize(maxTailCapacity);
	auto fakeObjects = GameObject::Find("fake");
	if (fakeObjects) {
		for (auto& index : fakeObjects->m_childrenIndices) {
			auto object = GameObject::FindIndex(index);
			m_fakeItemQueue.push_back(object);
		}
	}
}

void EntityAsis::OnTriggerEnter(const Collision& collision)
{
	auto item = collision.otherObj->GetComponent<EntityItem>();
	if (item) {
		std::cout << "OnTrigger Item" << std::endl;
		AddItem(item);
	}
}

void EntityAsis::OnCollisionEnter(const Collision& collision)
{
	auto item = collision.otherObj->GetComponent<EntityItem>();
	if (item) {
		std::cout << "OnCollision Item" << std::endl;
		auto owner = item->GetThrowOwner();
		if (owner) {
			bool result = AddItem(item);

			if (!result) {
				// 획득을 실패했을 때.
			}
			else {
				// 획득했을 때 처리.
			}
		}
	}
}

void EntityAsis::Update(float tick)
{
	timer += tick;
	angle += tick * 5.f;
	Transform* tailTr = asisTail->GetComponent<Transform>();
	Vector3 tailPos = tailTr->GetWorldPosition();
	Vector3 tailForward = XMVector3Rotate(XMVectorSet(0, 0, 1, 0), tailTr->GetWorldQuaternion());
	for (int i = 0; i < maxTailCapacity; i++)
	{
		if (m_fakeItemQueue.size() < i + 1) return;

		if (m_fakeItemQueue[i] != nullptr)
		{
			m_fakeItemQueue[i]->SetEnabled(m_EntityItemQueue[i] != nullptr ? true : false);

			float orbitAngle = angle + XM_PI * 2.f * i / 3.f;;
			float r = radius + sinf(timer) * 3.f;
			Vector3 localOrbit = Vector3(cos(orbitAngle) * r, 0.f, sin(orbitAngle) * r);
			XMMATRIX axisRotation = XMMatrixRotationAxis(tailForward, 0.0f);
			XMVECTOR orbitOffset = XMVector3Transform(localOrbit, axisRotation);

			Vector3 finalPos = tailPos + Vector3(orbitOffset.m128_f32[0], orbitOffset.m128_f32[1], orbitOffset.m128_f32[2]);
			m_fakeItemQueue[i]->GetComponent<Transform>()->SetPosition(finalPos);
		}
	}


	Purification(tick);
}

bool EntityAsis::AddItem(EntityItem* item)
{
	if (m_currentEntityItemCount >= maxTailCapacity)
	{
		std::cout << "EntityAsis: Max item count reached, cannot add more items." << std::endl;
		return false;
	}

	if (item == nullptr)
	{
		std::cout << "EntityAsis: Cannot add a null item." << std::endl;
		return false;
	}

	m_EntityItemQueue[m_currentEntityItemCount] = item;
	std::cout << "EntityAsis: Adding item at index " << m_currentEntityItemCount << std::endl;

	m_currentEntityItemCount++;
	return true;
}

void EntityAsis::Purification(float tick)
{
	// 꼬리에 아이템이 있다면 정화를 진행.
	if (m_currentEntityItemCount > 0) {
		m_currentTailPurificationDuration += tick;
		if (m_currentTailPurificationDuration >= tailPurificationDuration) {
			// 정화 시간 완료 시
			auto item = GetPurificationItemInEntityItemQueue();

			auto weapon = GameObject::Find("Sword");
			if (weapon)
			{
				auto player = GameObject::Find("Punch");
				if (player)
				{
					auto playerScr = player->GetComponent<Player>();
					playerScr->AddWeapon(weapon);

					m_currentTailPurificationDuration = 0;
				}
			}
			//item->GetOwner()->GetComponent<RigidBodyComponent>().
		}
	}
}

EntityItem* EntityAsis::GetPurificationItemInEntityItemQueue()
{
	if (m_currentEntityItemCount <= 0) 
		return nullptr;

	m_currentEntityItemCount--;
	EntityItem* purificationItem = m_EntityItemQueue[m_EntityItemQueueIndex];
	m_EntityItemQueue[m_EntityItemQueueIndex] = nullptr;
	m_EntityItemQueueIndex++;
	if (m_EntityItemQueueIndex >= maxTailCapacity)
	{
		m_EntityItemQueueIndex = 0; // Reset index if it exceeds the max count
	}

	return purificationItem;
}

void EntityAsis::Inputblabla(Mathf::Vector2 dir)
{
	this->dir = dir;
}

