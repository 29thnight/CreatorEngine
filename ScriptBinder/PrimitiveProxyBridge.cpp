// PrimitiveRenderProxy의 컴포넌트 읽기 생성자들 (PHASE 4-2 C1).
//
// 프록시 "타입"은 렌더 소유(MeshRendererProxy.h), 프록시 "생성"(컴포넌트 -> 프록시
// 변환)은 게임플레이 소유가 경계 원칙이다. 이 파일은 게임플레이 컴포넌트를 읽는
// 생성자만 담고, Draw 계열 렌더 로직은 RenderEngine/MeshRendererProxy.cpp에 남는다.
#include "MeshRendererProxy.h"
#include "Animator.h"
#include "MeshRenderer.h"
#include "Mesh.h"
#include "Material.h"
#include "FoliageComponent.h"
#include "Terrain.h"
#include "DecalComponent.h"
#include "Texture.h"
#include "SpriteRenderer.h"
#include "ShaderSystem.h"

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

PrimitiveRenderProxy::PrimitiveRenderProxy(DecalComponent* component) :
    m_diffuseTexture(component->GetDecalTexture()),
    m_normalTexture(component->GetNormalTexture()),
    m_occluroughmetalTexture(component->GetORMTexture()),
	m_sliceX(component->sliceX),
	m_sliceY(component->sliceY),
    m_sliceNum(component->sliceNumber)
{
    GameObject* owner = component->GetOwner();
    if (owner)
    {
        //m_materialGuid = m_Material->m_materialGuid;
        m_instancedID = component->GetInstanceID();
    }
    m_proxyType = PrimitiveProxyType::DecalComponent;
}

PrimitiveRenderProxy::PrimitiveRenderProxy(SpriteRenderer* component) :
    m_spriteTexture(component->GetSprite().get()),
    m_customPSOName(component->GetCustomPSOName()),
    m_billboardType(component->GetBillboardType()),
    m_billboardAxis(component->GetBillboardAxis()),
    m_enableDepth(component->IsEnableDepth())
{
    if (!m_customPSOName.empty())
    {
        auto it = ShaderSystem->ShaderAssets.find(m_customPSOName);
        if (it != ShaderSystem->ShaderAssets.end())
        {
            m_customPSO = it->second;
        }
    }
    m_quadMesh = std::make_shared<Mesh>(
        component->GetOwner()->m_name.ToString(),
        PrimitiveCreator::QuadVertices(),
        PrimitiveCreator::QuadIndices()
    );
    m_proxyType = PrimitiveProxyType::SpriteRenderer;
}

PrimitiveRenderProxy::PrimitiveRenderProxy(FoliageComponent* component) :
    m_isSkinnedMesh(false),
	m_foliageInstances(component->GetFoliageInstances()),
	m_foliageTypes(component->GetFoliageTypes()),
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
    for (auto& instance : m_foliageInstances)
    {
        uint32 key = instance.m_foliageTypeID;
        if (!instance.m_isCulled)
        {
            instanceMap[key].push_back(&instance);
        }
    }
}
