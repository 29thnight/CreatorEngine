#pragma once
#include "Core.Minimal.h"
#include "Component.h"
#include "IRenderable.h"
#include "../RenderEngine/Skeleton.h"
#include "Animator.generated.h"

constexpr uint32 MAX_BONES{ 512 };

class Animator : public Component, public IRenderable
{
public:
    [[Property]]
    Skeleton* m_Skeleton{ nullptr };
    [[Property]]
    float m_TimeElapsed{};
    [[Property]]
    uint32_t m_AnimIndexChosen{};
    DirectX::XMMATRIX m_FinalTransforms[MAX_BONES]{};
    [[property]]
    std::vector<std::string> aniName;
   /* [[property]]
    std::string aniName;*/
    
    [[Property]]
    int m_AnimIndex{};
    void SetAnimation(int index);
    void UpdateAnimation();
public:
   ReflectAnimator
    [[Serializable(Inheritance:Component)]]
    GENERATED_BODY(Animator)

    bool IsEnabled() const override
    {
        return m_IsEnabled;
    }

    void SetEnabled(bool able) override
    {
        m_IsEnabled = able;
    }

private:
    bool m_IsEnabled = false;
};
