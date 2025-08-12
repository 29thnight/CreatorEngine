#include "PhysicsManager.h"
#include "SceneManager.h"
#include "Scene.h"
#include "GameObject.h"
#include "Transform.h"
#include "Component.h"
#include "RigidBodyComponent.h"
#include "BoxColliderComponent.h"
#include "SphereColliderComponent.h"
#include "CapsuleColliderComponent.h"
#include "MeshCollider.h"
#include "MeshRenderer.h"
#include "Terrain.h"
#include "TerrainCollider.h"
#include "CharacterControllerComponent.h"

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
	Physics->SetCallBackCollisionFunction([this](CollisionData data, ECollisionEventType type) {
		this->CallbackEvent(data, type);
	});
	
	//기본 전체 충돌 매트릭스 설정
	std::vector<std::vector<uint8_t>> collisionGrid;
	collisionGrid.resize(32);
	for (auto& row : collisionGrid) {
		row.resize(32, 1); // 기본적으로 모든 충돌체가 충돌 가능하도록 설정
	}
	SetCollisionMatrix(collisionGrid);
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
	
	// 물리 엔진에서 씬 데이터 가져오기
	GetPhysicData();
	// 콜백 이벤트 처리
	ProcessCallback();
	
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

void PhysicsManager::AddCollider(BoxColliderComponent* box)
{
	if (!box) return;

	auto obj = box->GetOwner();
	if (obj == nullptr)
	{
		Debug->LogError("BoxColliderComponent has no owner GameObject.");
		return;
	}

	auto& transform = obj->m_transform;
	unsigned int gameObjectID = obj->GetInstanceID();
	std::cout << "PhysicsManager::AddCollider(Box) - GameObject InstanceID: " << gameObjectID << std::endl;
	auto boxInfo = box->GetBoxInfo();
	auto tranformOffset = box->GetPositionOffset();
	auto rotationOffset = box->GetRotationOffset();

	boxInfo.colliderInfo.id = gameObjectID;
	boxInfo.colliderInfo.layerNumber = obj->GetCollisionType();
	boxInfo.colliderInfo.collsionTransform.localMatrix = transform.GetLocalMatrix();
	boxInfo.colliderInfo.collsionTransform.worldMatrix = transform.GetWorldMatrix();
	boxInfo.colliderInfo.collsionTransform.localMatrix.Decompose(boxInfo.colliderInfo.collsionTransform.localScale, boxInfo.colliderInfo.collsionTransform.localRotation, boxInfo.colliderInfo.collsionTransform.localPosition);
	boxInfo.colliderInfo.collsionTransform.worldMatrix.Decompose(boxInfo.colliderInfo.collsionTransform.worldScale, boxInfo.colliderInfo.collsionTransform.worldRotation, boxInfo.colliderInfo.collsionTransform.worldPosition);
	//offset
	if (tranformOffset != DirectX::SimpleMath::Vector3::Zero)
	{
		boxInfo.colliderInfo.collsionTransform.worldMatrix._41 = 0.0f;
		boxInfo.colliderInfo.collsionTransform.worldMatrix._42 = 0.0f;
		boxInfo.colliderInfo.collsionTransform.worldMatrix._43 = 0.0f;

		tranformOffset = DirectX::SimpleMath::Vector3::Transform(tranformOffset, boxInfo.colliderInfo.collsionTransform.worldMatrix);

		boxInfo.colliderInfo.collsionTransform.worldPosition += tranformOffset;

		boxInfo.colliderInfo.collsionTransform.worldMatrix._41 = boxInfo.colliderInfo.collsionTransform.worldPosition.x;
		boxInfo.colliderInfo.collsionTransform.worldMatrix._42 = boxInfo.colliderInfo.collsionTransform.worldPosition.y;
		boxInfo.colliderInfo.collsionTransform.worldMatrix._43 = boxInfo.colliderInfo.collsionTransform.worldPosition.z;

	}

	box->SetBoxInfoMation(boxInfo);
}

