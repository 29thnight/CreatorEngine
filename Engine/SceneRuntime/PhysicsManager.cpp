#include "PhysicsManager.h"
#include "Interfaces/AssetAuthoringPort.h"

#include <sstream>

#include "SceneManager.h"
#include "Scene.h"
#include "Entity.h"
#include "Transform.h"
#include "Component.h"
#include "RigidBodyComponent.h"
#include "BoxColliderComponent.h"
#include "SphereColliderComponent.h"
#include "CapsuleColliderComponent.h"
#include "MeshCollider.h"
#include "MeshRenderer.h"
#include "TagManager.h"
#include "Terrain.h"
#include "TerrainCollider.h"
#include "CharacterControllerComponent.h"
#include "PathFinder.h"

namespace
{
	void FillPhysicsTransform(PhysicsTransform& output, Transform& transform)
	{
		output.localMatrix = transform.GetLocalMatrix();
		output.worldMatrix = transform.GetWorldMatrix();
		math::decompose(output.localMatrix, output.localScale,
			output.localRotation, output.localPosition);
		math::decompose(output.worldMatrix, output.worldScale,
			output.worldRotation, output.worldPosition);
	}

	void ApplyScaledColliderOffset(
		PhysicsTransform& transform,
		const math::vector3& offset)
	{
		if (offset == math::vector3{}) return;

		transform.worldPosition +=
			math::transform_direction(offset, transform.worldMatrix);
		transform.worldMatrix.m[3][0] = transform.worldPosition.x;
		transform.worldMatrix.m[3][1] = transform.worldPosition.y;
		transform.worldMatrix.m[3][2] = transform.worldPosition.z;
	}
}

class Scene;
void PhysicsManager::Initialize()
{
	// PhysicsManager 초기화
	m_bIsInitialized = Physics->Initialize();
	
	// 씬 로드, 언로드, 변경 이벤트 핸들러 등록
	m_OnSceneLoadHandle		= sceneLoadedEvent.AddRaw(this, &PhysicsManager::OnLoadScene);
	m_OnSceneUnloadHandle	= sceneUnloadedEvent.AddRaw(this, &PhysicsManager::OnUnloadScene);
	m_OnChangeSceneHandle	= SceneManagers->activeSceneChangedEvent.AddRaw(this, &PhysicsManager::ChangeScene);

	// 물리 엔진 콜백 함수 설정
	Physics->SetCallBackCollisionFunction([this](const CollisionData& data, ECollisionEventType type) {
		this->CallbackEvent(data, type);
	});
	
	//기본 전체 충돌 매트릭스 설정
	std::vector<std::vector<uint8_t>> collisionGrid;
	collisionGrid.resize(32);
	for (auto& row : collisionGrid) {
		row.resize(32, 1); // 기본적으로 모든 충돌체가 충돌 가능하도록 설정
	}
	SetCollisionMatrix(collisionGrid);

	LoadCollisionMatrix();
}
void PhysicsManager::Update(float fixedDeltaTime)
{
	if (!m_bIsInitialized) return;
	
	// 콜백 이벤트 초기화
	m_callbacks.clear();
	SetPhysicData();
	// 물리 엔진에 변경 사항 적용
	//Benchmark bm;
	ApplyPendingChanges();
	// 물리 엔진 업데이트
	Physics->Update(fixedDeltaTime);
	//std::cout << " Physics->Update" << bm.GetElapsedTime() << std::endl;
	
	// 1순위: GetPhysicData()
	// 물리 시뮬레이션의 결과를 모든 게임오브젝트의 Transform에 먼저 동기화합니다.
	// 이렇게 해야 세상의 모든 객체가 물리적으로 올바른 최신 위치에 있게 됩니다.
	GetPhysicData();

	// 2순위: ProcessCallback()
	// 모든 객체의 위치가 최신 상태로 동기화되었으므로, 이제 이 위치를 기준으로
	// OnCollisionEnter, OnTriggerEnter 등의 이벤트 스크립트를 실행하는 것이 안전하고 정확합니다.
	// 만약 이 순서가 반대가 되면, 스크립트는 '이전 프레임의 위치'를 기준으로 로직을 실행하는 오류가 발생할 수 있습니다.
	ProcessCallback();

	// 3순위: ApplyPendingControllerPositionChanges()
	// 해당 프레임의 모든 물리적 상호작용과 이벤트 처리가 완전히 끝났습니다.
	// 이제 모든 정산이 끝난 상태에서, '무한 복도'와 같은 특수한 게임플레이 효과(순간이동)를
	// 맨 마지막에 적용하여 다음 프레임을 준비시킵니다.
	ApplyPendingControllerPositionChanges();
}
void PhysicsManager::Shutdown()
{
	// 물리 엔진 씬 변경
	Physics->ChangeScene();
	//컨테이너 제거
	auto& Container = SceneManagers->GetActiveScene()->m_colliderContainer;
	Container.clear();
	// 물리 엔진 종료
	Physics->UnInitialize();
}
void PhysicsManager::ChangeScene()
{
	Physics->ChangeScene();
	/*Physics->Initialize();
	Physics->SetCallBackCollisionFunction([this](CollisionData data, ECollisionEventType type) {
		this->CallbackEvent(data, type);
		});*/
}
void PhysicsManager::OnLoadScene()
{
}

