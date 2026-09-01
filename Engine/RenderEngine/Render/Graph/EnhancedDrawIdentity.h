#pragma once

// I6-C — 드로우 아이템의 **신원·반경 창구**.
//
// 신원은 원래 `Mesh*` 포인터였다: 지오메트리 맵 키·배치 키·정렬 기준이 전부
// 게임 객체의 주소였다. 렌더가 게임 자료구조를 신원으로 쓰는 그 결합이 I6이
// 지우려는 것이고, 값 키는 자산 신원(experiment stableKey)에서 나온다.
//
// ★ 왜 아이템의 멤버 함수가 아니라 이 헤더인가: `EnhancedRenderPass.h`는
//   `Mesh`를 전방선언만 한다(렌더 그래프가 자산 타입을 알지 않는다는 경계).
//   legacy 유도는 `Mesh`를 역참조해야 하므로 그 경계를 넘지 않는 자리에 둔다.
//
// ★ 왜 "전 생산자가 새 필드를 채운다"로 하지 않았는가: 아이템을 짓는 곳이
//   제품 BuildDrawPool 하나가 아니다(격리 하네스 여럿·dx12.scene 자체 시공).
//   계약으로 걸었더니 안 채운 곳이 조용히 0이 되어 **드로우가 통째로
//   사라졌다** — 실측: 업로드 0·포워드 배치 0·dx12.scene 실패. 창구는 그
//   계약 없이 성립한다. legacy 유도는 `mesh`와 함께 I6-E에서 죽는다.

#include "EnhancedRenderPass.h"
#include "../../Mesh.h"

#include <cstddef>

namespace enhanced_draw
{
    [[nodiscard]] inline std::size_t GeometryKey(
        const EnhancedDrawItem& draw) noexcept
    {
        if (0 != draw.geometryKey) return draw.geometryKey;
        if (0 != draw.experimentView.stableKey)
        {
            return draw.experimentView.stableKey;
        }
        return nullptr != draw.mesh
            ? static_cast<std::size_t>(draw.mesh->m_hashingMesh.m_ID_Data)
            : std::size_t{ 0 };
    }

    [[nodiscard]] inline float BoundRadius(
        const EnhancedDrawItem& draw) noexcept
    {
        if (draw.boundRadius > 0.f) return draw.boundRadius;
        return nullptr != draw.mesh
            ? draw.mesh->GetBoundingSphere().radius : 0.f;
    }
}