void PhysicsManager::AddCollider(SphereColliderComponent* sphere)
{
	if (!sphere) return;

	auto obj = sphere->GetOwner();
	auto& transform = obj->m_transform;
	auto type = sphere->GetColliderType();
	auto sphereInfo = sphere->GetSphereInfo();
	auto posOffset = sphere->GetPositionOffset();
	auto rotOffset = sphere->GetRotationOffset();

	unsigned int gameObjectID = obj->GetInstanceID();
	std::cout << "PhysicsManager::AddCollider(Sphere) - GameObject InstanceID: " << gameObjectID << std::endl;
	sphereInfo.colliderInfo.id = gameObjectID;
	sphereInfo.colliderInfo.layerNumber = obj->GetCollisionType();
	sphereInfo.colliderInfo.collsionTransform.localMatrix = transform.GetLocalMatrix();
	sphereInfo.colliderInfo.collsionTransform.worldMatrix = transform.GetWorldMatrix();
	sphereInfo.colliderInfo.collsionTransform.localMatrix.Decompose(sphereInfo.colliderInfo.collsionTransform.localScale, sphereInfo.colliderInfo.collsionTransform.localRotation, sphereInfo.colliderInfo.collsionTransform.localPosition);
	sphereInfo.colliderInfo.collsionTransform.worldMatrix.Decompose(sphereInfo.colliderInfo.collsionTransform.worldScale, sphereInfo.colliderInfo.collsionTransform.worldRotation, sphereInfo.colliderInfo.collsionTransform.worldPosition);

	//offset 
	if (posOffset != DirectX::SimpleMath::Vector3::Zero)
	{
		sphereInfo.colliderInfo.collsionTransform.worldMatrix._41 = 0.0f;
		sphereInfo.colliderInfo.collsionTransform.worldMatrix._42 = 0.0f;
		sphereInfo.colliderInfo.collsionTransform.worldMatrix._43 = 0.0f;

		posOffset = DirectX::SimpleMath::Vector3::Transform(posOffset, sphereInfo.colliderInfo.collsionTransform.worldMatrix);

		sphereInfo.colliderInfo.collsionTransform.worldPosition += posOffset;

		sphereInfo.colliderInfo.collsionTransform.worldMatrix._41 = sphereInfo.colliderInfo.collsionTransform.worldPosition.x;
		sphereInfo.colliderInfo.collsionTransform.worldMatrix._42 = sphereInfo.colliderInfo.collsionTransform.worldPosition.y;
		sphereInfo.colliderInfo.collsionTransform.worldMatrix._43 = sphereInfo.colliderInfo.collsionTransform.worldPosition.z;
	}

	sphere->SetSphereInfoMation(sphereInfo);
}

void PhysicsManager::AddCollider(CapsuleColliderComponent* capsule)
{
	if (!capsule) return;

	auto obj = capsule->GetOwner();
	auto& transform = obj->m_transform;
	auto capsuleInfo = capsule->GetCapsuleInfo();
	auto posOffset = capsule->GetPositionOffset();
	auto rotOffset = capsule->GetRotationOffset();
	unsigned int gameObjectID = obj->GetInstanceID();
	std::cout << "PhysicsManager::AddCollider(Capsule) - GameObject InstanceID: " << gameObjectID << std::endl;
	capsuleInfo.colliderInfo.id = gameObjectID;
	capsuleInfo.colliderInfo.layerNumber = obj->GetCollisionType();
	capsuleInfo.colliderInfo.collsionTransform.localMatrix = transform.GetLocalMatrix();
	capsuleInfo.colliderInfo.collsionTransform.worldMatrix = transform.GetWorldMatrix();
	capsuleInfo.colliderInfo.collsionTransform.localMatrix.Decompose(capsuleInfo.colliderInfo.collsionTransform.localScale, capsuleInfo.colliderInfo.collsionTransform.localRotation, capsuleInfo.colliderInfo.collsionTransform.localPosition);
	capsuleInfo.colliderInfo.collsionTransform.worldMatrix.Decompose(capsuleInfo.colliderInfo.collsionTransform.worldScale, capsuleInfo.colliderInfo.collsionTransform.worldRotation, capsuleInfo.colliderInfo.collsionTransform.worldPosition);
	//offset 
	if (posOffset != DirectX::SimpleMath::Vector3::Zero)
	{
		capsuleInfo.colliderInfo.collsionTransform.worldMatrix._41 = 0.0f;
		capsuleInfo.colliderInfo.collsionTransform.worldMatrix._42 = 0.0f;
		capsuleInfo.colliderInfo.collsionTransform.worldMatrix._43 = 0.0f;
		posOffset = DirectX::SimpleMath::Vector3::Transform(posOffset, capsuleInfo.colliderInfo.collsionTransform.worldMatrix);
		capsuleInfo.colliderInfo.collsionTransform.worldPosition += posOffset;
		capsuleInfo.colliderInfo.collsionTransform.worldMatrix._41 = capsuleInfo.colliderInfo.collsionTransform.worldPosition.x;
		capsuleInfo.colliderInfo.collsionTransform.worldMatrix._42 = capsuleInfo.colliderInfo.collsionTransform.worldPosition.y;
		capsuleInfo.colliderInfo.collsionTransform.worldMatrix._43 = capsuleInfo.colliderInfo.collsionTransform.worldPosition.z;
	}
	capsule->SetCapsuleInfoMation(capsuleInfo);
}