void PhysicsManager::OnUnloadScene()
{
	auto& Container = SceneManagers->GetActiveScene()->m_colliderContainer;
	for (auto& [id, info] : Container) 
	{
		info.bIsDestroyed = true;
	}

	Physics->ChangeScene();
	Physics->Update(1.0f);
	Physics->FinalUpdate();
	m_callbacks.clear();
}

void PhysicsManager::ProcessCallback()
{
	auto& Container = SceneManagers->GetActiveScene()->m_colliderContainer;
	//std::cout << " ProcessCallback size :" << m_callbacks.size() << std::endl;
	//std::cout << " ColliderContainer size :" << Container.size() << std::endl;
	for (auto& [data, type] : m_callbacks) {

	

		auto lhs = Container.find(data.thisId);
		auto rhs = Container.find(data.otherId);
		bool isSameID = data.thisId == data.otherId;
		auto iterEnd = Container.end();

		if (isSameID || lhs == iterEnd || rhs == iterEnd)
		{
			//자신의 콜라이더와 충돌 이거나 충돌체가 없어 졌을 경우 -> error
			Debug->LogError("Collision Callback Error lfs :" + std::to_string(data.thisId) + " ,rhs : " + std::to_string(data.otherId));
			continue;
		}

		auto lhsObj = lhs->second.gameObject;
		auto rhsObj = rhs->second.gameObject;

		Collision collision{ lhsObj,rhsObj,data.contactPoints };
		
		//std::cout << " ProcessCallback thisId :" << lhsObj->GetHashedName().ToString() << " , otherId : " << rhsObj->GetHashedName().ToString() << " , type : " << static_cast<int>(type) << std::endl;

		switch (type)
		{
		case ECollisionEventType::ENTER_OVERLAP:
			SceneManagers->GetActiveScene()->OnTriggerEnter(collision);
			break;
		case ECollisionEventType::ON_OVERLAP:
			SceneManagers->GetActiveScene()->OnTriggerStay(collision);
			break;
		case ECollisionEventType::END_OVERLAP:
			SceneManagers->GetActiveScene()->OnTriggerExit(collision);
			break;
		case ECollisionEventType::ENTER_COLLISION:
			SceneManagers->GetActiveScene()->OnCollisionEnter(collision);
			break;
		case ECollisionEventType::ON_COLLISION:
			SceneManagers->GetActiveScene()->OnCollisionStay(collision);
			break;
		case ECollisionEventType::END_COLLISION:
			SceneManagers->GetActiveScene()->OnCollisionExit(collision);
			break;
		default:
			break;
		}

	}
}

void PhysicsManager::RayCast(RayEvent& rayEvent)
{
	
	auto& Container = SceneManagers->GetActiveScene()->m_colliderContainer;
	RayCastInput inputInfo;

	inputInfo.origin = rayEvent.origin;
	inputInfo.direction = rayEvent.direction;
	inputInfo.distance = rayEvent.distance;
	inputInfo.layerNumber = rayEvent.layerMask;

	auto result = Physics->RayCast(inputInfo,rayEvent.isStatic);

	if (result.hasBlock)
	{
		rayEvent.resultData->hasBlock = result.hasBlock;
		rayEvent.resultData->blockObject = Container[result.id].gameObject;
		rayEvent.resultData->blockPoint = result.blockPosition;
	}

	rayEvent.resultData->hitCount = result.hitSize;
	rayEvent.resultData->hitObjects.reserve(result.hitSize);
	rayEvent.resultData->hitPoints.reserve(result.hitSize);
	rayEvent.resultData->hitObjectLayer.reserve(result.hitSize);

	for (int i = 0; i < result.hitSize; i++)
	{
		rayEvent.resultData->hitObjects.push_back(Container[result.hitId[i]].gameObject);
		rayEvent.resultData->hitPoints.push_back(result.contectPoints[i]);
		rayEvent.resultData->hitObjectLayer.push_back(result.hitLayerNumber[i]);
	}

	if (rayEvent.bUseDebugDraw)
	{
		//todo : debug draw
		//origin , direction , distance
	}

}

bool PhysicsManager::Raycast(RayEvent& rayEvent, RaycastHit& hit)
{
	auto& Container = SceneManagers->GetActiveScene()->m_colliderContainer;
	RayCastInput inputInfo;

	inputInfo.origin = rayEvent.origin;
	inputInfo.direction = rayEvent.direction;
	inputInfo.distance = rayEvent.distance;
	inputInfo.layerNumber = rayEvent.layerMask;

	auto result = Physics->Raycast(inputInfo);

	if (result.hasBlock)
	{
		hit.hitObject = Container[result.id].gameObject;
		hit.hitPoint = result.blockPosition;
		hit.hitNormal = result.blockNormal;
		hit.hitObjectLayer = result.blockLayerNumber;
		return true;
	}

	if (rayEvent.bUseDebugDraw)
	{
		//todo : debug draw
		//origin , direction , distance
		Physics->DrawPVDLine(rayEvent.origin, rayEvent.origin + rayEvent.direction * rayEvent.distance);
	}
	return false;

}

