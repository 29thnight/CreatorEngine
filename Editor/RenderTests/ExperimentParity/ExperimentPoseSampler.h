#pragma once

#include "Experiment/ModelData.h"

#include <DirectXMath.h>

#include <cstdint>
#include <vector>

// experiment::Skeleton/AnimationClip 포즈 샘플러.
//
// ★ 정의는 여기 하나뿐이다. 재생 파리티 검사(experiment.anim)와 임포트
//   검사(experiment.gltf)가 각자 샘플러를 들면 둘이 갈라지는 순간 "검사는
//   통과하는데 화면은 다른" 상태가 되므로 공유한다.
//
// 규칙은 legacy calculAni 재현이 기본값이다:
//   - Linear 트랙: 위치 lerp, 회전 slerp, scale 은 **x 성분만** 읽어 3축 동일
//     적용(legacy 가 비균등 scale 을 조용히 uniform 화하는 알려진 특성).
//   - Step 트랙: 시각 이하의 마지막 키 값을 그대로 유지(계단).
namespace RenderTest::sampler
{
    [[nodiscard]] math::vector3 SampleTranslation(
        const experiment::AnimationChannel& channel, double time);
    [[nodiscard]] math::quaternion SampleRotation(
        const experiment::AnimationChannel& channel, double time);
    [[nodiscard]] float SampleUniformScale(
        const experiment::AnimationChannel& channel, double time);

    [[nodiscard]] DirectX::XMMATRIX SampleLocal(
        const experiment::AnimationChannel& channel, double time);

    struct Pose final
    {
        std::vector<DirectX::XMMATRIX> finals{};   // 게시 bone index 기준
        std::vector<std::uint8_t> hasChannel{};
    };

    void EvaluatePose(const experiment::Skeleton& skeleton,
        const experiment::AnimationClip& clip, double time, Pose& pose);
}
