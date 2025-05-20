#pragma once
#include "ParticleModule.h"

enum class EmitterType;
// spawn rate, emittertype
class SpawnModule : public ParticleModule
{
public:
	SpawnModule(float spawnRate, EmitterType emitterType)
		: m_spawnRate(spawnRate), m_emitterType(emitterType),
		m_Time(0.0f), m_random(std::random_device{}()) {
	}

	void Initialize() override;
	void Update(float delta, std::vector<ParticleData>& particles) override;

	void SetEmitterShape(EmitterType type) { m_emitterType = type; }
	void SetSpawnRate(float rate) { m_spawnRate = rate; }
	void SetMaxParticles(UINT maxParticles);
public:
	ParticleData m_particleTemplate;

private:
	void SpawnParticle(std::vector<ParticleData>& particles);
	void InitializeParticle(ParticleData& particle);

private:
	float m_spawnRate;
	float m_Time;
	EmitterType m_emitterType;
	std::mt19937 m_random;
	std::uniform_real_distribution<float> m_uniform;


	float m_lifeTime;
	float m_rotateSpeed;
	float2 m_initialSize;
	float4 m_initialColor;
	float3 m_initialAcceleration;

	float m_minVerticalVelocity;
	float m_maxVerticalVelocity;
	float m_horizontalVelocityRange;
};