int PhysicsManager::Raycast(RayEvent& rayEvent, std::vector<RaycastHit>& hits)
{
	auto& Container = SceneManagers->GetActiveScene()->m_colliderContainer;
	RayCastInput inputInfo;

	inputInfo.origin = rayEvent.origin;
	inputInfo.direction = rayEvent.direction;
	inputInfo.distance = rayEvent.distance;
	inputInfo.layerNumber = rayEvent.layerMask;

	auto result = Physics->RaycastAll(inputInfo);

	hits.reserve(result.hitSize);
	for (int i = 0; i < result.hitSize; i++)
	{
		RaycastHit hit;
		hit.hitObject = Container[result.hitId[i]].gameObject;
		hit.hitPoint = result.contectPoints[i];
		hit.hitNormal = result.contectNormals[i];
		hit.hitObjectLayer = result.hitLayerNumber[i];
		hits.push_back(hit);
	}

	if (rayEvent.bUseDebugDraw)
	{
		//todo : debug draw
		//origin , direction , distance
		Physics->DrawPVDLine(rayEvent.origin, rayEvent.origin + rayEvent.direction * rayEvent.distance);
	}
	return result.hitSize;
}

int PhysicsManager::BoxSweep(const SweepInput& in, const math::vector3& boxExtent, std::vector<HitResult>& out_hits) {
	SweepOutput pxOut;
	
	pxOut = Physics->BoxSweep(in, boxExtent);

	
	out_hits.clear();
	out_hits.reserve(pxOut.touches.size());

	auto& Container = SceneManagers->GetActiveScene()->m_colliderContainer;
	for (const SweepHitResult& hit : pxOut.touches)
	{
		auto it = Container.find(hit.hitObjectID);
		if (it != Container.end())
		{
			HitResult finalHit;
			finalHit.gameObject = Container[hit.hitObjectID].gameObject;
			finalHit.layer = hit.hitObjectLayer;

			//// 좌표계 변환 (왼손 -> 오른손)
			finalHit.point = hit.hitPoint;
			finalHit.normal = hit.hitNormal;
            finalHit.distance = hit.distance;

			out_hits.push_back(finalHit);
		}
	}

	
	return static_cast<int>(out_hits.size());
}

int PhysicsManager::SphereSweep(const SweepInput& in, float radius, std::vector<HitResult>& out_hits){
	SweepOutput pxOut;

	pxOut = Physics->SphereSweep(in, radius);

	out_hits.clear();
	out_hits.reserve(pxOut.touches.size());

	auto& Container = SceneManagers->GetActiveScene()->m_colliderContainer;
	for (const SweepHitResult& hit : pxOut.touches)
	{
		auto it = Container.find(hit.hitObjectID);
		if (it != Container.end())
		{
			HitResult finalHit;
			finalHit.gameObject = Container[hit.hitObjectID].gameObject;
			finalHit.layer = hit.hitObjectLayer;

			finalHit.point = hit.hitPoint;
			finalHit.normal = hit.hitNormal;
			finalHit.distance = hit.distance;

			out_hits.push_back(finalHit);
		}
	}


	return static_cast<int>(out_hits.size());
}

int PhysicsManager::CapsuleSweep(const SweepInput& in, float radius, float halfHeight, std::vector<HitResult>& out_hits){
	SweepOutput pxOut;

	pxOut = Physics->CapsuleSweep(in, radius, halfHeight);

	out_hits.clear();
	out_hits.reserve(pxOut.touches.size());
	auto& Container = SceneManagers->GetActiveScene()->m_colliderContainer;
	for (const SweepHitResult& hit : pxOut.touches)
	{
		auto it = Container.find(hit.hitObjectID);
		if (it != Container.end())
		{
			HitResult finalHit;
			finalHit.gameObject = Container[hit.hitObjectID].gameObject;
			finalHit.layer = hit.hitObjectLayer;
			
			finalHit.point = hit.hitPoint;
			finalHit.normal = hit.hitNormal;
			finalHit.distance = hit.distance;
			out_hits.push_back(finalHit);
		}
	}
	return static_cast<int>(out_hits.size());
}

int PhysicsManager::BoxOverlap(const OverlapInput& in, const math::vector3& boxExtent, std::vector<HitResult>& out_hits){
	OverlapOutput pxOut;
	pxOut = Physics->BoxOverlap(in, boxExtent);
	out_hits.clear();
	out_hits.reserve(pxOut.touches.size());
	auto& Container = SceneManagers->GetActiveScene()->m_colliderContainer;
	for (const OverlapHitResult& hit : pxOut.touches)
	{
		auto it = Container.find(hit.hitObjectID);
		if (it != Container.end())
		{
			HitResult finalHit;
			finalHit.gameObject = Container[hit.hitObjectID].gameObject;
			finalHit.layer = hit.hitObjectLayer;
			// overlap none hit point and normal
			/*finalHit.point = hit.hitPoint;
			finalHit.normal = hit.hitNormal;*/
			out_hits.push_back(finalHit);
		}
	}
	return static_cast<int>(out_hits.size());
}
int PhysicsManager::SphereOverlap(const OverlapInput& in, float radius, std::vector<HitResult>& out_hits){
	OverlapOutput pxOut;
	pxOut = Physics->SphereOverlap(in, radius);
	out_hits.clear();
	out_hits.reserve(pxOut.touches.size());
	auto& Container = SceneManagers->GetActiveScene()->m_colliderContainer;
	for (const OverlapHitResult& hit : pxOut.touches)
	{
		auto it = Container.find(hit.hitObjectID);
		if (it != Container.end())
		{
			HitResult finalHit;
			finalHit.gameObject = Container[hit.hitObjectID].gameObject;
			finalHit.layer = hit.hitObjectLayer;
			// overlap none hit point and normal
			/*finalHit.point = hit.hitPoint;
			finalHit.normal = hit.hitNormal;*/
			out_hits.push_back(finalHit);
		}
	}
	return static_cast<int>(out_hits.size());
}
int PhysicsManager::CapsuleOverlap(const OverlapInput& in, float radius, float halfHeight, std::vector<HitResult>& out_hits){
	OverlapOutput pxOut;
	pxOut = Physics->CapsuleOverlap(in, radius, halfHeight);
	out_hits.clear();
	out_hits.reserve(pxOut.touches.size());
	auto& Container = SceneManagers->GetActiveScene()->m_colliderContainer;
	for (const OverlapHitResult& hit : pxOut.touches)
	{
		auto it = Container.find(hit.hitObjectID);
		if (it != Container.end())
		{
			HitResult finalHit;
			finalHit.gameObject = Container[hit.hitObjectID].gameObject;
			finalHit.layer = hit.hitObjectLayer;
			// overlap none hit point and normal
			/*finalHit.point = hit.hitPoint;
			finalHit.normal = hit.hitNormal;*/
			out_hits.push_back(finalHit);
		}
	}
	return static_cast<int>(out_hits.size());
}


