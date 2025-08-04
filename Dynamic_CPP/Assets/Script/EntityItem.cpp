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

#include "GameObject.h"
#include "TweenManager.h"

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
	auto rigid = GetOwner()->GetComponent<RigidBodyComponent>();
	rigid->LockAngularXYZ();
	rigid->SetLinearDamping(0.1f);
	auto box = GetOwner()->GetComponent<BoxColliderComponent>();
	box->SetRestitution(0.f);
	box->SetStaticFriction(100.f);
	box->SetDynamicFriction(100.f);


	if (itemCode == 0)
	{
		itemType = EItemType::Mushroom;
	}
	else if(itemCode == 1)
	{
		itemType = EItemType::Mineral;
	}
	else if (itemCode == 2)
	{
		itemType = EItemType::Fruit;
	}
}

void EntityItem::OnTriggerEnter(const Collision& collision)
{
	if (collision.otherObj->m_tag == "Rock")
	{
		GetOwner()->GetComponent<RigidBodyComponent>()->SetIsTrigger(false);
		auto rigid = GetOwner()->GetComponent<RigidBodyComponent>();
		rigid->SetLinearVelocity(Mathf::Vector3::Zero);
		rigid->SetAngularVelocity(Mathf::Vector3::Zero);
		m_state = EItemState::FALLED;
		std::cout << collision.otherObj->m_name.ToString() << "OnTriggerEnter Item" << std::endl;
	}
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
	//return;
	//GetComponent<RigidBodyComponent>().SetLinearVelocity(Mathf::Vector3::Zero);
	/*if (asisTail != nullptr) {
		Transform* tailTransform = asisTail->GetComponent<Transform>();
		if (tailTransform)
		{
			speed -= tick;
			if (speed < 1.f) {
				speed = 1.f;
			}
		}
	}*/

	if (m_state == EItemState::CATCHED)
	{
		auto rigid = GetOwner()->GetComponent<RigidBodyComponent>();
		rigid->SetLinearVelocity(Mathf::Vector3::Zero);
		rigid->SetAngularVelocity(Mathf::Vector3::Zero);
		//여기서 아이템을 플레이어 손위에 가게하기?
		if (asisTail != nullptr)
		{
			Transform* tailTransform = asisTail->GetComponent<Transform>();
			if (tailTransform)
			{
				Vector3 tailPos = tailTransform->GetWorldPosition();
				Vector3 ownerPos = GetOwner()->m_transform.GetWorldPosition();

				// XZ 평면 거리 계산
				float dx = tailPos.x - ownerPos.x;
				float dz = tailPos.z - ownerPos.z;
				if (abs(dx) < indicatorDistacne && abs(dz) < indicatorDistacne)
				{
					isTargettingTail = true;
					std::cout << "On Indeicator " << std::endl;
					
				}
				else
				{
					isTargettingTail = false;
				}
			}
		}
	}

	if (m_state == EItemState::THROWN)
	{
		if (!isTargettingTail)
		{
			speed -= tick;
			if (speed < 1.f) {
				speed = 1.f;
			}
			Transform* myTr = GetOwner()->GetComponent<Transform>();
			Vector3 pB = ((endPos - startPos) / 2) + startPos;
			pB.y += 7.f;
			Vector3 pA = startPos;
			auto rigid = GetOwner()->GetComponent<RigidBodyComponent>();
			timer += tick * speed; // 10sec
			if (timer < 1.f) {
				Vector3 p0 = Lerp(pA, pB, timer);
				Vector3 p1 = Lerp(pB, endPos, timer);
				Vector3 p01 = Lerp(p0, p1, timer);
				myTr->SetPosition(p01);
			}
			else
			{
				GetOwner()->GetComponent<RigidBodyComponent>()->SetIsTrigger(false);
				speed = 2.f;
				rigid->SetLinearVelocity(Mathf::Vector3::Zero);
				rigid->SetAngularVelocity(Mathf::Vector3::Zero);
				m_state = EItemState::NONE;
			};
		}
		else
		{
			Transform* tailTransform = asisTail->GetComponent<Transform>();
			if (tailTransform)
			{
				speed -= tick;
				if (speed < 1.f) {
					speed = 1.f;
				}
				Transform* myTr = GetOwner()->GetComponent<Transform>();
				Mathf::Vector3 tailPos = tailTransform->GetWorldPosition();
				Vector3 pB = ((tailPos - startPos) / 2) + startPos;
				pB.y += 10.f;
				Vector3 pA = startPos;
				auto rigid = GetOwner()->GetComponent<RigidBodyComponent>();
				timer += tick * speed; // 10sec
				if (timer < 1.f) {
					Vector3 p0 = Lerp(pA, pB, timer);
					Vector3 p1 = Lerp(pB, tailPos, timer);
					Vector3 p01 = Lerp(p0, p1, timer);
					myTr->SetPosition(p01);
				}
				else
				{

					GetOwner()->GetComponent<RigidBodyComponent>()->SetIsTrigger(false);
					speed = 2.f;
					rigid->SetLinearVelocity(Mathf::Vector3::Zero);
					rigid->SetAngularVelocity(Mathf::Vector3::Zero);
					m_state = EItemState::NONE;
				};
			}
		}



	}
	if (m_state == EItemState::DROPPED)
	{
		speed -= tick;
		if (speed < 1.f) {
			speed = 1.f;
		}
		Transform* myTr = GetOwner()->GetComponent<Transform>();

		Vector3 pB = ((endPos - startPos) / 2) + startPos;
		Vector3 pA = startPos;
		auto rigid = GetOwner()->GetComponent<RigidBodyComponent>();
		timer += tick * speed;
		timer = std::min(timer, 1.0f); // 1.0 이상 못 넘어가게 제한

		if (timer < 1.f) {
			Vector3 p0 = Lerp(pA, pB, timer);
			Vector3 p1 = Lerp(pB, endPos, timer);
			Vector3 p01 = Lerp(p0, p1, timer);

			myTr->SetPosition(p01);
		}
		else
		{
			myTr->SetPosition(endPos);  // 위치 보정 필수!
			rigid->SetIsTrigger(false);
			speed = 2.f;
			rigid->SetLinearVelocity(Mathf::Vector3::Zero);
			rigid->SetAngularVelocity(Mathf::Vector3::Zero);
			m_state = EItemState::NONE;
		}
	}

	if (m_state == EItemState::FALLED)
	{
		//중력에의해 떨어짐
	}
	if (m_state == EItemState::NONE)
	{
		auto rigid = GetOwner()->GetComponent<RigidBodyComponent>();
		rigid->SetLinearVelocity(Mathf::Vector3::Zero);
		rigid->SetAngularVelocity(Mathf::Vector3::Zero);
	}
		
	
}
void EntityItem::Drop(Mathf::Vector3 ownerForward, float distance)
{
	startPos = GetOwner()->GetComponent<Transform>()->GetWorldPosition();
	m_state = EItemState::DROPPED;
	timer = 0.f;
	speed = 4.0f;
	Mathf::Vector3 offset = {-ownerForward.x * distance,0, -ownerForward.z * distance };
	endPos = startPos + offset;
	endPos.y = 1;
}
void EntityItem::Throw(Mathf::Vector3 ownerForward,float distance)
{
	startPos = GetOwner()->GetComponent<Transform>()->GetWorldPosition();
	m_state = EItemState::THROWN;
	timer = 0.f;
	Mathf::Vector3 offset = {-ownerForward.x * distance,0, -ownerForward.z * distance };
	endPos = startPos + offset;
	endPos.y = 1;
}

void EntityItem::SetThrowOwner(Player* player)
{
	throwOwner = player;
	asisTail = GameObject::Find("AsisTail");
	startPos = GetOwner()->GetComponent<Transform>()->GetWorldPosition();
	m_state = EItemState::CATCHED;
	
	/*auto tween = std::make_shared<Tweener<Mathf::Vector3>>([&]() {
		auto pos = GetOwner()->m_transform.GetWorldPosition();
		return Vector3(pos.m128_f32[0], pos.m128_f32[1], pos.m128_f32[2]); },
		[&](Vector3 val) {
			GetComponent<RigidBodyComponent>().SetLinearVelocity(Mathf::Vector3::Zero);
			GetOwner()->m_transform.SetPosition(val); },
		asisTail->m_transform.GetWorldPosition(),
		0.5f, 
		[](float t) {return Easing::Linear(t);}
	);

	GameObject::Find("GameManager")->GetComponent<TweenManager>()->AddTween(tween); */
}

Player* EntityItem::GetThrowOwner()
{
	return throwOwner;
}

void EntityItem::ClearThrowOwner()
{
	throwOwner = nullptr;
}

