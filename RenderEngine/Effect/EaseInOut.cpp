#include "EaseInOut.h"

#pragma region GraphFunc
// Linear
float EaseLinear(float x) { return x; }

// Sine
float EaseInSine(float x) { return 1 - std::cos((x * XM_PI) / 2); }
float EaseOutSine(float x) { return std::sin((x * XM_PI) / 2); }
float EaseInOutSine(float x) { return -(std::cos(XM_PI * x) - 1) / 2; }

// Quad
float EaseInQuad(float x) { return x * x; }
float EaseOutQuad(float x) { return 1 - (1 - x) * (1 - x); }
float EaseInOutQuad(float x) { return x < 0.5 ? 2 * x * x : 1 - std::pow(-2 * x + 2, 2) / 2; }

// Cubic
float EaseInCubic(float x) { return x * x * x; }
float EaseOutCubic(float x) { return 1 - std::pow(1 - x, 3); }
float EaseInOutCubic(float x) { return -(std::cos(XM_PI * x) - 1) / 2; }

// Quart
float EaseInQuart(float x) { return x * x * x * x; }
float EaseOutQuart(float x) { return 1 - std::pow(1 - x, 4); }
float EaseInOutQuart(float x) { return x < 0.5 ? 8 * x * x * x * x : 1 - std::pow(-2 * x + 2, 4) / 2; }

// Quint
float EaseInQuint(float x) { return x * x * x * x * x; }
float EaseOutQuint(float x) { return 1 - std::pow(1 - x, 5); }
float EaseInOutQuint(float x) { return x < 0.5 ? 16 * x * x * x * x * x : 1 - std::pow(-2 * x + 2, 5) / 2; }

// Expo
float EaseInExpo(float x) { return x == 0 ? 0 : std::pow(2, 10 * x - 10); }
float EaseOutExpo(float x) { return x == 1 ? 1 : 1 - std::pow(2, -10 * x); }
float EaseInOutExpo(float x)
{
    return x == 0
        ? 0
        : x == 1
        ? 1
        : x < 0.5
        ? std::pow(2, 20 * x - 10) / 2
        : (2 - std::pow(2, -20 * x + 10)) / 2;
}

// InCirc
float EaseInCirc(float x) { return 1 - std::sqrt(1 - std::pow(x, 2)); }
float EaseOutCirc(float x) { return std::sqrt(1 - std::pow(x - 1, 2)); }
float EaseInOutCirc(float x)
{
    return x < 0.5
        ? (1 - std::sqrt(1 - std::pow(2 * x, 2))) / 2
        : (std::sqrt(1 - std::pow(-2 * x + 2, 2)) + 1) / 2;
}

// Back
float EaseInBack(float x)
{
    const float c1 = 1.70158;
    const float c3 = c1 + 1;

    return c3 * x * x * x - c1 * x * x;
}
float EaseOutBack(float x)
{
    const float c1 = 1.70158;
    const float c3 = c1 + 1;

    return 1 + c3 * std::pow(x - 1, 3) + c1 * std::pow(x - 1, 2);
}
float EaseInOutBack(float x)
{
    const float c1 = 1.70158;
    const float c2 = c1 * 1.525;

    return x < 0.5
        ? (std::pow(2 * x, 2) * ((c2 + 1) * 2 * x - c2)) / 2
        : (std::pow(2 * x - 2, 2) * ((c2 + 1) * (x * 2 - 2) + c2) + 2) / 2;
}

// Elastic
float EaseInElastic(float x)
{
    const float c4 = (2 * XM_PI) / 3;

    return x == 0
        ? 0
        : (x == 1
            ? 1
            : -std::pow(2, 10 * x - 10) * std::sin((x * 10 - 10.75) * c4));
}
float EaseOutElastic(float x)
{
    const float c4 = (2 * XM_PI) / 3;

    return x == 0
        ? 0
        : x == 1
        ? 1
        : std::pow(2, -10 * x) * std::sin((x * 10 - 0.75) * c4) + 1;
}
float EaseInOutElastic(float x)
{
    const float c5 = (2 * XM_PI) / 4.5;

    return x == 0
        ? 0
        : x == 1
        ? 1
        : x < 0.5
        ? -(std::pow(2, 20 * x - 10) * std::sin((20 * x - 11.125) * c5)) / 2
        : (std::pow(2, -20 * x + 10) * std::sin((20 * x - 11.125) * c5)) / 2 + 1;
}

