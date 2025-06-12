#include "ParticleModule.h"

float ParticleModule::ApplyEasing(float normalizedTime)
{
	if (!m_useEasing) return normalizedTime;

	int easingIndex = static_cast<int>(m_easingType);
	if (easingIndex >= 0 && easingIndex < static_cast<int>(EasingEffect::EasingEffectEnd))
	{
		return EasingFunction[easingIndex](normalizedTime);
	}

	return normalizedTime;
}
