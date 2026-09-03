#include "ModelSceneInstantiation.h"

#include "Scene.h" // MeshCollider.h가 완전한 Scene을 전제한다 — 먼저 든다
#include "SceneManager.h"
#include "Animator.h"
#include "BoneComponent.h"
#include "DataSystem.h"
#include "Entity.h"
#include "ExperimentMaterialMigration.h"
#include "Material.h"
#include "MeshCollider.h"
#include "MeshRenderer.h"
#include "ModelConsumptionDiagnostics.h" // MBC10
#include "RigidBodyComponent.h"
#include "Assets/ModelAssetGeneration.h"
#include "Experiment/ModelData.h"

#include <algorithm>
#include <cstdio>
#include <span>
#include <vector>

namespace ModelSceneInstantiation
{
	namespace
	{
		[[nodiscard]] std::uint32_t IndexOfMesh(
			std::span<const assets::ModelMeshAsset> meshes, const Uuid::Uuid16& meshId) noexcept
		{
			for (std::size_t index = 0; index < meshes.size(); ++index)
			{
				if (meshes[index].meshId == meshId) return static_cast<std::uint32_t>(index);
			}
			return assets::kInvalidModelAssetIndex;
		}

		[[nodiscard]] std::uint32_t IndexOfMaterial(
			std::span<const assets::ModelMaterialAsset> materials,
			const Uuid::Uuid16& materialId) noexcept
		{
			for (std::size_t index = 0; index < materials.size(); ++index)
			{
				if (materials[index].materialId == materialId)
					return static_cast<std::uint32_t>(index);
			}
			return assets::kInvalidModelAssetIndex;
		}

		// generation의 immutable material → renderer 소유 runtime Material. 값은
		// 1:1이고 embedded texture owner는 BindModelGeneration이 같은 closure에서
		// 묶는다. 변환 실패는 빈 재질이다 — 지어낸 재질로 조용히 그리지 않는다.
		[[nodiscard]] std::shared_ptr<Material> MakeRuntimeMaterial(
			const assets::ModelAssetGeneration& generation, std::uint32_t materialIndex)
		{
			const auto materials = generation.Materials();
			if (materialIndex >= materials.size()) return nullptr;
			experiment::Material converted;
			ExperimentMaterialMigration::ConvertModelMaterialAsset(
				materials[materialIndex], generation, converted);
			auto material = std::make_shared<Material>();
			std::string error;
			if (!ExperimentMaterialMigration::ConvertToLegacyMaterial(
				converted, nullptr, *material, error))
			{
				Debug->LogWarning("generation 재질 변환 실패 — 빈 재질로 세운다: " + error);
				return nullptr;
			}
			DataSystems->FinalizeMaterialRuntime(*material);
			return material;
		}
	}

