#include "MeshRenderer.h"
#include "AuthoringNodeViewAccess.h" // D3-a-4
#include "ReflectionYml.h"
#include "DataSystem.h"
#include "ProfileScope.h"
#include <optional>
#include "Entity.h"
#include "Material.h"
#include "SceneManager.h"
#include "RenderScene.h"
#include "Scene.h"
#include "Experiment/MaterialAuthoringCodec.h" // S2c-2a: override 값 표기 정본
#include "ExperimentMaterialMigration.h" // S2c-2a: diff 타이핑·override 적용
#include "Experiment/MaterialInstance.h" // I5-D5c1: 재질 병행 표현
#include "Assets/ModelMeshDescriptor.h"
#include "Assets/ModelAssetGeneration.h" // PHASE 3.75 MBC7: typed 정본
#include <mathematics/transform.hpp>

#include <algorithm>
#include <limits>
#include "ModelConsumptionDiagnostics.h" // MBC10: 읽기 전용 계수(무조건 stdout 출력 제거)

namespace
{
	// I5-M5 S2c-2a — base 참조(ref) 표기의 읽기.
	//
	//   m_Material:
	//     ref: <base 재질 자산 GUID>
	//     blendMode: transparent            # 선택 — base와 다를 때만
	//     keywordSelections: [1, 0]         # 선택 — base와 다를 때만(전체 표기)
	//     overrides:                        # 선택 — 인스턴스 diff(코덱 값 표기)
	//       - name: roughness
	//         float: 0.25
	//
	// base를 로드해 소유 사본을 만들고 override를 겹친다. 실패는 부분 결과를
	// 내지 않는다 — 지어낸 재질로 조용히 그리는 것보다 빈 재질이 낫다.
	// I5-D5c1 — override를 legacy와 experiment 인스턴스에 **한 번의 파싱으로**
	// 함께 얹는다. 두 번 파싱하면 backend 탈출구가 하나 더 생겨 D3-b 래칫이
	// 역행한다 — 실제로 그렇게 짰다가 게이트가 잡았다(래칫은 raw 텍스트를
	// 세므로 주석에 그 함수 이름을 적는 것만으로도 계수가 는다).
	// outInstance는 선택이다(nullptr이면 legacy만).
	[[nodiscard]] bool DecodeMaterialReferenceNode(
		const Authoring::ReadNode materialNode, own::shared_owner<Material>& outMaterial,
		FileGuid& outBaseGuid, std::string& outError,
		experiment::MaterialInstance* outInstance = nullptr,
        std::span<const own::shared_owner<const Texture>> preparedTextures = {})
	{
		const Authoring::ReadNode ref = materialNode["ref"];
		if (!ref || !ref.IsScalar())
		{
			outError = "ref가 스칼라가 아니다";
			return false;
		}
		const FileGuid baseGuid(ref.AsString());
		if (FileGuid{} == baseGuid)
		{
			outError = "ref가 nil GUID다";
			return false;
		}
        const auto base = DataSystems->LoadMaterialByGuid(baseGuid);
        if (!base)
        {
            outError = "Base material requires completed asynchronous preparation: " + baseGuid.ToString();
            return false;
        }

		auto owned = own::make_shared<Material>(*base);
		if (const Authoring::ReadNode blend = materialNode["blendMode"];
			blend && blend.IsScalar())
		{
            if (blend.AsString() != "opaque" && blend.AsString() != "masked"
                && blend.AsString() != "transparent")
            { outError = "Unknown material alpha mode"; return false; }
			owned->m_renderingMode = blend.AsString() == "transparent"
				? MaterialRenderingMode::Transparent
                : blend.AsString() == "masked" ? MaterialRenderingMode::Masked
                : MaterialRenderingMode::Opaque;
		}
		if (const Authoring::ReadNode selections = materialNode["keywordSelections"];
			selections && selections.IsSequence())
		{
			std::vector<std::uint16_t> values;
			values.reserve(selections.Size());
			for (const Authoring::ReadNode selection : selections)
			{
                if (!selection.IsScalar())
                {
                    outError = "Keyword selection must be an unsigned scalar.";
                    return false;
                }
                const auto value = selection.As<std::uint32_t>();
                if (value > (std::numeric_limits<std::uint16_t>::max)())
                {
                    outError = "Keyword selection exceeds uint16 range.";
                    return false;
                }
                values.push_back(static_cast<std::uint16_t>(value));
			}
            if (outInstance && outInstance->Base() && outInstance->Base()->assetOrigin)
            {
                const auto program = outInstance->Base()->assetOrigin->codeProgram;
                if (!program || values.size() != program->meta.keywords.size())
                {
                    outError = "Cooked keyword override count differs from its program.";
                    return false;
                }
                for (std::size_t axis = 0; axis < values.size(); ++axis)
                {
                    if (values[axis] >= program->meta.keywords[axis].values.size())
                    {
                        outError = "Cooked keyword override is outside its exact program domain.";
                        return false;
                    }
                }
            }
            if (outInstance && outInstance->Base())
            {
                outInstance->SetKeywordSelectionOverrides(values);
            }
			owned->m_keywordSelections = std::move(values);
		}
        std::vector<experiment::MaterialProperty> propertyOverrides;
		if (const Authoring::ReadNode overrides = materialNode["overrides"];
			overrides && overrides.IsSequence())
		{
			for (const Authoring::ReadNode entry : overrides)
			{
				if (!entry.IsMap() || !entry["name"]
					|| !entry["name"].IsScalar())
				{
					outError = "override 항목에 name이 없다";
					return false;
				}
				experiment::MaterialProperty property;
				property.name = entry["name"].AsString();
				if (!experiment::DeserializeMaterialPropertyValue(
						entry, property.name, property.value, outError)
					|| !ExperimentMaterialMigration::ApplyPropertyToLegacy(
						*owned, property, outError))
				{
					return false;
				}
                propertyOverrides.push_back(property);
				// 같은 값을 experiment 인스턴스에도 얹는다 — 두 표현이 같은
				// 파싱 결과를 공유해야 병행 대조가 의미를 갖는다.
				if (nullptr != outInstance
					&& !outInstance->SetPropertyOverride(property.name,
						property.value))
				{
					outError = "override 설치 거부: " + property.name;
					return false;
				}
			}
		}

        if (outInstance && !outInstance->BindPreparedTextureOwners(preparedTextures, outError))
        {
            return false;
        }
        if (owned->GetAssetOrigin() && !owned->HasMaterialGraph())
        {
            if (!DataSystems->RebuildCookedMaterialInstance(*owned, outError, preparedTextures, propertyOverrides))
            {
                return false;
            }
        }
        else
        {
            DataSystems->FinalizeMaterialRuntime(*owned);
        }
		outMaterial = std::move(owned);
		outBaseGuid = baseGuid;
		return true;
	}

