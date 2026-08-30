// I5-D1a — experiment::Model → legacy ::Model 역브리지 (전환기, I6 은퇴).
//
// 방향에 주의: RenderTests의 ExperimentLegacyBridge는 legacy → experiment
// (패리티 검증용 정브리지)이고, 이 파일이 그 역이다. 렌더 소유가 아직
// legacy인 동안(I5-D4 이전) experiment 로드 결과를 기존 파이프(96B Vertex ·
// Bone* 트리 · 이름 키 NodeAnimation)에 소비시킨다 — I5-M S1의
// ConvertToLegacyMaterial과 같은 지위의 단방향 어댑터다.
//
// 시공 견본은 legacy 모델 캐시 로드(ModelLoader::LoadModelFromAsset 계열)다 —
// 평탄 목록+부모 인덱스에서 트리를 재구축하는 그 규약을 그대로 따른다.
// legacy Model/Mesh 컨테이너가 private+friend(ModelLoader·DataSystem)라 이
// 함수는 DataSystem 멤버로만 시공 가능하고, 그래서 별도 TU에 정의한다
// (legacy 본체의 friend 목록을 편집하지 않는다).
//
// 알려진 표현 격차(둘 다 관측 가능해야 한다 — outError가 아니라 시공 규약):
//   - bitangent: legacy는 저장, experiment는 tangent.w(handedness)만 저장.
//     cross(normal, tangent.xyz) * w 재구성 — V2가 무손실이라 주장한 그 계약.
//   - Step 보간: legacy NodeAnimation에는 보간 모드가 없어 Linear로 강등된다
//     (실측: 현 코퍼스의 Step 트랙은 전부 상수 트랙이라 시각 손실 0).
//   - uv1 부재 시 uv0 복사 — legacy 임포터(ConvertToAiMesh)의 규약.
#include "DataSystem.h"
#include "Experiment/Model.h"
#include "ExperimentMaterialMigration.h"
#include "Material.h"
#include "Mesh.h"
#include "Model.h"
#include "ReflectionYml.h"
#include "Skeleton.h"

#include <mathematics/vector3.hpp>

#include <cmath>
#include <string>
#include <vector>

namespace
{
	[[nodiscard]] float PackedBoneLane(experiment::PackedBoneIndex packed)
	{
		// 255(미사용 slot)는 legacy 규약의 "인덱스 0 + 가중치 0"으로 내린다 —
		// legacy 임포터도 미사용 lane을 0으로 남긴다(ProcessBones).
		return experiment::InvalidPackedBoneIndex == packed
			? 0.0f : static_cast<float>(packed);
	}

	[[nodiscard]] Vertex UnpackVertex(const experiment::VertexBuffer& buffer,
		std::size_t index)
	{
		const experiment::Vertex packed = buffer[index];
		Vertex vertex{};
		vertex.position = packed.position;
		vertex.normal = packed.normal;
		vertex.uv0 = packed.uv0;
		vertex.uv1 = buffer.Uv1(index).value_or(packed.uv0);
		vertex.tangent = { packed.tangent.x, packed.tangent.y,
			packed.tangent.z };
		const math::vector3 bitangent =
			math::cross(vertex.normal, vertex.tangent);
		vertex.bitangent = { bitangent.x * packed.tangent.w,
			bitangent.y * packed.tangent.w, bitangent.z * packed.tangent.w };
		vertex.boneIndices = {
			PackedBoneLane(packed.boneIndices[0]),
			PackedBoneLane(packed.boneIndices[1]),
			PackedBoneLane(packed.boneIndices[2]),
			PackedBoneLane(packed.boneIndices[3]) };
		vertex.boneWeights = {
			packed.boneWeights[0], packed.boneWeights[1],
			packed.boneWeights[2], packed.boneWeights[3] };
		return vertex;
	}

