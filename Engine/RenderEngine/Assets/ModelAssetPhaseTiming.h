#pragma once
// PHASE 3.75 MBC11 — 모델 자산 경로의 단계별 경과 시간(읽기 전용 진단).
//
// LoadModelAssetGeneration / AuthorModelAsset 결과에 붙는 벽시계 분해다. MBC0 기준선 문서
// §4.3이 "새 경로의 cooked 읽기 시간"을 계측 공백으로 적었고, §8.4 예산(B1/B2)을 판정하려면
// 총합만이 아니라 어느 단계(embedded texture 디코드·corpus 스캔·stage 검증)가 비용인지
// 알아야 한다. 값은 판정에 쓰지 않고 `assets.modelbench`가 그대로 출력한다.
#include <string>
#include <vector>

namespace assets
{
    struct ModelAssetPhaseMs final
    {
        std::string phase{};
        double milliseconds{};
    };

    using ModelAssetPhaseTimeline = std::vector<ModelAssetPhaseMs>;
}
