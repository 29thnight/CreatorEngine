// Model/ModelLoader의 씬 인스턴스화 구현 (PHASE 4-2 C5).
//
// 모델 파싱(assimp -> Mesh/Material/Skeleton)은 렌더/리소스 도메인이라
// RenderEngine에 남고, 파싱 결과를 씬에 세우는 일(GameObject 계층 생성,
// MeshRenderer/Animator/RigidBody/MeshCollider 부착)은 게임플레이 도메인이라
// 이 파일이 맡는다.
#include "Model.h"
#include "ModelLoader.h"
#include "Scene.h"
#include "Entity.h"
#include "SceneManager.h"
#include "Animator.h"
#include "MeshRenderer.h"
#include "RigidBodyComponent.h"
#include "MeshCollider.h"
#include "Skeleton.h"
#include "BoneComponent.h"
#include "DataSystem.h"          // I5-D4d: TryGetExperimentModel
#include "Experiment/Model.h"    // I5-D4d: 인스턴스화의 experiment 정본

#include <algorithm> // I5-D4d: 본 이름 대조 find_if
#include <cstdio>    // I5-D4d: [model.instantiate] 경로 관측(stdout — CLI 게이트 채널)
#include <span>
#include <vector>

namespace
{
	// I5-D4d — experiment 직행의 전제인 역브리지 1:1 순서 계약을 검증한다.
	// 하나라도 어긋나면(Assimp 폴백 모델·낡은 등록) 전부 legacy 폴백이다 —
	// 인덱스 대응이 깨진 채 절반만 experiment로 세우는 것이 최악이다.
	[[nodiscard]] bool MatchesExperimentContract(
		const Model& legacy, const experiment::Model& source)
	{
		return legacy.GetNodes().size() == source.Nodes().size()
			&& legacy.GetMeshCount() == source.Meshes().size()
			&& legacy.GetMaterialCount() == source.Materials().size();
	}

	// ModelLoader 멤버는 private(friend: Model)라 시공 호출은 Model:: 안에서
	// 한다 — 여기서는 정본 조회와 계약 검증까지만.
	[[nodiscard]] std::shared_ptr<const experiment::Model>
		TryGetInstantiationSource(const Model& model)
	{
		std::shared_ptr<const experiment::Model> source =
			DataSystems->TryGetExperimentModel(model.guid);
		if (nullptr == source || !MatchesExperimentContract(model, *source))
		{
			return nullptr;
		}
		return source;
	}
}

