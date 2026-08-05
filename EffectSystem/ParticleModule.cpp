#include "ParticleModule.h"
#include "ParticleSystem.h"

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

Mathf::Vector3 ParticleModule::GetSystemWorldPosition() const
{
	return m_ownerSystem ? m_ownerSystem->GetWorldPosition() : Mathf::Vector3::Zero;
}

Mathf::Vector3 ParticleModule::GetSystemRelativePosition() const
{
	return m_ownerSystem ? m_ownerSystem->GetRelativePosition() : Mathf::Vector3::Zero;
}

Mathf::Vector3 ParticleModule::GetSystemEffectBasePosition() const
{
	return m_ownerSystem ? m_ownerSystem->GetEffectBasePosition() : Mathf::Vector3::Zero;
}

bool ParticleModule::IsSystemRunning() const
{
	return m_ownerSystem ? m_ownerSystem->IsRunning() : false;
}
