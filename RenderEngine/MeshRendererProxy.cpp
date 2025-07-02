#include "MeshRendererProxy.h"
#include "MeshRenderer.h"
#include "Mesh.h"
#include "RenderScene.h"
#include "Material.h"
#include "Core.OctreeNode.h"
#include "CullingManager.h"
#include "Terrain.h"

constexpr size_t TRANSFORM_SIZE = sizeof(Mathf::xMatrix) * MAX_BONES;

PrimitiveRenderProxy::PrimitiveRenderProxy(MeshRenderer* component) :
    m_Material(component->m_Material),
    m_Mesh(component->m_Mesh),
    m_LightMapping(component->m_LightMapping),
    m_isSkinnedMesh(component->m_isSkinnedMesh),
    m_worldMatrix(component->GetOwner()->m_transform.GetWorldMatrix()),
	m_worldPosition(component->GetOwner()->m_transform.GetWorldPosition())
{
    GameObject* owner = GameObject::FindIndex(component->GetOwner()->m_parentIndex);
    if(owner)
    {
        Animator* animator = owner->GetComponent<Animator>();
        if (nullptr != animator && animator->IsEnabled())
        {
            m_isAnimationEnabled = true;
            m_animatorGuid = animator->GetInstanceID();
        }

        if(nullptr != m_Material)
        {
            m_materialGuid = m_Material->m_materialGuid;
        }
        m_instancedID = component->GetInstanceID();
    }

    if (!m_isSkinnedMesh)
    {
        //TODO : Change CullingManager Collect Class : MeshRenderer -> PrimitiveRenderProxy
        //CullingManagers->Insert(this);

        m_isNeedUpdateCulling = true;
    }
}

PrimitiveRenderProxy::PrimitiveRenderProxy(TerrainComponent* component) : 
    m_terrainMaterial(component->GetMaterial()),
    m_terrainMesh(component->GetMesh()),
    m_isSkinnedMesh(false),
    m_worldMatrix(component->GetOwner()->m_transform.GetWorldMatrix()),
    m_worldPosition(component->GetOwner()->m_transform.GetWorldPosition())
{
    GameObject* owner = GameObject::FindIndex(component->GetOwner()->m_parentIndex);
    if (owner)
    {
        //m_materialGuid = m_Material->m_materialGuid;
        m_instancedID = component->GetInstanceID();
    }

    if (!m_isSkinnedMesh)
    {
        //TODO : Change CullingManager Collect Class : MeshRenderer -> PrimitiveRenderProxy
        //CullingManagers->Insert(this);

        m_isNeedUpdateCulling = true;
    }
}

PrimitiveRenderProxy::~PrimitiveRenderProxy()
{
}

PrimitiveRenderProxy::PrimitiveRenderProxy(const PrimitiveRenderProxy& other) :
    m_Material(other.m_Material),
    m_Mesh(other.m_Mesh),
    m_LightMapping(other.m_LightMapping),
    m_isSkinnedMesh(other.m_isSkinnedMesh),
    m_worldMatrix(other.m_worldMatrix),
    m_animatorGuid(other.m_animatorGuid),
    m_materialGuid(other.m_materialGuid),
    m_finalTransforms(other.m_finalTransforms),
    m_worldPosition(other.m_worldPosition)
{
}

PrimitiveRenderProxy::PrimitiveRenderProxy(PrimitiveRenderProxy&& other) noexcept :
    m_Material(std::exchange(other.m_Material, nullptr)),
    m_Mesh(std::exchange(other.m_Mesh, nullptr)),
    m_LightMapping(other.m_LightMapping),
    m_isSkinnedMesh(other.m_isSkinnedMesh),
    m_worldMatrix(std::exchange(other.m_worldMatrix, {})),
    m_animatorGuid(std::exchange(other.m_animatorGuid, {})),
    m_materialGuid(std::exchange(other.m_materialGuid, {})),
    m_finalTransforms(std::exchange(other.m_finalTransforms, nullptr)),
    m_worldPosition(std::exchange(other.m_worldPosition, {}))
{
}

void PrimitiveRenderProxy::Draw()
{
    if (nullptr == m_Mesh) return;

    if (m_EnableLOD)
    {
        if (!m_LODGroup) return;
        m_LODGroup->GetLODByDistance(m_LODDistance)->Draw();
    }
    else
    {
        m_Mesh->Draw();
    }
}

void PrimitiveRenderProxy::Draw(ID3D11DeviceContext* _defferedContext)
{
    if (nullptr == m_Mesh || nullptr == _defferedContext) return;

    if (m_EnableLOD)
    {
        if (!m_LODGroup) return;
        m_LODGroup->GetLODByDistance(m_LODDistance)->Draw(_defferedContext);
    }
    else
    {
        m_Mesh->Draw(_defferedContext);
    }
}

void PrimitiveRenderProxy::DistroyProxy()
{
    RenderScene::RegisteredDistroyProxyGUIDs.push(m_instancedID);
}

void PrimitiveRenderProxy::GenerateLODGroup()
{
    if (nullptr == m_Mesh || nullptr == m_Material) return;
	bool isTerrain = (m_proxyType == PrimitiveProxyType::TerrainComponent);

	if (isTerrain || m_isSkinnedMesh) return;
    m_EnableLOD = LODSettings::IsLODEnabled();

    if(!m_EnableLOD) return;
	bool isChangeLODRatio = m_LODReductionRatio != LODSettings::g_LODReductionRatio.load();
	bool isChangeMaxLODLevels = m_MaxLODLevels != LODSettings::g_MaxLODLevels.load();

    if (nullptr == m_LODGroup)
    {
        m_LODGroup = std::make_unique<LODGroup>(m_Mesh);
        m_LODReductionRatio = LODSettings::g_LODReductionRatio.load();
        m_MaxLODLevels = LODSettings::g_MaxLODLevels.load();
    }
    else
    {
        if (isChangeLODRatio || isChangeMaxLODLevels)
        {
            m_LODGroup->UpdateLODs();
            m_LODReductionRatio = LODSettings::g_LODReductionRatio.load();
            m_MaxLODLevels = LODSettings::g_MaxLODLevels.load();
        }
    }
}

void PrimitiveRenderProxy::DrawShadow()
{
    if (nullptr == m_Mesh) return;

    if(m_EnableLOD)
    {
        if (!m_LODGroup) return;
        m_LODGroup->GetLODByDistance(m_LODDistance)->DrawShadow();
	}
    else
    {
        if (m_Mesh->IsShadowOptimized())
        {
            m_Mesh->DrawShadow();
        }
        else
        {
            m_Mesh->Draw();
        }
    }

}

void PrimitiveRenderProxy::DrawShadow(ID3D11DeviceContext* _defferedContext)
{
    if (nullptr == m_Mesh || nullptr == _defferedContext) return;

    if (m_EnableLOD)
    {
        if (!m_LODGroup) return;
        m_LODGroup->GetLODByDistance(m_LODDistance)->DrawShadow(_defferedContext);
    }
    else
    {
        if (m_Mesh->IsShadowOptimized())
        {
            m_Mesh->DrawShadow(_defferedContext);
        }
        else
        {
            m_Mesh->Draw(_defferedContext);
        }
    }
}