	// I5-M5 S2c-2a — base 참조(ref) 표기의 쓰기. 현 재질과 base를 같은 meta로
	// experiment 변환해 diff만 남긴다. base에만 있는 저작은 참조 표기로
	// "되돌림"을 표현할 수 없으므로 실패(인라인 폴백)다. variant 값 비교는
	// 정본 코덱의 ryml 인코딩 텍스트로 한다 — 수학 타입에 operator==가 없다.
	[[nodiscard]] bool BuildMaterialReferenceNode(const Material& current,
		FileGuid baseGuid, Authoring::WriteNode outNode, std::string& outError)
	{
        const auto base = DataSystems->LoadMaterialByGuid(baseGuid);
        if (!base)
        {
            outError = "Base material requires completed asynchronous preparation: " + baseGuid.ToString();
            return false;
        }
        std::string metaError;
        ShaderMetaHandle handle;
        own::shared_owner<const ShaderMeta> meta;
        if (const auto origin = current.GetAssetOrigin(); origin && origin->codeProgram)
        {
            meta = origin->codeProgram->assetOrigin->shaderMetadata;
            handle = origin->codeProgram->codeHandle;
        }
        else
        {
            meta = DataSystems->LoadShaderMetaOwner(current.m_shaderMetaGuid, handle, metaError);
        }
        if (!meta)
        {
            outError = "Material reference typing requires prepared metadata: " + metaError;
            return false;
        }
        experiment::Material currentConverted;
        experiment::Material baseConverted;
        if (!ExperimentMaterialMigration::ConvertLegacyMaterial(current, *meta, currentConverted, outError)
            || !ExperimentMaterialMigration::ConvertLegacyMaterial(*base, *meta, baseConverted, outError))
        {
            return false;
        }
        if (meta->codeProgram)
        {
            const auto preserveColors = [](const Material& source, experiment::Material& converted)
            {
                for (auto& property : converted.properties)
                {
                    auto* reference = std::get_if<experiment::TextureReference>(&property.value);
                    const auto owner = reference ? source.GetTextureMapShared(property.name) : nullptr;
                    if (!reference || !owner)
                    {
                        continue;
                    }
                    const auto format = owner->GetImageDescription().Format();
                    const bool srgb = format == RHIFormat::RGBA8UnormSrgb || format == RHIFormat::BGRA8UnormSrgb
                        || format == RHIFormat::BC1UnormSrgb || format == RHIFormat::BC3UnormSrgb;
                    reference->colorSpace = srgb ? experiment::TextureColorSpace::Srgb : experiment::TextureColorSpace::Linear;
                }
            };
            preserveColors(current, currentConverted);
            preserveColors(*base, baseConverted);
        }

		const auto findByName = [](const experiment::Material& material,
			const std::string& name) -> const experiment::MaterialProperty*
		{
			const auto found = std::find_if(material.properties.begin(),
				material.properties.end(),
				[&](const experiment::MaterialProperty& candidate)
				{
					return candidate.name == name;
				});
			return found == material.properties.end() ? nullptr : &*found;
		};
		for (const experiment::MaterialProperty& baseProperty :
			baseConverted.properties)
		{
			if (nullptr == findByName(currentConverted, baseProperty.name))
			{
				outError = "base에만 있는 저작(" + baseProperty.name
					+ ") — 참조 표기는 되돌림을 표현하지 못한다";
				return false;
			}
		}

		const auto encodeValueText = [&outError](
			const experiment::MaterialProperty& property, std::string& outText)
		{
			Authoring::WriteDocument document;
			const Authoring::WriteNode entry = document.Root();
			entry.SetMap();
			if (!experiment::SerializeMaterialPropertyValue(property,
				entry, outError))
			{
				return false;
			}
			outText = document.Dump();
			return true;
		};

		Authoring::WriteDocument staging;
		const Authoring::WriteNode result = staging.Root();
		result.SetMap();
		result.Child("ref").SetScalar(baseGuid.ToString());
		Authoring::WriteNode overrides;
		for (const experiment::MaterialProperty& property :
			currentConverted.properties)
		{
			if (const experiment::MaterialProperty* baseProperty =
				findByName(baseConverted, property.name))
			{
				std::string currentText;
				std::string baseText;
				if (!encodeValueText(property, currentText)
					|| !encodeValueText(*baseProperty, baseText))
				{
					return false;
				}
				if (currentText == baseText)
				{
					continue;
				}
			}

			if (!overrides)
			{
				overrides = result.Child("overrides");
				overrides.SetSequence();
			}
			const Authoring::WriteNode entry = overrides.Append();
			entry.SetMap();
			entry.Child("name").SetScalar(property.name);
			if (!experiment::SerializeMaterialPropertyValue(property,
				entry, outError))
			{
				return false;
			}
		}

		if (currentConverted.blendMode != baseConverted.blendMode)
		{
			result.Child("blendMode").SetScalar(
				experiment::MaterialBlendMode::Transparent
					== currentConverted.blendMode
				? "transparent" : experiment::MaterialBlendMode::Masked == currentConverted.blendMode
                ? "masked" : "opaque");
		}
		if (currentConverted.keywordSelections
			!= baseConverted.keywordSelections)
		{
			const Authoring::WriteNode selections =
				result.Child("keywordSelections");
			selections.SetSequence(true);
			for (const std::uint16_t value : currentConverted.keywordSelections)
			{
				selections.Append().SetScalar(static_cast<std::uint32_t>(value));
			}
		}
		outNode.Assign(result);
		outError.clear();
		return true;
	}
}

