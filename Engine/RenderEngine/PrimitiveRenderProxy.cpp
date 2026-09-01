// 렌더 측 프록시 로직만 남는다 — 컴포넌트를 읽는 생성자들은
// ScriptBinder/PrimitiveProxyBridge.cpp에 있다 (PHASE 4-2 C1).
#include "PrimitiveRenderProxy.h"
#include "Mesh.h"

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

void FoliageRenderProxy::RebuildInstanceMap()
{
	instanceMap.clear();

	// 색인이 원소 주소를 든다. m_foliageInstances를 갈아 끼운 직후에만
	// 부르는 규약이고, 그래서 이 함수가 벡터를 건드리지 않는다.
	for (auto& instance : m_foliageInstances)
	{
		if (instance.m_foliageTypeID >= m_foliageTypes.size()) continue;
		instanceMap[instance.m_foliageTypeID].push_back(&instance);
	}
}

std::vector<FoliageRenderProxy::DrawSource>
FoliageRenderProxy::CaptureDrawSources() const
{
	std::vector<DrawSource> draws;
	draws.reserve(m_foliageInstances.size());

	// 타입 순서로 내보내 같은 mesh/material 인스턴스가 인접하게 남도록 한다.
	// Forward 투명 정렬은 이후 view 단계가 다시 back-to-front로 수행한다.
	for (std::size_t typeIndex = 0; typeIndex < m_foliageTypes.size(); ++typeIndex)
	{
		const FoliageType& type = m_foliageTypes[typeIndex];
		if (!type.m_mesh) continue;

		const auto found = instanceMap.find(static_cast<uint32>(typeIndex));
		if (found == instanceMap.end()) continue;

		for (const FoliageInstance* instance : found->second)
		{
			if (nullptr == instance
				|| instance->m_foliageTypeID != static_cast<uint32>(typeIndex))
			{
				continue;
			}

			DrawSource draw{};
			draw.mesh = type.m_mesh;
			draw.material = type.m_material;
			draw.experimentModel = type.m_experimentModel;
			draw.experimentMeshIndex = type.m_experimentMeshIndex;
			draw.worldMatrix = instance->m_worldMatrix;
			draw.worldBounds = math::transform(
				type.m_mesh->GetBoundingBox(), instance->m_worldMatrix);
			draw.foliageTypeID = static_cast<uint32>(typeIndex);
			draws.push_back(std::move(draw));
		}
	}

	return draws;
}
