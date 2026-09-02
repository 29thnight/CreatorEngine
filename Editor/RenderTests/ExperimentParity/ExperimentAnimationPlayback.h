#pragma once

#include <string>

namespace RenderTest
{
    // Experiment 모델 경계의 애니메이션 재생 배선 검증.
    //
    // 게시된 experiment::Model 의 clip 을 자체 샘플러(legacy calculAni 와 같은
    // 규칙: pos lerp · rot slerp · scale 은 x 성분 uniform · S*R*T 합성,
    // global = local * parent, 채널 없는 bone 은 parent 전달)로 재생해
    //  1. 스키닝 팔레트가 전 구간 유한한지
    //  2. 시간에 따라 포즈가 실제로 변하는지(재생)
    //  3. legacy 데이터·순회로 계산한 참조 팔레트와 일치하는지(브리지 충실도)
    // 를 검사한다.
    bool RunExperimentAnimationPlaybackSelfTest(
        const std::string& modelPath, std::string& outLog);

    // PHASE 17 local high-fidelity corpus acceptance.  Unlike experiment.anim,
    // this exercises the experiment source importer and pose sampler directly;
    // it does not require the retiring Assimp/legacy bridge to understand a
    // Blender-authored glTF 2 file.
    bool RunPhase17ModelCorpusSelfTest(
        const std::string& modelPath, bool requireAnimation, std::string& outLog);

    // legacy 로드 대 Experiment 경계 비용 벤치마크.
    // legacy full load / 브리지 변환 / Validate / 게시(Load) / 포즈 샘플링을
    // 각각 반복 측정한다. Debug 빌드 수치는 방향 참고용 — 판정은 Release 로만.
    bool RunExperimentModelBenchmark(
        const std::string& modelPath, int iterations, std::string& outLog);
}
