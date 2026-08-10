// 렌더 프록시의 컴포넌트 읽기 생성자들 (PHASE 4-2 C1).
//
// 프록시 "타입"은 렌더 소유(RenderEngine/PrimitiveRenderProxy.h ·
// LightRenderProxy.h), 프록시 "생성"(컴포넌트 -> 프록시 변환)은 게임플레이
// 소유가 경계 원칙이다. 이 파일은 게임플레이 컴포넌트를 읽는 생성자만 담고,
// 렌더 로직은 RenderEngine 쪽 .cpp에 남는다.
#include "PrimitiveRenderProxy.h"
#include "LightRenderProxy.h"
#include "Animator.h"
#include "MeshRenderer.h"
#include "Mesh.h"
#include "Material.h"
#include "FoliageComponent.h"
#include "Terrain.h"
#include "DecalComponent.h"
#include "Texture.h"
#include "SpriteRenderer.h"
#include "LightComponent.h"

// 월드 변환은 RenderProxy(기반)의 필드라 파생 생성자의 초기화 목록에
// 넣을 수 없다. 읽는 자리가 다섯 곳이라 함수로 묶었다.
static void CopyWorldTransform(RenderProxy& proxy, GameObject* owner)
{
    if (nullptr == owner) return;

    proxy.m_worldMatrix = owner->m_transform.GetWorldMatrix();
    proxy.m_worldPosition = owner->m_transform.GetWorldPosition();
}

MeshRenderProxy::MeshRenderProxy(MeshRenderer* component) :
    PrimitiveRenderProxy(kProxyType),
    m_Material(component->m_Material),
    m_Mesh(component->m_Mesh),
    m_LightMapping(component->m_LightMapping),
    m_isSkinnedMesh(component->m_isSkinnedMesh)
{
    CopyWorldTransform(*this, component->GetOwner());

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
        SetNeedUpdateCulling(true);
    }

    // 컬링용 월드 AABB. 스키닝은 담지 않는다 — 메시의 상자가 바인드 포즈
    // 것이라 애니메이션이 그 밖으로 정점을 민다(헤더의 m_hasWorldBounds).
    m_hasWorldBounds = (!m_isSkinnedMesh && nullptr != m_Mesh);
    if (m_hasWorldBounds)
    {
        m_worldBounds = component->GetBoundingBox();
    }
}

TerrainRenderProxy::TerrainRenderProxy(TerrainComponent* component) :
    PrimitiveRenderProxy(kProxyType),
    m_terrainMesh(component->GetMesh()),
    m_terrainMaterial(component->GetMaterial())
{
    CopyWorldTransform(*this, component->GetOwner());

    if (nullptr != component->GetOwner())
    {
        m_instancedID = component->GetInstanceID();
    }
}

DecalRenderProxy::DecalRenderProxy(DecalComponent* component) :
    PrimitiveRenderProxy(kProxyType),
    m_diffuseTexture(component->GetDecalTexture()),
    m_normalTexture(component->GetNormalTexture()),
    m_occluroughmetalTexture(component->GetORMTexture()),
	m_sliceX(component->sliceX),
	m_sliceY(component->sliceY),
    m_sliceNum(component->sliceNumber)
{
    if (nullptr != component->GetOwner())
    {
        m_instancedID = component->GetInstanceID();
    }
}

SpriteRenderProxy::SpriteRenderProxy(SpriteRenderer* component) :
    PrimitiveRenderProxy(kProxyType),
    m_spriteTexture(component->GetSprite().get()),
    m_billboardType(component->GetBillboardType()),
    m_billboardAxis(component->GetBillboardAxis()),
    m_enableDepth(component->IsEnableDepth())
{
    m_quadMesh = std::make_shared<Mesh>(
        component->GetOwner()->m_name.ToString(),
        PrimitiveCreator::QuadVertices(),
        PrimitiveCreator::QuadIndices()
    );
}

FoliageRenderProxy::FoliageRenderProxy(FoliageComponent* component) :
    PrimitiveRenderProxy(kProxyType),
	m_foliageInstances(component->GetFoliageInstances()),
	m_foliageTypes(component->GetFoliageTypes())
{
    CopyWorldTransform(*this, component->GetOwner());

    if (nullptr != component->GetOwner())
    {
        m_instancedID = component->GetInstanceID();
    }

    RebuildInstanceMap();
}

// ── 광원 ──

LightRenderProxy::Values LightRenderProxy::ReadFrom(LightComponent* component)
{
    Values values{};
    if (nullptr == component) return values;

    GameObject* owner = component->GetOwner();
    if (nullptr != owner)
    {
        values.worldPosition = owner->m_transform.GetWorldPosition();

        Mathf::Vector4 direction =
            XMVector3Rotate(XMVectorSet(0, 0, 1, 0), owner->m_transform.GetWorldQuaternion());
        direction.Normalize();
        values.direction = direction;
    }

    // ★ 세기를 색에 곱하지 않는다.
    //
    //   예전 경로는 컴포넌트가 m_color * m_intencity를 씬 광원에 넣고,
    //   라이브가 다시 color.w = m_intencity를 실어 셰이더의 rgb*a에서
    //   세기가 제곱으로 들어갔다(세기 1.6이면 2.56배). 저작 색을 그대로
    //   싣고 세기를 따로 두면 곱은 셰이더 한 번뿐이다.
    values.color = component->m_color;
    values.intensity = component->m_intencity;

    values.constantAttenuation = component->m_constantAttenuation;
    values.linearAttenuation = component->m_linearAttenuation;
    values.quadraticAttenuation = component->m_quadraticAttenuation;
    values.spotLightAngle = component->m_spotLightAngle;
    values.range = component->m_range;
    values.lightType = static_cast<int>(component->m_lightType);
    values.lightStatus = static_cast<int>(component->m_lightStatus);

    return values;
}

LightRenderProxy::LightRenderProxy(LightComponent* component)
{
    if (nullptr == component) return;

    m_instancedID = component->GetInstanceID();
    Apply(ReadFrom(component));
}