MeshRenderer::MeshRenderer()
{
}

MeshRenderer::~MeshRenderer() = default;

void MeshRenderer::ReleaseManagedResources()
{
    m_meshRequest.Cancel();
    m_meshRequest = {};
    m_meshRequested = false;
}

void MeshRenderer::OnInitialized()
{
    EnsureMeshBinding();
    auto scene = GetOwner()->m_ownerScene;
    auto renderScene = SceneManagers->GetRenderScene();
    if (scene)
    {
        scene->CollectMeshRenderer(this);
        renderScene->RegisterCommand(this);
    }

    // 옥트리 등록이 여기 있었다. 질의하던 쪽(Scene의 카메라별 컬링)이
    // RenderSceneViewPlan ③에서 사라져 쓰기 전용 자료구조가 됐고, 그래서
    // 옥트리 계통을 통째로 걷었다. 컬링은 이제 렌더 쪽 뷰가 프록시의 월드
    // AABB로 한다 — 가속 구조가 다시 필요해지면 그때는 보유층(RenderScene)에
    // 두는 것이 자리다(선형 검사가 실측으로 느려진 뒤에).
    if (!m_isSkinnedMesh)
    {
        m_isNeedUpdateCulling = true;
    }
}

void MeshRenderer::OnAddedToScene()
{
	if (!HasLifecycleState(State_Initialized) || !GetOwner()) return;
	if (Scene* scene = GetOwner()->GetScene())
	{
		scene->CollectMeshRenderer(this);
		if (auto* renderScene = SceneManagers->GetRenderScene())
			renderScene->RegisterCommand(this);
	}
}

