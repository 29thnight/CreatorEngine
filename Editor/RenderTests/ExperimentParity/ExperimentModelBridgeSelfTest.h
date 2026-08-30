#pragma once

#include <string>

namespace RenderTest
{
    // I5-D1a — experiment::Model → legacy ::Model 역브리지 왕복 검사.
    //
    // legacy 실모델 → 정브리지(legacy→draft) → ModelLoader 게시 → 역브리지
    // (DataSystem::BuildLegacyModelFromExperiment) → legacy' 를 만들고,
    // 원본 legacy와 legacy'를 대조한다. 정브리지가 본을 위상 정렬하므로
    // 스켈레톤·정점 본 lane 비교는 remap을 통과시킨다. bitangent는 experiment가
    // handedness(w)만 저장하므로 재구성 방향 일치(부호 보존)로 판정한다.
    [[nodiscard]] bool RunExperimentModelBridgeSelfTest(
        const std::string& modelPath, std::string& outLog);
}
