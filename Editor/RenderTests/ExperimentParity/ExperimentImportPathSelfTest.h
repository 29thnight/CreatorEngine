#pragma once

#include <string>

namespace RenderTest
{
    // 임포트 경로 검증: legacy → ImportedScene → ModelDraft.
    //
    // 기존 experiment.model 은 legacy → ModelDraft 직행 경로를 잰다. 이 검사는
    // 그 사이에 임포트 IR 과 변환 경계를 끼워 넣고, 두 경로의 결과를 **이름
    // 기준**으로 비교한다(인덱스는 두 경로의 정렬 정책이 달라 의미가 없다 —
    // 실제 fastgltf/ufbx 임포터가 들어와도 같은 이유로 이름 기준이어야 한다).
    //
    // 답을 얻으려는 질문:
    //   1. ValidateImportedScene 이 legacy 유래 IR 을 통과시키는가
    //   2. 변환 경계가 계수한 손실이 무엇인가
    //   3. TRS 분해 → ComposeTrs 왕복이 원본 행렬을 복원하는가
    //   4. 스켈레톤을 joint 의 **조상 폐포**로 잡은 결정이 실제로 채널 생존을
    //      늘리는가(직행 경로는 클립당 1채널을 잃었다 — 실측 10/620)
    //   5. 두 경로의 정점·스킨 가중치가 일치하는가(서로 다른 bone index 공간을
    //      거치므로 이름 기준으로 비교한다)
    bool RunExperimentImportPathSelfTest(
        const std::string& modelPath, std::string& outLog);
}
