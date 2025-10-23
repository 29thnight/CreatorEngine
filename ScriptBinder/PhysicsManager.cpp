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
#include "TagManager.h"
#include "Terrain.h"
#include "TerrainCollider.h"
#include "CharacterControllerComponent.h"
#include "MetaYaml.h"

class Scene;
void PhysicsManager::Initialize()
{
	// PhysicsManager ʱȭ
	m_bIsInitialized = Physics->Initialize();
	
	//  ε, ε,  ̺Ʈ ڵ鷯 
	m_OnSceneLoadHandle		= sceneLoadedEvent.AddRaw(this, &PhysicsManager::OnLoadScene);
	m_OnSceneUnloadHandle	= sceneUnloadedEvent.AddRaw(this, &PhysicsManager::OnUnloadScene);
	m_OnChangeSceneHandle	= SceneManagers->activeSceneChangedEvent.AddRaw(this, &PhysicsManager::ChangeScene);

	//   ݹ Լ 
	Physics->SetCallBackCollisionFunction([this](const CollisionData& data, ECollisionEventType type) {
		this->CallbackEvent(data, type);
	});
	
	//⺻ ü 浹 Ʈ 
	std::vector<std::vector<uint8_t>> collisionGrid;
	collisionGrid.resize(32);
	for (auto& row : collisionGrid) {
		row.resize(32, 1); // ⺻  浹ü 浹 ϵ 
	}
	SetCollisionMatrix(collisionGrid);

	LoadCollisionMatrix();
}
void PhysicsManager::Update(float fixedDeltaTime)
{
	if (!m_bIsInitialized) return;
	
	// ݹ ̺Ʈ ʱȭ
	m_callbacks.clear();
	SetPhysicData();
	//     
	//Benchmark bm;
	ApplyPendingChanges();
	//   Ʈ
	Physics->Update(fixedDeltaTime);
	//std::cout << " Physics->Update" << bm.GetElapsedTime() << std::endl;
	
	// 1: GetPhysicData()
	//  ùķ̼   ӿƮ Transform  ȭմϴ.
	// ̷ ؾ   ü  ùٸ ֽ ġ ְ ˴ϴ.
	GetPhysicData();

	// 2: ProcessCallback()
	//  ü ġ ֽ · ȭǾǷ,   ġ 
	// OnCollisionEnter, OnTriggerEnter  ̺Ʈ ũƮ ϴ  ϰ Ȯմϴ.
	//    ݴ밡 Ǹ, ũƮ '  ġ'   ϴ  ߻  ֽϴ.
	ProcessCallback();

	// 3: ApplyPendingControllerPositionChanges()
	// ش    ȣۿ ̺Ʈ ó  ϴ.
	//     ¿, ' '  Ư ÷ ȿ(̵)
	//   Ͽ   غŵϴ.
	ApplyPendingControllerPositionChanges();
}
void PhysicsManager::Shutdown()
{
	//    
	Physics->ChangeScene();
	//̳ 
	auto& Container = SceneManagers->GetActiveScene()->m_colliderContainer;
	Container.clear();
	//   
	Physics->UnInitialize();

	SaveCollisionMatrix();

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
			//ڽ ݶ̴ 浹 ̰ų 浹ü    -> error
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

int PhysicsManager::BoxSweep(const SweepInput& in, const DirectX::SimpleMath::Vector3& boxExtent, std::vector<HitResult>& out_hits) {
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

			//// ǥ ȯ (޼ -> )
			//ConvertVectorPxToDx(hit.hitPoint, finalHit.point);
			// ConvertVectorPxToDx(hit.hitNormal, finalHit.normal);
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

int PhysicsManager::BoxOverlap(const OverlapInput& in, const DirectX::SimpleMath::Vector3& boxExtent, std::vector<HitResult>& out_hits){
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

		auto& transform = colliderInfo.gameObject->m_transform;
		//colliderInfo.collider.
		auto rigidbody = colliderInfo.gameObject->GetComponent<RigidBodyComponent>();
		auto offset = colliderInfo.collider->GetPositionOffset();
		bool _isColliderEnabled = rigidbody->IsColliderEnabled();
		//todo : CCT,Controller,ragdoll,capsule,?중??deformeSuface
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
			DirectX::SimpleMath::Vector3 position = transform.GetWorldPosition();
			data.position = position+controller->GetPositionOffset();
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

			data.LayerNumber = static_cast<unsigned int>(colliderInfo.gameObject->GetCollisionType());

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

//  CCT ġ  û ϰ óϴ Լ
void PhysicsManager::ApplyPendingControllerPositionChanges()
{
	if (!Physics || m_pendingControllerPositions.empty()) return;

	// ID  ӿƮ ã  ݶ̴ ̳ʿ մϴ.
	auto& colliderContainer = SceneManagers->GetActiveScene()->m_colliderContainer;

	for (const auto& change : m_pendingControllerPositions)
	{
		// 1. PhysX   Ʈѷ ġ  մϴ.
		Physics->SetControllerPosition(change.id, change.position);

		// 2. ش ID  ӿƮ ãϴ.
		auto it = colliderContainer.find(change.id);
		if (it != colliderContainer.end())
		{
			auto& colliderInfo = it->second;
			if (colliderInfo.gameObject)
			{
				// 3. (ٽ) ӿƮ Transform Ʈ ġ  ȭմϴ.
				// CCT  Ϲ  ų, ִ ̵   ϴ  Ȯմϴ.
				//   ؾ Ѵٸ, change.position    SetWorldPosition Ѱ־ մϴ.
				colliderInfo.gameObject->m_transform.SetWorldPosition(change.position);
			}
		}
	}

	m_pendingControllerPositions.clear();
}


void PhysicsManager::SaveCollisionMatrix()
{
	file::path matrixSettingsPath = PathFinder::ProjectSettingPath("CollisionMatrix.asset");

	std::ofstream settingsFile(matrixSettingsPath);
	MetaYml::Node matrixNode;
	constexpr int MAX_LAYER_SIZE = 32;
	std::vector<std::string> layerNames = TagManagers->GetLayers();

	for(int i = 0; i < layerNames.size(); ++i)
	{
		auto vec = m_collisionMatrix[i];
		for (int j = 0; j < layerNames.size(); ++j)
		{
			matrixNode[i][j] = (bool)vec[j];
		}
	}

	settingsFile << matrixNode;

	settingsFile.close();
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
				m_collisionMatrix[i][j] = true; // ⺻ 
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

void PhysicsManager::SetControllerPosition(UINT id, const DirectX::SimpleMath::Vector3& pos)
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