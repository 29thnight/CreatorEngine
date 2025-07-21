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

	std::queue<EntityItem*> tempQueue = m_EntityItemQueue;

	int i = 0;
	while (!tempQueue.empty()) {
		auto item = tempQueue.front();
		tempQueue.pop();


		float orbitAngle = angle + XM_PI * 2.f * i / 3.f;;
		float r = radius + sinf(timer) * 3.f;
		Vector3 localOrbit = Vector3(cos(orbitAngle) * r, 0.f, sin(orbitAngle) * r);
		XMMATRIX axisRotation = XMMatrixRotationAxis(tailForward, 0.0f);
		XMVECTOR orbitOffset = XMVector3Transform(localOrbit, axisRotation);

		Vector3 finalPos = tailPos + Vector3(orbitOffset.m128_f32[0], orbitOffset.m128_f32[1], orbitOffset.m128_f32[2]);
		item->GetComponent<Transform>().SetPosition(finalPos);
		i++;
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
	
	m_EntityItemQueue.push(item);
	m_currentEntityItemCount++;

	auto queue = m_EntityItemQueue;
	while (!queue.empty()) {
		auto i = queue.front();
		queue.pop();
		if (item == i)
		{
			std::cout << "EntityAsis: Item already exists in the queue." << std::endl;
			return false; // 이미 큐에 존재하는 아이템은 추가하지 않음.
		}
	}

	std::cout << "EntityAsis: Adding item at index " << m_currentEntityItemCount << std::endl;
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
			m_currentTailPurificationDuration = 0;

			auto weapon = GameObject::Find("Sword");	// 이부분은 아이템에 등록된 정화무기가 될것.
			if (weapon)
			{
				auto player = item->GetThrowOwner();
				item->GetComponent<Transform>().SetPosition({2000,0,2000});
				item->SetThrowOwner(nullptr);
				if (player)
				{
					player->AddWeapon(weapon);
				}
			}
		}
	}
}

EntityItem* EntityAsis::GetPurificationItemInEntityItemQueue()
{
	if (m_currentEntityItemCount <= 0)
		return nullptr;

	m_currentEntityItemCount--;
	EntityItem* purificationItem = m_EntityItemQueue.front();
	m_EntityItemQueue.pop();

	return purificationItem;
}