	[[nodiscard]] Skeleton* BuildLegacySkeleton(
		const experiment::Skeleton& source)
	{
		// 캐시 로드(LoadSkeleton)와 같은 시공: 평탄 목록 → 부모 인덱스로 트리.
		Skeleton* skeleton = new Skeleton();
		skeleton->m_rootTransform = source.rootTransform;
		skeleton->m_globalInverseTransform = source.globalInverseTransform;
		skeleton->m_bones.reserve(source.bones.size());
		for (std::size_t index = 0; index < source.bones.size(); ++index)
		{
			const experiment::Bone& bone = source.bones[index];
			Bone* legacyBone = new Bone();
			legacyBone->m_name = bone.name;
			legacyBone->m_index = static_cast<int>(index);
			legacyBone->m_parentIndex = bone.parent.IsValid()
				? static_cast<int>(bone.parent.Value()) : -1;
			legacyBone->m_offset = bone.inverseBindMatrix;
			skeleton->m_bones.push_back(legacyBone);
		}
		const int boneCount = static_cast<int>(skeleton->m_bones.size());
		for (Bone* bone : skeleton->m_bones)
		{
			if (bone->m_parentIndex >= 0 && bone->m_parentIndex < boneCount)
			{
				skeleton->m_bones[bone->m_parentIndex]
					->m_children.push_back(bone);
			}
			else
			{
				skeleton->m_rootBone = bone;
			}
		}

		skeleton->m_animations.reserve(source.clips.size());
		for (const experiment::AnimationClip& clip : source.clips)
		{
			Animation animation{};
			animation.m_name = clip.name;
			animation.m_duration = static_cast<float>(clip.durationTicks);
			animation.m_ticksPerSecond = clip.ticksPerSecond;
			animation.m_isLoop = clip.looping;

			std::size_t totalKeys = 0;
			for (const experiment::AnimationChannel& channel : clip.channels)
			{
				if (!experiment::IsInRange(channel.bone, source.bones.size()))
				{
					continue; // Validate가 이미 거른다 — 방어적 스킵.
				}
				const std::string& boneName =
					source.bones[channel.bone.Value()].name;
				NodeAnimation nodeAnimation{};
				nodeAnimation.m_name = boneName;
				nodeAnimation.m_positionKeys.reserve(
					channel.translations.size());
				for (const experiment::TranslationKey& key
					: channel.translations)
				{
					nodeAnimation.m_positionKeys.push_back({
						{ key.value.x, key.value.y, key.value.z, 1.0f },
						key.time });
				}
				nodeAnimation.m_rotationKeys.reserve(channel.rotations.size());
				for (const experiment::RotationKey& key : channel.rotations)
				{
					nodeAnimation.m_rotationKeys.push_back(
						{ key.quaternion, key.time });
				}
				nodeAnimation.m_scaleKeys.reserve(channel.scales.size());
				for (const experiment::ScaleKey& key : channel.scales)
				{
					nodeAnimation.m_scaleKeys.push_back(
						{ key.value, key.time });
				}
				totalKeys += channel.translations.size()
					+ channel.rotations.size() + channel.scales.size();
				animation.m_nodeAnimations.emplace(boneName,
					std::move(nodeAnimation));
			}
			animation.m_totalKeyFrames = totalKeys;
			skeleton->m_animations.push_back(std::move(animation));
		}
		return skeleton;
	}
}