void PhysicsManager::AddCollider(BoxColliderComponent* box)
{
	if (!box) return;

	auto obj = box->GetOwner();
	if (obj == nullptr)
	{
		Debug->LogError("BoxColliderComponent has no owner Entity.");
		return;
	}

	auto& transform = obj->Transform_();
	unsigned int gameObjectID = obj->GetInstanceID();
	std::cout << "PhysicsManager::AddCollider(Box) - Entity InstanceID: " << gameObjectID << std::endl;
	auto boxInfo = box->GetBoxInfo();
	auto tranformOffset = box->GetPositionOffset();
	auto rotationOffset = box->GetRotationOffset();

	boxInfo.colliderInfo.id = gameObjectID;
	boxInfo.colliderInfo.layerNumber = obj->GetCollisionType();
	auto& collisionTransform = boxInfo.colliderInfo.collsionTransform;
	FillPhysicsTransform(collisionTransform, transform);
	ApplyScaledColliderOffset(collisionTransform, tranformOffset);

	box->SetBoxInfoMation(boxInfo);
}

void PhysicsManager::AddCollider(SphereColliderComponent* sphere)
{
	if (!sphere) return;

	auto obj = sphere->GetOwner();
	auto& transform = obj->Transform_();
	auto type = sphere->GetColliderType();
	auto sphereInfo = sphere->GetSphereInfo();
	auto posOffset = sphere->GetPositionOffset();
	auto rotOffset = sphere->GetRotationOffset();

	unsigned int gameObjectID = obj->GetInstanceID();
	std::cout << "PhysicsManager::AddCollider(Sphere) - Entity InstanceID: " << gameObjectID << std::endl;
	sphereInfo.colliderInfo.id = gameObjectID;
	sphereInfo.colliderInfo.layerNumber = obj->GetCollisionType();
	auto& collisionTransform = sphereInfo.colliderInfo.collsionTransform;
	FillPhysicsTransform(collisionTransform, transform);
	ApplyScaledColliderOffset(collisionTransform, posOffset);

	sphere->SetSphereInfoMation(sphereInfo);
}

void PhysicsManager::AddCollider(CapsuleColliderComponent* capsule)
{
	if (!capsule) return;

	auto obj = capsule->GetOwner();
	auto& transform = obj->Transform_();
	auto capsuleInfo = capsule->GetCapsuleInfo();
	auto posOffset = capsule->GetPositionOffset();
	auto rotOffset = capsule->GetRotationOffset();
	unsigned int gameObjectID = obj->GetInstanceID();
	std::cout << "PhysicsManager::AddCollider(Capsule) - Entity InstanceID: " << gameObjectID << std::endl;
	capsuleInfo.colliderInfo.id = gameObjectID;
	capsuleInfo.colliderInfo.layerNumber = obj->GetCollisionType();

	math::vector3 ignoredScale{};
	math::quaternion pureWorldRot{};
	math::vector3 pureWorldPos{};
	math::decompose(transform.GetWorldMatrix_NoScale(), ignoredScale,
		pureWorldRot, pureWorldPos);

	const math::quaternion offsetRot = capsule->GetRotationOffset();
	const math::vector3 offsetPos = capsule->GetPositionOffset();
	const math::quaternion finalWorldRot = offsetRot * pureWorldRot;
	const math::vector3 finalWorldPos =
		pureWorldPos + math::rotate(offsetPos, pureWorldRot);

	auto& collisionTransform = capsuleInfo.colliderInfo.collsionTransform;
	FillPhysicsTransform(collisionTransform, transform);
	collisionTransform.worldPosition = finalWorldPos;
	collisionTransform.worldRotation = finalWorldRot;
	collisionTransform.worldMatrix = math::compose(
		collisionTransform.worldScale, finalWorldRot, finalWorldPos);
	
	capsule->SetCapsuleInfoMation(capsuleInfo);
}