void PhysicsManager::AddCollider(MeshColliderComponent* mesh)
{
	if (!mesh) return;

	auto obj = mesh->GetOwner();
	bool hasMesh = obj->HasComponent<MeshRenderer>();

	if (!hasMesh) return;

	auto& transform = obj->m_transform;
	auto type = mesh->GetColliderType();
	auto convexMeshInfo = mesh->GetMeshInfo();
	auto posOffset = mesh->GetPositionOffset();
	auto rotOffset = mesh->GetRotationOffset();
	unsigned int gameObjectID = obj->GetInstanceID();
	std::cout << "PhysicsManager::AddCollider(Mesh) - GameObject InstanceID: " << gameObjectID << std::endl;
	convexMeshInfo.colliderInfo.id = gameObjectID;
	convexMeshInfo.colliderInfo.layerNumber = obj->GetCollisionType();
	convexMeshInfo.colliderInfo.collsionTransform.localMatrix = transform.GetLocalMatrix();
	convexMeshInfo.colliderInfo.collsionTransform.worldMatrix = transform.GetWorldMatrix();
	convexMeshInfo.colliderInfo.collsionTransform.localMatrix.Decompose(convexMeshInfo.colliderInfo.collsionTransform.localScale, convexMeshInfo.colliderInfo.collsionTransform.localRotation, convexMeshInfo.colliderInfo.collsionTransform.localPosition);
	convexMeshInfo.colliderInfo.collsionTransform.worldMatrix.Decompose(convexMeshInfo.colliderInfo.collsionTransform.worldScale, convexMeshInfo.colliderInfo.collsionTransform.worldRotation, convexMeshInfo.colliderInfo.collsionTransform.worldPosition);
	//offset 
	if (posOffset != DirectX::SimpleMath::Vector3::Zero)
	{
		convexMeshInfo.colliderInfo.collsionTransform.worldMatrix._41 = 0.0f;
		convexMeshInfo.colliderInfo.collsionTransform.worldMatrix._42 = 0.0f;
		convexMeshInfo.colliderInfo.collsionTransform.worldMatrix._43 = 0.0f;
		posOffset = DirectX::SimpleMath::Vector3::Transform(posOffset, convexMeshInfo.colliderInfo.collsionTransform.worldMatrix);
		convexMeshInfo.colliderInfo.collsionTransform.worldPosition += posOffset;
		convexMeshInfo.colliderInfo.collsionTransform.worldMatrix._41 = convexMeshInfo.colliderInfo.collsionTransform.worldPosition.x;
		convexMeshInfo.colliderInfo.collsionTransform.worldMatrix._42 = convexMeshInfo.colliderInfo.collsionTransform.worldPosition.y;
		convexMeshInfo.colliderInfo.collsionTransform.worldMatrix._43 = convexMeshInfo.colliderInfo.collsionTransform.worldPosition.z;
	}
	auto model = obj->GetComponent<MeshRenderer>();
	auto modelVertices = model->m_Mesh->GetVertices();

	mesh->SetMeshInfoMation(convexMeshInfo);
}

void PhysicsManager::AddCollider(CharacterControllerComponent* controller)
{
	if (!controller) return;

	auto obj = controller->GetOwner();
	auto& transform = obj->m_transform;
	auto controllerInfo = controller->GetControllerInfo();
	auto movementInfo = controller->GetMovementInfo();
	auto posOffset = controller->GetPositionOffset();
	auto rotOffset = controller->GetRotationOffset();
	ColliderID gameObjectID = obj->GetInstanceID();
	std::cout << "PhysicsManager::AddCollider(CharacterController) - GameObject InstanceID: " << gameObjectID << std::endl;
	controllerInfo.id = gameObjectID;
	controllerInfo.layerNumber = obj->GetCollisionType();
	DirectX::SimpleMath::Vector3 position = transform.GetWorldPosition();
	controllerInfo.position = position + controller->GetPositionOffset();
	Physics->CreateCCT(controllerInfo, movementInfo);


	controller->SetControllerInfo(controllerInfo);
}

