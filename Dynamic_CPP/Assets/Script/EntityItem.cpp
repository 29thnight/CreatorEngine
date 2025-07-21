#include "EntityItem.h"
#include "pch.h"
#include "MeshRenderer.h"
#include "Material.h"
#include "MaterialInfomation.h"
#include "EntityAsis.h"
#include "GameManager.h"
#include "BoxColliderComponent.h"
#include "RigidBodyComponent.h"
#include "Player.h"

using namespace Mathf;
void EntityItem::Start()
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

	//GetComponent<BoxColliderComponent>().SetExtents({ 10.f, 10.f, 10.f });



}

void EntityItem::OnTriggerEnter(const Collision& collision)
{
	//std::cout << "OnTriggerEnter Item" << std::endl;
}

void EntityItem::OnTriggerExit(const Collision& collision)
{
	//std::cout << "OnCollisionEnter Item" << std::endl;
}

void EntityItem::OnCollisionEnter(const Collision& collision)
{
}

void EntityItem::OnCollisionExit(const Collision& collision)
{
}

void EntityItem::Update(float tick)
{
	if (asisTail != nullptr) {
		Transform* tailTransform = asisTail->GetComponent<Transform>();
		if (tailTransform)
		{
			speed -= tick;
			if (speed < 1.f) {
				speed = 1.f;
			}

			Transform* myTr = GetOwner()->GetComponent<Transform>();
			
			Vector3 pC = tailTransform->GetWorldPosition();
			Vector3 pB = ((pC - startPos) / 2) + startPos;
			pB.y += 35.f;
			Vector3 pA = startPos;

			timer += tick * speed; // 10sec
			if (timer <= 1.f) {
				Vector3 p0 = Lerp(pA, pB, timer);
				Vector3 p1 = Lerp(pB, pC, timer);
				Vector3 p01 = Lerp(p0, p1, timer);

				myTr->SetPosition(p01);
			}
			else {
				auto asis = GameObject::Find("Asis_01");
				if (asis != nullptr) {
					auto entityAsis = asis->GetComponent<EntityAsis>();
					if(entityAsis) 
						entityAsis->AddItem(this);
				}
				asisTail = nullptr;
				timer = 0.f;
			}
		}
	}
}

void EntityItem::SetThrowOwner(Player* player)
{
	throwOwner = player;
	asisTail = GameObject::Find("AsisTail");
	startPos = GetOwner()->GetComponent<Transform>()->GetWorldPosition();
}

Player* EntityItem::GetThrowOwner()
{
	return throwOwner;
}

void EntityItem::ClearThrowOwner()
{
	throwOwner = nullptr;
}