void MeshRenderer::OnRemovingFromScene()
{
	if (!GetOwner() || GetOwner()->IsDestroyMark()) return;
	if (Scene* scene = GetOwner()->GetScene())
	{
		scene->UnCollectMeshRenderer(this);
		if (auto* renderScene = SceneManagers->GetRenderScene())
			renderScene->UnregisterCommand(this);
	}
}

void MeshRenderer::OnUninitializing()
{
    ReleaseManagedResources();
    auto scene = GetOwner() ? GetOwner()->m_ownerScene : nullptr;
    auto renderScene = SceneManagers->GetRenderScene();
	if (scene)
	{
		scene->UnCollectMeshRenderer(this);
        if (renderScene)
        {
            renderScene->UnregisterCommand(this);
        }
	}

}

// I5-D5c1 — 재질 병행 표현의 base 설치. base가 없으면 인스턴스도 없다
// (빈 인스턴스를 들고 있으면 "저작 원본이 있다"는 거짓 신호가 된다).
void MeshRenderer::SetExperimentMaterialBase(
    own::shared_owner<const experiment::Material> base)
{
    if (!base)
    {
        m_materialInstance.reset();
        return;
    }
    m_materialInstance =
        own::make_unique<experiment::MaterialInstance>(std::move(base));
}

math::aabb MeshRenderer::GetBoundingBox() const
{
    return GetBoundingBox(m_pOwner->Transform_().GetWorldMatrix());
}

math::aabb MeshRenderer::GetBoundingBox(const math::matrix4x4& world) const
{
    if (m_meshDescriptor)
    {
        return math::transform(m_meshDescriptor->bounds, world);
    }
    // typed 정본의 바운드(immutable aggregate가 소유). MBC9: legacy Mesh 바운드 폴백은
    // 은퇴했다 — generation이 없으면 빈 상자다.
    if (m_modelGeneration
        && m_modelMeshIndex < m_modelGeneration->Meshes().size())
    {
        return math::transform(
            m_modelGeneration->Meshes()[m_modelMeshIndex].bounds,
            world);
    }
    return math::aabb{};
}

