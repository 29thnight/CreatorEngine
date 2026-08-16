#include "MeshRenderer.h"
#include "Model.h"
#include "ReflectionYml.h"
#include "DataSystem.h"
#include "GameObject.h"
#include "Mesh.h"
#include "Material.h"
#include "SceneManager.h"
#include "RenderScene.h"
#include "Scene.h"

MeshRenderer::MeshRenderer()
{
    m_name = "MeshRenderer"; 
    m_typeID = TypeTrait::GUIDCreator::GetTypeID<MeshRenderer>();
}

MeshRenderer::~MeshRenderer()
{
}

void MeshRenderer::Awake()
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

void MeshRenderer::OnDestroy()
{
    auto scene = GetOwner()->m_ownerScene;
    auto renderScene = SceneManagers->GetRenderScene();
	if (scene)
	{
		scene->UnCollectMeshRenderer(this);
        renderScene->UnregisterCommand(this);
	}

}

BoundingBox MeshRenderer::GetBoundingBox() const
{
    if (m_Mesh)
    {
        BoundingBox localBoundingBox = m_Mesh->GetBoundingBox();
        auto mat = m_pOwner->m_transform.GetWorldMatrix();
        BoundingBox worldBoundingBox;
        localBoundingBox.Transform(worldBoundingBox, mat);

        return worldBoundingBox;
    }

    return BoundingBox();
}

void MeshRenderer::OnDeserialized(const YAML::Node& node)
{
	// CT6-d: 구 ComponentFactory MeshRenderer 분기 이동(동작 보존).
	// 반영 멤버는 typed 역직렬화가 이미 채웠고, 여기서는 머티리얼·메시
	// GUID 해석과 텍스처 로드만 한다. SetOwner는 훅 이전에 공통 경로가
	// 수행한다(구 분기의 애셋 코드는 소유자를 쓰지 않음을 감사).
	std::string materialName{};
	Model* model = nullptr;
	MaterialRenderingMode renderingMode = MaterialRenderingMode::Opaque;

	if (node["m_Material"])
	{
		auto materialNode = node["m_Material"];
		materialName = materialNode["m_name"].as<std::string>();
		FileGuid guid = materialNode["m_fileGuid"].as<std::string>();
		model = DataSystems->LoadModelGUID(guid);
		if (materialNode["m_renderingMode"])
		{
			renderingMode = static_cast<MaterialRenderingMode>(materialNode["m_renderingMode"].as<int>());
		}
	}

	MetaYml::Node getMeshNode = node["m_Mesh"];
	if (model && getMeshNode)
	{
		auto matPtr = model->GetMaterialShared(getMeshNode["m_materialIndex"].as<int>());
		if (matPtr)
		{
			// 컴포넌트가 머티리얼을 공동 소유하도록 shared 조회를 쓴다.
			m_Material = DataSystems->LoadMaterialShared(materialName);
			if (!m_Material)
			{
				m_Material = matPtr;
			}
			m_Material->m_renderingMode = renderingMode;
			if (node["m_Material"])
			{
				auto& materialNode = node["m_Material"];
				Meta::Deserialize(m_Material.get(), materialNode);
				if (!materialName.empty())
					m_Material->m_name = materialName;

				if (0.04f > m_Material->m_materialInfo.m_IOR || 4.f < m_Material->m_materialInfo.m_IOR)
				{
					m_Material->m_materialInfo.m_IOR = 1.5f;
				}

				auto loadTex = [](const std::string& texName, Texture*& texPtr, bool compress = false)
				{
					if (!texName.empty())
					{
						texPtr = DataSystems->LoadMaterialTexture(texName, compress);
					}
				};

				if (!m_Material->m_pBaseColor || m_Material->m_baseColorTexName != m_Material->m_pBaseColor->m_name)
				{
					loadTex(m_Material->m_baseColorTexName, m_Material->m_pBaseColor, false);
					if (m_Material->m_pBaseColor)
					{
						m_Material->m_materialInfo.m_useBaseColor = true;
					}
				}
				if (!m_Material->m_pNormal || m_Material->m_normalTexName != m_Material->m_pNormal->m_name)
				{
					loadTex(m_Material->m_normalTexName, m_Material->m_pNormal);
					if (m_Material->m_pNormal)
					{
						m_Material->m_materialInfo.m_useNormalMap = true;
					}
				}
				if (!m_Material->m_pOccRoughMetal || m_Material->m_ORM_TexName != m_Material->m_pOccRoughMetal->m_name)
				{
					loadTex(m_Material->m_ORM_TexName, m_Material->m_pOccRoughMetal);
					if (m_Material->m_pOccRoughMetal)
					{
						m_Material->m_materialInfo.m_useOccRoughMetal = true;
					}
				}
				if (!m_Material->m_AOMap || m_Material->m_AO_TexName != m_Material->m_AOMap->m_name)
				{
					loadTex(m_Material->m_AO_TexName, m_Material->m_AOMap);
					if (m_Material->m_AOMap)
					{
						m_Material->m_materialInfo.m_useAOMap = true;
					}
				}
				if (!m_Material->m_pEmissive || m_Material->m_EmissiveTexName != m_Material->m_pEmissive->m_name)
				{
					loadTex(m_Material->m_EmissiveTexName, m_Material->m_pEmissive);
					if (m_Material->m_pEmissive)
					{
						m_Material->m_materialInfo.m_useEmissive = true;
					}
				}
			}
		}

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