void PhysicsManager::AddCollider(MeshColliderComponent* mesh)
{
	if (!mesh) return;

	auto obj = mesh->GetOwner();
	bool hasMesh = obj->HasComponent<MeshRenderer>();

	if (!hasMesh) return;

	auto& transform = obj->Transform_();
	auto type = mesh->GetColliderType();
	auto convexMeshInfo = mesh->GetMeshInfo();
	auto posOffset = mesh->GetPositionOffset();
	auto rotOffset = mesh->GetRotationOffset();
	unsigned int gameObjectID = obj->GetInstanceID();
	std::cout << "PhysicsManager::AddCollider(Mesh) - Entity InstanceID: " << gameObjectID << std::endl;
	convexMeshInfo.colliderInfo.id = gameObjectID;
	convexMeshInfo.colliderInfo.layerNumber = obj->GetCollisionType();
	auto& collisionTransform = convexMeshInfo.colliderInfo.collsionTransform;
	FillPhysicsTransform(collisionTransform, transform);
	ApplyScaledColliderOffset(collisionTransform, posOffset);
	// I5-D4a: MeshRenderer 정점의 값 복사 후 미사용(죽은 줄) 제거. convex cook에
	// 정점을 채우는 코드는 엔진 전체에 없고(ConvexMeshColliderInfo::vertices는
	// nullptr로 cook — PHASE 19 소관) m_Mesh가 null이면 여기서 죽기까지 했다.

	mesh->SetMeshInfoMation(convexMeshInfo);
}

void PhysicsManager::AddCollider(CharacterControllerComponent* controller)
{
	if (!controller) return;

	auto obj = controller->GetOwner();
	auto& transform = obj->Transform_();
	auto controllerInfo = controller->GetControllerInfo();
	auto movementInfo = controller->GetMovementInfo();
	auto posOffset = controller->GetPositionOffset();
	auto rotOffset = controller->GetRotationOffset();
	ColliderID gameObjectID = obj->GetInstanceID();
	std::cout << "PhysicsManager::AddCollider(CharacterController) - Entity InstanceID: " << gameObjectID << std::endl;
	controllerInfo.id = gameObjectID;
	controllerInfo.layerNumber = obj->GetCollisionType();
	controllerInfo.position =
		transform.GetWorldPosition() + controller->GetPositionOffset();
	Physics->CreateCCT(controllerInfo, movementInfo);


	controller->SetControllerInfo(controllerInfo);
}

void PhysicsManager::AddCollider(TerrainColliderComponent* terrain)
{
	if (!terrain) return;

	auto object = terrain->GetOwner();
	TerrainColliderComponent* collider = object->GetComponent<TerrainColliderComponent>();
	Transform& transform = object->Transform_();
	auto terrainComponent = object->GetComponent<TerrainComponent>();

	HeightFieldColliderInfo heightFieldInfo;

	ColliderID gameObjectID = object->GetInstanceID();
	std::cout << "PhysicsManager::AddCollider(Terrain) - Entity InstanceID: " << gameObjectID << std::endl;

	collider->SetColliderID(gameObjectID);
	heightFieldInfo.colliderInfo.id = gameObjectID;
	heightFieldInfo.colliderInfo.layerNumber = object->GetCollisionType();
	FillPhysicsTransform(
		heightFieldInfo.colliderInfo.collsionTransform, transform);

	heightFieldInfo.numCols = terrainComponent->GetWidth();
	heightFieldInfo.numRows = terrainComponent->GetHeight();
	heightFieldInfo.heightMep = terrainComponent->GetHeightMap();

	heightFieldInfo.colliderInfo.staticFriction = 0.5f;
	heightFieldInfo.colliderInfo.dynamicFriction = 0.5f;
	heightFieldInfo.colliderInfo.restitution = 0.1f;

	Physics->CreateStaticBody(heightFieldInfo, EColliderType::COLLISION);

	collider->SetHeightFieldColliderInfo(heightFieldInfo);
}

void PhysicsManager::RemoveCollider(BoxColliderComponent* box)
{
	auto& Container = SceneManagers->GetActiveScene()->m_colliderContainer;
	if (box && box->IsDestroyMark())
	{
		auto ID = box->GetBoxInfo().colliderInfo.id;
		Container[ID].bIsDestroyed = true;
		//Physics->DestroyActor(ID);
	}
}

void PhysicsManager::RemoveCollider(SphereColliderComponent* sphere)
{
	auto& Container = SceneManagers->GetActiveScene()->m_colliderContainer;
	if (sphere && sphere->IsDestroyMark())
	{
		auto ID = sphere->GetSphereInfo().colliderInfo.id;
		Container[ID].bIsDestroyed = true;
		//Physics->DestroyActor(ID);
	}
}

void PhysicsManager::RemoveCollider(CapsuleColliderComponent* capsule)
{
	auto& Container = SceneManagers->GetActiveScene()->m_colliderContainer;
	if (capsule && capsule->IsDestroyMark())
	{
		auto ID = capsule->GetCapsuleInfo().colliderInfo.id;
		Container[ID].bIsDestroyed = true;
		//Physics->DestroyActor(ID);
	}
}

void PhysicsManager::RemoveCollider(MeshColliderComponent* mesh)
{
	auto& Container = SceneManagers->GetActiveScene()->m_colliderContainer;
	if (mesh && mesh->IsDestroyMark())
	{
		auto ID = mesh->GetMeshInfo().colliderInfo.id;
		Container[ID].bIsDestroyed = true;
		//Physics->DestroyActor(ID);
	}
}

void PhysicsManager::RemoveCollider(CharacterControllerComponent* controller)
{
	auto& Container = SceneManagers->GetActiveScene()->m_colliderContainer;
	if (controller && controller->IsDestroyMark())
	{
		auto ID = controller->GetControllerInfo().id;
		Container[ID].bIsDestroyed = true;
		Physics->RemoveCCT(ID);
	}
}

