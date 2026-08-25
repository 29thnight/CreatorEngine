#pragma once

// 완전히 같은 정점을 하나로 합친다 (트랙 I4, ImportOptions::weldVertices).
//
// ★ 왜 필요한가: legacy 는 Assimp 의 aiProcess_JoinIdenticalVertices 로 이걸
//   했고 experiment 는 하지 않았다. 같은 자산을 비교하면서 한쪽만 용접을
//   생략하고 있었던 것이다. 게다가 `weldVertices` 는 **기본값이 true 인데
//   소비자가 0** 이었다 — 죽은 플래그가 아니라 거짓말하는 플래그였다.
//
// ★ 순서: 법선 생성 **뒤**, 탄젠트 생성 **앞**.
//   - 법선 뒤여야 생성된 법선이 정점 정체성에 들어간다. 앞에서 용접하면
//     평면 법선 패스가 방금 갈라 놓을 정점을 미리 붙여 버린다.
//   - 탄젠트 앞이어야 mikktspace 입력이 줄어든다. 탄젠트 생성은 임포트
//     시간의 60% 안팎이라 여기서 줄이는 것이 그대로 이득이다. 그리고
//     탄젠트 생성은 자기 규약대로 다시 갈라 놓으므로 이음매가 보존된다.
//
// ★ 탄젠트도 정체성의 일부다. 원본이 탄젠트를 들고 온 경우 그것을 무시하고
//   합치면 mikktspace 가 갈라 둔 이음매를 도로 붙이게 된다. 그래서 키는
//   VertexStreams::ValueStreams() **전부**에서 만든다 — 목록이 정본이므로
//   새 스트림이 생기면 복사뿐 아니라 키에도 자동으로 들어온다.

#include "ImportedScene.h"

#include <cstddef>

namespace experiment::importer
{
    struct VertexWeldStats final
    {
        std::size_t meshesProcessed{};
        std::size_t meshesSkipped{};    // 인덱스가 없거나 정점이 없는 메시
        std::size_t verticesBefore{};
        std::size_t verticesAfter{};
        double weldMs{};

        [[nodiscard]] std::size_t VerticesRemoved() const noexcept
        {
            return verticesBefore > verticesAfter ? verticesBefore - verticesAfter : 0;
        }
    };

    // options.weldVertices 가 false 면 아무것도 하지 않는다.
    VertexWeldStats WeldVertices(ImportedScene& scene,
        const ImportOptions& options, ImportNoteSink& notes);
}
