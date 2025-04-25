#include "PhysicsManager.h"
#include "SceneManager.h"
#include "Scene.h"
#include "GameObject.h"
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

}
void PhysicsManager::ChangeScene()
{
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
			auto tranformOffset = box->GetOffsetTransform();
			auto rotationOffset = box->GetOffsetRotation();

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
				//m_colliderContainer.insert({ colliderID, boxInfo });
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

		//if (colliderInfo.id == )
		//{

		//}
	


	}
}

void PhysicsManager::GetPhysicData()
{
}
