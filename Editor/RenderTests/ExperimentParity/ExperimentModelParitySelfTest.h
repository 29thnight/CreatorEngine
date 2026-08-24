#pragma once

#include <string>

namespace RenderTest
{
    // 기존(legacy) Model 로딩 경로와 Experiment 모델 경계를 같은 자산으로 비교한다.
    //
    //  1. legacy Model::LoadModelShared 로 로드
    //  2. legacy 구조를 experiment::ModelDraft 로 브리지(읽기 전용)
    //  3. experiment::ModelLoader::Load 로 검증·게시
    //  4. 게시된 experiment::Model 과 legacy 구조를 전수 비교
    //
    // 실패 사유는 셋으로 구분되어 outLog 에 남는다:
    //  - [bridge]   legacy 구조가 브리지 자체가 불가능한 형태
    //  - [validate] Experiment 검증 계약 위반(새 파이프라인이 만날 실데이터 결함)
    //  - [diff]     게시본과 legacy 의 구조 불일치
    //
    // node 애니메이션이 bone 에 매핑되지 못해 떨어진 채널 수도 함께 보고한다.
    // 이는 "Experiment 가 bone 대상 애니메이션만 표현한다"는 설계 갭의 실측치다.
    bool RunExperimentModelParitySelfTest(
        const std::string& modelPath, std::string& outLog);
}
