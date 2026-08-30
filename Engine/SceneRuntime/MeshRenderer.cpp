#include "MeshRenderer.h"
#include "Model.h"
#include "ReflectionYml.h"
#include "DataSystem.h"
#include "Entity.h"
#include "Mesh.h"
#include "Material.h"
#include "SceneManager.h"
#include "RenderScene.h"
#include "Scene.h"
#include <mathematics/transform.hpp>

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

void MeshRenderer::OnDeserialized(const YAML::Node& node)
{
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
		materialNode && materialNode.IsMap()
		&& materialNode["schema"] && materialNode["shaderAssetId"])
	{
		auto decoded = std::make_shared<Material>();
		if (DataSystems->DeserializeMaterialPayload(*decoded, materialNode))
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

	Model* model = nullptr;
	if (m_Material)
	{
		model = DataSystems->LoadModelGUID(m_Material->m_fileGuid);
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
	node["m_Material"] = DataSystems->SerializeMaterialPayload(*m_Material);
}

