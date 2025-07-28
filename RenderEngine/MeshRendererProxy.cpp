#include "MeshRendererProxy.h"
#include "MeshRenderer.h"
#include "Mesh.h"
#include "RenderScene.h"
#include "Material.h"
#include "Camera.h" // [NEW] Camera 정의 포함
#include "FoliageComponent.h"
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
    m_proxyType = PrimitiveProxyType::TerrainComponent;
}

PrimitiveRenderProxy::PrimitiveRenderProxy(FoliageComponent* component) :
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
    m_proxyType = PrimitiveProxyType::FoliageComponent;
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
    m_isAnimationEnabled(other.m_isAnimationEnabled),
    m_isEnableShadow(other.m_isEnableShadow),
    m_isShadowCast(other.m_isShadowCast),
    m_isShadowRecive(other.m_isShadowRecive),
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
    m_isAnimationEnabled(other.m_isAnimationEnabled),
	m_isEnableShadow(other.m_isEnableShadow),
    m_isShadowCast(other.m_isShadowCast),
    m_isShadowRecive(other.m_isShadowRecive),
    m_isInstanced(other.m_isInstanced),
	m_terrainMesh(std::exchange(other.m_terrainMesh, nullptr)),
    m_terrainMaterial(std::exchange(other.m_terrainMaterial, nullptr)),
	m_isNeedUpdateCulling(other.m_isNeedUpdateCulling)
{
}

void PrimitiveRenderProxy::Draw(ID3D11DeviceContext* _deferredContext)
{
    switch (m_proxyType)
    {
    case PrimitiveProxyType::MeshRenderer:
    {
        if (nullptr == m_Mesh || nullptr == _deferredContext) return;

        if (m_EnableLOD && !m_isSkinnedMesh && m_Mesh->HasLODs())
        {
            m_Mesh->DrawLOD(_deferredContext, m_currLOD);
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
		Debug->LogError("FoliageComponent does not support normal draw function");
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

// [CHANGED] LOD 생성 요청 함수 구현
void PrimitiveRenderProxy::InitializeLODs(const std::vector<float>& lodScreenSpaceThresholds)
{
    if (nullptr == m_Mesh || false == m_isShadowCast) return;

    // 스키닝 메쉬는 LOD를 생성하지 않습니다.
    if (m_isSkinnedMesh)
    {
        return;
    }

    // 메쉬에 아직 LOD가 생성되지 않았을 경우에만 생성을 요청합니다.
    if (!m_Mesh->HasLODs())
    {
        m_Mesh->GenerateLODs(lodScreenSpaceThresholds);
    }
}

// [NEW] 렌더링 시스템이 사용할 LOD 레벨 결정 함수
uint32_t PrimitiveRenderProxy::GetLODLevel(Camera* camera)
{
    if (nullptr == m_Mesh || nullptr == camera || false == m_EnableLOD)
    {
        return 0; // 유효하지 않은 경우, 원본 메쉬(LOD 0) 반환
    }

	m_currLOD = m_Mesh->SelectLOD(camera, m_worldMatrix);

    // 실제 계산은 Mesh 클래스에 위임합니다.
    return m_currLOD;
}

void PrimitiveRenderProxy::DrawShadow(ID3D11DeviceContext* _deferredContext)
{
    if (nullptr == m_Mesh || nullptr == _deferredContext || false == m_isShadowCast) return;

    if (m_EnableLOD && !m_isSkinnedMesh && m_Mesh->HasLODs())
    {
        m_Mesh->DrawLOD(_deferredContext, m_currLOD);
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

    if (m_EnableLOD && !m_isSkinnedMesh && m_Mesh->HasLODs())
    {
        m_Mesh->DrawInstancedLOD(_deferredContext, m_currLOD, instanceCount);
    }
    else
    {
        m_Mesh->DrawInstanced(_deferredContext, instanceCount);
    }
}
