#include "ExperimentParity/ExperimentPoseSampler.h"
#include "ExperimentParity/ExperimentLegacyBridge.h"

#include <cstddef>

namespace RenderTest::sampler
{
    namespace
    {
        namespace ex = experiment;
        using namespace DirectX;

        // 이름이 다른 TU 의 동류 헬퍼와 겹치면 안 된다 — 유니티 빌드가 두 TU 를
        // 합치면 같은 익명 네임스페이스로 병합돼 재정의가 된다.
        double SamplerKeyTime(const ex::TranslationKey& key) { return key.time; }
        double SamplerKeyTime(const ex::RotationKey& key) { return key.time; }
        double SamplerKeyTime(const ex::ScaleKey& key) { return key.time; }

        // legacy CurrentKeyIndex 와 같은 규칙: 보간 구간 [index, index+1] 을
        // 만들 수 있도록 size-2 에서 멈춘다.
        template <typename Key>
        std::size_t LinearIntervalIndex(const std::vector<Key>& keys, double time)
        {
            std::size_t index = 0;
            while (index + 2 < keys.size() && SamplerKeyTime(keys[index + 1]) <= time)
            {
                ++index;
            }
            return index;
        }

        // Step 은 구간이 아니라 "시각 이하의 마지막 키" 하나가 답이다. Linear 용
        // index 를 그대로 쓰면 마지막 키 이후 구간에서 한 칸 뒤처지므로 보정한다.
        template <typename Key>
        std::size_t StepKeyIndex(const std::vector<Key>& keys, double time)
        {
            std::size_t index = LinearIntervalIndex(keys, time);
            if (index + 1 < keys.size() && SamplerKeyTime(keys[index + 1]) <= time)
            {
                ++index;
            }
            return index;
        }

        // 두 키 사이의 정규화 위치. 같은 시각이면 0(0-나누기 금지).
        template <typename Key>
        float Alpha(const Key& k0, const Key& k1, double time)
        {
            const double span = SamplerKeyTime(k1) - SamplerKeyTime(k0);
            if (!(span > 0.0)) return 0.0f;
            return static_cast<float>((time - SamplerKeyTime(k0)) / span);
        }
    }

    ex::Float3 SampleTranslation(const ex::AnimationChannel& channel, double time)
    {
        const auto& keys = channel.translations;
        if (keys.empty()) return {};
        if (keys.size() == 1) return keys.front().value;

        if (channel.translationInterpolation == ex::InterpolationMode::Step)
        {
            return keys[StepKeyIndex(keys, time)].value;
        }

        const std::size_t index = LinearIntervalIndex(keys, time);
        const auto& k0 = keys[index];
        const auto& k1 = keys[index + 1];
        const float t = Alpha(k0, k1, time);
        return {
            k0.value.x + (k1.value.x - k0.value.x) * t,
            k0.value.y + (k1.value.y - k0.value.y) * t,
            k0.value.z + (k1.value.z - k0.value.z) * t };
    }

    ex::Float4 SampleRotation(const ex::AnimationChannel& channel, double time)
    {
        const auto& keys = channel.rotations;
        if (keys.empty()) return { 0.0f, 0.0f, 0.0f, 1.0f };
        if (keys.size() == 1) return keys.front().quaternion;

        if (channel.rotationInterpolation == ex::InterpolationMode::Step)
        {
            return keys[StepKeyIndex(keys, time)].quaternion;
        }

        const std::size_t index = LinearIntervalIndex(keys, time);
        const auto& k0 = keys[index];
        const auto& k1 = keys[index + 1];
        const XMVECTOR slerped = XMQuaternionSlerp(
            XMVectorSet(k0.quaternion.x, k0.quaternion.y, k0.quaternion.z, k0.quaternion.w),
            XMVectorSet(k1.quaternion.x, k1.quaternion.y, k1.quaternion.z, k1.quaternion.w),
            Alpha(k0, k1, time));
        XMFLOAT4 stored;
        XMStoreFloat4(&stored, slerped);
        return { stored.x, stored.y, stored.z, stored.w };
    }

    float SampleUniformScale(const ex::AnimationChannel& channel, double time)
    {
        const auto& keys = channel.scales;
        if (keys.empty()) return 1.0f;
        if (keys.size() == 1) return keys.front().value.x;

        if (channel.scaleInterpolation == ex::InterpolationMode::Step)
        {
            return keys[StepKeyIndex(keys, time)].value.x;
        }

        const std::size_t index = LinearIntervalIndex(keys, time);
        const auto& k0 = keys[index];
        const auto& k1 = keys[index + 1];
        return k0.value.x + (k1.value.x - k0.value.x) * Alpha(k0, k1, time);
    }

    XMMATRIX SampleLocal(const ex::AnimationChannel& channel, double time)
    {
        const ex::Float3 position = SampleTranslation(channel, time);
        const ex::Float4 rotation = SampleRotation(channel, time);
        const float scale = SampleUniformScale(channel, time);

        return XMMatrixScaling(scale, scale, scale)
            * XMMatrixRotationQuaternion(
                XMVectorSet(rotation.x, rotation.y, rotation.z, rotation.w))
            * XMMatrixTranslation(position.x, position.y, position.z);
    }

    void EvaluatePose(const ex::Skeleton& skeleton, const ex::AnimationClip& clip,
        double time, Pose& pose)
    {
        const std::size_t boneCount = skeleton.bones.size();
        pose.finals.assign(boneCount, XMMatrixIdentity());
        pose.hasChannel.assign(boneCount, 0);

        std::vector<const ex::AnimationChannel*> channelOf(boneCount, nullptr);
        for (const ex::AnimationChannel& channel : clip.channels)
        {
            if (ex::IsInRange(channel.bone, boneCount))
                channelOf[channel.bone.Value()] = &channel;
        }

        const XMMATRIX rootTransform = bridge::ToXMMatrix(skeleton.rootTransform);
        const XMMATRIX globalInverse =
            bridge::ToXMMatrix(skeleton.globalInverseTransform);

        // 게시 계약(parent < index) 덕에 단일 순회로 충분하다.
        std::vector<XMMATRIX> globals(boneCount, rootTransform);
        for (std::size_t boneIndex = 0; boneIndex < boneCount; ++boneIndex)
        {
            const ex::Bone& bone = skeleton.bones[boneIndex];
            const XMMATRIX parentGlobal = bone.parent.IsValid()
                ? globals[bone.parent.Value()] : rootTransform;
            if (const ex::AnimationChannel* channel = channelOf[boneIndex])
            {
                const XMMATRIX global = SampleLocal(*channel, time) * parentGlobal;
                globals[boneIndex] = global;
                pose.finals[boneIndex] = bridge::ToXMMatrix(bone.inverseBindMatrix)
                    * global * globalInverse;
                pose.hasChannel[boneIndex] = 1;
            }
            else
            {
                globals[boneIndex] = parentGlobal;
            }
        }
    }
}