void MeshRenderer::OnDeserialized(const Authoring::NodeView& view)
{
	const Authoring::ReadNode node = Authoring::NodeViewAccess::Node(view);
    const auto scenePins = GetOwner() && GetOwner()->GetScene()
        ? GetOwner()->GetScene()->PreparedTextureImagePins() : nullptr;
    const auto preparedTextures = scenePins ? scenePins->Owners() : std::vector<own::shared_owner<const Texture>>{};

    // Graph textures may be embedded in this model. Publish the immutable
    // model owner before resolving an inline graph instance's texture GUIDs.
    const AssetDepot::AssetLink<assets::ModelMeshDescriptor> meshLink{
        { experiment::AssetId{ m_meshAssetId.m_guid }, {} } };
    m_granularMeshBinding = meshLink.IsValid() && DataSystems->HasModelMeshDescriptor(meshLink);
    if (!m_granularMeshBinding && m_modelGuid != FileGuid{})
    {
        DataSystems->LoadModelAssetGeneration(m_modelGuid);
    }
    // 재질 해석 → 모델 묶기 단계를 캡처에 가른다.
    std::optional<ce::profile_scope> step{ std::in_place, ce::marker<"MeshRenderer.Material">() };
	// typed 역직렬화가 m_Material의 소유 인스턴스를 이미 만들었다. 예전 경로는
	// 이름으로 cache material을 꺼낸 뒤 scene snapshot을 그 공유 객체에 다시
	// Deserialize해 다른 renderer까지 바꿨다. snapshot 소유권은 유지하고 runtime
	// texture 복원만 DataSystem의 Material finalize 경계에 맡긴다.
	//
	// I5-M5 S2-a: m_Material 노드가 새 정본(schema+shaderAssetId)이면 typed
	// 역직렬화가 legacy 필드를 하나도 못 채운 상태다 — S1 이중화 경로로
	// 재해석한다. reflection의 shared_ptr 멤버는 컴파일 타임 재귀라
	// (ReflectionTypedYml.h EmitMember/ReadMember의 meta::reflectable 분기)
	// 타입 단위 후킹이 불가능하고, 그래서 소비자 postLoad가 절단선이다.
	if (const Authoring::ReadNode materialNode = node["m_Material"];
		materialNode && materialNode.IsMap() && materialNode["ref"])
	{
		// I5-M5 S2c-2a — base 참조 표기. typed 역직렬화는 ref 노드에서 기본값
		// 재질을 만들었을 뿐이다 — base 소유 사본+override로 교체한다.
		own::shared_owner<Material> resolved;
		FileGuid baseGuid;
		std::string error;
		// I5-D5c1 — base 저작 원본을 먼저 세운다. 그래야 아래 한 번의 파싱이
		// override를 legacy와 인스턴스 양쪽에 함께 얹는다(ref는 노드가 갖고
		// 있으므로 base GUID를 미리 읽는다).
		if (const Authoring::ReadNode ref = materialNode["ref"];
			ref && ref.IsScalar())
		{
			SetExperimentMaterialBase(DataSystems->LoadAuthoredMaterialShared(
				FileGuid(ref.AsString())));
		}
		if (DecodeMaterialReferenceNode(materialNode, resolved, baseGuid,
			error, GetMaterialInstance(), preparedTextures))
		{
			m_Material = std::move(resolved);
			m_materialBaseGuid = baseGuid;
		}
		else
		{
			Debug::PrintLog(spdlog::level::err, "MeshRenderer m_Material base 참조 해석 실패 — "
				"재질을 비운다: " + error);
			m_Material.reset();
			SetExperimentMaterialBase(nullptr); // 부분 상태를 남기지 않는다
		}
	}
    else if (const Authoring::ReadNode materialNode = node["m_Material"];
             materialNode && materialNode.IsMap() && materialNode["lattice_material"])
    {
        auto decoded = own::make_shared<Material>();
        if (DataSystems->DeserializeMaterialPayload(*decoded, Authoring::NodeViewAccess::Make(materialNode)))
        {
            m_Material = std::move(decoded);
        }
        else
        {
            Debug::PrintLog(spdlog::level::err, "MeshRenderer LX material decode failed; clearing the material.");
            m_Material.reset();
        }
        SetExperimentMaterialBase(nullptr);
    }
    else if (const Authoring::ReadNode materialNode = node["m_Material"];
             materialNode && materialNode.IsMap() && materialNode["schema"] && materialNode["shaderAssetId"])
    {
        auto decoded = own::make_shared<Material>();
        auto authored = own::make_shared<experiment::Material>();
		if (DataSystems->DeserializeMaterialPayload(*decoded,
			Authoring::NodeViewAccess::Make(materialNode), (authored ? &*authored.borrow() : nullptr), true, preparedTextures))
		{
			// FinalizeMaterialRuntime은 이중화 경로 안에서 이미 수행됐다.
			m_Material = std::move(decoded);
			// I5-D5c1 — 인라인 새 정본은 자기 문서가 곧 저작 원본이다
			// (override 없음 — 인라인은 전체 표기다).
			SetExperimentMaterialBase(std::move(authored));
		}
		else
		{
			Debug::PrintLog(spdlog::level::err, "MeshRenderer m_Material 새 정본 해석 실패 — "
				"typed 기본값 상태를 유지한다");
		}
    }
    else if (m_Material)
	{
		DataSystems->FinalizeMaterialRuntime(*m_Material);
		// I5-D5c1 — legacy 표기 문서에는 저작 원본이 없다. 여기서 legacy를
		// experiment로 변환해 채우면 "원본을 보관했다"는 거짓 신호가 되고
		// 왕복 손실이 병행 표현 안으로 들어온다 — 비워 둔다(게이트가 계수).
		SetExperimentMaterialBase(nullptr);
	}

	// I5-M5 S2c-1 — 모델 해석의 정본은 자기 m_modelGuid다. legacy 씬은 인라인
	// 재질의 m_fileGuid가 모델 GUID를 나르는 편법이라 폴백으로 읽고, 읽는 즉시
	// 자기 필드로 이주해 다음 저장부터 정본이 된다.
	// S2c-2a: base 링크 재질의 m_fileGuid는 **재질 자산** GUID라 모델 폴백
	// 대상이 아니다.
	//
	// PHASE 3.75 MBC9 — 모델의 유일한 런타임 표현은 ModelAssetGeneration이다.
	// legacy Model·experiment 병행 핸들·이름 폴백·전역 임베디드 등록부는 없다.
	// embedded texture owner는 BindModelGeneration이 generation closure에서 묶는다.
	if (FileGuid{} == m_modelGuid && m_Material && FileGuid{} == m_materialBaseGuid
		&& assets::IsUuidV8(m_Material->m_fileGuid.m_guid))
	{
		m_modelGuid = m_Material->m_fileGuid;
	}
	step.reset();
	step.emplace(ce::marker<"MeshRenderer.BindModel">());
    if (m_granularMeshBinding)
    {
        // A mounted v3 candidate, including a failed/unsupported one, never
        // falls through to source-path or CEMCv11 loading.
        EnsureMeshBinding();
        return;
    }
	const assets::ModelAssetGeneration::Shared generation =
		DataSystems->LoadModelAssetGeneration(m_modelGuid);

	// 구 씬은 메시를 이름으로 적었다(m_Mesh.m_name) — 영속 MeshId가 없으면 이름으로
	// 한 번 되찾아 m_meshAssetId로 이주한다. 새 씬은 MeshId만 적는다.
	const Authoring::ReadNode legacyMeshNode = node["m_Mesh"];
	const std::string meshName = (legacyMeshNode && legacyMeshNode["m_name"])
		? legacyMeshNode["m_name"].AsString() : std::string{};
	bool resolvedViaGeneration = false;
	if (generation)
	{
		// ① 영속 MeshId(UUIDv8 subasset). ② 이름 — semantic stable key와 같은 축이라
		// generation 안에서 유일할 때만 신원으로 인정한다(둘 이상이면 authoring key
		// 자산이고, 그 씬은 MeshId를 적어야 한다 — 짐작하지 않는다).
		const auto meshes = generation->Meshes();
		std::uint32_t meshIndex = assets::kInvalidModelAssetIndex;
		if (FileGuid{} != m_meshAssetId)
		{
			for (std::size_t index = 0; index < meshes.size(); ++index)
			{
				if (meshes[index].meshId != m_meshAssetId.m_guid) continue;
				meshIndex = static_cast<std::uint32_t>(index);
				break;
			}
			if (assets::kInvalidModelAssetIndex == meshIndex)
			{
				Debug::PrintLog(spdlog::level::warn, "MeshRenderer m_meshAssetId가 모델 generation에 없다 — "
					"이름으로 다시 찾는다: " + m_meshAssetId.ToString());
			}
		}
		if (assets::kInvalidModelAssetIndex == meshIndex && !meshName.empty())
		{
			std::size_t matches = 0;
			for (std::size_t index = 0; index < meshes.size(); ++index)
			{
				if (meshes[index].name != meshName) continue;
				++matches;
				meshIndex = static_cast<std::uint32_t>(index);
			}
			if (1u != matches)
			{
				meshIndex = assets::kInvalidModelAssetIndex;
				if (matches > 1u)
				{
					Debug::PrintLog(spdlog::level::warn, "MeshRenderer 메시 이름이 generation 안에서 유일하지 "
						"않다 — MeshId가 필요하다: " + meshName);
				}
			}
		}
		resolvedViaGeneration = assets::kInvalidModelAssetIndex != meshIndex
			&& BindModelGeneration(generation, meshIndex);
	}
	if (resolvedViaGeneration)
	{
		// MBC10 — 해석 관측은 읽기 전용 계수다(`assets.modeldiag`가 읽는다).
		ModelConsumptionDiagnostics::NoteMeshResolved();
	}
	else if (FileGuid{} != m_modelGuid)
	{
		ModelConsumptionDiagnostics::NoteMeshResolveFailed();
		Debug::PrintLog(spdlog::level::err, "MeshRenderer 모델 generation 해석 실패 — 그리지 않는다: model="
			+ m_modelGuid.ToString() + " mesh=" + (meshName.empty()
				? m_meshAssetId.ToString() : meshName));
	}

	SetEnabled(true); // 구 분기 말미의 강제 활성 보존
}

