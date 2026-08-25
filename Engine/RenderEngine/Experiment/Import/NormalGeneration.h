#pragma once

#include "ImportedScene.h"

#include <cstddef>
#include <string>

// 법선 생성 패스.
//
// ★ 탄젠트 생성보다 **먼저** 돌아야 한다. mikktspace 가 법선을 입력으로
//   받으므로 법선이 없으면 탄젠트도 만들 수 없다.
//
// ★ 평면(flat) 법선이다. 규약이 둘 다 그렇게 정한다:
//   - glTF 2.0: NORMAL 이 없으면 클라이언트가 flat normal 을 계산해야 하고
//     제공된 탄젠트는 무시한다.
//   - legacy 가 쓰는 aiProcess_GenNormals 도 평면이다(부드러운 쪽은
//     GenSmoothNormals 로 별개다).
//   평면 법선은 면마다 값이 달라야 하므로 **정점 분리가 불가피하다** —
//   삼각형 하나가 정점 3개를 독점한다. 정점 수가 늘어난다.
//
// ★ 포맷별 임포터가 각자 만들지 않고 이 한 곳을 부른다. ufbx 의
//   generate_missing_normals 를 쓰면 glTF 와 FBX 가 다른 알고리즘을 타 같은
//   모델이 포맷에 따라 다르게 보인다(탄젠트에 적용한 것과 같은 규칙).
namespace experiment::importer
{
    struct NormalGenerationStats final
    {
        std::size_t meshesProcessed{};   // 실제로 생성한 메시
        std::size_t meshesSkipped{};     // 이미 법선 보유 또는 전제 미충족
        std::size_t verticesBefore{};
        std::size_t verticesAfter{};     // 평면화로 늘어난 결과
        std::size_t degenerateFaces{};   // 넓이 0 — 법선 방향을 정할 수 없다
    };

    // 메시 하나. 이미 법선이 있으면 손대지 않는다(source 가 정본).
    // 성공하면 streams 와 indices 가 평면화된 것으로 교체된다.
    [[nodiscard]] bool GenerateFlatNormals(ImportedMesh& mesh,
        const std::string& context, ImportNoteSink& notes,
        NormalGenerationStats& stats);

    // 씬 전체. options.generateMissingNormals 가 false 면 아무것도 하지 않는다.
    NormalGenerationStats GenerateMissingNormals(ImportedScene& scene,
        const ImportOptions& options, ImportNoteSink& notes);
}
