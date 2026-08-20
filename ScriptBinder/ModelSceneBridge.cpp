// Model/ModelLoader의 씬 인스턴스화 구현 (PHASE 4-2 C5).
//
// 모델 파싱(assimp -> Mesh/Material/Skeleton)은 렌더/리소스 도메인이라
// RenderEngine에 남고, 파싱 결과를 씬에 세우는 일(GameObject 계층 생성,
// MeshRenderer/Animator/RigidBody/MeshCollider 부착)은 게임플레이 도메인이라
// 이 파일이 맡는다.
#include "Model.h"
#include "ModelLoader.h"
#include "Scene.h"
#include "GameObject.h"
#include "SceneManager.h"
#include "Animator.h"
#include "MeshRenderer.h"
#include "RigidBodyComponent.h"
#include "MeshCollider.h"
#include "Skeleton.h"
#include "BoneComponent.h"

Model* Model::LoadModelToScene(Model* model, Scene& Scene)
{
    if (nullptr == model)
    {
        return nullptr;
    }

    ModelLoader loader = ModelLoader(model, &Scene);
    file::path path_ = model->path;

	loader.GenerateSceneObjectHierarchy(model->m_nodes[0], true, 0);
	if (model->m_hasBones)
	{
		loader.GenerateSkeletonToSceneObjectHierarchy(model->m_nodes[0], model->m_Skeleton->m_rootBone, true, 0);
	}

	WorkerPools->NotifyAllAndWait();

	return model;
}

