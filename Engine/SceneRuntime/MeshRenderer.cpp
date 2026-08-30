#include "MeshRenderer.h"
#include "AuthoringNodeViewAccess.h" // D3-a-4
#include "Model.h"
#include "ReflectionYml.h"
#include "DataSystem.h"
#include "Entity.h"
#include "Mesh.h"
#include "Material.h"
#include "SceneManager.h"
#include "RenderScene.h"
#include "Scene.h"
#include "Experiment/MaterialAuthoringCodec.h" // S2c-2a: override 값 표기 정본
#include "ExperimentMaterialMigration.h" // S2c-2a: diff 타이핑·override 적용
#include <mathematics/transform.hpp>

#include <algorithm>

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
	[[nodiscard]] bool DecodeMaterialReferenceNode(
		const YAML::Node& materialNode, std::shared_ptr<Material>& outMaterial,
		FileGuid& outBaseGuid, std::string& outError)
	{
		const YAML::Node ref = materialNode["ref"];
		if (!ref || !ref.IsScalar())
		{
			outError = "ref가 스칼라가 아니다";
			return false;
		}
		const FileGuid baseGuid(ref.as<std::string>());
		if (FileGuid{} == baseGuid)
		{
			outError = "ref가 nil GUID다";
			return false;
		}
		const file::path basePath = DataSystems->GetFilePath(baseGuid);
		if (basePath.empty())
		{
			outError = "base 자산 경로 미해석: " + ref.as<std::string>();
			return false;
		}
		const std::shared_ptr<Material> base =
			DataSystems->LoadMaterialShared(basePath.stem().string());
		if (!base)
		{
			outError = "base 재질 로드 실패: " + basePath.stem().string();
			return false;
		}

		auto owned = std::make_shared<Material>(*base);
		if (const YAML::Node blend = materialNode["blendMode"];
			blend && blend.IsScalar())
		{
			owned->m_renderingMode = blend.as<std::string>() == "transparent"
				? MaterialRenderingMode::Transparent
				: MaterialRenderingMode::Opaque;
		}
		if (const YAML::Node selections = materialNode["keywordSelections"];
			selections && selections.IsSequence())
		{
			std::vector<std::uint16_t> values;
			values.reserve(selections.size());
			for (const YAML::Node& selection : selections)
			{
				values.push_back(
					static_cast<std::uint16_t>(selection.as<std::uint32_t>()));
			}
			owned->m_keywordSelections = std::move(values);
		}
		if (const YAML::Node overrides = materialNode["overrides"];
			overrides && overrides.IsSequence())
		{
			for (const YAML::Node& entry : overrides)
			{
				if (!entry.IsMap() || !entry["name"]
					|| !entry["name"].IsScalar())
				{
					outError = "override 항목에 name이 없다";
					return false;
				}
				experiment::MaterialProperty property;
				property.name = entry["name"].as<std::string>();
				if (!experiment::DeserializeMaterialPropertyValue(entry,
						property.name, property.value, outError)
					|| !ExperimentMaterialMigration::ApplyPropertyToLegacy(
						*owned, property, outError))
				{
					return false;
				}
			}
		}

		DataSystems->FinalizeMaterialRuntime(*owned);
		outMaterial = std::move(owned);
		outBaseGuid = baseGuid;
		return true;
	}

	// I5-M5 S2c-2a — base 참조(ref) 표기의 쓰기. 현 재질과 base를 같은 meta로
	// experiment 변환해 diff만 남긴다. base에만 있는 저작은 참조 표기로
	// "되돌림"을 표현할 수 없으므로 실패(인라인 폴백)다. variant 값 비교는
	// 코덱 인코딩 텍스트로 한다 — 수학 타입에 operator==가 없다.
	[[nodiscard]] bool BuildMaterialReferenceNode(const Material& current,
		FileGuid baseGuid, YAML::Node& outNode, std::string& outError)
	{
		const file::path basePath = DataSystems->GetFilePath(baseGuid);
		if (basePath.empty())
		{
			outError = "base 자산 경로 미해석";
			return false;
		}
		const std::shared_ptr<Material> base =
			DataSystems->LoadMaterialShared(basePath.stem().string());
		if (!base)
		{
			outError = "base 재질 로드 실패: " + basePath.stem().string();
			return false;
		}
		std::string metaError;
		const ShaderMetaHandle handle = DataSystems->LoadShaderMetaHandle(
			current.m_shaderMetaGuid, metaError);
		const std::shared_ptr<const ShaderMeta> meta =
			DataSystems->ResolveShaderMeta(handle);
		if (!meta)
		{
			outError = "diff 타이핑용 ShaderMeta 미해석: " + metaError;
			return false;
		}
		experiment::Material currentConverted;
		experiment::Material baseConverted;
		if (!ExperimentMaterialMigration::ConvertLegacyMaterial(current, *meta,
				currentConverted, outError)
			|| !ExperimentMaterialMigration::ConvertLegacyMaterial(*base, *meta,
				baseConverted, outError))
		{
			return false;
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

		const auto encodeEntry = [&outError](
			const experiment::MaterialProperty& property,
			YAML::Node& outEntry)
		{
			outEntry["name"] = property.name;
			return experiment::SerializeMaterialPropertyValue(property,
				outEntry, outError);
		};
		YAML::Node overrides(YAML::NodeType::Sequence);
		for (const experiment::MaterialProperty& property :
			currentConverted.properties)
		{
			YAML::Node entry;
			if (!encodeEntry(property, entry))
			{
				return false;
			}
			if (const experiment::MaterialProperty* baseProperty =
				findByName(baseConverted, property.name))
			{
				YAML::Node baseEntry;
				if (!encodeEntry(*baseProperty, baseEntry))
				{
					return false;
				}
				if (YAML::Dump(entry) == YAML::Dump(baseEntry))
				{
					continue;
				}
			}
			overrides.push_back(entry);
		}

		YAML::Node result;
		result["ref"] = baseGuid.ToString();
		if (currentConverted.blendMode != baseConverted.blendMode)
		{
			result["blendMode"] =
				experiment::MaterialBlendMode::Transparent
					== currentConverted.blendMode
				? "transparent" : "opaque";
		}
		if (currentConverted.keywordSelections
			!= baseConverted.keywordSelections)
		{
			YAML::Node selections(YAML::NodeType::Sequence);
			selections.SetStyle(YAML::EmitterStyle::Flow);
			for (const std::uint16_t value : currentConverted.keywordSelections)
			{
				selections.push_back(static_cast<std::uint32_t>(value));
			}
			result["keywordSelections"] = selections;
		}
		if (overrides.size() > 0)
		{
			result["overrides"] = overrides;
		}
		outNode = result;
		return true;
	}
}

