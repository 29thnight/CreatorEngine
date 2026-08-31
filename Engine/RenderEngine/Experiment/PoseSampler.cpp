#include "PoseSampler.h"

#include <cstddef>
#include <mathematics/transform.hpp>

// RenderTests ExperimentPoseSampler.cpp에서 이동(I5-D4e-1 승격) — 산술 무변경.
namespace experiment::sampler
{
	namespace
	{
		// 이름이 다른 TU 의 동류 헬퍼와 겹치면 안 된다 — 유니티 빌드가 두 TU 를
		// 합치면 같은 익명 네임스페이스로 병합돼 재정의가 된다.
		double SamplerKeyTime(const experiment::TranslationKey& key) { return key.time; }
		double SamplerKeyTime(const experiment::RotationKey& key) { return key.time; }
		double SamplerKeyTime(const experiment::ScaleKey& key) { return key.time; }

		// legacy CurrentKeyIndex 와 같은 규칙: 보간 구간 [index, index+1] 을
		// 만들 수 있도록 size-2 에서 멈춘다.
		//
		// ★ 경계는 등호 제외다(keys[index+1] < time 인 동안 전진) — legacy 는
		//   "time <= 다음 키"인 첫 구간에 남으므로, time 이 키 시각과 정확히
		//   일치하면 이전 구간(t=1)이지 다음 구간(t=0)이 아니다. 값은 같지만
		//   부동소수점 경로가 달라 팔레트 비트가 갈린다(I5-D4e-1 animtick 실측:
		//   프레임 격자 키에 fraction 0.5 가 정확히 떨어져 4.88e-4 편차 —
		//   구 규약 `<=` 는 참조 재구현끼리만 맞고 실물 legacy 와 어긋났다).
		template <typename Key>
		std::size_t LinearIntervalIndex(const std::vector<Key>& keys, double time)
		{
			std::size_t index = 0;
			while (index + 2 < keys.size() && SamplerKeyTime(keys[index + 1]) < time)
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

	math::vector3 SampleTranslation(const experiment::AnimationChannel& channel, double time)
	{
		const auto& keys = channel.translations;
		if (keys.empty()) return {};
		if (keys.size() == 1) return keys.front().value;

		if (channel.translationInterpolation == experiment::InterpolationMode::Step)
		{
			return keys[StepKeyIndex(keys, time)].value;
		}

		const std::size_t index = LinearIntervalIndex(keys, time);
		const auto& k0 = keys[index];
		const auto& k1 = keys[index + 1];
		const float t = Alpha(k0, k1, time);
		return math::lerp(k0.value, k1.value, t);
	}

	math::quaternion SampleRotation(const experiment::AnimationChannel& channel, double time)
	{
		const auto& keys = channel.rotations;
		if (keys.empty()) return { 0.0f, 0.0f, 0.0f, 1.0f };
		if (keys.size() == 1) return keys.front().quaternion;

		if (channel.rotationInterpolation == experiment::InterpolationMode::Step)
		{
			return keys[StepKeyIndex(keys, time)].quaternion;
		}

		const std::size_t index = LinearIntervalIndex(keys, time);
		const auto& k0 = keys[index];
		const auto& k1 = keys[index + 1];
		return math::slerp(
			k0.quaternion, k1.quaternion, Alpha(k0, k1, time));
	}

	float SampleUniformScale(const experiment::AnimationChannel& channel, double time)
	{
		const auto& keys = channel.scales;
		if (keys.empty()) return 1.0f;
		if (keys.size() == 1) return keys.front().value.x;

		if (channel.scaleInterpolation == experiment::InterpolationMode::Step)
		{
			return keys[StepKeyIndex(keys, time)].value.x;
		}

		const std::size_t index = LinearIntervalIndex(keys, time);
		const auto& k0 = keys[index];
		const auto& k1 = keys[index + 1];
		return k0.value.x + (k1.value.x - k0.value.x) * Alpha(k0, k1, time);
	}

	math::matrix4x4 SampleLocal(const experiment::AnimationChannel& channel, double time)
	{
		const math::vector3 position = SampleTranslation(channel, time);
		const math::quaternion rotation = SampleRotation(channel, time);
		const float scale = SampleUniformScale(channel, time);

		return math::compose(
			math::vector3{ scale, scale, scale }, rotation, position);
	}

	void EvaluatePose(const experiment::Skeleton& skeleton, const experiment::AnimationClip& clip,
		double time, Pose& pose)
	{
		const std::size_t boneCount = skeleton.bones.size();
		pose.finals.assign(boneCount, math::matrix4x4::identity());
		pose.hasChannel.assign(boneCount, 0);

		std::vector<const experiment::AnimationChannel*> channelOf(boneCount, nullptr);
		for (const experiment::AnimationChannel& channel : clip.channels)
		{
			if (experiment::IsInRange(channel.bone, boneCount))
				channelOf[channel.bone.Value()] = &channel;
		}

		const math::matrix4x4& rootTransform = skeleton.rootTransform;
		const math::matrix4x4& globalInverse = skeleton.globalInverseTransform;

		// 게시 계약(parent < index) 덕에 단일 순회로 충분하다.
		std::vector<math::matrix4x4> globals(boneCount, rootTransform);
		for (std::size_t boneIndex = 0; boneIndex < boneCount; ++boneIndex)
		{
			const experiment::Bone& bone = skeleton.bones[boneIndex];
			const math::matrix4x4 parentGlobal = bone.parent.IsValid()
				? globals[bone.parent.Value()] : rootTransform;
			if (const experiment::AnimationChannel* channel = channelOf[boneIndex])
			{
				const math::matrix4x4 global =
					SampleLocal(*channel, time) * parentGlobal;
				globals[boneIndex] = global;
				pose.finals[boneIndex] =
					bone.inverseBindMatrix * global * globalInverse;
				pose.hasChannel[boneIndex] = 1;
			}
			else
			{
				globals[boneIndex] = parentGlobal;
			}
		}
	}
}
