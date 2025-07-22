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
    GameObject::Index animatorOwnerIndex = component->GetOwner()->m_parentIndex;
    while(animatorOwnerIndex != GameObject::INVALID_INDEX)
    {
        GameObject* animatorOwner = GameObject::FindIndex(animatorOwnerIndex);
        if (animatorOwner)
        {
            Animator* animator = animatorOwner->GetComponent<Animator>();
            if (animator && animator->IsEnabled())
            {
                m_isAnimationEnabled = true;
                m_animatorGuid = animator->GetInstanceID();
                break;
            }
        }
        animatorOwnerIndex = animatorOwner ? animatorOwner->m_parentIndex : GameObject::INVALID_INDEX;
	}

    if (nullptr != m_Material)
    {
        m_materialGuid = m_Material->m_materialGuid;
    }
    m_instancedID = component->GetInstanceID();

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
    GameObject* owner = component->GetOwner();
    if (owner)
    {
        //m_materialGuid = m_Material->m_materialGuid;
        m_instancedID = component->GetInstanceID();
    }

PrimitiveRenderProxy::PrimitiveRenderProxy(FoliageComponent* component) :
    m_isSkinnedMesh(false),
    m_worldMatrix(component->GetOwner()->m_transform.GetWorldMatrix()),
    m_worldPosition(component->GetOwner()->m_transform.GetWorldPosition())
{
    m_instancedID = component->GetInstanceID();
    m_proxyType = PrimitiveProxyType::FoliageComponent;
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
    m_worldPosition(other.m_worldPosition),
	m_proxyType(other.m_proxyType),
    m_instancedID(other.m_instancedID),
    m_isCulled(other.m_isCulled),
    m_isStatic(other.m_isStatic),
    m_EnableLOD(other.m_EnableLOD),
    m_LODReductionRatio(other.m_LODReductionRatio),
    m_MaxLODLevels(other.m_MaxLODLevels),
	m_LODGroup(other.m_LODGroup ? std::make_unique<LODGroup>(*other.m_LODGroup) : nullptr),
	m_LODDistance(other.m_LODDistance),
    m_isAnimationEnabled(other.m_isAnimationEnabled),
    m_isEnableShadow(other.m_isEnableShadow),
	m_isInstanced(other.m_isInstanced),
    m_terrainMesh(other.m_terrainMesh),
	m_terrainMaterial(other.m_terrainMaterial),
    m_isNeedUpdateCulling(other.m_isNeedUpdateCulling)
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
    m_worldPosition(std::exchange(other.m_worldPosition, {})),
	m_proxyType(other.m_proxyType),
    m_instancedID(std::exchange(other.m_instancedID, {})),
    m_isCulled(other.m_isCulled),
    m_isStatic(other.m_isStatic),
    m_EnableLOD(other.m_EnableLOD),
    m_LODReductionRatio(other.m_LODReductionRatio),
	m_MaxLODLevels(other.m_MaxLODLevels),
	m_LODGroup(std::move(other.m_LODGroup)),
    m_LODDistance(other.m_LODDistance),
    m_isAnimationEnabled(other.m_isAnimationEnabled),
	m_isEnableShadow(other.m_isEnableShadow),
    m_isInstanced(other.m_isInstanced),
	m_terrainMesh(std::exchange(other.m_terrainMesh, nullptr)),
    m_terrainMaterial(std::exchange(other.m_terrainMaterial, nullptr)),
	m_isNeedUpdateCulling(other.m_isNeedUpdateCulling)
{
}

void PrimitiveRenderProxy::Draw()
{
    switch (m_proxyType)
    {
    case PrimitiveProxyType::MeshRenderer:
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
        break;
    }
    case PrimitiveProxyType::TerrainComponent:
    {
		if (nullptr == m_terrainMesh || nullptr == m_terrainMaterial) return;

        m_terrainMesh->Draw();
        break;
    }
    case PrimitiveProxyType::FoliageComponent:
        if (nullptr == m_Mesh) return;
        m_Mesh->Draw();
        break;
    default:
        break;
    }
    
}

void PrimitiveRenderProxy::Draw(ID3D11DeviceContext* _deferredContext)
{
    switch (m_proxyType)
    {
    case PrimitiveProxyType::MeshRenderer:
    {
        if (nullptr == m_Mesh || nullptr == _deferredContext) return;

        if (m_EnableLOD)
        {
            if (!m_LODGroup) return;
            m_LODGroup->GetLODByDistance(m_LODDistance)->Draw(_deferredContext);
        }
        else
        {
            m_Mesh->Draw(_deferredContext);
        }
        break;
    }
    case PrimitiveProxyType::TerrainComponent:
    {
        if (nullptr == m_terrainMesh || nullptr == m_terrainMaterial) return;

        m_terrainMesh->Draw(_deferredContext);
        break;
    }
    case PrimitiveProxyType::FoliageComponent:
    {
        if (nullptr == m_Mesh || nullptr == _deferredContext) return;
        m_Mesh->Draw(_deferredContext);
        break;
    }
    default:
        break;
    }
}

void PrimitiveRenderProxy::DestroyProxy()
{
    RenderScene::RegisteredDestroyProxyGUIDs.push(m_instancedID);
}

void PrimitiveRenderProxy::GenerateLODGroup()
{
    if (nullptr == m_Mesh || nullptr == m_Material) return;
	bool isTerrain = (m_proxyType == PrimitiveProxyType::TerrainComponent || m_proxyType == PrimitiveProxyType::FoliageComponent);

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

void PrimitiveRenderProxy::DrawShadow(ID3D11DeviceContext* _deferredContext)
{
    if (nullptr == m_Mesh || nullptr == _deferredContext) return;

    if (m_EnableLOD)
    {
        if (!m_LODGroup) return;
        m_LODGroup->GetLODByDistance(m_LODDistance)->DrawShadow(_deferredContext);
    }
    else
    {
        if (m_Mesh->IsShadowOptimized())
        {
            m_Mesh->DrawShadow(_deferredContext);
        }
        else
        {
            m_Mesh->Draw(_deferredContext);
        }
    }
}

void PrimitiveRenderProxy::DrawInstanced(ID3D11DeviceContext* _deferredContext, size_t instanceCount)
{
    if (nullptr == m_Mesh || nullptr == _deferredContext) return;

    m_Mesh->DrawInstanced(_deferredContext, instanceCount);
}
