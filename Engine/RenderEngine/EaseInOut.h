#pragma once
#include "Core.Minimal.h"

enum class EasingEffect
{
    // Linear
    Linear,

    // Sine
    InSine,
    OutSine,
    InOutSine,

    // Quad
    InQuad,
    OutQuad,
    InOutQuad,

    // Cubic
    InCubic,
    OutCubic,
    InOutCubic,

    // Quart
    InQuart,
    OutQuart,
    InOutQuart,

    // Quint
    InQuint,
    OutQuint,
    InOutQuint,

    // Expo
    InExpo,
    OutExpo,
    InOutExpo,

    // InCirc
    InCirc,
    OutCirc,
    InOutCirc,

    // Back
    InBack,
    OutBack,
    InOutBack,

    // Elastic
    InElastic,
    OutElastic,
    InOutElastic,

    // Bounce
    InBounce,
    OutBounce,
    InOutBounce,

    EasingEffectEnd
};

#pragma region GraphFunc
// Linear
float EaseLinear(float x);

// Sine
float EaseInSine(float x);
float EaseOutSine(float x);
float EaseInOutSine(float x);

// Quad
float EaseInQuad(float x);
float EaseOutQuad(float x);
float EaseInOutQuad(float x);

// Cubic
float EaseInCubic(float x);
float EaseOutCubic(float x);
float EaseInOutCubic(float x);

// Quart
float EaseInQuart(float x);
float EaseOutQuart(float x);
float EaseInOutQuart(float x);

// Quint
float EaseInQuint(float x);
float EaseOutQuint(float x);
float EaseInOutQuint(float x);

// Expo
float EaseInExpo(float x);
float EaseOutExpo(float x);
float EaseInOutExpo(float x);

// InCirc
float EaseInCirc(float x);
float EaseOutCirc(float x);
float EaseInOutCirc(float x);

// Back
float EaseInBack(float x);
float EaseOutBack(float x);
float EaseInOutBack(float x);

// Elastic
float EaseInElastic(float x);
float EaseOutElastic(float x);
float EaseInOutElastic(float x);

// Bounce
float EaseInBounce(float x);
float EaseOutBounce(float x);
float EaseInOutBounce(float x);
#pragma endregion GraphFunc

inline std::function<float(float)> EasingFunction[static_cast<int>(EasingEffect::EasingEffectEnd)] =
{
    // Linear
    &EaseLinear,

    // Sine
    &EaseInSine,
    &EaseOutSine,
    &EaseInOutSine,

    // Quad
    &EaseInQuad,
    &EaseOutQuad,
    &EaseInOutQuad,

    // Cubic
    &EaseInCubic,
    &EaseOutCubic,
    &EaseInOutCubic,
    
    // Quart
    &EaseInQuart,
    &EaseOutQuart,
    &EaseInOutQuart,
    
    // Quint
    &EaseInQuint,
    &EaseOutQuint,
    &EaseInOutQuint,
    
    // Expo
    &EaseInExpo,
    &EaseOutExpo,
    &EaseInOutExpo,
    
    // InCirc
    &EaseInCirc,
    &EaseOutCirc,
    &EaseInOutCirc,
    
    // Back
    &EaseInBack,
    &EaseOutBack,
    &EaseInOutBack,
    
    // Elastic
    &EaseInElastic,
    &EaseOutElastic,
    &EaseInOutElastic,
    
    // Bounce
    &EaseInBounce,
    &EaseOutBounce,
    &EaseInOutBounce,
};

enum class StepAnimation
{
    StepOnceForward,
    StepOnceBack,
    StepOncePingPong,
    StepLoopForward,
    StepLoopBack,
    StepLoopPingPong,
};

class EaseInOut
{
public:
    EaseInOut()
        : m_easingType(EasingEffect::Linear),
          m_animationType(StepAnimation::StepOnceForward),
          m_duration(1.0f),
          m_currentTime(0.0f),
          m_isFinished(false),
          m_direction(1.0f),
          m_isReverse(false)
    {}

    EaseInOut(EasingEffect easingType, StepAnimation animationType, float duration = 1.0f)
        : m_easingType(easingType),
          m_animationType(animationType),
          m_duration(duration),
          m_currentTime(0.0f),
          m_isFinished(false),
          m_direction(1.0f),
          m_isReverse(false)
    {}

    void SetEasingType(EasingEffect type) { m_easingType = type; }

    void SetAnimationType(StepAnimation type) { m_animationType = type; }

    void SetDuration(float duration) { m_duration = duration; }

    void Reset();

    float Update(float deltatime);

    bool isFinished() const { return m_isFinished; }

    EasingEffect GetEasingType() const { return m_easingType; }
    StepAnimation GetAnimationType() const { return m_animationType; }
    float GetDuration() const { return m_duration; }

private:
    EasingEffect m_easingType;
    StepAnimation m_animationType;
    float m_duration;
    float m_currentTime;
    bool m_isFinished;
    float m_direction;
    bool m_isReverse;
};

