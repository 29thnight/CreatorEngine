#include "ModelAnimationSampler.h"

#include <mathematics/transform.hpp>

#include <algorithm>
#include <cmath>

namespace assets::animation
{
    namespace
    {
        // legacy CurrentKeyIndex와 같은 규칙: 보간 구간 [index, index+1]을 만들 수
        // 있도록 size-2에서 멈춘다. 경계는 등호 제외 — time이 키 시각과 정확히
        // 일치하면 이전 구간(t=1)이다(experiment 샘플러·실물 legacy와 같은 부동소수점
        // 경로 — animtick 골든이 이 비트를 잰다).
        template <typename Key>
        [[nodiscard]] std::size_t LinearIntervalIndex(const std::vector<Key>& keys,
            double time) noexcept
        {
            std::size_t index = 0;
            while (index + 2 < keys.size() && keys[index + 1].time < time)
            {
                ++index;
            }
            return index;
        }

        // Step은 "시각 이하의 마지막 키" 하나가 답이다. Linear 인덱스를 그대로 쓰면
        // 마지막 키 이후 구간에서 한 칸 뒤처지므로 보정한다.
        template <typename Key>
        [[nodiscard]] std::size_t StepKeyIndex(const std::vector<Key>& keys,
            double time) noexcept
        {
            std::size_t index = LinearIntervalIndex(keys, time);
            if (index + 1 < keys.size() && keys[index + 1].time <= time)
            {
                ++index;
            }
            return index;
        }

        template <typename Key>
        [[nodiscard]] float Alpha(const Key& k0, const Key& k1, double time) noexcept
        {
            const double span = k1.time - k0.time;
            if (!(span > 0.0)) return 0.0f;
            return static_cast<float>((time - k0.time) / span);
        }
    }

    math::vector3 SampleTranslation(const ModelAnimationTrack& track, double time)
    {
        const auto& keys = track.translations;
        if (keys.empty()) return {};
        if (keys.size() == 1) return keys.front().value;
        if (track.translationInterpolation == ModelInterpolationMode::Step)
        {
            return keys[StepKeyIndex(keys, time)].value;
        }
        const std::size_t index = LinearIntervalIndex(keys, time);
        const auto& k0 = keys[index];
        const auto& k1 = keys[index + 1];
        return math::lerp(k0.value, k1.value, Alpha(k0, k1, time));
    }

    math::quaternion SampleRotation(const ModelAnimationTrack& track, double time)
    {
        const auto& keys = track.rotations;
        if (keys.empty()) return { 0.0f, 0.0f, 0.0f, 1.0f };
        if (keys.size() == 1) return keys.front().value;
        if (track.rotationInterpolation == ModelInterpolationMode::Step)
        {
            return keys[StepKeyIndex(keys, time)].value;
        }
        const std::size_t index = LinearIntervalIndex(keys, time);
        const auto& k0 = keys[index];
        const auto& k1 = keys[index + 1];
        return math::slerp(k0.value, k1.value, Alpha(k0, k1, time));
    }

    float SampleUniformScale(const ModelAnimationTrack& track, double time)
    {
        const auto& keys = track.scales;
        if (keys.empty()) return 1.0f;
        if (keys.size() == 1) return keys.front().value.x;
        if (track.scaleInterpolation == ModelInterpolationMode::Step)
        {
            return keys[StepKeyIndex(keys, time)].value.x;
        }
        const std::size_t index = LinearIntervalIndex(keys, time);
        const auto& k0 = keys[index];
        const auto& k1 = keys[index + 1];
        return k0.value.x + (k1.value.x - k0.value.x) * Alpha(k0, k1, time);
    }

    math::matrix4x4 SampleLocal(const ModelAnimationTrack& track, double time)
    {
        const math::vector3 position = SampleTranslation(track, time);
        const math::quaternion rotation = SampleRotation(track, time);
        const float scale = SampleUniformScale(track, time);
        return math::compose(math::vector3{ scale, scale, scale }, rotation, position);
    }

    std::size_t CountUniqueKeyTimes(const ModelAnimationAsset& clip, double eps)
    {
        std::vector<double> times;
        for (const ModelAnimationTrack& track : clip.tracks)
        {
            for (const ModelTranslationKey& key : track.translations) times.push_back(key.time);
            for (const ModelRotationKey& key : track.rotations) times.push_back(key.time);
            for (const ModelScaleKey& key : track.scales) times.push_back(key.time);
        }
        if (times.empty()) return 0;
        std::sort(times.begin(), times.end());
        std::size_t uniqueCount = 1;
        double previous = times[0];
        for (std::size_t i = 1; i < times.size(); ++i)
        {
            if (std::abs(times[i] - previous) > eps)
            {
                ++uniqueCount;
                previous = times[i];
            }
        }
        return uniqueCount;
    }

    void BuildTrackTable(const ModelAnimationAsset& clip, std::size_t boneCount,
        std::vector<const ModelAnimationTrack*>& outTable)
    {
        outTable.assign(boneCount, nullptr);
        for (const ModelAnimationTrack& track : clip.tracks)
        {
            if (track.bone < boneCount) outTable[track.bone] = &track;
        }
    }

    void EvaluatePose(const ModelSkeletonAsset& skeleton,
        const ModelAnimationAsset& clip, double time, Pose& pose)
    {
        const std::size_t boneCount = skeleton.bones.size();
        pose.finals.assign(boneCount, math::matrix4x4::identity());
        pose.hasTrack.assign(boneCount, 0);

        std::vector<const ModelAnimationTrack*> trackOf;
        BuildTrackTable(clip, boneCount, trackOf);

        std::vector<math::matrix4x4> globals(boneCount, skeleton.rootTransform);
        for (std::size_t boneIndex = 0; boneIndex < boneCount; ++boneIndex)
        {
            const ModelBoneAsset& bone = skeleton.bones[boneIndex];
            const math::matrix4x4 parentGlobal =
                (bone.parent != kInvalidModelAssetIndex && bone.parent < boneIndex)
                ? globals[bone.parent] : skeleton.rootTransform;
            if (const ModelAnimationTrack* track = trackOf[boneIndex])
            {
                const math::matrix4x4 global = SampleLocal(*track, time) * parentGlobal;
                globals[boneIndex] = global;
                pose.finals[boneIndex] =
                    bone.inverseBindMatrix * global * skeleton.globalInverseTransform;
                pose.hasTrack[boneIndex] = 1;
            }
            else
            {
                globals[boneIndex] = parentGlobal;
            }
        }
    }
}