void PhysicsManager::RemoveCollider(TerrainColliderComponent* terrain)
{
	auto& Container = SceneManagers->GetActiveScene()->m_colliderContainer;
	if (terrain && terrain->IsDestroyMark())
	{
		auto ID = terrain->GetColliderID();
		Container[ID].bIsDestroyed = true;
		//Physics->DestroyActor(ID);
	}
}

void PhysicsManager::CallbackEvent(CollisionData data, ECollisionEventType type)
{
	//std::cout << "PhysicsManager::CallbackEvent - ThisID: " << data.thisId << ", OtherID: " << data.otherId << ", EventType: " << static_cast<int>(type) << std::endl;
	m_callbacks.push_back({ data,type });
}

void PhysicsManager::SetPhysicData()
{
	
	auto& Container = SceneManagers->GetActiveScene()->m_colliderContainer;
	//std::cout << "Container size" << Container.size()<<std::endl;
	for (auto& [id, colliderInfo] : Container) 
	{
		if (colliderInfo.bIsDestroyed)
		{
			continue;
		}

		auto& transform = colliderInfo.gameObject->Transform_();
		//colliderInfo.collider.
		auto rigidbody = colliderInfo.gameObject->GetComponent<RigidBodyComponent>();

		// 콜라이더가 붙은 오브젝트는 RigidBodyComponent도 함께 있다고 가정하고 짜여 있다
		// (아래 CCT 분기도 rigidbody->GetLinearVelocity()를 쓴다).
		// 하나라도 빠지면 여기서 널 역참조로 엔진 전체가 죽었다 — 잘못 구성된
		// 오브젝트 하나 때문에 프레임을 통째로 잃지 않도록 건너뛴다.
		if (nullptr == rigidbody)
		{
			static std::unordered_set<unsigned int> reported;
			if (reported.insert(id).second)
			{
				Debug->LogError("[물리] RigidBodyComponent 없이 콜라이더만 붙은 오브젝트를 건너뜁니다: "
					+ colliderInfo.gameObject->m_name.ToString());
			}
			continue;
		}

		bool _isColliderEnabled = rigidbody->IsColliderEnabled();
		//todo : CCT,Controller,ragdoll,capsule,?섏쨷??deformeSuface
		//sleeping
		bool enable = colliderInfo.gameObject->IsEnabled();

		if (!enable) {
			Physics->PutToSleep(id);
			continue;
		}
		else {
			Physics->WakeUp(id);
		}

		if (colliderInfo.id == m_controllerTypeId)
		{
			//Benchmark bm;
			
			auto controller = colliderInfo.gameObject->GetComponent<CharacterControllerComponent>();
			CharacterControllerGetSetData data;
			data.position =
				transform.GetWorldPosition() + controller->GetPositionOffset();
			data.rotation = transform.GetWorldQuaternion();
			data.Scale = transform.GetWorldScale();

			auto controllerInfo = controller->GetControllerInfo();
			auto prevlayer = controllerInfo.layerNumber;
			auto currentLayer = static_cast<unsigned int>(colliderInfo.gameObject->GetCollisionType());
			
			if (prevlayer != currentLayer) 
			{
				data.LayerNumber = currentLayer;
				controllerInfo.layerNumber = currentLayer;
				controller->SetControllerInfo(controllerInfo);
			}

			Physics->SetCCTData(id, data);

			CharacterMovementGetSetData movementData;

			auto movementInfo = controller->GetMovementInfo();
			movementData.acceleration = movementInfo.acceleration;
			movementData.maxSpeed = movementInfo.maxSpeed;
			movementData.velocity = rigidbody->GetLinearVelocity();
			movementData.isFall = controller->IsFalling();
			movementData.restrictDirection = controller->GetMoveRestrict();

			Physics->SetMovementData(id, movementData);

			//std::cout << " PhysicsManager::SetPhysicData CCT : " << bm.GetElapsedTime() << std::endl;
		}
		else if (colliderInfo.id == m_heightFieldTypeId)
		{
			HeightFieldColliderInfo data = colliderInfo.gameObject->GetComponent<TerrainColliderComponent>()->GetHeightFieldColliderInfo();
			continue;
		}
		else
		{

			//Benchmark bm1;
			RigidBodyGetSetData data;
			if (colliderInfo.gameObject->m_tag == "Asis") {
				int a = 0;
			}
			math::vector3 ignoredScale{};
			math::quaternion pureWorldRot{};
			math::vector3 pureWorldPos{};
			math::decompose(transform.GetWorldMatrix_NoScale(), ignoredScale,
				pureWorldRot, pureWorldPos);

			// 2. 콜라이더의 로컬 오프셋을 가져옵니다.  
			auto offset = colliderInfo.collider->GetPositionOffset();
			auto rotOffset = colliderInfo.collider->GetRotationOffset();

			auto type = colliderInfo.collider->GetColliderType();

			// 3. 오프셋을 적용하여 물리 객체의 '최종 월드 Pose'를 계산합니다
			data.rotation = rotOffset * pureWorldRot;
			data.position = pureWorldPos + math::rotate(offset, pureWorldRot);
			// 4. 월드 스케일 값을 따로 가져옵니다.  
			data.scale = transform.GetWorldScale();
			if(data.scale != rigidbody->GetScale())
			{
				data.isGeometryDirty = true;
			}

			data.angularVelocity = rigidbody->GetAngularVelocity();
			data.linearVelocity = rigidbody->GetLinearVelocity();
			data.isLockLinearX = rigidbody->IsLockLinearX();
			data.isLockLinearY = rigidbody->IsLockLinearY();
			data.isLockLinearZ = rigidbody->IsLockLinearZ();
			data.isLockAngularX = rigidbody->IsLockAngularX();
			data.isLockAngularY = rigidbody->IsLockAngularY();
			data.isLockAngularZ = rigidbody->IsLockAngularZ();

			data.maxAngularVelocity = rigidbody->GetMaxAngularVelocity();
			data.maxLinearVelocity = rigidbody->GetMaxLinearVelocity();
			data.maxContactImpulse = rigidbody->GetMaxContactImpulse();
			data.maxDepenetrationVelocity = rigidbody->GetMaxDepenetrationVelocity();

			data.forceMode = static_cast<int>(rigidbody->GetForceMode());
			rigidbody->SetForceMode(EForceMode::NONE); 
			data.velocity = rigidbody->GetLinearVelocity();
			data.AngularDamping = rigidbody->GetAngularDamping();
			data.LinearDamping = rigidbody->GetLinearDamping();
			data.mass = rigidbody->GetMass();

			
			data.m_EColliderType = rigidbody->IsTrigger() ? EColliderType::TRIGGER : EColliderType::COLLISION;
			data.isColliderEnabled = rigidbody->IsColliderEnabled();
			data.useGravity = rigidbody->IsUsingGravity();
			data.isKinematic = rigidbody->IsKinematic();
			data.isDisabled = !rigidbody->IsColliderEnabled();

			data.LayerNumber = static_cast<unsigned int>(colliderInfo.gameObject->GetCollisionType());

			data.isDirty = rigidbody->IsRigidbodyDirty();
			rigidbody->DevelopOnlyDirtySet(false);
			

			//std::cout << " PhysicsManager::SetPhysicData CCT elses RigidBodyGetSetData Set  : " << bm1.GetElapsedTime() << std::endl;

			//Benchmark bm2;
			Physics->SetRigidBodyData(id, data);
			//std::cout << " PhysicsManager::SetPhysicData CCT elses SetRigidBodyData : " << bm2.GetElapsedTime() << std::endl;
		}
	}
	
}

