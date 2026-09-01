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
// 함수는 DataSystem 멤버로만 시공 가능하고, 그래서 별도 TU에 정의한다.
// (D1a 당시 이 줄은 Mesh에 대해 틀렸다 — friend는 ModelLoader·MeshOptimizer
//  뿐이었고, D4f-1이 바운드 주입 창구로 DataSystem을 들이며 사실이 됐다.)
//
// 알려진 표현 격차(둘 다 관측 가능해야 한다 — outError가 아니라 시공 규약):
//   - bitangent: legacy는 저장, experiment는 tangent.w(handedness)만 저장.
//     cross(normal, tangent.xyz) * w 재구성 — V2가 무손실이라 주장한 그 계약.
//   - Step 보간: legacy NodeAnimation에는 보간 모드가 없어 Linear로 강등된다
//     (실측: 현 코퍼스의 Step 트랙은 전부 상수 트랙이라 시각 손실 0).
//   - uv1 부재 시 uv0 복사 — legacy 임포터(ConvertToAiMesh)의 규약.
#include "DataSystem.h"
#include "Experiment/Model.h"
#include "Experiment/AnimationClipMetrics.h" // I5-D5b: totalKeyFrames 정본
#include "Experiment/Cooked/CookedAssetCatalog.h" // I7-C1
#include "Experiment/Cooked/ModelCookIdentity.h" // I2-E: 임베디드 신원
#include "Experiment/Cooked/CookedModelCodec.h"
#include "Experiment/Cooked/ResolvingModelDecoder.h"
#include "Experiment/Import/ImporterModelDecoder.h"
#include "ExperimentMaterialMigration.h"
#include "Material.h"
#include "Mesh.h"
#include "Model.h"
#include "RHI/IRenderDeviceServices.h" // I5-D34a: RHIExperimentVertexView
#include "Skeleton.h"
#include "StandardMaterialProperty.h"

#include <mathematics/vector3.hpp>

#include <cmath>
#include <cstdio>
#include <cstdlib> // I5-D34a: A/B 스위치 getenv
#include <fstream> // I7-C1: manifest 읽기
#include <string>
#include <unordered_set> // I7-C2: stale 집합
#include <vector>