bool DataSystem::BuildLegacyModelFromExperiment(
	const experiment::Model& source, std::shared_ptr<Model>& outModel,
	std::string& outError)
{
	auto legacy = std::make_shared<Model>();
	legacy->name = source.Metadata().name;
	legacy->guid.m_guid = source.Metadata().assetId.value;
	legacy->path = source.Metadata().sourcePath;
	legacy->loadType = ModelLoadType::Form3DModel;

	// ── 노드: parent-only → legacy(index/parent/children 중복 표현) 재구축 ──
	const std::span<const experiment::ModelNode> nodes = source.Nodes();
	legacy->m_nodes.reserve(nodes.size());
	for (std::size_t index = 0; index < nodes.size(); ++index)
	{
		const experiment::ModelNode& node = nodes[index];
		ModelNode* legacyNode = new ModelNode(node.name);
		legacyNode->m_index = static_cast<uint32>(index);
		// legacy 규약: 루트의 parentIndex는 0(자기 자신)이다.
		legacyNode->m_parentIndex = node.parent.IsValid()
			? node.parent.Value() : 0u;
		legacyNode->m_transform = node.localTransform;
		legacyNode->m_meshes.reserve(node.meshes.size());
		for (const experiment::MeshIndex mesh : node.meshes)
		{
			legacyNode->m_meshes.push_back(mesh.Value());
		}
		legacyNode->m_numMeshes =
			static_cast<uint32>(legacyNode->m_meshes.size());
		legacy->m_nodes.push_back(legacyNode);
	}
	for (std::size_t index = 0; index < legacy->m_nodes.size(); ++index)
	{
		ModelNode* node = legacy->m_nodes[index];
		// ★ 루트(자기 참조 parent 0)를 자식으로 넣으면 순회가 무한이다 —
		//   findbone-name-lookup-fails가 기록한 그 함정의 대우.
		if (index == 0 || node->m_parentIndex >= legacy->m_nodes.size())
		{
			continue;
		}
		legacy->m_nodes[node->m_parentIndex]->m_childrenIndex.push_back(
			node->m_index);
	}
	for (ModelNode* node : legacy->m_nodes)
	{
		node->m_numChildren = static_cast<uint32>(node->m_childrenIndex.size());
	}

	// ── 메시: packed 정점 언팩(96B) + 리플렉션 스키마로 materialIndex 주입 ──
	legacy->m_Meshes.reserve(source.Meshes().size());
	for (const experiment::Mesh& mesh : source.Meshes())
	{
		std::vector<Vertex> vertices;
		vertices.reserve(mesh.vertices.size());
		for (std::size_t index = 0; index < mesh.vertices.size(); ++index)
		{
			vertices.push_back(UnpackVertex(mesh.vertices, index));
		}
		std::vector<uint32> indices(mesh.indices.begin(), mesh.indices.end());
		auto legacyMesh = std::make_shared<Mesh>(mesh.name,
			std::move(vertices), std::move(indices));
		// m_materialIndex는 private(friend: ModelLoader·MeshOptimizer)이지만
		// reflect() 스키마가 멤버를 공개한다 — legacy 본체의 friend 목록을
		// 편집하지 않기 위해 스키마 순회로 쓴다. 브리지와 함께 I6에서 죽는다.
		const uint32 materialIndex = mesh.material.IsValid()
			? mesh.material.Value() : 0u;
		meta::for_each_field(*legacyMesh,
			[&](std::string_view fieldName, auto& value)
			{
				if constexpr (std::is_same_v<
					std::remove_reference_t<decltype(value)>, uint32>)
				{
					if (fieldName == "m_materialIndex")
					{
						value = materialIndex;
					}
				}
			});
		legacyMesh->RecalculateBounds();
		legacy->m_Meshes.push_back(std::move(legacyMesh));
	}
	legacy->m_numTotalMeshes = static_cast<int>(legacy->m_Meshes.size());

	// ── 재질: 변환 정본(ConvertToLegacyMaterial) + runtime finalize ─────────
	legacy->m_Materials.reserve(source.Materials().size());
	for (const experiment::Material& material : source.Materials())
	{
		auto legacyMaterial = std::make_shared<Material>();
		// 모델 재질의 keywords는 인덱스 표기뿐이라 meta 없이 변환 가능하다.
		// 이름 keywords가 실려 있으면 fail-closed로 떨어진다(짐작 금지).
		if (!ExperimentMaterialMigration::ConvertToLegacyMaterial(material,
			nullptr, *legacyMaterial, outError))
		{
			outError = "모델 재질 변환 실패(" + material.name + "): "
				+ outError;
			return false;
		}
		FinalizeMaterialRuntime(*legacyMaterial);
		legacy->m_Materials.push_back(std::move(legacyMaterial));
	}

	// ── 스켈레톤·애니메이션·AnimatorData ────────────────────────────────────
	if (const experiment::Skeleton* skeleton = source.TryGetSkeleton())
	{
		legacy->m_Skeleton = BuildLegacySkeleton(*skeleton);
		legacy->m_hasBones = true;
		legacy->m_animator = new AnimatorData();
		legacy->m_animator->m_Skeleton = legacy->m_Skeleton;
		if (const experiment::AnimatorData* animator = source.TryGetAnimator())
		{
			legacy->m_animator->m_Motion.m_guid = animator->motionAssetId.value;
		}
	}

	outModel = std::move(legacy);
	outError.clear();
	return true;
}