//PxScene --> GameScene
void PhysicsManager::GetPhysicData()
{
	//Benchmark bm;
	Scene* scene = SceneManagers->GetActiveScene();
	if (!scene) return;
	auto& Container = scene->m_colliderContainer;
	std::vector<TransformWorldWrite> worldWrites;
	worldWrites.reserve(Container.size());
	for (auto& [id, ColliderInfo] : Container) {

		if (nullptr == ColliderInfo.gameObject)
			continue;

		if (ColliderInfo.gameObject->IsDestroyMark())
		{
			ColliderInfo.bIsDestroyed = true;
			continue;
		}

		
		if (ColliderInfo.bIsDestroyed)
		{
			continue;
		}

		auto rigidbody = ColliderInfo.gameObject->GetComponent<RigidBodyComponent>();
		auto& transform = ColliderInfo.gameObject->Transform_();
		if (!rigidbody) continue;


		if (rigidbody->GetBodyType() != EBodyType::DYNAMIC)
		{
			
			continue;
		}

		
		if (ColliderInfo.id == m_controllerTypeId) {
			
			auto controller = ColliderInfo.gameObject->GetComponent<CharacterControllerComponent>();
			auto controll = Physics->GetCCTData(id);
			auto movement = Physics->GetMovementData(id);
			const math::vector3 position =
				controll.position - controller->GetPositionOffset();

			controller->SetFalling(movement.isFall);
			rigidbody->SetLinearVelocity(movement.velocity);
		worldWrites.push_back(TransformWorldWrite{
			scene->HandleOf(ColliderInfo.gameObject->m_index),
			math::compose(transform.GetWorldScale(),
				transform.GetWorldQuaternion(), position) });
		}
		else
		{
			
			auto data = Physics->GetRigidBodyData(id);
			rigidbody->SetLinearVelocity(data.linearVelocity);
			rigidbody->SetAngularVelocity(data.angularVelocity);

			rigidbody->SetLockLinearX(data.isLockLinearX);
			rigidbody->SetLockLinearY(data.isLockLinearY);
			rigidbody->SetLockLinearZ(data.isLockLinearZ);
			rigidbody->SetLockAngularX(data.isLockAngularX);
			rigidbody->SetLockAngularY(data.isLockAngularY);
			rigidbody->SetLockAngularZ(data.isLockAngularZ);

			auto posOffset = ColliderInfo.collider->GetPositionOffset();
			auto rotOffset = ColliderInfo.collider->GetRotationOffset();

			// 2. 회전 역연산: 물리 월드에서 받은 회전값(data.rotation)에서 콜라이더의 회전 오프셋을 제거합니다.
			const math::quaternion pureWorldRot =
				math::inverse(rotOffset) * data.rotation;

			// 3. 위치 역연산: 위에서 계산한 '순수 월드 회전'을 사용하여 위치 오프셋의 영향을 제거합니다.
			const math::vector3 pureWorldPos =
				data.position - math::rotate(posOffset, pureWorldRot);

			const math::matrix4x4 matrix =
				math::compose(data.scale, pureWorldRot, pureWorldPos);

		worldWrites.push_back(TransformWorldWrite{
			scene->HandleOf(ColliderInfo.gameObject->m_index), matrix });
		}
	}
	// X7: PhysX 결과를 한 잠금/한 spatial epoch으로 publish한다. Scene이
	// compiled parent order로 정렬해 parent+child 동시 write도 결정론적으로
	// local을 역산하고 world cache를 즉시 맞춘다.
	scene->ApplyWorldWriteBatch(worldWrites, TransformWriteReason::Physics);
	//std::cout <<" PhysicsManager::GetPhysicData" << bm.GetElapsedTime() << std::endl;
}