Entity* Model::LoadModelToSceneObj(Model* model, Scene& Scene)
{
	if (nullptr == model)
	{
		return nullptr;
	}

    ModelLoader loader = ModelLoader(model, &Scene);
    file::path path_ = model->path;

	auto rootObj = loader.GenerateSceneObjectHierarchyObj(model->m_nodes[0], true, 0);
	if (model->m_hasBones)
	{
		rootObj = loader.GenerateSkeletonToSceneObjectHierarchyObj(model->m_nodes[0], model->m_Skeleton->m_rootBone, true, 0);
	}

	WorkerPools->NotifyAllAndWait();

	return rootObj;
}
void ModelLoader::GenerateSceneObjectHierarchy(ModelNode* node, bool isRoot, int parentIndex)
{
	static int modelSeparator = 0;
	int nextIndex = parentIndex;

	if (true == isRoot)
	{
		auto rootObject = m_scene->CreateGameObject(m_model->name, GameObjectType::Mesh, nextIndex);
		m_gameObjects.push_back(rootObject);
		nextIndex = rootObject->m_index;
		m_modelRootIndex = rootObject->m_index;

		if (m_model->m_hasBones)
		{
			m_animator = rootObject->AddComponent<Animator>();
			m_animator->SetEnabled(true);
			m_animator->m_Motion = m_model->m_animator->m_Motion;
			m_animator->m_Skeleton = m_model->m_Skeleton;
			m_isSkinnedMesh = true;
		}
		else
		{
			m_isSkinnedMesh = false;
		}

        if (1 == node->m_numMeshes && 0 == node->m_numChildren)
        {
            uint32 meshId = node->m_meshes[0];
            Mesh* mesh = m_model->m_Meshes[meshId].get();
            Material* material = m_model->m_Materials[mesh->m_materialIndex].get();
            MeshRenderer* meshRenderer = rootObject->AddComponent<MeshRenderer>();

			if (m_model->m_isMakeMeshCollider)
			{
				RigidBodyComponent* rigidbody = rootObject->AddComponent<RigidBodyComponent>();
				MeshColliderComponent* convexMesh = rootObject->AddComponent<MeshColliderComponent>();
				convexMesh->SetDensity(0);
				convexMesh->SetDynamicFriction(0);
				convexMesh->SetStaticFriction(0);
				convexMesh->SetRestitution(0);
			}

            meshRenderer->m_Mesh = m_model->m_Meshes[meshId];
            meshRenderer->m_Material = m_model->m_Materials[mesh->m_materialIndex];
			meshRenderer->m_isSkinnedMesh = m_isSkinnedMesh;
			rootObject->Transform_().SetLocalMatrix(node->m_transform);
			nextIndex = rootObject->m_index;
			
			return;
		}
	}

	for (uint32 i = 0; i < node->m_numMeshes; ++i)
	{
		std::shared_ptr<Entity> object = m_scene->CreateGameObject(node->m_name, GameObjectType::Mesh, nextIndex);

		uint32 meshId			= node->m_meshes[i];
		auto mesh				= m_model->m_Meshes[meshId];
		auto material			= m_model->m_Materials[mesh->m_materialIndex];
                
		Mathf::Matrix transform = node->m_transform;
		Model* model = m_model;

		// 컴포넌트 부착은 호출 스레드에서 한다.
		//
		// 예전에는 이 블록을 WorkerPools에 던졌다. 그런데 AddComponent는
		// Scene::RegisterComponent를 거쳐 씬 전역의 생명주기 벡터
		// (m_pendingAwake·m_updateList 등)에 push_back 한다 — 그 벡터들에는
		// 잠금이 없다. 메시 노드가 여럿인 모델을 씬에 놓으면 노드 수만큼의
		// 워커가 같은 벡터를 동시에 밀어 재할당과 겹치고, 힙이 깨진다
		// (스폰자 25노드에서 0xC0000374로 재현했다. Debug CRT에서는 어설션으로 뜬다).
		//
		// 던져서 얻는 것도 없다 — 이 안에 무거운 일이 없다. 컴포넌트를 달고
		// 필드를 채울 뿐이고, 바로 위의 CreateGameObject도 이미 이 스레드다.
		MeshRenderer* meshRenderer = object->AddComponent<MeshRenderer>();

		if (model->m_isMakeMeshCollider)
		{
			RigidBodyComponent* rigidbody = object->AddComponent<RigidBodyComponent>();
			MeshColliderComponent* convexMesh = object->AddComponent<MeshColliderComponent>();
			convexMesh->SetDensity(0);
			convexMesh->SetDynamicFriction(0);
			convexMesh->SetStaticFriction(0);
			convexMesh->SetRestitution(0);
		}

		meshRenderer->m_Mesh = mesh;
		meshRenderer->m_Material = material;
		meshRenderer->m_isSkinnedMesh = m_isSkinnedMesh;
		object->Transform_().SetLocalMatrix(transform);

		nextIndex = object->m_index;
		//m_gameObjects.push_back(object);
	}

	if (false == isRoot && 0 == node->m_numMeshes)
	{
		std::shared_ptr<Entity> object = m_scene->CreateGameObject(node->m_name, GameObjectType::Mesh, nextIndex);
		m_gameObjects.push_back(object);
		object->Transform_().SetLocalMatrix(node->m_transform);
		nextIndex = object->m_index;
	}

	for (uint32 i = 0; i < node->m_numChildren; ++i)
	{
		GenerateSceneObjectHierarchy(m_model->m_nodes[node->m_childrenIndex[i]], false, nextIndex);
	}
}

