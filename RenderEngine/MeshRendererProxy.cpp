// 렌더 측 프록시 로직만 남는다 — 컴포넌트를 읽는 생성자들은
// ScriptBinder/PrimitiveProxyBridge.cpp로 이동했다 (PHASE 4-2 C1).
#include "MeshRendererProxy.h"
#include "Mesh.h"
#include "RenderScene.h"
#include "Camera.h"
#include "TerrainMesh.h"
#include "Texture.h"

//constexpr size_t TRANSFORM_SIZE = sizeof(Mathf::xMatrix) * MAX_BONES;

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
    m_bitflag(other.m_bitflag),
    m_isAnimationEnabled(other.m_isAnimationEnabled),
    m_isEnableShadow(other.m_isEnableShadow),
    m_isShadowCast(other.m_isShadowCast),
    m_isShadowRecive(other.m_isShadowRecive),
	m_isInstanced(other.m_isInstanced),
    m_terrainMesh(other.m_terrainMesh),
	m_terrainMaterial(other.m_terrainMaterial),
    m_isNeedUpdateCulling(other.m_isNeedUpdateCulling),
    m_diffuseTexture(other.m_diffuseTexture),
    m_normalTexture(other.m_normalTexture),
    m_occluroughmetalTexture(other.m_occluroughmetalTexture),
    m_sliceX(other.m_sliceX),
    m_sliceY(other.m_sliceY),
    m_sliceNum(other.m_sliceNum),
    m_quadMesh(other.m_quadMesh),
    m_spriteTexture(other.m_spriteTexture),
    m_customPSOName(other.m_customPSOName),
    m_customPSO(other.m_customPSO),
    m_billboardType(other.m_billboardType),
    m_billboardAxis(other.m_billboardAxis),
    m_foliageInstances(other.m_foliageInstances)
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
    m_bitflag(other.m_bitflag),
    m_isAnimationEnabled(other.m_isAnimationEnabled),
	m_isEnableShadow(other.m_isEnableShadow),
    m_isShadowCast(other.m_isShadowCast),
    m_isShadowRecive(other.m_isShadowRecive),
    m_isInstanced(other.m_isInstanced),
	m_terrainMesh(std::exchange(other.m_terrainMesh, nullptr)),
    m_terrainMaterial(std::exchange(other.m_terrainMaterial, nullptr)),
	m_isNeedUpdateCulling(other.m_isNeedUpdateCulling),
    m_diffuseTexture(std::exchange(other.m_diffuseTexture, nullptr)),
    m_normalTexture(std::exchange(other.m_normalTexture, nullptr)),
    m_occluroughmetalTexture(std::exchange(other.m_occluroughmetalTexture, nullptr)),
    m_sliceX(other.m_sliceX),
    m_sliceY(other.m_sliceY),
    m_sliceNum(other.m_sliceNum),
    m_quadMesh(std::move(other.m_quadMesh)),
    m_spriteTexture(std::exchange(other.m_spriteTexture, nullptr)),
    m_customPSOName(std::move(other.m_customPSOName)),
    m_customPSO(std::move(other.m_customPSO)),
    m_billboardType(other.m_billboardType),
    m_billboardAxis(other.m_billboardAxis),
    m_foliageInstances(std::move(other.m_foliageInstances))
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
    case PrimitiveProxyType::DecalComponent:
    {
        Debug->LogError("DecalComponent does not support normal draw function");
        break;
    }
    default:
        break;
    }
}

void PrimitiveRenderProxy::DestroyProxy()
{
	m_proxyType = PrimitiveProxyType::Expired;
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
