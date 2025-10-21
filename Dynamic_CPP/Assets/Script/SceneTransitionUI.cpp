#include "SceneTransitionUI.h"
#include "ImageComponent.h"
#include "Canvas.h"
#include "pch.h"

void SceneTransitionUI::Awake()
{
    m_image = GetComponent<ImageComponent>();
    if (!m_image) return;

    // ���� ��� ����: ������ (0,0,0), ���Ĵ� ���� �� ����
    // �� ������ �÷�/���� API�� �°� ����
    m_currAlpha = m_image->color.w;
    ApplyAlpha(m_currAlpha);

}

void SceneTransitionUI::Update(float tick)
{
	constexpr int MAX_CANVAS_ORDER = std::numeric_limits<int>::max();
	constexpr int MIN_CANVAS_ORDER = std::numeric_limits<int>::min();

    if (!m_image) return;
    if (m_state == State::Idle)
    {
        auto canvas = m_image->GetOwnerCanvas();
        if (canvas)
        {
            canvas->SetCanvasOrder(MIN_CANVAS_ORDER); // ��������
        }
        return;
    }

    // �ʿ� �� timeScale ���� ƽ���� ��ü
    const float dt = tick; // m_ignoreTimeScale ? Time::GetUnscaledDelta() : tick;

    switch (m_state)
    {
    case State::FadingIn:
    case State::FadingOut:
    {
		auto canvas = m_image->GetOwnerCanvas();
        if (canvas)
        {
			canvas->SetCanvasOrder(MAX_CANVAS_ORDER); // �ֻ�����
        }

        m_elapsed += dt;
        const float tRaw = (m_duration > 0.f) ? std::clamp(m_elapsed / m_duration, 0.f, 1.f) : 1.f;
        const float t = Smooth01(tRaw); // �� ������ ���ϸ� t=tRaw �� �ٲ㵵 ��

        m_currAlpha = Mathf::Lerp(m_fromAlpha, m_toAlpha, t);
        ApplyAlpha(m_currAlpha);

        if (tRaw >= 1.f)
        {
            // ��Ȯ�� Ÿ������ ����
            m_currAlpha = m_toAlpha;
            ApplyAlpha(m_currAlpha);

            if (m_state == State::FadingIn)
            {
                if (m_onFadeInComplete) { m_onFadeInComplete(); }
            }

            if (m_holdRemain > 0.f)
            {
                m_state = State::Holding;
            }
            else
            {
                m_state = State::Idle;
                if (m_onAllComplete) { m_onAllComplete(); }
                // �ݹ� ���� ����
                m_onFadeInComplete = nullptr;
                m_onAllComplete = nullptr;
            }
        }
        break;
    }
    case State::Holding:
    {
        m_holdRemain -= dt;
        if (m_holdRemain <= 0.f)
        {
            // Hold �Ϸ� �� �ݴ� �������� ��ȯ(�Ρ�ƿ� �ó�����)
            // ���� ���İ� 1�̹Ƿ� 1->0
            BeginFade(1.f, 0.f, m_defaultFadeOut, State::Idle);
        }
        break;
    }
    default: break;
    }
}

void SceneTransitionUI::FadeIn(float duration, std::function<void()> onComplete)
{
    if (!m_image) return;
    m_onFadeInComplete = onComplete;      // �� �Ϸ��
    m_onAllComplete = nullptr;            // ��ü �Ϸ� �ݹ� ����
    m_holdRemain = 0.f;

    const float d = duration > 0.f ? duration : m_defaultFadeIn;
    BeginFade(m_currAlpha, 1.f, d, State::Idle);
}

void SceneTransitionUI::FadeOut(float duration, std::function<void()> onComplete)
{
    if (!m_image) return;
    m_onFadeInComplete = nullptr;         // �� �Ϸ� �ݹ� ����
    m_onAllComplete = onComplete;         // ���� �ϷḦ AllComplete�� ���
    m_holdRemain = 0.f;

    const float d = duration > 0.f ? duration : m_defaultFadeOut;
    BeginFade(m_currAlpha, 0.f, d, State::Idle);
}

void SceneTransitionUI::FadeInOut(float inDuration, float holdSeconds, float outDuration, std::function<void()> onFadeInComplete, std::function<void()> onAllComplete)
{
    if (!m_image) return;
    m_onFadeInComplete = onFadeInComplete;
    m_onAllComplete = onAllComplete;

    // outDuration�� �⺻���� ����(holding ���� �� ���)
    m_defaultFadeOut = (outDuration > 0.f) ? outDuration : m_defaultFadeOut;
    m_holdRemain = std::max(0.f, holdSeconds);

    const float dIn = (inDuration > 0.f) ? inDuration : m_defaultFadeIn;
    BeginFade(m_currAlpha, 1.f, dIn, State::Holding);
}

void SceneTransitionUI::SetInstantAlpha(float a)
{
    m_currAlpha = std::clamp(a, 0.f, 1.f);
    ApplyAlpha(m_currAlpha);
    m_state = State::Idle;
    m_elapsed = 0.f;
    m_duration = 0.f;
    m_holdRemain = 0.f;
    m_onFadeInComplete = nullptr;
    m_onAllComplete = nullptr;
}

void SceneTransitionUI::BeginFade(float from, float to, float duration, State nextStateIfHold)
{
    m_fromAlpha = std::clamp(from, 0.f, 1.f);
    m_toAlpha = std::clamp(to, 0.f, 1.f);
    m_duration = std::max(0.f, duration);
    m_elapsed = 0.f;

    // ���⿡ ���� ���� ����
    if (m_toAlpha > m_fromAlpha) m_state = State::FadingIn;
    else                         m_state = State::FadingOut;

    // Holding�� �� ��ȹ�̸� ���� ���̿� ��Ʈ�� ����
    if (m_holdRemain <= 0.f && nextStateIfHold == State::Holding)
        nextStateIfHold = State::Idle; // hold�� ������ idle

    // ��� �ݿ�
    ApplyAlpha(m_fromAlpha);
}

void SceneTransitionUI::ApplyAlpha(float a)
{
    m_image->color.w = std::clamp(a, 0.f, 1.f);
}

float SceneTransitionUI::Smooth01(float x)
{
    // ����������(������) Ŀ��: 3x^2 - 2x^3
    x = std::clamp(x, 0.f, 1.f);
    return x * x * (3.f - 2.f * x);
}