#include "PhysicsManager.h"
#include "SceneManager.h"
#include "Scene.h"
#include "GameObject.h"
#include "Transform.h"
#include "RigidBodyComponent.h"
#include "BoxColliderComponent.h"

class Scene;
void PhysicsManager::Initialize()
{
	//물리엔진 초기화
	m_bIsInitialized = Physics->Initialize();
}
void PhysicsManager::Update(float fixedDeltaTime)
{
	if (m_bPlay) {
		//물리씬에 데이터 셋
		SetPhysicData();

		//물리씬 업데이트
		Physics->Update(fixedDeltaTime);

		//물리씬에 데이터 가져오기
		GetPhysicData();
	}
}
void PhysicsManager::Shutdown()
{
	//모든 객체 제거
	Physics->ChangeScene();
	//컨테이너 제거
	m_colliderContainer.clear();
	//물리 씬 해제
	Physics->UnInitialize();
}
void PhysicsManager::ChangeScene()
{
	
	Physics->ChangeScene();
}
void PhysicsManager::OnLoadScene()
{
	//todo : 물리씬 초기화
	//CleanUp();

	//todo : 현제의 게임씬을 찾아오기
	auto scene = SceneManagers->GetActiveScene();
	
	for (auto& obj : scene->m_SceneObjects)
	{
		auto rigid = obj->GetComponent<RigidBodyComponent>();
		if (!rigid)
		{
			return;
		}

		auto transform = obj->m_transform;
		auto bodyType = rigid->GetBodyType();

		auto box = obj->GetComponent<BoxColliderComponent>();

		//박스 콜라이더라면
		if (box) {
			unsigned int colliderID = ++m_lastColliderID;
			auto boxInfo = box->m_Info;
			auto tranformOffset = box->GetPositionOffset();
			auto rotationOffset = box->GetRotationOffset();

			boxInfo.colliderInfo.id = colliderID;
			boxInfo.colliderInfo.layerNumber = 0;
			boxInfo.colliderInfo.collsionTransform.localMatrix = transform.GetLocalMatrix();
			boxInfo.colliderInfo.collsionTransform.worldMatrix = transform.GetWorldMatrix();
			boxInfo.colliderInfo.collsionTransform.localMatrix.Decompose(boxInfo.colliderInfo.collsionTransform.localScale, boxInfo.colliderInfo.collsionTransform.localRotation, boxInfo.colliderInfo.collsionTransform.localPosition);
			boxInfo.colliderInfo.collsionTransform.worldMatrix.Decompose(boxInfo.colliderInfo.collsionTransform.worldScale, boxInfo.colliderInfo.collsionTransform.worldRotation, boxInfo.colliderInfo.collsionTransform.worldPosition);
			
			//offset 계산
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
			
			if (bodyType == EBodyType::STATIC) {
				//pxScene에 엑터 추가
				Physics->CreateStaticBody(boxInfo, EColliderType::COLLISION);
				//콜라이더 정보 저장
				m_colliderContainer.insert({ colliderID, {
					m_boxTypeId,
					box,
					box->GetOwner(),
					box,
					false
					} });
			}
			else {
				bool isKinematic = bodyType == EBodyType::KINEMATIC;
				Physics->CreateDynamicBody(boxInfo, EColliderType::COLLISION, isKinematic);
				//콜라이더 정보 저장
				//m_colliderContainer.insert({ colliderID, boxInfo });
			}

		}
		//todo :sphere 콜라이더라면
		//auto sphere = obj->GetComponent<SphereColliderComponent>();
		//if (sphere){
			//todo : sphere 콜라이더 생성
		//}
		//todo : capsule 콜라이더라면
		//auto capsule = obj->GetComponent<CapsuleColliderComponent>();
		//if (capsule) {
			//todo : capsule 콜라이더 생성
		//}
		//todo : convex 콜라이더라면
		//auto convex = obj->GetComponent<ConvexMeshColliderComponent>();
		//if (convex) {
			//todo : convex 콜라이더 생성
		// }
		//todo : controller Component라면
		//auto controller = obj->GetComponent<CharacterControllerComponent>();
		//if (controller) {
			//todo : controller 콜라이더 생성
		//}
		//관절 정보가 있는 ragdoll이라면
		//auto ragdoll = obj->GetComponent<RagdollComponent>();
		//if (ragdoll) {
			//todo : ragdoll 콜라이더 생성
		//}
		//todo : heightfield 콜라이더라면
		//auto heightfield = obj->GetComponent<HeightFieldColliderComponent>();
		//if (heightfield) {
			//todo : heightfield 콜라이더 생성
		// }


	};
	
}