void ModelLoader::GenerateSkeletonToSceneObjectHierarchy(ModelNode* node, Bone* bone, bool isRoot, int parentIndex)
{
	int nextIndex = parentIndex;
	static std::shared_ptr<Entity> rootObject{};
	if (true == isRoot)
	{
		rootObject = m_scene->GetGameObject(m_modelRootIndex);
		// E1(슬롯맵)의 GetGameObject 루트 폴백 제거 후속 배선: m_modelRootIndex가
		// 무효화됐으면(세대 불일치 등) nullptr이 돌아온다 — 예전엔 루트 오브젝트로
		// 조용히 대체됐지만 지금은 명시적으로 막아야 한다.
		if (!rootObject)
		{
			Debug->LogError("ModelLoader::GenerateSkeletonToSceneObjectHierarchy: root Entity not found for index " + std::to_string(m_modelRootIndex));
			return;
		}
		nextIndex = rootObject->m_index;
	}
	else
	{
		std::shared_ptr<Entity> boneObject{};
		auto it = std::find_if(m_gameObjects.begin(), m_gameObjects.end(), [bone](std::shared_ptr<Entity> shPtr)
		{
			std::string name = shPtr->RemoveSuffixNumberTag();
			return name == bone->m_name;
		});

		boneObject = (*it);
		if (nullptr == boneObject)
		{
			boneObject = m_scene->CreateGameObject(bone->m_name, GameObjectType::Bone, nextIndex);
			//m_gameObjects.push_back(boneObject);
		}
		// E7-b/E7-c(트랙 E): Bone 정체성의 정본인 마커를 붙인다. 이미
		// 붙어 있으면(재방문 경로) AddComponent<T>()가 조용히 nullptr만
		// 돌려주고 끝난다 — 중복 경고 없음(GameObject.inl 템플릿 오버로드는
		// Meta::Type 오버로드와 달리 경고를 찍지 않는다).
		boneObject->AddComponent<BoneComponent>();
		nextIndex = boneObject->m_index;
		boneObject->SetRootIndex(m_modelRootIndex);
	}

	for (uint32 i = 0; i < bone->m_children.size(); ++i)
	{
		GenerateSkeletonToSceneObjectHierarchy(node, bone->m_children[i], false, nextIndex);
	}
}

Entity* ModelLoader::GenerateSceneObjectHierarchyObj(ModelNode* node, bool isRoot, int parentIndex)
{
	static int modelSeparator = 0;
	int nextIndex = parentIndex;
	std::shared_ptr<Entity> rootObject;

	if (true == isRoot)
	{
		rootObject = m_scene->CreateGameObject(m_model->name, GameObjectType::Mesh, nextIndex);
		nextIndex = rootObject->m_index;
		m_modelRootIndex = rootObject->m_index;

		if (m_model->m_hasBones)
		{
			m_animator = rootObject->AddComponent<Animator>();
			m_animator->m_Motion = m_model->m_animator->m_Motion;
			m_animator->m_Skeleton = m_model->m_Skeleton;
			m_isSkinnedMesh = true;
		}
		else
		{
			m_isSkinnedMesh = false;
		}

		if (1 == node->m_numMeshes && 0 == node->m_numChildren)
		{
			uint32 meshId = node->m_meshes[0];
			auto mesh = m_model->m_Meshes[meshId];
			// 머티리얼은 메시 번호가 아니라 메시가 가리키는 재질 번호로 찾는다.
			// meshId로 찾으면 재질이 메시보다 적은 모델에서 범위를 넘어
			// (Debug에서 vector 첨자 어설션) 터지고, 넘지 않아도 남의 재질이 붙는다.
			auto material = m_model->m_Materials[mesh->m_materialIndex];

			MeshRenderer* meshRenderer = rootObject->AddComponent<MeshRenderer>();

			if (m_model->m_isMakeMeshCollider)
			{
				RigidBodyComponent* rigidbody = rootObject->AddComponent<RigidBodyComponent>();
				MeshColliderComponent* convexMesh = rootObject->AddComponent<MeshColliderComponent>();
				convexMesh->SetDensity(0);
				convexMesh->SetDynamicFriction(0);
				convexMesh->SetStaticFriction(0);
				convexMesh->SetRestitution(0);
			}

			meshRenderer->m_Mesh = mesh;
			meshRenderer->m_Material = material;
			meshRenderer->m_isSkinnedMesh = m_isSkinnedMesh;
			rootObject->Transform_().SetLocalMatrix(node->m_transform);

			nextIndex = rootObject->m_index;
			return rootObject.get();
		}
	}

	for (uint32 i = 0; i < node->m_numMeshes; ++i)
	{
		std::shared_ptr<Entity> object = m_scene->CreateGameObject(node->m_name, GameObjectType::Mesh, nextIndex);

		uint32 meshId = node->m_meshes[i];
		auto mesh = m_model->m_Meshes[meshId];
		auto material = m_model->m_Materials[mesh->m_materialIndex];

		Mathf::Matrix transform = node->m_transform;

		// 위 GenerateSceneObjectHierarchy와 같은 이유로 호출 스레드에서 처리한다
		// (씬 생명주기 벡터에 잠금이 없다).
		MeshRenderer* meshRenderer = object->AddComponent<MeshRenderer>();

		if (m_model->m_isMakeMeshCollider)
		{
			RigidBodyComponent* rigidbody = object->AddComponent<RigidBodyComponent>();
			MeshColliderComponent* convexMesh = object->AddComponent<MeshColliderComponent>();
			convexMesh->SetDensity(0);
			convexMesh->SetDynamicFriction(0);
			convexMesh->SetStaticFriction(0);
			convexMesh->SetRestitution(0);
		}

		meshRenderer->m_Mesh = mesh;
		meshRenderer->m_Material = material;
		meshRenderer->m_isSkinnedMesh = m_isSkinnedMesh;
		object->Transform_().SetLocalMatrix(transform);

		nextIndex = object->m_index;
		//m_gameObjects.push_back(object);
	}

	if (false == isRoot && 0 == node->m_numMeshes)
	{
		std::shared_ptr<Entity> object = m_scene->CreateGameObject(node->m_name, GameObjectType::Mesh, nextIndex);
		object->Transform_().SetLocalMatrix(node->m_transform);
		nextIndex = object->m_index;
	}

	for (uint32 i = 0; i < node->m_numChildren; ++i)
	{
		GenerateSceneObjectHierarchy(m_model->m_nodes[node->m_childrenIndex[i]], false, nextIndex);
	}

	return rootObject.get();
}

