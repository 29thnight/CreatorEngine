#pragma once

// 삼각형 순서와 정점 순서를 GPU 캐시에 맞춘다 (트랙 I4,
// ImportOptions::optimizeVertexCache).
//
// ★ `weldVertices` 와 똑같은 상태였다 — **기본값이 true 인데 소비자가 0.**
//   죽은 플래그가 아니라 거짓말하는 플래그였다.
//
// ★ 순서: 모든 재용접 패스 **뒤**, 즉 파이프라인 맨 끝.
//   법선 생성·용접·탄젠트 생성은 전부 정점을 갈라내거나 합치면서 순서를
//   다시 쓴다. 그 앞에서 정렬해 봐야 뒤에서 무너진다.
//
// ★ 두 단계는 성격이 다르다.
//   - vertex cache: **인덱스만** 바꾼다(삼각형 순서). 정점 배열은 그대로다.
//   - vertex fetch: **정점 배열을 다시 늘어놓는다.** VertexStreams 는 SoA 라
//     meshoptimizer 의 단일 버퍼 API 를 쓸 수 없고, 헤더가 지시하는 대로
//     `meshopt_optimizeVertexFetchRemap` 으로 remap 만 받아 스트림마다 적용한다.
//
// ★ 효과를 **재서** 남긴다. "최적화했다"는 말은 검증할 수 없지만 ACMR 과
//   overfetch 는 숫자다. 용접에서 배운 것 — 아무 일도 하지 않는 패스와
//   일하는 패스가 겉으로 같아 보이면 안 된다.

#include "ImportedScene.h"

#include <cstddef>

namespace experiment::importer
{
    struct VertexCacheStats final
    {
        std::size_t meshesProcessed{};
        std::size_t meshesSkipped{};
        std::size_t verticesBefore{};
        std::size_t verticesAfter{};   // 참조되지 않는 정점은 사라진다
        double optimizeMs{};

        // 가중 평균(삼각형 수 기준). 낮을수록 좋다.
        double acmrBefore{};           // 변환된 정점 / 삼각형 (최선 0.5)
        double acmrAfter{};
        double overfetchBefore{};      // 가져온 바이트 / 정점 버퍼 크기 (최선 1.0)
        double overfetchAfter{};
    };

    // options.optimizeVertexCache 가 false 면 아무것도 하지 않는다.
    VertexCacheStats OptimizeVertexCache(ImportedScene& scene,
        const ImportOptions& options, ImportNoteSink& notes);
}
