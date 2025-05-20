#include "MovementModule.h"

void MovementModule::Update(float delta, std::vector<ParticleData>& particles)
{
	for (auto& particle : particles)
	{
		if (particle.isActive)
		{
			float normalizedAge = particle.age / particle.lifeTime;

			float easingFactor = 1.0f;
			if (IsEasingEnabled())
			{
				easingFactor = ApplyEasing(normalizedAge);
			}

			if (m_gravity)
			{
				// 중력 적용 시 가속도 추가
				particle.velocity += particle.acceleration * m_gravityStrength * delta * easingFactor;
			}

			particle.position += particle.velocity * delta * easingFactor;

			particle.rotation += particle.rotatespeed * delta * easingFactor;
		}
	}
}