namespace
{
	// A/B 판정 스위치(부팅 고정): CREATOR_EXPERIMENT_VERTEX=0이면 전부 legacy
	// 96B로 올린다. 같은 빌드에서 픽셀 diff 0을 재고 변이의 이빨을 증명하는
	// 유일한 창구다 — 전환기와 함께 I6에서 은퇴한다. D4b부터 lookup 경로와
	// 핸들 경로(프록시 바인딩)가 같은 스위치를 본다 — 한쪽만 꺼지면 A/B
	// 대조군이 반쪽이 된다.
	//
	// ★ I5-D4f-1에서 뜻이 하나 넓어졌다 — 이 스위치는 이제 **역브리지의 legacy
	// 정점 시공**까지 가른다. on(기본)은 experiment packed만 올리므로 96B 배열을
	// 짓지 않고, off는 대조군을 위해 예전처럼 짓는다. 넓히지 않으면 off가
	// 그릴 것을 잃어(예행 실측: 드로우 0·커버리지 0) A/B 자체가 무너진다.
	bool IsExperimentVertexEnabled()
	{
		static const bool enabled = []
		{
			const char* value = std::getenv("CREATOR_EXPERIMENT_VERTEX");
			return nullptr == value || '0' != value[0];
		}();
		return enabled;
	}

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
				animation.m_nodeAnimations.emplace(boneName,
					std::move(nodeAnimation));
			}
			// I5-D5b — 정본은 "유니크 키 시각 수"다(legacy 임포터 정의).
			// 여기서 채널 키 개수를 합산하던 것이 같은 이름의 다른 값을
			// 만들어, 로드 경로에 따라 이벤트 발화 시점이 갈렸다.
			animation.m_totalKeyFrames = experiment::clip::CountUniqueKeyTimes(clip);
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

	// ── 메시: 인덱스·재질 인덱스·바운드 시공(정점은 D4f-1에서 절단) ──
	legacy->m_Meshes.reserve(source.Meshes().size());
	// I5-D4f-1 — 정점 시공 절단. on(기본)에서 GPU 업로드는 experiment packed를
	// 직접 올리고(D34a lookup·D4b 핸들, 양 backend 가드는 D4f-0/D4f-1), legacy
	// 96B 배열의 제품 소비자는 0이다(D4f 정찰 전수). 그래서 짓지 않는다 —
	// 이 배열은 언팩 비용과 모델당 수 MB를 그대로 무는 사본이었다.
	// off(A/B 대조군)에서는 그것이 유일한 그림의 출처이므로 예전처럼 짓는다.
	const bool buildLegacyVertices = !IsExperimentVertexEnabled();
	for (const experiment::Mesh& mesh : source.Meshes())
	{
		std::vector<Vertex> vertices;
		if (buildLegacyVertices)
		{
			vertices.reserve(mesh.vertices.size());
			for (std::size_t index = 0; index < mesh.vertices.size(); ++index)
			{
				vertices.push_back(UnpackVertex(mesh.vertices, index));
			}
		}
		std::vector<uint32> indices(mesh.indices.begin(), mesh.indices.end());
		auto legacyMesh = std::make_shared<Mesh>(mesh.name,
			std::move(vertices), std::move(indices));
		// m_materialIndex·바운드는 private이다. D1a는 friend 목록을 안 건드리려고
		// reflect() 스키마 순회로 썼지만, D4f-1이 바운드 주입 때문에 결국
		// friend를 들였다(스키마에 바운드가 없어 그 수법이 통하지 않는다 —
		// Mesh.h 주석). 우회로만 남기면 이유가 거짓이 되므로 직접 쓴다.
		legacyMesh->m_materialIndex = mesh.material.IsValid()
			? mesh.material.Value() : 0u;
		// 바운드는 **experiment 정본을 직접 주입**한다(친구 창구 — Mesh.h 주석).
		// RecalculateBounds()는 정점을 요구하므로 절단 뒤에는 기본값(빈 AABB·
		// 반지름 0)을 남기고, 컬링·피킹·그림자 반경이 조용히 틀어진다.
		// off에서는 legacy 유도(정점→min/max)를 그대로 태워 A/B가 두 유도를
		// 대조하게 둔다 — 같은 값이어야 한다(experiment.meshbounds 축).
		if (buildLegacyVertices)
		{
			legacyMesh->RecalculateBounds();
		}
		else
		{
			legacyMesh->m_boundingBox = mesh.bounds;
			legacyMesh->m_boundingSphere = math::bounding_sphere(mesh.bounds);
		}
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
		// 전환기 보강 — assetId를 못 푼 텍스처 참조(소스 임포트 산물)는
		// fallbackPath 파일명을 legacy 이름 필드로 나른다. 변환 정본
		// (ConvertToLegacyMaterial)은 이름 부활 금지 규약이라 나르지 않지만,
		// legacy 파이프 소비가 목적인 이 브리지에서는 Finalize의 이름 폴백이
		// 텍스처를 실제로 붙드는 유일한 길이다. I6에서 브리지와 함께 죽는다.
		for (const experiment::MaterialProperty& property : material.properties)
		{
			const auto* reference =
				std::get_if<experiment::TextureReference>(&property.value);
			if (nullptr == reference || reference->assetId.IsValid()
				|| reference->fallbackPath.empty())
			{
				continue;
			}
			const std::string textureName =
				reference->fallbackPath.stem().string();
			if (property.name == standard_material::property::BaseColorMap)
				legacyMaterial->m_baseColorTexName = textureName;
			else if (property.name == standard_material::property::NormalMap)
				legacyMaterial->m_normalTexName = textureName;
			else if (property.name == standard_material::property::OrmMap)
				legacyMaterial->m_ORM_TexName = textureName;
			else if (property.name == standard_material::property::AoMap)
				legacyMaterial->m_AO_TexName = textureName;
			else if (property.name == standard_material::property::EmissiveMap)
				legacyMaterial->m_EmissiveTexName = textureName;
		}
		FinalizeMaterialRuntime(*legacyMaterial);
		legacy->m_Materials.push_back(std::move(legacyMaterial));
	}

	// ── 스켈레톤·애니메이션·AnimatorData ────────────────────────────────────
	if (const experiment::Skeleton* skeleton = source.TryGetSkeleton())
	{
		legacy->m_Skeleton = BuildLegacySkeleton(*skeleton);
		// D34b 크래시 진단으로 넣은 fail-closed — 루트 없는 스켈레톤이 씬에
		// 나가면 AnimationJob이 널 역참조로 죽는다(0x80). 여기서 막히면 로드가
		// legacy 폴백으로 가고 [model.dual]에 그 모델이 빠지는 것이 관측이다.
		if (nullptr == legacy->m_Skeleton->m_rootBone)
		{
			outError = "역브리지 스켈레톤에 루트 본이 없다(본 "
				+ std::to_string(legacy->m_Skeleton->m_bones.size()) + "개)";
			return false;
		}
		legacy->m_hasBones = true;
		legacy->m_animator = new AnimatorData();
		legacy->m_animator->m_Skeleton = legacy->m_Skeleton;
		if (const experiment::AnimatorData* animator = source.TryGetAnimator())
		{
			legacy->m_animator->m_Motion.m_guid = animator->motionAssetId.value;
		}
		// D34b: m_Motion이 비면 씬 저장·재로드가 스켈레톤을 복원하지 못한다 —
		// Animator postLoad가 m_Motion guid로 LoadModelGUID를 불러 m_Skeleton을
		// 되찾는 구조라, nil이면 reflect가 만든 빈 스켈레톤(본 0)이 틱에 나가
		// 널 역참조로 죽었다(게이트 실측). 임포터 경로는 별도 모션 자산이 없어
		// 모델 자신이 모션의 출처다 — Assimp 경로와 같은 의미론으로 폴백한다.
		if (FileGuid{} == legacy->m_animator->m_Motion)
		{
			legacy->m_animator->m_Motion = legacy->guid;
		}
	}

	outModel = std::move(legacy);
	outError.clear();
	return true;
}

