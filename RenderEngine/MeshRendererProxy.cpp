#include "MeshRendererProxy.h"
#include "MeshRenderer.h"
#include "Mesh.h"
#include "RenderScene.h"
#include "Material.h"
#include "Core.OctreeNode.h"
#include "CullingManager.h"

constexpr size_t TRANSFORM_SIZE = sizeof(Mathf::xMatrix) * MAX_BONES;

PrimitiveRenderProxy::PrimitiveRenderProxy(MeshRenderer* component) :
    m_Material(component->m_Material),
    m_Mesh(component->m_Mesh),
    m_LightMapping(component->m_LightMapping),
    m_isSkinnedMesh(component->m_isSkinnedMesh),
    m_worldMatrix(component->GetOwner()->m_transform.GetWorldMatrix())
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

        m_materialGuid = m_Material->m_materialGuid;
        m_instancedID = component->GetInstanceID();
    }

    if (!m_isSkinnedMesh)
    {
        //TODO : Change CullingManager Collect Class : MeshRenderer -> PrimitiveRenderProxy
        //CullingManagers->Insert(this);

        m_isNeedUptateCulling = true;
    }
}

PrimitiveRenderProxy::PrimitiveRenderProxy(TerrainComponent* component)
{
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
    m_finalTransforms(other.m_finalTransforms)
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
    m_finalTransforms(std::exchange(other.m_finalTransforms, nullptr))
{
}

void PrimitiveRenderProxy::Draw()
{
    if (nullptr == m_Mesh) return;

    m_Mesh->Draw();
}

void PrimitiveRenderProxy::Draw(ID3D11DeviceContext* _defferedContext)
{
    if (nullptr == m_Mesh || nullptr == _defferedContext) return;

    m_Mesh->Draw(_defferedContext);
}

void PrimitiveRenderProxy::DistroyProxy()
{
    RenderScene::RegisteredDistroyProxyGUIDs.push(m_instancedID);
}
