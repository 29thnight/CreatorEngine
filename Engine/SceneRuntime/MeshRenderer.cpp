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
#include "MathematicsInterop.h"

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

DirectX::BoundingBox MeshRenderer::GetBoundingBox() const
{
    if (m_Mesh)
    {
        DirectX::BoundingBox localBoundingBox = m_Mesh->GetBoundingBox();
        auto mat = m_pOwner->Transform_().GetWorldMatrix();
        DirectX::BoundingBox worldBoundingBox;
        localBoundingBox.Transform(worldBoundingBox, MathematicsInterop::ToDirectX(mat));

        return worldBoundingBox;
    }

    return DirectX::BoundingBox();
}

void MeshRenderer::OnDeserialized(const YAML::Node& node)
{
	// typed 역직렬화가 m_Material의 소유 인스턴스를 이미 만들었다. 예전 경로는
	// 이름으로 cache material을 꺼낸 뒤 scene snapshot을 그 공유 객체에 다시
	// Deserialize해 다른 renderer까지 바꿨다. snapshot 소유권은 유지하고 runtime
	// texture 복원만 DataSystem의 Material finalize 경계에 맡긴다.
	Model* model = nullptr;
	if (m_Material)
	{
		DataSystems->FinalizeMaterialRuntime(*m_Material);
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