void MeshRenderer::OnAfterSerialize(const Authoring::MutableNodeView& view)
{
	// I5-M5 S2b — 씬 embed writer 전환. typed 리플렉션이 legacy 형상으로 적은
	// m_Material 서브트리를 정본 writer로 교체한다. reflection의 shared_ptr
	// 멤버는 컴파일 타임 재귀라(OnDeserialized의 같은 제약) 쓰기 쪽 절단선도
	// 소비자 훅이다. SerializeMaterialPayload는 ShaderMeta를 모르는 재질을
	// legacy 표기로 폴백하므로, 그 경우 이 교체는 형상 무변경이다.
	if (nullptr == m_Material) return;
	const Authoring::WriteNode node = Authoring::MutableNodeViewAccess::Node(view);

	// I5-M5 S2c-2a — base 자산에 링크된 재질은 인라인 embed 대신 참조+diff를
	// 적는다(자산 연결이 저장에서 소실되던 결함의 교정). 실패는 인라인 폴백.
	if (FileGuid{} != m_materialBaseGuid)
	{
		std::string error;
		if (BuildMaterialReferenceNode(*m_Material, m_materialBaseGuid,
			node.Child("m_Material"), error))
		{
			return;
		}
		Debug::PrintLog(spdlog::level::warn, "m_Material base 참조 저장 실패 — 인라인 폴백: "
			+ error);
	}
	if (!DataSystems->SerializeMaterialPayload(
		*m_Material, node.Child("m_Material")))
	{
		Debug::PrintLog(spdlog::level::warn, "m_Material writer ryml 전환 실패");
	}
}

