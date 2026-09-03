#pragma once
// PHASE 3.75 MBC8 — typed 재생 데이터(ModelSkeletonAsset·ModelAnimationAsset)의
// 포즈 샘플러와 클립 계량.
//
// 산술 규칙은 experiment::sampler(PoseSampler.h)와 같다 — legacy calculAni 재현:
//   - Linear 트랙: 위치 lerp, 회전 slerp, scale은 **x 성분만** 읽어 3축 동일 적용.
//   - Step 트랙: 시각 이하의 마지막 키 값을 유지(계단).
//   - 보간 구간 경계는 등호 제외(keys[index+1] < time인 동안 전진).
// 같은 자산의 재생 골든(experiment.animtick poseDigest)이 experiment 경로와 비트
// 단위로 같아야 한다 — 그것이 이 파일의 정확성 게이트다. experiment 샘플러는 MBC9에서
// 은퇴하고 이 파일만 남는다.

#include "ModelAssetGeneration.h"

#include <mathematics/matrix4x4.hpp>
#include <mathematics/quaternion.hpp>
#include <mathematics/vector3.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace assets::animation
{
    [[nodiscard]] math::vector3 SampleTranslation(
        const ModelAnimationTrack& track, double time);
    [[nodiscard]] math::quaternion SampleRotation(
        const ModelAnimationTrack& track, double time);
    [[nodiscard]] float SampleUniformScale(
        const ModelAnimationTrack& track, double time);
    [[nodiscard]] math::matrix4x4 SampleLocal(
        const ModelAnimationTrack& track, double time);

    // 클립의 키프레임 수 = 유니크 키 시각 개수(eps 1e-6) — legacy 임포터 정의가
    // 정본이고 experiment::clip::CountUniqueKeyTimes와 같은 값이다. 이벤트 저작의
    // frameKey 상한과 key(0~1 진행률) 환산이 이 값을 쓴다.
    [[nodiscard]] std::size_t CountUniqueKeyTimes(
        const ModelAnimationAsset& clip, double eps = 1e-6);

    // bone index → track. 범위 밖 bone은 무시한다(게시 검증이 이미 막는다).
    void BuildTrackTable(const ModelAnimationAsset& clip, std::size_t boneCount,
        std::vector<const ModelAnimationTrack*>& outTable);

    struct Pose final
    {
        std::vector<math::matrix4x4> finals{};   // 게시 bone index 기준
        std::vector<std::uint8_t> hasTrack{};
    };

    // 단일 순회 포즈 평가(parent < index 계약). 진단·검사 전용 — 제품 틱은
    // AnimationJob이 같은 규칙으로 컨트롤러·블렌드를 얹어 계산한다.
    void EvaluatePose(const ModelSkeletonAsset& skeleton,
        const ModelAnimationAsset& clip, double time, Pose& pose);
}
