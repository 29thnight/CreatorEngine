#pragma once

#include "ImportedScene.h"

#include <cstddef>

// 탄젠트 생성 패스 (mikktspace).
//
// ★ ImportedScene 위에서 도는 후처리다. 포맷별 임포터가 각자 만들지 않고
//   이 한 곳을 부른다 — 임포터마다 다른 탄젠트가 나오면 같은 모델이 포맷에
//   따라 다르게 보인다.
//
// ★ mikktspace 는 결과를 **코너(면-정점) 단위**로 돌려주고, 원본 헤더가
//   "기존 인덱스 리스트로 덮어쓰면 결과가 틀린다"고 대문자로 경고한다.
//   같은 정점이라도 면에 따라 탄젠트가 갈릴 수 있기 때문이다(UV 이음매·
//   거울 대칭). 그래서 이 패스는 코너별로 받아 두었다가 **재용접**해
//   정점과 인덱스를 새로 만든다. 정점 수가 늘어날 수 있다.
namespace experiment::importer
{
    struct TangentGenerationStats final
    {
        std::size_t meshesProcessed{};   // 실제로 생성한 메시
        std::size_t meshesSkipped{};     // 전제 미충족 또는 이미 탄젠트 보유
        std::size_t verticesBefore{};
        std::size_t verticesAfter{};     // 재용접으로 늘어난 결과

        // 이 패스가 임포트 시간의 60% 안팎을 차지한다(실측: Gunner 27.4/42.4ms).
        // 어디에 쓰였는지 모르면 최적화가 추측이 되므로 구간을 나눠 기록한다.
        double mikktspaceMs{};      // genTangSpaceDefault 호출
        double reorthogonalizeMs{}; // 퇴화 코너 직교화
        double weldMs{};            // 코너 → 정점 재용접

        [[nodiscard]] std::size_t VerticesAdded() const noexcept
        {
            return verticesAfter > verticesBefore ? verticesAfter - verticesBefore : 0;
        }
    };

    // 메시 하나에 탄젠트를 생성한다. positions·normals·uv0·indices 가 모두
    // 있어야 하고 인덱스는 삼각형이어야 한다. 전제가 깨지면 계수하고 건너뛴다
    // (조용히 넘어가지 않는다). 이미 탄젠트가 있으면 손대지 않는다.
    //
    // 성공하면 mesh.streams 와 mesh.indices 가 재용접된 것으로 교체된다.
    [[nodiscard]] bool GenerateTangents(ImportedMesh& mesh,
        const std::string& context, ImportNoteSink& notes,
        TangentGenerationStats& stats, std::uint32_t uvSet = 0);

    // 씬 전체. options.generateMissingTangents 가 false 면 아무것도 하지 않는다.
    TangentGenerationStats GenerateMissingTangents(ImportedScene& scene,
        const ImportOptions& options, ImportNoteSink& notes);
}
