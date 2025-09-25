#include "EntityAsis.h"
#include "pch.h"
#include "EntityItem.h"
#include "MeshRenderer.h"
#include "Material.h"
#include "MaterialInfomation.h"
#include "SceneManager.h"
#include "InputActionManager.h"
#include "RigidBodyComponent.h"
#include "BoxColliderComponent.h"

#include "Animator.h"
#include "Player.h"
#include "PrefabUtility.h"
#include "GameManager.h"
#include "GameObject.h"
#include "Weapon.h"
#include "WeaponCapsule.h"
#include "DebugLog.h"
#include "Core.Random.h"
using namespace Mathf;
inline static Mathf::Vector3 GetBothPointAndLineClosestPoint(const Mathf::Vector3& point, const Mathf::Vector3& lineStart, const Mathf::Vector3& lineEnd)
{
	Mathf::Vector3 lineDirection = lineEnd - lineStart;
	Mathf::Vector3 pointToLineStart = point - lineStart;

	float t = pointToLineStart.Dot(lineDirection) / lineDirection.Dot(lineDirection);
	Mathf::Clamp(t, 0.f, 1.f);
	Mathf::Vector3 closestPoint = lineStart + t * lineDirection;
	return closestPoint;
}
inline static float GetBothPointAndLineDistance(const Mathf::Vector3& point, const Mathf::Vector3& lineStart, const Mathf::Vector3& lineEnd)
{
	Mathf::Vector3 closestPoint = GetBothPointAndLineClosestPoint(point, lineStart, lineEnd);
	return Mathf::Distance(point, closestPoint);
}
inline static Mathf::Vector3 VectorProjection(const Mathf::Vector3& vector, const Mathf::Vector3& lineStart, const Mathf::Vector3& lineEnd)
{
	Mathf::Vector3 lineDirection = lineEnd - lineStart;
	float t = vector.Dot(lineDirection) / lineDirection.Dot(lineDirection);
	return lineStart + t * lineDirection;
}
void EntityAsis::Start()
{
	m_EntityItemQueue.resize(maxTailCapacity);

	auto gameManager = GameObject::Find("GameManager");
	if (gameManager)
	{
		auto gm = gameManager->GetComponent<GameManager>();
		if (gm)
		{
			gm->PushEntity(this);
			gm->PushAsis(this);
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
	//m_asismove = GetOwner()->GetComponent<AsisMove>();

	/*auto fakeObjects = GameObject::Find("fake");
	if (fakeObjects) {
		for (auto& index : fakeObjects->m_childrenIndices) {
			auto object = GameObject::FindIndex(index);
			m_fakeItemQueue.push_back(object);
		}
	}*/


	auto childred = GetOwner()->m_childrenIndices;
	for (auto& child : childred)
	{
		auto animator = GameObject::FindIndex(child)->GetComponent<Animator>();

		if (animator)
		{
			m_animator = animator;
			break;
		}

	}
	if (!m_animator)
	{
		m_animator = GetOwner()->GetComponent<Animator>();
	}







	auto paths = GameObject::Find("Paths");
	if (paths) {
		for (auto& index : paths->m_childrenIndices) {
			auto object = GameObject::FindIndex(index);
			if (object) {
				points.push_back(object->m_transform.GetWorldPosition());
			}
		}
	}
	else {
		auto point1 = GameObject::Find("TestPoint1");
		auto point2 = GameObject::Find("TestPoint2");
		auto point3 = GameObject::Find("TestPoint3");

		if (point1 != nullptr)
			points.push_back(point1->GetComponent<Transform>()->GetWorldPosition());
		if (point2 != nullptr)
			points.push_back(point2->GetComponent<Transform>()->GetWorldPosition());
		if (point3 != nullptr)
			points.push_back(point3->GetComponent<Transform>()->GetWorldPosition());
	}
#ifdef _DEBUG
	DebugPoint = GameObject::Find("DebugPoint");
#endif // _DEBUG
}

void EntityAsis::OnTriggerEnter(const Collision& collision)
{
	auto item = collision.otherObj->GetComponent<EntityItem>();
	if (item) {
		if (item->m_state != EItemState::THROWN) return;
		if (item->canEat == false) return;
		LOG("OnCollision Item: " << collision.otherObj->m_name.data());
		auto owner = item->GetThrowOwner();
		if (owner) {
			bool result = AddItem(item);
			item->GetOwner()->GetComponent<RigidBodyComponent>()->SetColliderEnabled(false);
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
		if (item->m_state != EItemState::THROWN) return; 
		if (item->canEat == false) return;
		LOG("OnCollision Item: " << collision.otherObj->m_name.data());
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
	if (asisTail) {
		Debug->Log(asisTail->m_name.data());
	}

	if (InputManagement->IsKeyDown((unsigned int)KeyBoard::N)) {
		SendDamage(nullptr, 10);
	}

	m_currentGracePeriod -= tick;
	m_currentStaggerDuration -= tick;

	if (m_currentStaggerDuration <= 0.f) {
		m_animator->SetParameter("OnMove", true);
		m_purificationAngle += tick * 5.f;

		PathMove(tick);

		Purification(tick);
	}
	else {
		m_animator->SetParameter("OnMove", false);
	}
}

void EntityAsis::SendDamage(Entity* sender, int damage, HitInfo hitinfo)
{
	if (m_currentGracePeriod > 0.f) {
		// 무적이지만 히트 사운드나 별도 처리시 여기서 처리.
		return;
	}

	m_currentHP -= damage;
	m_currentStaggerDuration = staggerDuration; // 경직
	m_currentGracePeriod = graceperiod;			// 무적
	m_currentTailPurificationDuration = 0.f;	// 진행중인 정화 취소

	// 피격 시 정화중인 아이템 떨구는 기능. 드랍되었다면 isDroped로 사운드처리. (ex. 뱉는 사운드, 정화실패 사운드 등)
	bool isDroped = DropItem();

	LOG("EntityAsis: Current HP: " << m_currentHP);
	if(m_currentHP <= 0)
	{
		// 스턴
	}
}

bool EntityAsis::AddItem(EntityItem* item)
{
	if (m_currentEntityItemCount >= maxTailCapacity)
	{
		LOG("EntityAsis: Max item count reached, cannot add more items.");
		return false;
	}

	if (item == nullptr)
	{
		LOG("EntityAsis: Cannot add a null item.");
		return false;
	}
	
	auto queue = m_EntityItemQueue;
	while (!queue.isEmpty()) {
		auto i = queue.dequeue();
		if (item == i)
		{
			LOG("EntityAsis: Item already exists in the queue.");
			return false; // 이미 큐에 존재하는 아이템은 추가하지 않음.
		}
	}

	m_EntityItemQueue.enqueue(item);
	m_currentEntityItemCount++;
	item->GetOwner()->GetComponent<RigidBodyComponent>()->SetIsTrigger(true);

	LOG("EntityAsis: Adding item at index " << m_currentEntityItemCount);
	return true;
}

void EntityAsis::Purification(float tick)
{
	Transform* tailTr = asisTail->GetComponent<Transform>();
	Vector3 tailPos = tailTr->GetWorldPosition();
	//Vector3 tailForward = XMVector3Rotate(XMVectorSet(0, 0, 1, 0), tailTr->GetWorldQuaternion());

	// 큐에 있는 아이템들 회전연출
	auto& arr = m_EntityItemQueue.getArray();
	int size = arr.size();
	for (int i = 0; i < size; i++) {
		if (arr[i] == nullptr) continue; // nullptr 체크
		float orbitAngle = m_purificationAngle + XM_PI * 2.f * i / 3.f;
		Vector3 localOrbit = Vector3(cos(orbitAngle) * m_purificationRadius, 0.f, sin(orbitAngle) * m_purificationRadius);
		XMMATRIX axisRotation = XMMatrixRotationAxis(Vector3::Forward, 0.0f);
		XMVECTOR orbitOffset = XMVector3Transform(localOrbit, axisRotation);

		Vector3 finalPos = tailPos + Vector3(orbitOffset.m128_f32[0], orbitOffset.m128_f32[1], orbitOffset.m128_f32[2]);
		arr[i]->GetComponent<RigidBodyComponent>()->SetLinearVelocity(Mathf::Vector3::Zero);
		arr[i]->GetOwner()->m_transform.SetPosition(finalPos);
		i++;
	}

	// 꼬리에 아이템이 있다면 정화를 진행.
	if (m_currentEntityItemCount > 0) 
	{
		m_currentTailPurificationDuration += tick;
		if (m_currentTailPurificationDuration >= tailPurificationDuration) 
		{
			// 정화 시간 완료 시
			auto item = GetPurificationItemInEntityItemQueue();
			m_currentTailPurificationDuration = 0;

			// 재화 지급
			AddPollutionGauge(item->itemReward);

			// 이부분은 아이템에 등록된 정화무기가 될것.
			auto player = item->GetThrowOwner();
				
			item->SetThrowOwner(nullptr);
			//Prefab* meleeweapon = PrefabUtilitys->LoadPrefab("MeleeWeapon");
			//if (meleeweapon && player)
			//{
			//	GameObject* weaponObj = PrefabUtilitys->InstantiatePrefab(meleeweapon, "meleeWeapon");
			//	//플레이어 방향으로 웨폰날리기
			//	auto weaponcom = weaponObj->GetComponent<Weapon>();
			//	weaponcom->Throw(player, GetOwner()->m_transform.GetWorldPosition());

			//}

			Prefab* weaponCapsuleprefab = PrefabUtilitys->LoadPrefab("WeaponCapsule");
			if (weaponCapsuleprefab && player)
			{
				GameObject* weaponCapsule = PrefabUtilitys->InstantiatePrefab(weaponCapsuleprefab, "weaponCapsule");
				//플레이어 방향으로 웨폰날리기
				auto weaponcapcom = weaponCapsule->GetComponent<WeaponCapsule>();
				weaponcapcom->weaponCode = static_cast<int>(item->itemType);  //아이템 코드에 대응되는 무기 생성    //필요시 weaponCode 추가구현
				weaponcapcom->Throw(player, GetOwner()->m_transform.GetWorldPosition());

			}

			item->GetOwner()->Destroy();

			/*static int index = 1;
			std::string weaponName = "MeleeWeapon";
			auto curweapon = GameObject::Find(weaponName);
			if (curweapon)
			{
				auto weapon2 = curweapon->GetComponent<Weapon>();
				
				if (weapon2->OwnerPlayerIndex != -1)
				{
					curweapon = nullptr;
				}
					
			}
			if (!curweapon)
			{
				while (!curweapon && index <= 35)
				{
					std::string realWeaponName = weaponName + " (" + std::to_string(index) + ")";
					curweapon = GameObject::Find(realWeaponName);
					if (curweapon)
					{
						index++;
						break;
					}
				}
			}


			if (curweapon)
			{
				auto weaponcom = curweapon->GetComponent<Weapon>();
				weaponcom->Throw(player, GetOwner()->m_transform.GetWorldPosition());

			}*/
			
		}
	}
}

void EntityAsis::PathMove(float tick)
{
	int pathSize = points.size();
	if (pathSize == 0) return;
	int nextPointIndex = (currentPointIndex + 1) % pathSize;
	Vector3 currentPosition = GetOwner()->m_transform.GetWorldPosition();
	Quaternion currentRotation = GetOwner()->m_transform.GetWorldQuaternion();
	currentRotation.Normalize();
	Vector3 currentForward = XMVector3Rotate(XMVectorSet(0, 0, 1, 0), currentRotation);

	Vector3 dir = Mathf::Normalize(points[nextPointIndex] - points[currentPointIndex]);
	Vector3 endResult = points[nextPointIndex] - dir * m_pathEndRadius;
	Vector3 startResult = points[currentPointIndex] + dir * m_pathEndRadius;
	Vector3 closestPoint = GetBothPointAndLineClosestPoint(currentPosition, startResult, endResult);
	Vector3 predictClosestPosition = GetBothPointAndLineClosestPoint(closestPoint + dir * m_predictNextTime * moveSpeed, startResult, endResult);

#ifdef _DEBUG
	if (DebugPoint)
		DebugPoint->m_transform.SetPosition(predictClosestPosition);
#endif // _DEBUG

	Mathf::Vector3 direction = currentForward;
	float rotDownSpeed = 1.f;

	// 1. 미래위치의 투영점이 경로의 반지름을 벗어난 경우, 경로를 재설정
	if (Mathf::Distance(currentPosition, closestPoint) > m_pathRadius) {
		nextMovePoint = predictClosestPosition; // 새로운 목적지 설정
		direction = Mathf::Normalize(nextMovePoint - currentPosition);
		Vector3 right = Vector3::Up.Cross(direction);
		if (right.LengthSquared() < 0.0001f)
			right = Vector3::Right; // fallback for colinear
		right.Normalize();
		Vector3 up = direction.Cross(right);

		Matrix rotMatrix = Matrix(
			right.x, right.y, right.z, 0,
			up.x, up.y, up.z, 0,
			direction.x, direction.y, direction.z, 0,
			0, 0, 0, 1
		);

		Quaternion rot = Quaternion::CreateFromRotationMatrix(rotMatrix);
		Quaternion newRot = Quaternion::Slerp(currentRotation, rot, m_rotateSpeed * tick);

		rotDownSpeed = rot.Dot(GetComponent<Transform>()->GetWorldQuaternion());
		rotDownSpeed = std::clamp(rotDownSpeed, 0.f, 1.f);

		GetOwner()->m_transform.SetRotation(newRot);
	}

	Vector3 newPosition = currentPosition + direction * moveSpeed * tick * rotDownSpeed;
	GetOwner()->m_transform.SetPosition(newPosition);
	//GetOwner()->GetComponent<RigidBodyComponent>()->NotifyPhysicsStateChange(newPosition);

	float newDistance = Mathf::Distance(newPosition, points[nextPointIndex]);
	if (newDistance <= m_pathEndRadius) {
		currentPointIndex = nextPointIndex; // Loop through the points
	}
}

void EntityAsis::Stun()
{
}

bool EntityAsis::DropItem()
{
	if (m_EntityItemQueue.isEmpty())
		return false;

	auto item = GetPurificationItemInEntityItemQueue();
	item->GetOwner()->GetComponent<RigidBodyComponent>()->SetColliderEnabled(true);
	//item->Throw(nullptr, GetOwner()->m_transform.GetForward(), Vector2(3.f, 3.f), false);
	Mathf::Vector3 forward = GetOwner()->m_transform.GetForward();
	float angle = std::atan2(forward.z, forward.x);
	angle += Mathf::Deg2Rad * Random<float>(-30.f, 30.f).Generate();
	forward.x = std::cos(angle);
	forward.z = std::sin(angle);
	forward.y = 0.f;

	item->Throw(asisHead->m_transform.GetWorldPosition(), forward * 3.f, 3.f);

	return false;
}

float EntityAsis::GetPollutionGaugePercent()
{
	return (float)m_currentPollutionGauge / maxPollutionGauge;
}

EntityItem* EntityAsis::GetPurificationItemInEntityItemQueue()
{
	if (m_currentEntityItemCount <= 0)
		return nullptr;

	m_currentEntityItemCount--;
	EntityItem* purificationItem = m_EntityItemQueue.dequeue();

	return purificationItem;
}

void EntityAsis::AddPollutionGauge(int amount)
{
	m_currentPollutionGauge += amount;
	int reward = 0;
	while (m_currentPollutionGauge >= maxPollutionGauge) {
		m_currentPollutionGauge -= maxPollutionGauge;
		reward += pollutionCoreAmount;
	}
	std::cout << "Current Pollution Gauge: " << m_currentPollutionGauge << std::endl;
	std::cout << "Total Reward to give: " << reward << std::endl;

	auto gameManager = GameObject::Find("GameManager");
	if (gameManager) {
		auto gm = gameManager->GetComponent<GameManager>();
		if (gm)
		{
			gm->AddReward(reward);
		}
	}
}