// ── I7-C1: cooked catalog 기동 ──────────────────────────────────────────
//
// 굽는 쪽은 D5-b2c에서 다 섰는데(AssetCooker → Derived/ + CEMF → pak) **읽는
// 쪽이 이어져 있지 않았다**: resolver는 `nullptr` catalog로 불렸고
// `ModelLoadRequest::cookedPath`는 늘 비어 있었다(실측 texCooked=0). 그래서
// cooked 경로가 제품에서 한 번도 돌지 않았다 — 이 함수가 그 입구다.
bool DataSystem::MountCookedCatalog(const file::path& derivedRoot,
	std::string& outError)
{
	outError.clear();
	const file::path manifestPath =
		derivedRoot / "Derived" / "asset-manifest.cemf";
	std::error_code errorCode;
	if (!file::is_regular_file(manifestPath, errorCode))
	{
		// 미게시는 실패가 아니다 — 에디터 작업 트리에는 Derived가 없다.
		// 조용히 두면 resolver·모델 로드가 예전처럼 source로 간다.
		std::lock_guard lock(m_cookedCatalogMutex);
		m_cookedCatalog.reset();
		m_cookedStaleAssets.clear();
		return false;
	}

	std::vector<std::byte> bytes;
	{
		std::ifstream input(manifestPath, std::ios::binary);
		if (!input)
		{
			outError = "manifest를 열 수 없다: " + manifestPath.string();
			return false;
		}
		input.seekg(0, std::ios::end);
		const std::streamoff size = input.tellg();
		input.seekg(0, std::ios::beg);
		bytes.resize(static_cast<std::size_t>(size));
		if (size > 0)
		{
			input.read(reinterpret_cast<char*>(bytes.data()), size);
			if (!input)
			{
				outError = "manifest 읽기가 끊겼다: " + manifestPath.string();
				return false;
			}
		}
	}

	std::vector<experiment::cooked::AssetManifestIssue> issues;
	// Load는 실패하면 **빈 catalog**를 준다(부분 표를 내놓지 않는 계약).
	auto catalog = std::make_shared<experiment::cooked::CookedAssetCatalog>(
		experiment::cooked::CookedAssetCatalog::Load(bytes, derivedRoot, issues));
	if (catalog->IsEmpty())
	{
		outError = "catalog가 비었다(entry 0) — manifest 손상 또는 빈 게시";
		for (const auto& issue : issues)
		{
			outError += " | " + issue.context + ": " + issue.message;
		}
		std::lock_guard lock(m_cookedCatalogMutex);
		m_cookedCatalog.reset();
		m_cookedStaleAssets.clear();
		return false;
	}

	// ── I7-C2: 신선도 판정 ──
	//
	// 소스가 아티팩트보다 새로우면 그 entry는 낡았다. 소스가 아예 없으면
	// (게시된 배포) 비교 대상이 없으므로 신선한 것으로 둔다 — 그 경우 신선도는
	// 빌드가 보증한다. registry가 GUID를 못 푸는 entry(모델 안의 subasset 등)도
	// 같은 이유로 건너뛴다: 그 위험은 부모 모델 entry가 대신 진다(부모가 낡으면
	// 모델이 source로 가고, 그러면 subasset artifact를 아무도 안 본다).
	std::unordered_set<FileGuid> stale;
	for (const experiment::cooked::CookedAssetManifestEntry& entry
		: catalog->Entries())
	{
		FileGuid guid;
		guid.m_guid = entry.assetId.value;
		const file::path sourcePath = GetFilePath(guid);
		if (sourcePath.empty()) continue;
		std::error_code sourceError;
		if (!file::is_regular_file(sourcePath, sourceError)) continue;
		const file::path artifact = catalog->ResolveArtifactPath(entry.assetId);
		std::error_code artifactError;
		if (artifact.empty()
			|| !file::is_regular_file(artifact, artifactError))
		{
			// 표에는 있는데 파일이 없다 — 낡음보다 나쁘다. 같이 끊는다.
			stale.insert(guid);
			continue;
		}
		const auto sourceTime = file::last_write_time(sourcePath, sourceError);
		const auto artifactTime = file::last_write_time(artifact, artifactError);
		if (sourceError || artifactError) continue;
		if (artifactTime < sourceTime) stale.insert(guid);
	}

	const std::size_t entryCount = catalog->Size();
	const std::size_t staleCount = stale.size();
	{
		std::lock_guard lock(m_cookedCatalogMutex);
		m_cookedCatalog = std::move(catalog);
		m_cookedStaleAssets = std::move(stale);
	}
	std::printf("[cooked.catalog] mount %s entries=%zu stale=%zu\n",
		manifestPath.string().c_str(), entryCount, staleCount);
	return true;
}