void PhysicsManager::OnUnloadScene()
{

	for (auto& [id, info] : m_colliderContainer) {
		info.bIsDestroyed = true;
	}

	Physics->ChangeScene();
	Physics->Update(1.0f);
	Physics->FinalUpdate();
	m_callbacks.clear();
}

void PhysicsManager::ProcessCallback()
{
	for (auto& [data, type] : m_callbacks) {

		auto lhs = m_colliderContainer.find(data.thisId);
		auto rhs = m_colliderContainer.find(data.otherId);

		if (data.thisId == data.otherId|| lhs==m_colliderContainer.end()|| rhs == m_colliderContainer.end())
		{
			//자신의 콜라이더와 충돌 이거나 충돌체가 등록이 되어 있지 않은 경우 -> error
			Debug->LogError("Collision Callback Error lfs :" + std::to_string(data.thisId) + " ,rhs : " + std::to_string(data.otherId));
			continue;
		}

		auto lhsObj = lhs->second.gameObject;
		auto rhsObj = rhs->second.gameObject;

		Collision collision{ lhsObj,rhsObj,data.contactPoints };

		switch (type)
		{
		case ECollisionEventType::ENTER_OVERLAP:
			SceneManagers->GetActiveScene()->OnCollisionEnter(collision);
			break;
		case ECollisionEventType::ON_OVERLAP:
			break;
		case ECollisionEventType::END_OVERLAP:
			break;
		case ECollisionEventType::ENTER_COLLISION:
			break;
		case ECollisionEventType::ON_COLLISION:
			break;
		case ECollisionEventType::END_COLLISION:
			break;
		default:
			break;
		}

	}
}


void PhysicsManager::DrawDebugInfo()
{
	//collider 정보를 수집하여 render에서 wireframe로 그려줄 수 있도록 한다.
	//DebugRender debug;

	//물리씬에 있는 객체를 순회하며 콜리전 정보를 가져온다.

	//DebugData dd;
	//// 컴포넌트별 데이터 수집
	//for (auto* rb : m_rigidBodies)
	//	rb->FillDebugData(dd);

	//// 매니저가 처리하는 Raycast 디버그 추가
	//for (auto& rq : m_raycastSystem->GetRequests())
	//{
	//	if (rq.hit)
	//	{
	//		dd.lines.push_back({ rq.origin, rq.origin + rq.direction * rq.distance, {1,1,0,1} });
	//		dd.points.push_back({ rq.hitPosition, {1,0,0,1} });
	//	}
	//}

	//// IDebugRenderer에 제출
	//for (auto& l : dd.lines)   debug->DrawLine(l);
	//for (auto& s : dd.spheres) debug->DrawSphere(s);
	//for (auto& p : dd.points)  debug->DrawPoint(p);
	//for (auto& b : dd.boxes)   debug->DrawBox(b);
	//for (auto& c : dd.capsules) debug->DrawCapsule(c);
	//for (auto& m : dd.convexes) debug->DrawConvexMesh(c);
	//debug->Flush();
	
}

