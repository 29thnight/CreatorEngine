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
#include "DebugLog.h"
#include "SceneManager.h"
#include "EffectComponent.h"
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

	auto rigid = GetOwner()->GetComponent<RigidBodyComponent>();
	rigid->LockAngularXYZ();
	rigid->SetLinearDamping(0.1f);
	auto box = GetOwner()->GetComponent<BoxColliderComponent>();
	box->SetRestitution(0.f);
	box->SetStaticFriction(100.f);
	box->SetDynamicFriction(100.f);

	auto newEffect = SceneManagers->GetActiveScene()->CreateGameObject("effect",GameObjectType::Empty,GetOwner()->m_index);
	m_effect = newEffect->AddComponent<EffectComponent>();
	m_effect->m_effectTemplateName = "resourceView";
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
	m_effect->Apply();
}

void EntityItem::OnTriggerEnter(const Collision& collision)
{
	if (collision.otherObj->m_tag == "Wall")
	{
		//GetOwner()->GetComponent<RigidBodyComponent>()->SetIsTrigger(false);
		auto rigid = GetOwner()->GetComponent<RigidBodyComponent>();
		m_state = EItemState::FALLED;
		LOG(collision.otherObj->m_name.ToString() << "OnTriggerEnter Item");
		rigid->UseGravity(true);
	}

	
	//LOG("OnTriggerEnter Item");
}

void EntityItem::OnTriggerExit(const Collision& collision)
{
	//LOG("OnCollisionEnter Item");
}

void EntityItem::OnCollisionEnter(const Collision& collision)
{
	
}

void EntityItem::OnCollisionExit(const Collision& collision)
{
}

void EntityItem::Update(float tick)
{
	Mathf::Vector3 pos = GetOwner()->m_transform.GetWorldPosition();
	if (abs(pos.y) <= 0.05f)
	{
		auto rigid = GetOwner()->GetComponent<RigidBodyComponent>();
		rigid->SetLinearVelocity(Mathf::Vector3::Zero);
		rigid->SetAngularVelocity(Mathf::Vector3::Zero);
		rigid->UseGravity(false);
	}
	if (m_state == EItemState::THROWN)
	{
		if (!isTargettingTail)
		{
			speed -= tick;
			if (speed < 1.f) {
				speed = 1.f;
			}
			canEat = false;
			Transform* myTr = GetOwner()->GetComponent<Transform>();
			Vector3 pB = ((endPos - startPos) / 2) + startPos;
			pB.y += throwDistacneY;
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
				//GetOwner()->GetComponent<RigidBodyComponent>()->SetIsTrigger(false);
				GetOwner()->GetComponent<RigidBodyComponent>()->UseGravity(false);
				speed = 2.f;
				rigid->SetLinearVelocity(Mathf::Vector3::Zero);
				rigid->SetAngularVelocity(Mathf::Vector3::Zero);
				m_state = EItemState::NONE;
				if (m_effect->m_isPlaying == false)
				{
					m_effect->Apply();
				}
			};
		}
		else
		{
			Transform* tailTransform = asisTail->GetComponent<Transform>();
			if (tailTransform)
			{
				canEat = true;
				speed -= tick;
				if (speed < 1.f) {
					speed = 1.f;
				}
				Transform* myTr = GetOwner()->GetComponent<Transform>();
				Mathf::Vector3 tailPos = tailTransform->GetWorldPosition();
				Vector3 pB = ((tailPos - startPos) / 2) + startPos;
				pB.y += throwDistacneY;
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

					//GetOwner()->GetComponent<RigidBodyComponent>()->SetIsTrigger(false);
					GetOwner()->GetComponent<RigidBodyComponent>()->UseGravity(false);
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
		pB.y += throwDistacneY;
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
			if (m_effect->m_isPlaying == false)
			{
				m_effect->Apply();
			}
		}
	}

	if (m_state == EItemState::FALLED)
	{
		//중력에의해 떨어짐 //바닥 plane 객체 전맵에 깔아야 할듯함 
	}
	if (m_state == EItemState::NONE)
	{
		auto rigid = GetOwner()->GetComponent<RigidBodyComponent>();
		rigid->SetLinearVelocity(Mathf::Vector3::Zero);
		rigid->SetAngularVelocity(Mathf::Vector3::Zero);
	}
	
	UpdateOutLine(tick);
	
}
void EntityItem::Drop(Mathf::Vector3 ownerForward, Mathf::Vector2 distance)
{
	startPos = GetOwner()->GetComponent<Transform>()->GetWorldPosition();
	m_state = EItemState::DROPPED;
	timer = 0.f;
	speed = 4.0f;
	Mathf::Vector3 offset = {ownerForward.x * distance.x,0, ownerForward.z * distance.x };
	throwDistacneY = distance.y;
	endPos = startPos + offset;
	endPos.y += 0.2f;
}
void EntityItem::Throw(Player* player,Mathf::Vector3 ownerForward,Mathf::Vector2 distance,bool indicate)
{
	isTargettingTail = false;
	if (indicate)
	{
		auto GMobj = GameObject::Find("GameManager");
		GameManager* GM = GMobj->GetComponent<GameManager>();
		auto& asis = GM->GetAsis()[0];
		asisTail = asis->GetOwner();  //일단 아시스 위치로 테스트
		isTargettingTail = true;
		
	}
	throwOwner = player;
	startPos = GetOwner()->GetComponent<Transform>()->GetWorldPosition();
	m_state = EItemState::THROWN;
	timer = 0.f;
	Mathf::Vector3 offset = {ownerForward.x * distance.x,0, ownerForward.z * distance.x};
	throwDistacneY = distance.y;
	endPos = startPos + offset;
	endPos.y += 0.2f;
}

void EntityItem::Throw(Mathf::Vector3 _startPos, Mathf::Vector3 velocity, float height)
{
	isTargettingTail = false;
	throwOwner = nullptr;
	startPos = _startPos;
	m_state = EItemState::THROWN;
	timer = 0.f;
	throwDistacneY = height;
	endPos = startPos + velocity;
	endPos.y = 0.2f;
}

void EntityItem::SetThrowOwner(Player* player)
{
	throwOwner = player;
	m_effect->StopEffect();
	//asisTail = GameObject::Find("AsisTail");
	//startPos = GetOwner()->GetComponent<Transform>()->GetWorldPosition();
	//m_state = EItemState::CATCHED;
	
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