void PhysicsManager::AddCollider(TerrainColliderComponent* terrain)
{
	if (!terrain) return;

	auto object = terrain->GetOwner();
	TerrainColliderComponent* collider = object->GetComponent<TerrainColliderComponent>();
	Transform& transform = object->m_transform;
	auto terrainComponent = object->GetComponent<TerrainComponent>();

	HeightFieldColliderInfo heightFieldInfo;

	ColliderID gameObjectID = object->GetInstanceID();
	std::cout << "PhysicsManager::AddCollider(Terrain) - GameObject InstanceID: " << gameObjectID << std::endl;

	collider->SetColliderID(gameObjectID);
	heightFieldInfo.colliderInfo.id = gameObjectID;
	heightFieldInfo.colliderInfo.layerNumber = object->GetCollisionType();
	heightFieldInfo.colliderInfo.collsionTransform.localMatrix = transform.GetLocalMatrix();
	heightFieldInfo.colliderInfo.collsionTransform.worldMatrix = transform.GetWorldMatrix();

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

		auto& transform = colliderInfo.gameObject->m_transform;
		//colliderInfo.collider.
		auto rigidbody = colliderInfo.gameObject->GetComponent<RigidBodyComponent>();
		auto offset = colliderInfo.collider->GetPositionOffset();
		bool _isColliderEnabled = rigidbody->IsColliderEnabled();
		//todo : CCT,Controller,ragdoll,capsule,?섏쨷??deformeSuface
		if (colliderInfo.id == m_controllerTypeId)
		{
			//Benchmark bm;
			
			auto controller = colliderInfo.gameObject->GetComponent<CharacterControllerComponent>();
			CharacterControllerGetSetData data;
			DirectX::SimpleMath::Vector3 position = transform.GetWorldPosition();
			data.position = position+controller->GetPositionOffset();
			data.rotation = transform.GetWorldQuaternion();
			data.Scale = transform.GetWorldScale();

			auto controllerInfo = controller->GetControllerInfo();
			auto prevlayer = controllerInfo.layerNumber;
			auto currentLayer = static_cast<unsigned int>(colliderInfo.gameObject->GetCollisionType());
			
			if (prevlayer != currentLayer) {
				data.LayerNumber = currentLayer;
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
		else
		{
			//Benchmark bm1;
			RigidBodyGetSetData data;
			data.transform = transform.GetWorldMatrix();
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

			data.isDirty = rigidbody->IsRigidbodyDirty();
			rigidbody->DevelopOnlyDirtySet(false);

			if (offset != DirectX::SimpleMath::Vector3::Zero) 
			{
				data.transform._41 = 0.0f;
				data.transform._42 = 0.0f;
				data.transform._43 = 0.0f;

				auto pos = transform.GetWorldPosition();
				auto vecPos = DirectX::SimpleMath::Vector3(pos);
				offset = DirectX::SimpleMath::Vector3::Transform(offset, data.transform);
				data.transform._41 = vecPos.x + offset.x;
				data.transform._42 = vecPos.y + offset.y;
				data.transform._43 = vecPos.z + offset.z;

			}

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
	auto& Container = SceneManagers->GetActiveScene()->m_colliderContainer;
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
		auto& transform = ColliderInfo.gameObject->m_transform;
		auto offset = ColliderInfo.collider->GetPositionOffset();


		if (rigidbody->GetBodyType() != EBodyType::DYNAMIC)
		{
			
			continue;
		}

		
		if (ColliderInfo.id == m_controllerTypeId) {
			
			auto controller = ColliderInfo.gameObject->GetComponent<CharacterControllerComponent>();
			auto controll = Physics->GetCCTData(id);
			auto movement = Physics->GetMovementData(id);
			auto position = controll.position - controller->GetPositionOffset();

			controller->SetFalling(movement.isFall);
			rigidbody->SetLinearVelocity(movement.velocity);
			transform.SetPosition(position);
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

			auto matrix = data.transform;

			if (offset != DirectX::SimpleMath::Vector3::Zero) {

				DirectX::SimpleMath::Vector3 pos, scale;
				DirectX::SimpleMath::Quaternion rotation;
				matrix.Decompose(scale, rotation, pos);
				matrix._41 = 0.0f;
				matrix._42 = 0.0f;
				matrix._43 = 0.0f;

				offset = DirectX::SimpleMath::Vector3::Transform(offset, matrix);
				pos -= offset;

				matrix._41 = pos.x;
				matrix._42 = pos.y;
				matrix._43 = pos.z;
				transform.SetPosition(pos);

			}

			transform.SetAndDecomposeMatrix(matrix, true);

		}
	}
	//std::cout <<" PhysicsManager::GetPhysicData" << bm.GetElapsedTime() << std::endl;
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

		Physics->SetRigidBodyData(change.id, data);
	}
	m_pendingChanges.clear();
}