std::shared_ptr<const experiment::cooked::CookedAssetCatalog>
DataSystem::GetCookedCatalog() const
{
	std::lock_guard lock(m_cookedCatalogMutex);
	return m_cookedCatalog;
}

file::path DataSystem::ResolveCookedArtifact(
	const experiment::AssetId& assetId) const
{
	std::lock_guard lock(m_cookedCatalogMutex);
	if (!m_cookedCatalog) return {};
	FileGuid probe;
	probe.m_guid = assetId.value;
	if (m_cookedStaleAssets.contains(probe)) return {};
	return m_cookedCatalog->ResolveArtifactPath(assetId);
}

std::size_t DataSystem::CookedCatalogStaleCount() const
{
	std::lock_guard lock(m_cookedCatalogMutex);
	return m_cookedStaleAssets.size();
}

std::size_t DataSystem::CookedCatalogEntryCount() const
{
	const auto catalog = GetCookedCatalog();
	return catalog ? catalog->Size() : 0u;
}

std::shared_ptr<Model> DataSystem::LoadModelViaExperiment(
	FileGuid guid, const file::path& sourcePath)
{
	namespace exi = experiment::importer;

	exi::ImporterDecoderOptions options;
	// 모델 정체성은 .meta 소유 — 호출자가 이미 GUID로 왔으므로 그대로 준다.
	options.resolveModelAsset =
		[guid](const std::filesystem::path&) -> experiment::AssetId
		{
			experiment::AssetId id;
			id.value = guid.m_guid;
			return id;
		};
	// I2-E — 임베디드 텍스처의 신원은 **모델 sidecar가 정본**이다.
	// 예전에는 `sourcePath`가 빈 임베디드 텍스처가 nil GUID로 떨어져 변환
	// 경계가 property를 통째로 생략했다(§1.4의 "재질당 ×3 생략"). cook 경로는
	// D5-b1이 `subAssets.embeddedTextures`의 UUIDv4로 이미 풀고 있었는데
	// **소스 로드 경로만 그 표를 안 읽고 있었다** — 같은 정본 함수
	// (ReadModelCookIdentity)를 여기서도 쓴다.
	//
	// sidecar가 없거나 표가 비면 예전대로 nil이고, 브리지의 fallbackPath 이름
	// 폴백이 받는다(전환기 보강 — 그 경로를 없애지 않는다).
	experiment::cooked::ModelCookIdentity identity;
	{
		file::path metaPath = sourcePath;
		metaPath += ".meta";
		std::ifstream metaInput(metaPath);
		if (metaInput)
		{
			const std::string yaml{
				std::istreambuf_iterator<char>(metaInput),
				std::istreambuf_iterator<char>() };
			std::vector<experiment::cooked::ModelIdentityIssue> identityIssues;
			if (!experiment::cooked::ReadModelCookIdentity(yaml, identity,
				identityIssues))
			{
				identity = {};
			}
		}
	}

	// 외부 텍스처 정체성도 .meta 정본이다 — 원본 경로를 카탈로그로 되물어
	// GUID를 준다(이름 부활 금지).
	options.conversion.resolveTextureAsset =
		[this, identity](const exi::ImportedTexture& texture)
			-> experiment::AssetId
		{
			if (texture.IsEmbedded())
			{
				return identity.FindEmbeddedTexture(texture.sourceKey);
			}
			experiment::AssetId id{};
			if (!texture.sourcePath.empty())
			{
				id.value = GetFileGuid(texture.sourcePath).m_guid;
			}
			return id;
		};

	auto importerDecoder =
		std::make_unique<exi::ImporterModelDecoder>(std::move(options));
	if (!importerDecoder->CanDecode(sourcePath))
	{
		return nullptr; // 임포터 포맷 밖 — 조용히 legacy 경로로.
	}

	experiment::ModelLoader loader(
		std::make_unique<experiment::cooked::ResolvingModelDecoder>(
			std::make_unique<experiment::cooked::CookedModelDecoder>(),
			std::move(importerDecoder)));
	experiment::ModelLoadRequest request;
	request.sourcePath = sourcePath;
	// I7-C1 — catalog가 서 있으면 cooked artifact 경로를 채운다. 없으면 빈
	// 경로 그대로이고 resolver가 Info로 계수한 뒤 source로 간다(그 정책은
	// ResolvingModelDecoder가 이미 갖고 있었다 — 여기서 바꾸지 않는다).
	//
	// ★ 신선도(cooked가 source보다 낡았는가)는 **아직 판정하지 않는다**.
	//   그래서 게시된 Derived가 있는 배포에서만 cooked를 탄다 — 저작 트리에는
	//   Derived가 없어 이 줄이 무동작이다. 신선도 정책은 I7의 남은 항목이다.
	if (const auto catalog = GetCookedCatalog())
	{
		experiment::AssetId modelAsset;
		modelAsset.value = guid.m_guid;
		request.cookedPath = ResolveCookedArtifact(modelAsset);
	}
	experiment::ModelLoadResult result = loader.Load(request);

	// 관측 — Warning 이상만 요약한다(빈 cookedPath의 Info 계수는 기본 상태라
	// 로그 스팸이 된다).
	std::size_t warningCount = 0;
	for (const experiment::ModelLoadIssue& issue : result.issues)
	{
		if (experiment::ModelLoadIssueSeverity::Warning == issue.severity
			|| experiment::ModelLoadIssueSeverity::Error == issue.severity)
		{
			++warningCount;
		}
	}
	if (!result.Succeeded())
	{
		Debug->LogWarning("[model.dual] experiment 로드 실패 — Assimp 폴백: "
			+ sourcePath.string() + " (경고/오류 "
			+ std::to_string(warningCount) + "건)");
		return nullptr;
	}

	std::shared_ptr<Model> legacyModel;
	std::string error;
	if (!BuildLegacyModelFromExperiment(*result.model, legacyModel, error))
	{
		Debug->LogWarning("[model.dual] 역브리지 실패 — Assimp 폴백: "
			+ sourcePath.string() + " (" + error + ")");
		return nullptr;
	}
	if (warningCount > 0)
	{
		Debug->LogWarning("[model.dual] experiment 로드 경고 "
			+ std::to_string(warningCount) + "건: " + sourcePath.string());
	}
	// I5-D34a — 병행 바인딩: 역브리지가 legacy만 남기면 packed 정점의 출처가
	// 사라지므로, 렌더 캐시가 experiment 정점을 집을 수 있게 신원을 잇는다.
	// 역브리지는 source.Meshes() 순서 그대로 legacy 메시를 만들므로 인덱스가
	// 1:1이다 — 그 대응이 깨지면 아래 계수 불일치로 등록을 통째로 건너뛴다
	// (일부만 잇는 것보다 전부 legacy 폴백이 낫다).
	if (legacyModel->m_Meshes.size() == result.model->Meshes().size())
	{
		std::lock_guard lock(m_experimentMeshMutex);
		for (std::size_t index = 0; index < legacyModel->m_Meshes.size(); ++index)
		{
			m_experimentMeshBindings[legacyModel->m_Meshes[index]->m_hashingMesh] =
				ExperimentMeshBinding{ result.model,
					static_cast<std::uint32_t>(index) };
		}
		// I5-D4c — 모델 GUID로도 잇는다(씬 postLoad의 이름 해석 정본 창구).
		// 계수 불일치면 메시 바인딩과 함께 생략된다 — 반쪽 등록 금지.
		m_experimentModels[guid] = result.model;
	}
	else
	{
		Debug->LogWarning("[model.dual] 메시 계수 불일치 — 병행 바인딩 생략: "
			+ sourcePath.string());
	}

	// 성공도 관측한다 — 폴백만 로그하면 "전부 폴백"과 "전부 experiment"가
	// 같은 침묵이 된다(눈먼 초록). printf는 CLI 게이트 stdout에 잡히는
	// 채널이라 게이트가 경로를 실증할 수 있다. 전환기와 함께 I6에서 죽는다.
	//
	// I7-C1 — cooked를 **실제로 탔는가**까지 가른다. cookedPath가 채워진 것과
	// cooked가 draft를 낸 것은 다르다: 디코더가 거부하면 조용히 source로
	// 폴백하고(그 정책은 옳다) 계수만 보면 둘이 같아 보인다.
	const bool cookedAttempted = !request.cookedPath.empty();
	bool cookedFellBack = false;
	for (const experiment::ModelLoadIssue& issue : result.issues)
	{
		if (experiment::ModelLoadIssueCode::CookedFallbackToSource == issue.code)
		{
			cookedFellBack = true;
			break;
		}
	}
	std::printf("[model.dual] %s 경로: %s\n",
		(cookedAttempted && !cookedFellBack) ? "cooked" : "experiment",
		sourcePath.filename().string().c_str());
	return legacyModel;
}

