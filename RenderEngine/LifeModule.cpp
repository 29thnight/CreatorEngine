#include "LifeModule.h"

void LifeModule::Update(float delta, std::vector<ParticleData>& particles)
{
	for (auto& particle : particles)
	{
		if (particle.isActive)
		{
			particle.age += delta;

			if (particle.lifeTime > 0.0f && particle.age >= particle.lifeTime)
			{
				particle.isActive = false;
			}
		}
	}
}