void PhysicsManager::AddCollider(GameObject* object)
{
	if (!object->HasComponent<RigidBodyComponent>()) {
		return;
	}

	auto rigid = object->GetComponent<RigidBodyComponent>();
	auto transform = object->m_transform;
	auto bodyType = rigid->GetBodyType();

	if (object->HasComponent<BoxColliderComponent>())
	{
		auto box = object->GetComponent<BoxColliderComponent>();
		auto type = box->GetColliderType();
		auto boxInfo = box->m_Info;
		auto tranformOffset = box->GetOffsetTransform();
		auto rotationOffset = box->GetOffsetRotation();

		
		unsigned int colliderId = ++m_lastColliderID;
		boxInfo.colliderInfo.id = colliderId;
		boxInfo.colliderInfo.layerNumber = 0;
		boxInfo.colliderInfo.collsionTransform.localMatrix = transform.GetLocalMatrix();
		boxInfo.colliderInfo.collsionTransform.worldMatrix = transform.GetWorldMatrix();
		boxInfo.colliderInfo.collsionTransform.localMatrix.Decompose(boxInfo.colliderInfo.collsionTransform.localScale, boxInfo.colliderInfo.collsionTransform.localRotation, boxInfo.colliderInfo.collsionTransform.localPosition);
		boxInfo.colliderInfo.collsionTransform.worldMatrix.Decompose(boxInfo.colliderInfo.collsionTransform.worldScale, boxInfo.colliderInfo.collsionTransform.worldRotation, boxInfo.colliderInfo.collsionTransform.worldPosition);

		//offset 계산
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

		if (bodyType == EBodyType::STATIC) {
			//pxScene에 엑터 추가
			Physics->CreateStaticBody(boxInfo, EColliderType::COLLISION);
			//콜라이더 정보 저장
			m_colliderContainer.insert({ colliderId, {
				m_boxTypeId,
				box,
				box->GetOwner(),
				box,
				false
				} });
		}
		else {
			bool isKinematic = bodyType == EBodyType::KINEMATIC;
			Physics->CreateDynamicBody(boxInfo, EColliderType::COLLISION, isKinematic);
			//콜라이더 정보 저장
			m_colliderContainer.insert({ colliderId, {
			m_boxTypeId,
			box,
			box->GetOwner(),
			box,
			false
			} });
		}
			
	}


}

void PhysicsManager::RemoveCollider(GameObject* object)
{
	if (!object->HasComponent<RigidBodyComponent>()) {
		return;
	}

	if (object->HasComponent<BoxColliderComponent>()) {
		auto boxCollider = object->GetComponent<BoxColliderComponent>();
		auto id = boxCollider->m_Info.colliderInfo.id;

		m_colliderContainer.at(id).bIsDestroyed = true;
	}

}

void PhysicsManager::CallbackEvent(CollisionData data, ECollisionEventType type)
{
	m_callbacks.push_back({ data,type });
}

void PhysicsManager::SetPhysicData()
{
	for (auto& [id, colliderInfo] : m_colliderContainer) {
	
		if (colliderInfo.bIsDestroyed)
		{
			continue;
		}

		auto transform = colliderInfo.gameObject->m_transform;
		auto rigidbody = colliderInfo.gameObject->GetComponent<RigidBodyComponent>();
		auto offset = colliderInfo.collider->GetPositionOffset();

		//todo : CCT,Controller,ragdoll,capsule,차후 deformeSuface
		
		{
			//기본도형
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

			if (offset != DirectX::SimpleMath::Vector3::Zero) {
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
			
			Physics->SetRigidBodyData(id, data);
		}
	


	}
}

//PxScene --> GameScene
void PhysicsManager::GetPhysicData()
{
	for (auto& [id, ColliderInfo] : m_colliderContainer) {
		
		//삭제 예정된 콜라이더는 건너뛴다
		if (ColliderInfo.bIsDestroyed)
		{
			continue;
		}

		auto rigidbody = ColliderInfo.gameObject->GetComponent<RigidBodyComponent>();
		auto transform = ColliderInfo.gameObject->m_transform;
		auto offset = ColliderInfo.collider->GetPositionOffset();

		if (rigidbody->GetBodyType() != EBodyType::DYNAMIC) {
			continue;
		}

		//todo : CCT,Controller,ragdoll,capsule,차후 deformeSuface

		{
			//기본 도형
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

			}

			transform.SetAndDecomposeMatrix(matrix);
		}
	}
}
