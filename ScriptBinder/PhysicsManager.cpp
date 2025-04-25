#include "PhysicsManager.h"
#include "SceneManager.h"
#include "Scene.h"
#include "GameObject.h"
#include "RigidBodyComponent.h"
#include "BoxColliderComponent.h"

class Scene;
void PhysicsManager::Initialize()
{
	//�������� �ʱ�ȭ
	m_bIsInitialized = Physics->Initialize();
}
void PhysicsManager::Update(float fixedDeltaTime)
{
	if (m_bPlay) {
		//�������� ������ ��
		SetPhysicData();

		//������ ������Ʈ
		Physics->Update(fixedDeltaTime);

		//�������� ������ ��������
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
	//todo : ������ �ʱ�ȭ
	//CleanUp();

	//todo : ������ ���Ӿ��� ã�ƿ���
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

		//�ڽ� �ݶ��̴����
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
			
			//offset ���
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
				//pxScene�� ���� �߰�
				Physics->CreateStaticBody(boxInfo, EColliderType::COLLISION);
				//�ݶ��̴� ���� ����
				//m_colliderContainer.insert({ colliderID, boxInfo });
			}
			else {
				bool isKinematic = bodyType == EBodyType::KINEMATIC;
				Physics->CreateDynamicBody(boxInfo, EColliderType::COLLISION, isKinematic);
				//�ݶ��̴� ���� ����
				//m_colliderContainer.insert({ colliderID, boxInfo });
			}

		}
		//todo :sphere �ݶ��̴����
		//auto sphere = obj->GetComponent<SphereColliderComponent>();
		//if (sphere){
			//todo : sphere �ݶ��̴� ����
		//}
		//todo : capsule �ݶ��̴����
		//auto capsule = obj->GetComponent<CapsuleColliderComponent>();
		//if (capsule) {
			//todo : capsule �ݶ��̴� ����
		//}
		//todo : convex �ݶ��̴����
		//auto convex = obj->GetComponent<ConvexMeshColliderComponent>();
		//if (convex) {
			//todo : convex �ݶ��̴� ����
		// }
		//todo : controller Component���
		//auto controller = obj->GetComponent<CharacterControllerComponent>();
		//if (controller) {
			//todo : controller �ݶ��̴� ����
		//}
		//���� ������ �ִ� ragdoll�̶��
		//auto ragdoll = obj->GetComponent<RagdollComponent>();
		//if (ragdoll) {
			//todo : ragdoll �ݶ��̴� ����
		//}
		//todo : heightfield �ݶ��̴����
		//auto heightfield = obj->GetComponent<HeightFieldColliderComponent>();
		//if (heightfield) {
			//todo : heightfield �ݶ��̴� ����
		// }


	};
	
}

void PhysicsManager::OnUnloadScene()
{
}


void PhysicsManager::DrawDebugInfo()
{
	//collider ������ �����Ͽ� render���� wireframe�� �׷��� �� �ֵ��� �Ѵ�.
	//DebugRender debug;

	//�������� �ִ� ��ü�� ��ȸ�ϸ� �ݸ��� ������ �����´�.

	//DebugData dd;
	//// ������Ʈ�� ������ ����
	//for (auto* rb : m_rigidBodies)
	//	rb->FillDebugData(dd);

	//// �Ŵ����� ó���ϴ� Raycast ����� �߰�
	//for (auto& rq : m_raycastSystem->GetRequests())
	//{
	//	if (rq.hit)
	//	{
	//		dd.lines.push_back({ rq.origin, rq.origin + rq.direction * rq.distance, {1,1,0,1} });
	//		dd.points.push_back({ rq.hitPosition, {1,0,0,1} });
	//	}
	//}

	//// IDebugRenderer�� ����
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
