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
#include "MathematicsInterop.h"

class Scene;
void PhysicsManager::Initialize()
{
	// PhysicsManager �ʱ�ȭ
	m_bIsInitialized = Physics->Initialize();
	
	// �� �ε�, ��ε�, ���� �̺�Ʈ �ڵ鷯 ���
	m_OnSceneLoadHandle		= sceneLoadedEvent.AddRaw(this, &PhysicsManager::OnLoadScene);
	m_OnSceneUnloadHandle	= sceneUnloadedEvent.AddRaw(this, &PhysicsManager::OnUnloadScene);
	m_OnChangeSceneHandle	= SceneManagers->activeSceneChangedEvent.AddRaw(this, &PhysicsManager::ChangeScene);

	// ���� ���� �ݹ� �Լ� ����
	Physics->SetCallBackCollisionFunction([this](const CollisionData& data, ECollisionEventType type) {
		this->CallbackEvent(data, type);
	});
	
	//�⺻ ��ü �浹 ��Ʈ���� ����
	std::vector<std::vector<uint8_t>> collisionGrid;
	collisionGrid.resize(32);
	for (auto& row : collisionGrid) {
		row.resize(32, 1); // �⺻������ ��� �浹ü�� �浹 �����ϵ��� ����
	}
	SetCollisionMatrix(collisionGrid);

	LoadCollisionMatrix();
}
void PhysicsManager::Update(float fixedDeltaTime)
{
	if (!m_bIsInitialized) return;
	
	// �ݹ� �̺�Ʈ �ʱ�ȭ
	m_callbacks.clear();
	SetPhysicData();
	// ���� ������ ���� ���� ����
	//Benchmark bm;
	ApplyPendingChanges();
	// ���� ���� ������Ʈ
	Physics->Update(fixedDeltaTime);
	//std::cout << " Physics->Update" << bm.GetElapsedTime() << std::endl;
	
	// 1����: GetPhysicData()
	// ���� �ùķ��̼��� ����� ��� ���ӿ�����Ʈ�� Transform�� ���� ����ȭ�մϴ�.
	// �̷��� �ؾ� ������ ��� ��ü�� ���������� �ùٸ� �ֽ� ��ġ�� �ְ� �˴ϴ�.
	GetPhysicData();

	// 2����: ProcessCallback()
	// ��� ��ü�� ��ġ�� �ֽ� ���·� ����ȭ�Ǿ����Ƿ�, ���� �� ��ġ�� ��������
	// OnCollisionEnter, OnTriggerEnter ���� �̺�Ʈ ��ũ��Ʈ�� �����ϴ� ���� �����ϰ� ��Ȯ�մϴ�.
	// ���� �� ������ �ݴ밡 �Ǹ�, ��ũ��Ʈ�� '���� �������� ��ġ'�� �������� ������ �����ϴ� ������ �߻��� �� �ֽ��ϴ�.
	ProcessCallback();

	// 3����: ApplyPendingControllerPositionChanges()
	// �ش� �������� ��� ������ ��ȣ�ۿ�� �̺�Ʈ ó���� ������ �������ϴ�.
	// ���� ��� ������ ���� ���¿���, '���� ����'�� ���� Ư���� �����÷��� ȿ��(�����̵�)��
	// �� �������� �����Ͽ� ���� �������� �غ��ŵ�ϴ�.
	ApplyPendingControllerPositionChanges();
}
void PhysicsManager::Shutdown()
{
	// ���� ���� �� ����
	Physics->ChangeScene();
	//�����̳� ����
	auto& Container = SceneManagers->GetActiveScene()->m_colliderContainer;
	Container.clear();
	// ���� ���� ����
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
			//�ڽ��� �ݶ��̴��� �浹 �̰ų� �浹ü�� ���� ���� ��� -> error
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

			//// ��ǥ�� ��ȯ (�޼� -> ������)
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
	boxInfo.colliderInfo.collsionTransform.localMatrix = MathematicsInterop::ToSimpleMath(
		transform.GetLocalMatrix());
	boxInfo.colliderInfo.collsionTransform.worldMatrix = MathematicsInterop::ToSimpleMath(
		transform.GetWorldMatrix());
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
	auto& transform = obj->Transform_();
	auto type = sphere->GetColliderType();
	auto sphereInfo = sphere->GetSphereInfo();
	auto posOffset = sphere->GetPositionOffset();
	auto rotOffset = sphere->GetRotationOffset();

	unsigned int gameObjectID = obj->GetInstanceID();
	std::cout << "PhysicsManager::AddCollider(Sphere) - Entity InstanceID: " << gameObjectID << std::endl;
	sphereInfo.colliderInfo.id = gameObjectID;
	sphereInfo.colliderInfo.layerNumber = obj->GetCollisionType();
	sphereInfo.colliderInfo.collsionTransform.localMatrix = MathematicsInterop::ToSimpleMath(
		transform.GetLocalMatrix());
	sphereInfo.colliderInfo.collsionTransform.worldMatrix = MathematicsInterop::ToSimpleMath(
		transform.GetWorldMatrix());
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
	auto& transform = obj->Transform_();
	auto capsuleInfo = capsule->GetCapsuleInfo();
	auto posOffset = capsule->GetPositionOffset();
	auto rotOffset = capsule->GetRotationOffset();
	unsigned int gameObjectID = obj->GetInstanceID();
	std::cout << "PhysicsManager::AddCollider(Capsule) - Entity InstanceID: " << gameObjectID << std::endl;
	capsuleInfo.colliderInfo.id = gameObjectID;
	capsuleInfo.colliderInfo.layerNumber = obj->GetCollisionType();

	Mathf::Matrix worldMatrix_NoScale = MathematicsInterop::ToSimpleMath(
		transform.GetWorldMatrix_NoScale());
	Mathf::Quaternion pureWorldRot;
	Mathf::Vector3 pureWorldPos;
	Mathf::Vector3 scale;
	worldMatrix_NoScale.Decompose(scale, pureWorldRot, pureWorldPos);

	Mathf::Quaternion offsetRot = capsule->GetRotationOffset();
	Mathf::Vector3 offsetPos = capsule->GetPositionOffset();

	Mathf::Quaternion finalWorldRot = Mathf::Quaternion::Concatenate(offsetRot, pureWorldRot);
	Mathf::Vector3 finalWorldPos = pureWorldPos + Mathf::Vector3::Transform(offsetPos, pureWorldRot);

	// 3. `collsionTransform` ����ü�� �ùٸ� �����ͷ� ä��ϴ�.
	auto& collsionTransform = capsuleInfo.colliderInfo.collsionTransform;
	collsionTransform.worldPosition = finalWorldPos; // ���� ��ġ
	collsionTransform.worldRotation = finalWorldRot; // ���� ȸ��
	collsionTransform.worldScale = MathematicsInterop::ToSimpleMath(
		transform.GetWorldScale()); // �������� ���� ����
	
	// ���� ������ ����ó�� ä���ֽ��ϴ�.
	collsionTransform.localMatrix = MathematicsInterop::ToSimpleMath(
		transform.GetLocalMatrix());
	collsionTransform.localMatrix.Decompose(
		collsionTransform.localScale,
		collsionTransform.localRotation,
		collsionTransform.localPosition
	);
	
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
	convexMeshInfo.colliderInfo.collsionTransform.localMatrix = MathematicsInterop::ToSimpleMath(
		transform.GetLocalMatrix());
	convexMeshInfo.colliderInfo.collsionTransform.worldMatrix = MathematicsInterop::ToSimpleMath(
		transform.GetWorldMatrix());
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
	auto& transform = obj->Transform_();
	auto controllerInfo = controller->GetControllerInfo();
	auto movementInfo = controller->GetMovementInfo();
	auto posOffset = controller->GetPositionOffset();
	auto rotOffset = controller->GetRotationOffset();
	ColliderID gameObjectID = obj->GetInstanceID();
	std::cout << "PhysicsManager::AddCollider(CharacterController) - Entity InstanceID: " << gameObjectID << std::endl;
	controllerInfo.id = gameObjectID;
	controllerInfo.layerNumber = obj->GetCollisionType();
	DirectX::SimpleMath::Vector3 position = MathematicsInterop::ToSimpleMath(
		transform.GetWorldPosition());
	controllerInfo.position = position + controller->GetPositionOffset();
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
	heightFieldInfo.colliderInfo.collsionTransform.localMatrix = MathematicsInterop::ToSimpleMath(
		transform.GetLocalMatrix());
	heightFieldInfo.colliderInfo.collsionTransform.worldMatrix = MathematicsInterop::ToSimpleMath(
		transform.GetWorldMatrix());

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

		auto offset = colliderInfo.collider->GetPositionOffset();
		bool _isColliderEnabled = rigidbody->IsColliderEnabled();
		//todo : CCT,Controller,ragdoll,capsule,?�중??deformeSuface
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
			DirectX::SimpleMath::Vector3 position = MathematicsInterop::ToSimpleMath(
				transform.GetWorldPosition());
			data.position = position+controller->GetPositionOffset();
			data.rotation = MathematicsInterop::ToSimpleMath(
				transform.GetWorldQuaternion());
			data.Scale = MathematicsInterop::ToSimpleMath(
				transform.GetWorldScale());

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
			Mathf::Matrix worldMatrix_NoScale = MathematicsInterop::ToSimpleMath(
				transform.GetWorldMatrix_NoScale());
			Mathf::Quaternion pureWorldRot;
			Mathf::Vector3 pureWorldPos;
			Mathf::Vector3 scale;
			worldMatrix_NoScale.Decompose(scale, pureWorldRot, pureWorldPos);

			// 2. �ݶ��̴��� ���� �������� �����ɴϴ�.  
			auto offset = colliderInfo.collider->GetPositionOffset();
			auto rotOffset = colliderInfo.collider->GetRotationOffset();

			auto type = colliderInfo.collider->GetColliderType();

			// 3. �������� �����Ͽ� ���� ��ü�� '���� ���� Pose'�� ����մϴ�
			data.rotation = Mathf::Quaternion::Concatenate(rotOffset, pureWorldRot);
			data.position = pureWorldPos + DirectX::SimpleMath::Vector3::Transform(offset, pureWorldRot);
			// 4. ���� ������ ���� ���� �����ɴϴ�.  
			data.scale = MathematicsInterop::ToSimpleMath(transform.GetWorldScale());
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
		auto& transform = ColliderInfo.gameObject->Transform_();
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
			transform.SetPosition(MathematicsInterop::FromSimpleMath(position));
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

			DirectX::SimpleMath::Vector3 pos = data.position;
			DirectX::SimpleMath::Quaternion rotation = data.rotation;
			DirectX::SimpleMath::Vector3 scale = data.scale;

			auto posOffset = ColliderInfo.collider->GetPositionOffset();
			auto rotOffset = ColliderInfo.collider->GetRotationOffset();

			// 2. ȸ�� ������: ���� ���忡�� ���� ȸ����(data.rotation)���� �ݶ��̴��� ȸ�� �������� �����մϴ�.
			Mathf::Quaternion pureWorldRot;
			rotOffset.Inverse(pureWorldRot);
			pureWorldRot = Mathf::Quaternion::Concatenate(pureWorldRot, data.rotation);

			// 3. ��ġ ������: ������ ����� '���� ���� ȸ��'�� ����Ͽ� ��ġ �������� ������ �����մϴ�.
			Mathf::Vector3 pureWorldPos = data.position - DirectX::SimpleMath::Vector3::Transform(posOffset,
				pureWorldRot);

			DirectX::SimpleMath::Matrix matrix = DirectX::SimpleMath::Matrix::CreateScale(scale)
				* DirectX::SimpleMath::Matrix::CreateFromQuaternion(pureWorldRot)
				* DirectX::SimpleMath::Matrix::CreateTranslation(pureWorldPos);

			transform.SetAndDecomposeMatrix(
				MathematicsInterop::FromSimpleMath(matrix), true);
		}
	}
	//std::cout <<" PhysicsManager::GetPhysicData" << bm.GetElapsedTime() << std::endl;
}