namespace
{
	// I5-D4b — 캐시 안정 키: 자산 신원(assetId 16바이트)과 메시 인덱스의
	// FNV-1a 64. legacy m_hashingMesh(make_guid 랜덤)와 같은 64비트 키 공간을
	// 공유하며 충돌 무시 가정도 같다. 0은 '핸들 아님' 표지라 회피한다.
	size_t HashExperimentMeshKey(
		const experiment::AssetId& assetId, std::uint32_t meshIndex)
	{
		std::uint64_t hash = 1469598103934665603ull;
		const auto mix = [&hash](const void* data, size_t size)
		{
			const auto* bytes = static_cast<const unsigned char*>(data);
			for (size_t i = 0; i < size; ++i)
			{
				hash ^= bytes[i];
				hash *= 1099511628211ull;
			}
		};
		mix(assetId.value.data.data(), assetId.value.data.size());
		mix(&meshIndex, sizeof(meshIndex));
		if (0 == hash) hash = 1;
		return static_cast<size_t>(hash);
	}
}

bool DataSystem::TryGetExperimentVertexView(
	const Mesh& mesh, RHIExperimentVertexView& outView)
{
	if (!IsExperimentVertexEnabled()) return false;

	std::lock_guard lock(m_experimentMeshMutex);
	const auto found = m_experimentMeshBindings.find(mesh.m_hashingMesh);
	if (found == m_experimentMeshBindings.end()) return false;

	// D34b: 스킨 마스크 개방 — GBuffer/Shadow가 스킨 레이아웃 PSO 축(uint4
	// BLENDINDICES)을 갖췄다. D4b: 뷰 시공은 핸들 경로와 같은 정본 함수 하나다
	// — lookup 폴백과 핸들 경로의 데이터가 갈릴 여지를 없앤다.
	return BuildExperimentVertexView(*found->second.model,
		found->second.meshIndex, outView);
}