Model* Model::LoadModelToScene(Model* model, Scene& Scene)
{
    if (nullptr == model)
    {
        return nullptr;
    }

    ModelLoader loader = ModelLoader(model, &Scene);
    file::path path_ = model->path;

	// I5-D4d — 인스턴스화 정본이 experiment다(parent 단일 순회). experiment
	// 모델이 없으면(Assimp 폴백·A/B off·계약 불일치) legacy 재귀가 폴백이다.
	// 경로는 stdout으로 계수한다([model.dual]·[mesh.resolve]와 같은 채널).
	Entity* experimentRoot = nullptr;
	if (const auto source = TryGetInstantiationSource(*model))
	{
		experimentRoot = loader.GenerateSceneObjectHierarchyExperiment(source);
	}
	if (nullptr == experimentRoot)
	{
		loader.GenerateSceneObjectHierarchy(model->m_nodes[0], true, 0);
		if (model->m_hasBones)
		{
			loader.GenerateSkeletonToSceneObjectHierarchy(model->m_nodes[0], model->m_Skeleton->m_rootBone, true, 0);
		}
	}
	std::printf("[model.instantiate] %s: %s\n",
		experimentRoot ? "experiment" : "legacy", model->name.c_str());

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

	// I5-D4d — Scene 변형과 같은 정본·같은 폴백. 구 Obj 변형의 본 조회는
	// 씬 전역 이름 검색이라 같은 이름의 남의 오브젝트를 붙잡을 수 있었다 —
	// experiment 순회는 이 모델이 만든 엔티티 안에서만 찾는다.
	Entity* rootObj = nullptr;
	if (const auto source = TryGetInstantiationSource(*model))
	{
		rootObj = loader.GenerateSceneObjectHierarchyExperiment(source);
	}
	const bool viaExperiment = nullptr != rootObj;
	if (!viaExperiment)
	{
		rootObj = loader.GenerateSceneObjectHierarchyObj(model->m_nodes[0], true, 0);
		if (model->m_hasBones)
		{
			rootObj = loader.GenerateSkeletonToSceneObjectHierarchyObj(model->m_nodes[0], model->m_Skeleton->m_rootBone, true, 0);
		}
	}
	std::printf("[model.instantiate] %s: %s\n",
		viaExperiment ? "experiment" : "legacy", model->name.c_str());

	WorkerPools->NotifyAllAndWait();

	return rootObj;
}
Entity* ModelLoader::GenerateSceneObjectHierarchyExperiment(
	const std::shared_ptr<const experiment::Model>& source)
{
	// I5-D4d — parent-only 표현의 단일 순회. 로더 검증이 "parent는 항상 자기보다
	// 앞선 인덱스"를 강제하므로(ModelLoader::Validate), 인덱스 순으로 돌면 부모
	// 엔티티가 항상 먼저 서 있다 — legacy 재귀(children 목록 DFS)와 같은 구조를
	// 재귀 없이 세운다. 만드는 계층·이름·트랜스폼은 legacy 규약 그대로다:
	//   - 메시 N개 노드는 노드 자신 대신 메시 엔티티 N개를 사슬로 만든다
	//     (i번째가 i-1번째의 자식). 자식 노드는 사슬 끝에 붙는다.
	//   - 메시 0개 비루트 노드만 자기 엔티티를 만들고 본 이름 대조 목록에 든다.
	//   - 루트 트랜스폼은 단일 노드·단일 메시 특례에서만 기록한다.
	const std::span<const experiment::ModelNode> nodes = source->Nodes();
	if (nodes.empty() || !source->RootNode().IsValid()
		|| 0 != source->RootNode().Value())
	{
		return nullptr; // 역브리지가 노드 0을 루트로 시공한다 — 어긋나면 폴백.
	}

	m_isSkinnedMesh = m_model->m_hasBones;

	// 메시 렌더러 시공 — legacy 컨테이너는 역브리지 1:1 순서 계약으로 같은
	// 인덱스에서 꺼내고(D4f 은퇴 전까지의 병행), 핸들 정본은 여기서 직접
	// 심는다. D4c의 EnsureExperimentBinding 신원 조회 폴백은 이 경로에서
	// 더는 필요 없다(no-op이 된다).
	const auto attachMeshRenderer = [&](Entity& object,
		const experiment::Mesh& mesh, experiment::MeshIndex meshIndex)
	{
		MeshRenderer* meshRenderer = object.AddComponent<MeshRenderer>();

		if (m_model->m_isMakeMeshCollider)
		{
			RigidBodyComponent* rigidbody = object.AddComponent<RigidBodyComponent>();
			MeshColliderComponent* convexMesh = object.AddComponent<MeshColliderComponent>();
			convexMesh->SetDensity(0);
			convexMesh->SetDynamicFriction(0);
			convexMesh->SetStaticFriction(0);
			convexMesh->SetRestitution(0);
		}

		const uint32 materialIndex = mesh.material.IsValid()
			? mesh.material.Value() : 0u;
		meshRenderer->m_Mesh = m_model->m_Meshes[meshIndex.Value()];
		meshRenderer->SetMaterial(m_model->m_Materials[materialIndex]);
		meshRenderer->m_modelGuid = m_model->guid;
		meshRenderer->m_isSkinnedMesh = m_isSkinnedMesh;
		meshRenderer->m_experimentModel = source;
		meshRenderer->m_experimentMeshIndex = meshIndex.Value();
	};

	// 노드별 부착점 — 그 노드 몫으로 마지막에 선 엔티티의 인덱스. 자식 노드와
	// (루트라면) 본 계층이 여기에 붙는다.
	std::vector<int> attachPoint(nodes.size(), 0);
	Entity* rootObject = nullptr;

	for (std::size_t nodeIndex = 0; nodeIndex < nodes.size(); ++nodeIndex)
	{
		const experiment::ModelNode& node = nodes[nodeIndex];
		const bool isRoot = 0 == nodeIndex;
		int nextIndex = 0;

		if (isRoot)
		{
			rootObject = m_scene->CreateEntity(m_model->name,
				GameObjectType::Mesh, nextIndex);
			m_gameObjects.push_back(rootObject);
			nextIndex = rootObject->m_index;
			m_modelRootIndex = rootObject->m_index;

			if (m_model->m_hasBones)
			{
				m_animator = rootObject->AddComponent<Animator>();
				m_animator->SetEnabled(true);
				m_animator->m_Motion = m_model->m_animator->m_Motion;
				m_animator->m_Skeleton = m_model->m_Skeleton;
				// I5-D4e-1 — 재생 핸들 직심기(D4d 메시 핸들과 같은 결).
				m_animator->EnsureExperimentAnimationBinding();
			}

			// 단일 노드·단일 메시 특례 — 루트 자신이 메시 엔티티다.
			if (1 == nodes.size() && 1 == node.meshes.size())
			{
				const experiment::MeshIndex meshIndex = node.meshes[0];
				attachMeshRenderer(*rootObject,
					*source->TryGetMesh(meshIndex), meshIndex);
	rootObject->Transform_().SetLocalMatrix(
		node.localTransform, TransformWriteReason::ModelImport);
				attachPoint[nodeIndex] = rootObject->m_index;
				continue;
			}
		}
		else
		{
			nextIndex = attachPoint[node.parent.Value()];
		}

		for (const experiment::MeshIndex meshIndex : node.meshes)
		{
			Entity* object = m_scene->CreateEntity(node.name,
				GameObjectType::Mesh, nextIndex);
			attachMeshRenderer(*object, *source->TryGetMesh(meshIndex), meshIndex);
	object->Transform_().SetLocalMatrix(
		node.localTransform, TransformWriteReason::ModelImport);
			nextIndex = object->m_index;
		}

		if (!isRoot && node.meshes.empty())
		{
			Entity* object = m_scene->CreateEntity(node.name,
				GameObjectType::Mesh, nextIndex);
			m_gameObjects.push_back(object);
		object->Transform_().SetLocalMatrix(
			node.localTransform, TransformWriteReason::ModelImport);
			nextIndex = object->m_index;
		}

		attachPoint[nodeIndex] = nextIndex;
	}

	// ── 본 계층 — 같은 단일 순회 규약(본 검증도 parent가 항상 앞선다) ──────
	//
	// legacy와 같은 대조: 이 모델이 만든 이름 대조 목록(m_gameObjects) 안에서
	// suffix 태그를 벗겨 찾는다. 구 Obj 변형의 씬 전역 GetEntity(name)는 같은
	// 이름의 남의 오브젝트를 붙잡는 결함이라 승계하지 않는다. legacy 재귀가
	// 대조 실패 시 end()를 역참조하던 잠재 결함도 여기서는 생성 폴백이다.
	const experiment::Skeleton* skeleton = source->TryGetSkeleton();
	if (m_model->m_hasBones && nullptr != skeleton
		&& experiment::IsInRange(skeleton->rootBone, skeleton->bones.size()))
	{
		std::vector<int> boneAttach(skeleton->bones.size(), m_modelRootIndex);
		for (std::size_t boneIndex = 0; boneIndex < skeleton->bones.size();
			++boneIndex)
		{
			// 루트 본은 모델 루트 엔티티가 대신한다 — legacy 순회도 루트 본
			// 자신은 세우지 않고 자식들을 루트 오브젝트 아래에 붙인다.
			if (skeleton->rootBone.Value() == boneIndex) continue;

			const experiment::Bone& bone = skeleton->bones[boneIndex];
			const int parentAttach = bone.parent.IsValid()
				? boneAttach[bone.parent.Value()] : m_modelRootIndex;

			const auto found = std::find_if(m_gameObjects.begin(),
				m_gameObjects.end(), [&bone](Entity* candidate)
				{
					return candidate->RemoveSuffixNumberTag() == bone.name;
				});
			Entity* boneObject = found != m_gameObjects.end() ? *found : nullptr;
			if (nullptr == boneObject)
			{
				boneObject = m_scene->CreateEntity(bone.name,
					GameObjectType::Bone, parentAttach);
			}
			boneObject->AddComponent<BoneComponent>();
			boneObject->SetRootIndex(m_modelRootIndex);
			boneAttach[boneIndex] = boneObject->m_index;
		}
	}

	return rootObject;
}

