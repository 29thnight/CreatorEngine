#include "PhysicsManager.h"
#include "SceneManager.h"
#include "Scene.h"
#include "RigidBodyComponent.h"
#include "BoxColliderComponent.h"

class Scene;
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