std::shared_ptr<const experiment::Model> DataSystem::TryGetExperimentModel(
	FileGuid modelGuid)
{
	if (!IsExperimentVertexEnabled()) return nullptr;
	if (FileGuid{} == modelGuid) return nullptr;

	std::lock_guard lock(m_experimentMeshMutex);
	const auto found = m_experimentModels.find(modelGuid);
	return found != m_experimentModels.end() ? found->second : nullptr;
}

bool DataSystem::TryGetExperimentMeshBinding(const Mesh& mesh,
	std::shared_ptr<const experiment::Model>& outModel,
	std::uint32_t& outMeshIndex)
{
	if (!IsExperimentVertexEnabled()) return false;

	std::lock_guard lock(m_experimentMeshMutex);
	const auto found = m_experimentMeshBindings.find(mesh.m_hashingMesh);
	if (found == m_experimentMeshBindings.end()) return false;
	if (nullptr == found->second.model) return false;

	outModel = found->second.model;
	outMeshIndex = found->second.meshIndex;
	return true;
}

bool DataSystem::BuildExperimentVertexView(
	const experiment::Model& model, std::uint32_t meshIndex,
	RHIExperimentVertexView& outView)
{
	const experiment::Mesh* source = model.TryGetMesh(
		experiment::MeshIndex{ meshIndex });
	if (nullptr == source) return false;

	const experiment::VertexBuffer& vertices = source->vertices;
	const std::span<const std::byte> bytes = vertices.Bytes();
	outView.data = bytes.data();
	outView.bytes = bytes.size();
	outView.stride = vertices.Stride();
	outView.attributeMask = vertices.AttributeMask();
	outView.indexData = source->indices.data();
	outView.indexCount = static_cast<std::uint32_t>(source->indices.size());
	outView.stableKey = HashExperimentMeshKey(
		model.Metadata().assetId, meshIndex);
	return outView.IsHandleComplete();
}