MeshRenderer::MeshRenderer()
{
}

MeshRenderer::~MeshRenderer()
{
}

void MeshRenderer::OnInitialized()
{
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

void MeshRenderer::OnUninitializing()
{
    auto scene = GetOwner()->m_ownerScene;
    auto renderScene = SceneManagers->GetRenderScene();
	if (scene)
	{
		scene->UnCollectMeshRenderer(this);
        renderScene->UnregisterCommand(this);
	}

}

math::aabb MeshRenderer::GetBoundingBox() const
{
    if (m_Mesh)
    {
		return math::transform(
			m_Mesh->GetBoundingBox(), m_pOwner->Transform_().GetWorldMatrix());
    }

    return math::aabb{};
}

void MeshRenderer::OnDeserialized(const Authoring::NodeView& view)
{
	const YAML::Node& node = Authoring::NodeViewAccess::Node(view);
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
	if (const YAML::Node materialNode = node["m_Material"];
		materialNode && materialNode.IsMap() && materialNode["ref"])
	{
		// I5-M5 S2c-2a — base 참조 표기. typed 역직렬화는 ref 노드에서 기본값
		// 재질을 만들었을 뿐이다 — base 소유 사본+override로 교체한다.
		std::shared_ptr<Material> resolved;
		FileGuid baseGuid;
		std::string error;
		if (DecodeMaterialReferenceNode(materialNode, resolved, baseGuid,
			error))
		{
			m_Material = std::move(resolved);
			m_materialBaseGuid = baseGuid;
		}
		else
		{
			Debug->LogError("MeshRenderer m_Material base 참조 해석 실패 — "
				"재질을 비운다: " + error);
			m_Material.reset();
		}
	}
	else if (const YAML::Node materialNode = node["m_Material"];
		materialNode && materialNode.IsMap()
		&& materialNode["schema"] && materialNode["shaderAssetId"])
	{
		auto decoded = std::make_shared<Material>();
		if (DataSystems->DeserializeMaterialPayload(*decoded,
			Authoring::NodeViewAccess::Make(materialNode)))
		{
			// FinalizeMaterialRuntime은 이중화 경로 안에서 이미 수행됐다.
			m_Material = std::move(decoded);
		}
		else
		{
			Debug->LogError("MeshRenderer m_Material 새 정본 해석 실패 — "
				"typed 기본값 상태를 유지한다");
		}
	}
	else if (m_Material)
	{
		DataSystems->FinalizeMaterialRuntime(*m_Material);
	}

	// I5-M5 S2c-1 — 모델 해석의 정본은 자기 m_modelGuid다. legacy 씬은 인라인
	// 재질의 m_fileGuid가 모델 GUID를 나르는 편법이라 폴백으로 읽고, 읽는 즉시
	// 자기 필드로 이주해 다음 저장부터 정본이 된다(로드 실패여도 정보는 동일).
	// S2c-2a: base 링크 재질의 m_fileGuid는 **재질 자산** GUID라 모델 폴백
	// 대상이 아니다 — ref 표기는 S2c-1 이후의 것이라 m_modelGuid가 정본이다.
	Model* model = nullptr;
	if (FileGuid{} != m_modelGuid)
	{
		model = DataSystems->LoadModelGUID(m_modelGuid);
	}
	if (nullptr == model && m_Material && FileGuid{} == m_materialBaseGuid)
	{
		model = DataSystems->LoadModelGUID(m_Material->m_fileGuid);
		if (FileGuid{} == m_modelGuid)
		{
			m_modelGuid = m_Material->m_fileGuid;
		}
	}

	MetaYml::Node getMeshNode = node["m_Mesh"];
	if (model && getMeshNode)
	{
		m_Mesh = model->GetMeshShared(getMeshNode["m_name"].as<std::string>());
		if (m_Mesh)
		{
			MetaYml::Node getLOD_Node = getMeshNode["m_LODThresholds"];
			if (getLOD_Node)
			{
				std::vector<float> lodThresholds;
				for (const auto& threshold : getLOD_Node)
				{
					lodThresholds.push_back(threshold.as<float>());
				}
				m_Mesh->GenerateLODs(lodThresholds);
			}
		}
	}

	SetEnabled(true); // 구 분기 말미의 강제 활성 보존
}

void MeshRenderer::OnAfterSerialize(YAML::Node& node)
{
	// I5-M5 S2b — 씬 embed writer 전환. typed 리플렉션이 legacy 형상으로 적은
	// m_Material 서브트리를 정본 writer로 교체한다. reflection의 shared_ptr
	// 멤버는 컴파일 타임 재귀라(OnDeserialized의 같은 제약) 쓰기 쪽 절단선도
	// 소비자 훅이다. SerializeMaterialPayload는 ShaderMeta를 모르는 재질을
	// legacy 표기로 폴백하므로, 그 경우 이 교체는 형상 무변경이다.
	if (nullptr == m_Material) return;

	// I5-M5 S2c-2a — base 자산에 링크된 재질은 인라인 embed 대신 참조+diff를
	// 적는다(자산 연결이 저장에서 소실되던 결함의 교정). 실패는 인라인 폴백.
	if (FileGuid{} != m_materialBaseGuid)
	{
		YAML::Node reference;
		std::string error;
		if (BuildMaterialReferenceNode(*m_Material, m_materialBaseGuid,
			reference, error))
		{
			node["m_Material"] = reference;
			return;
		}
		Debug->LogWarning("m_Material base 참조 저장 실패 — 인라인 폴백: "
			+ error);
	}
	node["m_Material"] = DataSystems->SerializeMaterialPayload(*m_Material);
}

