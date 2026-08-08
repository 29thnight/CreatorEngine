// 렌더 측 프록시 로직만 남는다 — 컴포넌트를 읽는 생성자들은
// ScriptBinder/PrimitiveProxyBridge.cpp로 이동했다 (PHASE 4-2 C1).
#include "MeshRendererProxy.h"
#include "RenderScene.h"

PrimitiveRenderProxy::~PrimitiveRenderProxy()
{
}

// ── DX11 드로우 경로를 걷었다 (PHASE 3-1 재정의, T5) ──
//
// 여기 Draw · DrawShadow · DrawInstanced 셋과 LOD 선택 둘
// (InitializeLODs · GetLODLevel)이 있었다. 전부 호출자가 0이다 — DX11
// 렌더러가 은퇴하면서 프록시의 드로우를 부르던 쪽이 통째로 사라졌고,
// DX12는 프록시를 그리지 않는다(EnhancedDrawItem으로 필요한 것만 복사해
// 간다). 그래서 이 파일이 Mesh · TerrainMesh · Camera · Texture를
// include하던 이유도 함께 없어졌다.
//
// ★ 지형이 이 함수의 마지막 사용처였다. TerrainComponent 분기가
//   m_terrainMesh->Draw(deferredContext)를 불렀고, 그것이 TerrainMesh의
//   DX11 드로우를 살려 두던 유일한 줄이다. 그쪽도 함께 걷었다.
//
// ★ m_EnableLOD는 남긴다. 게임 쪽(ProxyCommand)이 여전히 저작 값으로
//   설정하므로 의도가 살아 있고, 소비자는 DX12 지형·메시 경로가 LOD를
//   다시 붙일 때 생긴다. m_currLOD는 GetLODLevel만 쓰던 값이라 지웠다.

void PrimitiveRenderProxy::DestroyProxy()
{
	m_proxyType = PrimitiveProxyType::Expired;
    RenderScene::RegisteredDestroyProxyGUIDs.push(m_instancedID);
}
