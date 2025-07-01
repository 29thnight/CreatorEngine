#pragma once
#include "ParticleSystem.h"
#include "Component.h"
#include "EffectComponent.generated.h"


class EffectComponent : public Component, public IUpdatable, public IAwakable, public IOnDistroy
{
public:
   ReflectEffectComponent
        [[Serializable(Inheritance:Component)]]
    GENERATED_BODY(EffectComponent)

    void Awake() override;
    void Update(float tick) override;
    void OnDistroy() override;

    [[Property]]
    int num = 0;

private:
    ParticleSystem test;
};