Entity* ModelLoader::GenerateSkeletonToSceneObjectHierarchyObj(ModelNode* node, Bone* bone, bool isRoot, int parentIndex)
{
	int nextIndex = parentIndex;
	std::shared_ptr<Entity> rootObject;
	if (true == isRoot)
	{
		rootObject = m_scene->GetGameObject(m_modelRootIndex);
		// E1 후속 배선: 위 GenerateSkeletonToSceneObjectHierarchy와 동일한 사유.
		if (!rootObject)
		{
			Debug->LogError("ModelLoader::GenerateSkeletonToSceneObjectHierarchyObj: root Entity not found for index " + std::to_string(m_modelRootIndex));
			return nullptr;
		}
		nextIndex = rootObject->m_index;
	}
	else
	{
		std::shared_ptr<Entity> boneObject{};
		boneObject = m_scene->GetGameObject(bone->m_name);
		if (nullptr == boneObject)
		{
			boneObject = m_scene->CreateGameObject(bone->m_name, GameObjectType::Bone, nextIndex);
		}
		// E7-b(트랙 E): 위 GenerateSkeletonToSceneObjectHierarchy와 동일한 사유
		// (마커 컴포넌트 부착, 재방문 시 조용히 no-op).
		boneObject->AddComponent<BoneComponent>();
		nextIndex = boneObject->m_index;
		boneObject->SetRootIndex(m_modelRootIndex);
	}

	for (uint32 i = 0; i < bone->m_children.size(); ++i)
	{
		GenerateSkeletonToSceneObjectHierarchy(node, bone->m_children[i], false, nextIndex);
	}

	return rootObject.get();
}

// 여러 슬롯을 순서대로 훑어 처음 잡히는 텍스처를 돌려준다.
//
// Assimp의 glTF 임포터가 어느 aiTextureType에 넣는지는 버전마다 다르다. 한
// 슬롯만 보고 없으면 포기하면, 파일에는 텍스처가 있는데 재질이 비어 있는 상태가
// 되고 화면에서는 '검게 나온다'로만 보인다 — 원인이 임포터라는 것을 알아채기
// 어렵다. 그래서 후보를 나열하고, 어느 슬롯에서 잡혔는지(또는 전부 비었는지)를
// 로그에 남긴다.
