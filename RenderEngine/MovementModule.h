#pragma once
#include "ParticleModule.h"

// gravity, gravity strength
class MovementModule : public ParticleModule
{
public:
    MovementModule() : m_gravity(true), m_gravityStrength(1.0f) {}

    void Update(float delta, std::vector<ParticleData>& particles) override;

    void SetUseGravity(bool use) { m_gravity = use; }

    void SetGravityStrength(float strength) { m_gravityStrength = strength; }
private:
    bool m_gravity;
    float m_gravityStrength;
};