// ����� CCT ��ġ ���� ��û�� �ϰ� ó���ϴ� �Լ�
void PhysicsManager::ApplyPendingControllerPositionChanges()
{
	if (!Physics || m_pendingControllerPositions.empty()) return;

	// ID�� ���� ���ӿ�����Ʈ�� ã�� ���� �ݶ��̴� �����̳ʿ� �����մϴ�.
	auto& colliderContainer = SceneManagers->GetActiveScene()->m_colliderContainer;

	for (const auto& change : m_pendingControllerPositions)
	{
		// 1. PhysX ���� ������ ��Ʈ�ѷ� ��ġ�� ������ �����մϴ�.
		Physics->SetControllerPosition(change.id, change.position);

		// 2. �ش� ID�� ���� ���ӿ�����Ʈ�� ã���ϴ�.
		auto it = colliderContainer.find(change.id);
		if (it != colliderContainer.end())
		{
			auto& colliderInfo = it->second;
			if (colliderInfo.gameObject)
			{
				// 3. (�ٽ�) ���ӿ�����Ʈ�� Transform ������Ʈ ��ġ�� ��� ����ȭ�մϴ�.
				// CCT�� ��� �Ϲ������� �������� ���ų�, �ִ��� �����̵��� �������� �������� �ϴ� ���� ��Ȯ�մϴ�.
				// ���� �������� �����ؾ� �Ѵٸ�, change.position���� �������� ���� ���� SetWorldPosition�� �Ѱ��־�� �մϴ�.
				colliderInfo.gameObject->Transform_().SetWorldPosition(
					MathematicsInterop::FromSimpleMath(change.position));
			}
		}
	}

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
				m_collisionMatrix[i][j] = true; // �⺻�� ����
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