// 펜딩된 CCT 위치 변경 요청을 일괄 처리하는 함수
void PhysicsManager::ApplyPendingControllerPositionChanges()
{
	if (!Physics || m_pendingControllerPositions.empty()) return;

	// ID를 통해 게임오브젝트를 찾기 위해 콜라이더 컨테이너에 접근합니다.
	Scene* scene = SceneManagers->GetActiveScene();
	if (!scene) return;
	auto& colliderContainer = scene->m_colliderContainer;
	std::vector<TransformWorldWrite> worldWrites;
	worldWrites.reserve(m_pendingControllerPositions.size());

	for (const auto& change : m_pendingControllerPositions)
	{
		// 1. PhysX 엔진 내부의 컨트롤러 위치를 강제로 설정합니다.
		Physics->SetControllerPosition(change.id, change.position);

		// 2. 해당 ID를 가진 게임오브젝트를 찾습니다.
		auto it = colliderContainer.find(change.id);
		if (it != colliderContainer.end())
		{
			auto& colliderInfo = it->second;
			if (colliderInfo.gameObject)
			{
				Transform& transform = colliderInfo.gameObject->Transform_();
				worldWrites.push_back(TransformWorldWrite{
					scene->HandleOf(colliderInfo.gameObject->m_index),
					math::compose(transform.GetWorldScale(),
						transform.GetWorldQuaternion(), change.position) });
			}
		}
	}
	scene->ApplyWorldWriteBatch(worldWrites, TransformWriteReason::Physics);

	m_pendingControllerPositions.clear();
}


bool PhysicsManager::SaveCollisionMatrix()
{
	MetaYml::Node matrixNode;
	std::vector<std::string> layerNames = TagManagers->GetLayers();

	for (int i = 0; i < layerNames.size(); ++i)
	{
		auto vec = m_collisionMatrix[i];
		for (int j = 0; j < layerNames.size(); ++j)
		{
			matrixNode[i][j] = (bool)vec[j];
		}
	}

	std::ostringstream payload;
	payload << matrixNode;

	// 목적 경로는 LoadCollisionMatrix와 같은 규약으로 만든다. 게시는 Editor Host가
	// 소유하며 Player에는 handler가 없어 정상적으로 실패한다.
	UncatalogedAuthoringRequest request{};
	request.destinationPath = PathFinder::ProjectSettingPath("CollisionMatrix.asset");
	request.payload = payload.str();

	if (!AssetAuthoringPort::WriteCollisionMatrix(request))
	{
		Debug->LogError(
			"CollisionMatrix save requires a complete Editor authoring transaction");
		return false;
	}

	return true;
}

void PhysicsManager::LoadCollisionMatrix()
{
	file::path matrixSettingsPath = PathFinder::ProjectSettingPath("CollisionMatrix.asset");
	std::ifstream settingsFile(matrixSettingsPath);
	if (!settingsFile.is_open())
	{
		Debug->LogWarning("No CollisionMatrix.asset file found. Using default collision matrix.");
		return;
	}
	MetaYml::Node matrixNode = MetaYml::LoadFile(matrixSettingsPath.string());
	constexpr int MAX_LAYER_SIZE = 32;
	for (int i = 0; i < MAX_LAYER_SIZE; ++i)
	{
		for (int j = 0; j < MAX_LAYER_SIZE; ++j)
		{
			if (matrixNode[i] && matrixNode[i][j])
			{
				m_collisionMatrix[i][j] = matrixNode[i][j].as<bool>();
			}
			else
			{
				m_collisionMatrix[i][j] = true; // 기본값 설정
			}
		}
	}
	SetCollisionMatrix(m_collisionMatrix);
	settingsFile.close();
}

void PhysicsManager::SetRigidBodyState(const RigidBodyState& state)
{
	m_pendingChanges.push_back(state);
}

bool PhysicsManager::IsRigidBodyKinematic(unsigned int id) const
{
	return Physics->IsKinematic(id);
}

bool PhysicsManager::IsRigidBodyTrigger(unsigned int id) const
{
	
	return Physics->IsTrigger(id);
}
bool PhysicsManager::IsRigidBodyColliderEnabled(unsigned int id) const
{
	return Physics->IsColliderEnabled(id);
}

bool PhysicsManager::IsRigidBodyUseGravity(unsigned int id) const 
{
	return Physics->IsUseGravity(id);
}

void PhysicsManager::SetControllerPosition(UINT id, const math::vector3& pos)
{
	m_pendingControllerPositions.push_back({ id, pos });
}

void PhysicsManager::ApplyPendingChanges()
{
	for (const auto& change : m_pendingChanges)
	{
		RigidBodyGetSetData data = Physics->GetRigidBodyData(change.id);

		data.isKinematic = change.isKinematic;
		data.m_EColliderType = change.isTrigger ? EColliderType::TRIGGER : EColliderType::COLLISION;
		data.isColliderEnabled = change.isColliderEnabled;
		data.useGravity = change.useGravity;
		data.isDisabled = !change.isColliderEnabled;

		data.moveDirty = change.movePositionDirty;
		data.movePosition = change.movePosition;

		Physics->SetRigidBodyData(change.id, data);
	}
	m_pendingChanges.clear();
}