bool MeshRenderer::BindModelGeneration(
	assets::ModelAssetGeneration::Shared generation,
	std::uint32_t meshIndex)
{
	if (!generation || meshIndex >= generation->Meshes().size()) return false;
    if (m_materialBaseGuid == FileGuid{} && m_Material && !m_Material->HasMaterialGraph())
    {
        const auto* source = generation->FindMaterial(generation->Meshes()[meshIndex].materialId);
        std::string error;
        auto candidate = own::make_shared<Material>(*m_Material);
        if (source && !DataSystems->ConfigureModelMaterialGraph(*candidate, *generation, *source, error))
        {
            Debug::PrintLog(spdlog::level::err, "MeshRenderer model graph conversion failed: " + error);
            return false;
        }
        if (source)
        {
            m_Material = std::move(candidate);
            SetExperimentMaterialBase(nullptr);
        }
    }
    m_meshRequest.Cancel();
    m_meshRequest = {};
    m_meshDescriptor.reset();
    m_granularMeshBinding = false;
    m_meshRequested = false;
	m_modelGeneration = std::move(generation);
	m_modelMeshIndex = meshIndex;
	m_meshAssetId = FileGuid(m_modelGeneration->Meshes()[meshIndex].meshId);
	m_modelGuid = FileGuid(m_modelGeneration->Identity().modelId);
	// 재질의 embedded texture owner는 이 generation closure가 정본이다. 전역
	// 임베디드 등록부(소스 로드 부산물)·이름 폴백·로드 순서에 기대지 않는다 —
	// 콜드 로드 첫 렌더러에서 텍스처가 비던 결함(§6.2)의 실제 처방이다.
	if (m_Material)
	{
		DataSystems->BindModelGenerationTextures(*m_Material, *m_modelGeneration);
	}
	return true;
}