void ModelLoader::GenerateSceneObjectHierarchy(ModelNode* node, bool isRoot, int parentIndex)
{
	static int modelSeparator = 0;
	int nextIndex = parentIndex;

	if (true == isRoot)
	{
		auto rootObject = m_scene->CreateEntity(m_model->name, GameObjectType::Mesh, nextIndex);
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
            meshRenderer->SetMaterial(m_model->m_Materials[mesh->m_materialIndex]);
			meshRenderer->m_modelGuid = m_model->guid; // S2c-1: 모델 출처 자립
			meshRenderer->m_isSkinnedMesh = m_isSkinnedMesh;
	rootObject->Transform_().SetLocalMatrix(
		node->m_transform, TransformWriteReason::ModelImport);
			nextIndex = rootObject->m_index;

			return;
		}
	}

	for (uint32 i = 0; i < node->m_numMeshes; ++i)
	{
		Entity* object = m_scene->CreateEntity(node->m_name, GameObjectType::Mesh, nextIndex);

		uint32 meshId			= node->m_meshes[i];
		auto mesh				= m_model->m_Meshes[meshId];
		auto material			= m_model->m_Materials[mesh->m_materialIndex];
                
		const math::matrix4x4 transform = node->m_transform;
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
		// 필드를 채울 뿐이고, 바로 위의 CreateEntity도 이미 이 스레드다.
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
		meshRenderer->SetMaterial(material);
		meshRenderer->m_modelGuid = m_model->guid; // S2c-1: 모델 출처 자립
		meshRenderer->m_isSkinnedMesh = m_isSkinnedMesh;
	object->Transform_().SetLocalMatrix(
		transform, TransformWriteReason::ModelImport);

		nextIndex = object->m_index;
		//m_gameObjects.push_back(object);
	}

	if (false == isRoot && 0 == node->m_numMeshes)
	{
		Entity* object = m_scene->CreateEntity(node->m_name, GameObjectType::Mesh, nextIndex);
		m_gameObjects.push_back(object);
		object->Transform_().SetLocalMatrix(
			node->m_transform, TransformWriteReason::ModelImport);
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
	static Entity* rootObject{};
	if (true == isRoot)
	{
		rootObject = m_scene->GetEntity(m_modelRootIndex);
		// E1(슬롯맵)의 GetEntity 루트 폴백 제거 후속 배선: m_modelRootIndex가
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
		Entity* boneObject{};
		auto it = std::find_if(m_gameObjects.begin(), m_gameObjects.end(), [bone](Entity* shPtr)
		{
			std::string name = shPtr->RemoveSuffixNumberTag();
			return name == bone->m_name;
		});

		boneObject = (*it);
		if (nullptr == boneObject)
		{
			boneObject = m_scene->CreateEntity(bone->m_name, GameObjectType::Bone, nextIndex);
			//m_gameObjects.push_back(boneObject);
		}
		// E7-b/E7-c(트랙 E): Bone 정체성의 정본인 마커를 붙인다. 이미
		// 붙어 있으면(재방문 경로) AddComponent<T>()가 조용히 nullptr만
		// 돌려주고 끝난다 — 중복 경고 없음(Entity.inl 템플릿 오버로드는
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
	Entity* rootObject{};

	if (true == isRoot)
	{
		rootObject = m_scene->CreateEntity(m_model->name, GameObjectType::Mesh, nextIndex);
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
			meshRenderer->SetMaterial(material);
			meshRenderer->m_modelGuid = m_model->guid; // S2c-1: 모델 출처 자립
			meshRenderer->m_isSkinnedMesh = m_isSkinnedMesh;
	rootObject->Transform_().SetLocalMatrix(
		node->m_transform, TransformWriteReason::ModelImport);

			nextIndex = rootObject->m_index;
			return rootObject;
		}
	}

	for (uint32 i = 0; i < node->m_numMeshes; ++i)
	{
		Entity* object = m_scene->CreateEntity(node->m_name, GameObjectType::Mesh, nextIndex);

		uint32 meshId = node->m_meshes[i];
		auto mesh = m_model->m_Meshes[meshId];
		auto material = m_model->m_Materials[mesh->m_materialIndex];

		const math::matrix4x4 transform = node->m_transform;

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
		meshRenderer->SetMaterial(material);
		meshRenderer->m_modelGuid = m_model->guid; // S2c-1: 모델 출처 자립
		meshRenderer->m_isSkinnedMesh = m_isSkinnedMesh;
	object->Transform_().SetLocalMatrix(
		transform, TransformWriteReason::ModelImport);

		nextIndex = object->m_index;
		//m_gameObjects.push_back(object);
	}

	if (false == isRoot && 0 == node->m_numMeshes)
	{
		Entity* object = m_scene->CreateEntity(node->m_name, GameObjectType::Mesh, nextIndex);
		object->Transform_().SetLocalMatrix(
			node->m_transform, TransformWriteReason::ModelImport);
		nextIndex = object->m_index;
	}

	for (uint32 i = 0; i < node->m_numChildren; ++i)
	{
		GenerateSceneObjectHierarchy(m_model->m_nodes[node->m_childrenIndex[i]], false, nextIndex);
	}

	return rootObject;
}

Entity* ModelLoader::GenerateSkeletonToSceneObjectHierarchyObj(ModelNode* node, Bone* bone, bool isRoot, int parentIndex)
{
	int nextIndex = parentIndex;
	Entity* rootObject{};
	if (true == isRoot)
	{
		rootObject = m_scene->GetEntity(m_modelRootIndex);
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
		Entity* boneObject{};
		boneObject = m_scene->GetEntity(bone->m_name);
		if (nullptr == boneObject)
		{
			boneObject = m_scene->CreateEntity(bone->m_name, GameObjectType::Bone, nextIndex);
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

	return rootObject;
}

// 여러 슬롯을 순서대로 훑어 처음 잡히는 텍스처를 돌려준다.
//
// Assimp의 glTF 임포터가 어느 aiTextureType에 넣는지는 버전마다 다르다. 한
// 슬롯만 보고 없으면 포기하면, 파일에는 텍스처가 있는데 재질이 비어 있는 상태가
// 되고 화면에서는 '검게 나온다'로만 보인다 — 원인이 임포터라는 것을 알아채기
// 어렵다. 그래서 후보를 나열하고, 어느 슬롯에서 잡혔는지(또는 전부 비었는지)를
// 로그에 남긴다.
