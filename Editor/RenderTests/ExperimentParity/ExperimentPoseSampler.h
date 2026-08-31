#pragma once

// I5-D4e-1 — 포즈 샘플러 정본이 엔진으로 승격됐다(Experiment/PoseSampler.h,
// namespace experiment::sampler). 제품 재생(AnimationJob)과 검사가 같은 정의를
// 쓰도록 여기서는 재-export만 한다 — RenderTests 소비자는 기존 이름
// (RenderTest::sampler)을 그대로 쓴다.
#include "Experiment/PoseSampler.h"

namespace RenderTest
{
	namespace sampler = experiment::sampler;
}
