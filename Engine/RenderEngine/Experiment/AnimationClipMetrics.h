#pragma once

#include "ModelData.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

// experiment::AnimationClip 의 파생 계수. **정의는 여기 하나뿐이다.**
//
// I5-D5b 실측이 이 파일을 만들게 했다. legacy 임포터(AnimationLoader.cpp
// CountUniqueKeyTimes)는 `m_totalKeyFrames`를 "eps 1e-6 로 유니크한 키 **시각**
// 개수"로 정의하는데, D1a 역브리지는 같은 필드를 "전 채널 키 **개수의 합**"으로
// 채우고 있었다 — 이름이 같고 뜻이 다른 값이라 로드 경로(experiment vs Assimp
// 폴백)에 따라 한 자릿수씩 벌어진다. 이 값은 에디터 이벤트 저작의
// frameKey 상한과 `key = frameKey / totalKeyFrames`(0~1 진행률 — 실제 발화
// 시점) 를 정하므로, 같은 자산이 경로에 따라 다른 시점에 발화하게 된다.
//
// 그래서 legacy 임포터의 정의를 정본으로 삼고, 역브리지와 Animator 열거 창구가
// 함께 이 함수를 부른다.
namespace experiment::clip
{
	[[nodiscard]] inline std::size_t CountUniqueKeyTimes(
		const experiment::AnimationClip& clip, double eps = 1e-6)
	{
		std::vector<double> times;
		for (const experiment::AnimationChannel& channel : clip.channels)
		{
			for (const experiment::TranslationKey& key : channel.translations)
				times.push_back(key.time);
			for (const experiment::RotationKey& key : channel.rotations)
				times.push_back(key.time);
			for (const experiment::ScaleKey& key : channel.scales)
				times.push_back(key.time);
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
}
