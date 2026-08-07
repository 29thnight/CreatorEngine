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