assets::ModelMeshHandle MeshRenderer::GetModelMeshHandle() const
{
    if (m_meshDescriptor)
    {
        return assets::MakeModelMeshHandle(*m_meshDescriptor);
    }
	if (!m_modelGeneration
		|| m_modelMeshIndex >= m_modelGeneration->Meshes().size())
	{
		return {};
	}
	return { m_modelGeneration->Identity().modelId,
		m_modelGeneration->Meshes()[m_modelMeshIndex].meshId,
		m_modelGeneration->Identity().generation };
}

bool MeshRenderer::BindMeshDescriptor(
    own::shared_owner<const assets::ModelMeshDescriptor> descriptor)
{
    if (!descriptor || !assets::MakeModelMeshHandle(*descriptor).IsValid()
        || !assets::IsSupportedModelVertexLayout(descriptor->vertexAttributeMask)
        || descriptor->vertexStride == 0u
        || descriptor->skinned != assets::Has(descriptor->vertexAttributeMask, assets::VertexAttribute::BoneIndices)
        || descriptor->vertexStride != assets::StrideOf(descriptor->vertexAttributeMask)
        || descriptor->vertexLayoutHash != assets::VertexLayoutHash(descriptor->vertexAttributeMask)
        || descriptor->vertexCount == 0u || descriptor->indexCount == 0u
        || (descriptor->skinned && (!descriptor->skeleton
            || descriptor->requiredBoneCount != descriptor->skeleton->skeleton.bones.size()
            || descriptor->requiredSkinBindingSha256 != descriptor->skeleton->skinBindingSha256)))
    {
        return false;
    }
    m_meshRequest.Cancel();
    m_meshRequest = {};
    m_meshRequested = false;
    m_meshFailureReported = false;
    m_meshDescriptor = std::move(descriptor);
    m_modelGeneration.reset();
    m_modelMeshIndex = 0u;
    m_meshAssetId = FileGuid(m_meshDescriptor->meshId);
    m_isSkinnedMesh = m_meshDescriptor->skinned;
    m_granularMeshBinding = true;
    m_isNeedUpdateCulling = true;
    PublishRenderProxyDirty(ProxyDirty::Payload);
    return true;
}

void MeshRenderer::EnsureMeshBinding()
{
    const AssetDepot::AssetLink<assets::ModelMeshDescriptor> link{
        { experiment::AssetId{ m_meshAssetId.m_guid }, {} } };
    if (!link.IsValid())
    {
        return;
    }
    if (m_requestedMeshId != m_meshAssetId)
    {
        m_meshRequest.Cancel();
        m_meshRequest = {};
        m_requestedMeshId = m_meshAssetId;
        m_meshRequested = false;
        m_meshFailureReported = false;
        m_granularMeshBinding = DataSystems->HasModelMeshDescriptor(link);
    }
    if (!m_granularMeshBinding || (m_meshDescriptor && m_meshDescriptor->meshId == m_meshAssetId.m_guid))
    {
        return;
    }
    if (!m_meshRequested)
    {
        if (auto descriptor = DataSystems->TryAcquire(link))
        {
            if (BindMeshDescriptor(std::move(descriptor)))
            {
                return;
            }
        }
        m_meshRequest = DataSystems->RequestAsync(link);
        m_meshRequested = true;
    }
    const auto result = m_meshRequest.Snapshot();
    if (result.status == AssetDepot::AssetRequestStatus::Pending)
    {
        return;
    }
    if (result.status == AssetDepot::AssetRequestStatus::Ready && BindMeshDescriptor(result.asset))
    {
        m_meshRequest = {};
        m_meshRequested = false;
        return;
    }
    // Failed, Cancelled and Stale are terminal for this requested link. Keep
    // the result visible until an explicit bind or mesh-ID transition; silently
    // issuing a new current-link request here would implement FollowLatest.
    if (!m_meshFailureReported)
    {
        Debug::PrintLog(spdlog::level::err, "MeshRenderer v3 mesh binding failed: "
            + m_meshAssetId.ToString() + " " + result.message);
        m_meshFailureReported = true;
    }
    // Retain any previous valid descriptor/material while the replacement fails.
}