// Bounce
float EaseInBounce(float x) { return 1 - EaseOutBounce(1 - x); };
float EaseOutBounce(float x)
{
    const float n1 = 7.5625;
    const float d1 = 2.75;

    if (x < 1 / d1)
        return n1 * x * x;
    else if (x < 2 / d1)
        return n1 * (x -= 1.5 / d1) * x + 0.75;
    else if (x < 2.5 / d1)
        return n1 * (x -= 2.25 / d1) * x + 0.9375;
    else
        return n1 * (x -= 2.625 / d1) * x + 0.984375;
}
float EaseInOutBounce(float x)
{
    return x < 0.5
        ? (1 - EaseOutBounce(1 - 2 * x)) / 2
        : (1 + EaseOutBounce(2 * x - 1)) / 2;
}

#pragma endregion GraphFunc

void EaseInOut::Reset()
{
    m_currentTime = 0.0f;
    m_isFinished = false;
    m_direction = 1.0f;
    m_isReverse = false;
}

float EaseInOut::Update(float deltatime)
{
    if (m_isFinished && (m_animationType == StepAnimation::StepOnceForward || 
        m_animationType == StepAnimation::StepOnceBack || 
        m_animationType == StepAnimation::StepOncePingPong))
    {
        return (m_animationType == StepAnimation::StepOnceBack) ? 0.0f : 1.0f;
    }

    m_currentTime += deltatime * m_direction;

    switch (m_animationType)
    {
    case StepAnimation::StepOnceForward:
        if (m_currentTime >= m_duration)
        {
            m_currentTime = m_duration;
            m_isFinished = false;
        }
        break;
    case StepAnimation::StepOnceBack:
        if (m_currentTime >= m_duration)
        {
            m_currentTime = m_duration;
            m_isFinished = false;
        }
        break;
    case StepAnimation::StepOncePingPong:
        if (!m_isReverse && m_currentTime >= m_duration) 
        {
            m_currentTime = m_duration;
            m_direction = -1.0f;
            m_isReverse = true;
        }
        else if (m_isReverse && m_currentTime <= 0.0f) 
        {
            m_currentTime = 0.0f;
            m_isFinished = true;
        }
        break;
    case StepAnimation::StepLoopForward:
        while (m_currentTime >= m_duration) 
        {
            m_currentTime -= m_duration;
        }
        break;
    case StepAnimation::StepLoopBack:
        while (m_currentTime >= m_duration)
        {
            m_currentTime -= m_duration;
        }
        break;
    case StepAnimation::StepLoopPingPong:
        if (!m_isReverse && m_currentTime >= m_duration) {
            m_currentTime = 2.0f * m_duration - m_currentTime;
            m_direction = -1.0f;
            m_isReverse = true;
        }
        else if (m_isReverse && m_currentTime <= 0.0f) {
            m_currentTime = -m_currentTime;
            m_direction = 1.0f;
            m_isReverse = false;
        }
        break;
    }

    float normalizedTime = m_currentTime / m_duration;
    normalizedTime = std::max(0.0f, std::min(1.0f, normalizedTime));

    if (m_animationType == StepAnimation::StepOnceBack ||
        m_animationType == StepAnimation::StepLoopBack) {
        normalizedTime = 1.0f - normalizedTime;
    }

    int easingIndex = static_cast<int>(m_easingType);
    if (easingIndex >= 0 && easingIndex < static_cast<int>(EasingEffect::EasingEffectEnd)) {
        return EasingFunction[easingIndex](normalizedTime);
    }

    return normalizedTime;
}