	Entity* Instantiate(Scene& scene,
		const std::shared_ptr<const assets::ModelAssetGeneration>& generation,
		const Options& options)
	{
		if (!generation) return nullptr;
		const std::span<const assets::ModelNodeAsset> nodes = generation->Nodes();
		const std::span<const assets::ModelMeshAsset> meshes = generation->Meshes();
		const std::span<const assets::ModelMaterialAsset> materials = generation->Materials();
		if (nodes.empty() || assets::kInvalidModelAssetIndex != nodes[0].parent)
		{
			ModelConsumptionDiagnostics::NoteInstantiateRejected();
			return nullptr;
		}

		// 전부 만들기 전에 계약을 검증한다 — 반쪽 시공 금지.
		for (std::size_t nodeIndex = 0; nodeIndex < nodes.size(); ++nodeIndex)
		{
			const assets::ModelNodeAsset& node = nodes[nodeIndex];
			if (0 != nodeIndex && (assets::kInvalidModelAssetIndex == node.parent
				|| node.parent >= nodeIndex))
			{
				ModelConsumptionDiagnostics::NoteInstantiateRejected();
				return nullptr;
			}
			for (const Uuid::Uuid16& meshId : node.meshes)
			{
				if (assets::kInvalidModelAssetIndex == IndexOfMesh(meshes, meshId))
				{
					ModelConsumptionDiagnostics::NoteInstantiateRejected();
					return nullptr;
				}
			}
		}

		const assets::ModelSkeletonAsset* skeleton = generation->Skeleton();
		const bool hasBones = nullptr != skeleton && !skeleton->bones.empty()
			&& skeleton->rootBone < skeleton->bones.size();
		const std::string modelName = generation->Name();

		// 같은 재질을 여러 메시가 공유한다 — renderer마다 새로 변환하지 않는다.
		std::vector<std::shared_ptr<Material>> runtimeMaterials(materials.size());
		std::vector<bool> materialBuilt(materials.size(), false);
		const auto materialFor = [&](const Uuid::Uuid16& materialId) -> std::shared_ptr<Material>
		{
			const std::uint32_t index = IndexOfMaterial(materials, materialId);
			if (index >= materials.size()) return nullptr;
			if (!materialBuilt[index])
			{
				runtimeMaterials[index] = MakeRuntimeMaterial(*generation, index);
				materialBuilt[index] = true;
			}
			return runtimeMaterials[index];
		};

		const auto attachMeshRenderer = [&](Entity& object, std::uint32_t meshIndex)
		{
			MeshRenderer* meshRenderer = object.AddComponent<MeshRenderer>();
			if (options.createMeshCollider)
			{
				object.AddComponent<RigidBodyComponent>();
				MeshColliderComponent* convexMesh = object.AddComponent<MeshColliderComponent>();
				convexMesh->SetDensity(0);
				convexMesh->SetDynamicFriction(0);
				convexMesh->SetStaticFriction(0);
				convexMesh->SetRestitution(0);
			}
			const assets::ModelMeshAsset& mesh = meshes[meshIndex];
			if (std::shared_ptr<Material> material = materialFor(mesh.materialId))
			{
				meshRenderer->SetMaterial(std::move(material));
			}
			meshRenderer->m_isSkinnedMesh = hasBones;
			// typed 정본 + closure 텍스처 — SetMaterial 뒤에 부른다.
			meshRenderer->BindModelGeneration(generation, meshIndex);
			// 저작 base — shaderAssetId가 있는 재질만(nil이면 legacy 표기 재질이라
			// resolver가 거부한다; legacy seal이 closure owner를 그대로 받는다).
			const std::uint32_t materialIndex = IndexOfMaterial(materials, mesh.materialId);
			if (materialIndex < materials.size()
				&& !materials[materialIndex].shaderAssetId.IsNil())
			{
				auto authored = std::make_shared<experiment::Material>();
				ExperimentMaterialMigration::ConvertModelMaterialAsset(
					materials[materialIndex], *generation, *authored);
				meshRenderer->SetExperimentMaterialBase(std::move(authored));
			}
		};

		std::vector<int> attachPoint(nodes.size(), 0);
		std::vector<Entity*> nameCandidates; // 본 이름 대조 목록(이 모델의 엔티티만)
		Entity* rootObject = nullptr;
		int rootIndex = 0;

		for (std::size_t nodeIndex = 0; nodeIndex < nodes.size(); ++nodeIndex)
		{
			const assets::ModelNodeAsset& node = nodes[nodeIndex];
			const bool isRoot = 0 == nodeIndex;
			int nextIndex = 0;

			if (isRoot)
			{
				rootObject = scene.CreateEntity(modelName, GameObjectType::Mesh, nextIndex);
				nameCandidates.push_back(rootObject);
				nextIndex = rootObject->m_index;
				rootIndex = rootObject->m_index;

				if (hasBones)
				{
					Animator* animator = rootObject->AddComponent<Animator>();
					animator->SetEnabled(true);
					// 재생 정본은 이 모델의 generation이다 — m_Motion이 ModelId를 든다.
					animator->m_Motion = FileGuid(generation->Identity().modelId);
					animator->EnsureAnimationBinding();
				}

				if (1 == nodes.size() && 1 == node.meshes.size())
				{
					attachMeshRenderer(*rootObject, IndexOfMesh(meshes, node.meshes[0]));
					rootObject->Transform_().SetLocalMatrix(
						node.localTransform, TransformWriteReason::ModelImport);
					attachPoint[nodeIndex] = rootObject->m_index;
					continue;
				}
			}
			else
			{
				nextIndex = attachPoint[node.parent];
			}

			for (const Uuid::Uuid16& meshId : node.meshes)
			{
				Entity* object = scene.CreateEntity(node.name, GameObjectType::Mesh, nextIndex);
				attachMeshRenderer(*object, IndexOfMesh(meshes, meshId));
				object->Transform_().SetLocalMatrix(
					node.localTransform, TransformWriteReason::ModelImport);
				nextIndex = object->m_index;
			}

			if (!isRoot && node.meshes.empty())
			{
				Entity* object = scene.CreateEntity(node.name, GameObjectType::Mesh, nextIndex);
				nameCandidates.push_back(object);
				object->Transform_().SetLocalMatrix(
					node.localTransform, TransformWriteReason::ModelImport);
				nextIndex = object->m_index;
			}

			attachPoint[nodeIndex] = nextIndex;
		}

		if (hasBones)
		{
			std::vector<int> boneAttach(skeleton->bones.size(), rootIndex);
			for (std::size_t boneIndex = 0; boneIndex < skeleton->bones.size(); ++boneIndex)
			{
				// 루트 본은 모델 루트 엔티티가 대신한다.
				if (skeleton->rootBone == boneIndex) continue;
				const assets::ModelBoneAsset& bone = skeleton->bones[boneIndex];
				const int parentAttach =
					(assets::kInvalidModelAssetIndex != bone.parent
						&& bone.parent < boneAttach.size())
					? boneAttach[bone.parent] : rootIndex;

				const auto found = std::find_if(nameCandidates.begin(), nameCandidates.end(),
					[&bone](Entity* candidate)
					{ return candidate->RemoveSuffixNumberTag() == bone.name; });
				Entity* boneObject = found != nameCandidates.end() ? *found : nullptr;
				if (nullptr == boneObject)
				{
					boneObject = scene.CreateEntity(bone.name, GameObjectType::Bone, parentAttach);
				}
				boneObject->AddComponent<BoneComponent>();
				boneObject->SetRootIndex(rootIndex);
				boneAttach[boneIndex] = boneObject->m_index;
			}
		}

		// MBC10 — 인스턴스화 관측은 읽기 전용 계수다(`assets.modeldiag`).
		ModelConsumptionDiagnostics::NoteInstantiated(modelName);
		WorkerPools->NotifyAllAndWait();
		return rootObject;
	}
}
