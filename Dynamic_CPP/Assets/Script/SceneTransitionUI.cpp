#include "SceneTransitionUI.h"
#include "ImageComponent.h"
#include "Canvas.h"
#include "pch.h"

void SceneTransitionUI::Awake()
{
    m_image = GetComponent<ImageComponent>();
    if (!m_image) return;

    // 검정 배경 전제: 색상은 (0,0,0), 알파는 현재 값 유지
    // 네 엔진의 컬러/알파 API에 맞게 조정
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
            canvas->SetCanvasOrder(MIN_CANVAS_ORDER); // 최하위로
        }
        return;
    }

    // 필요 시 timeScale 무시 틱으로 교체
    const float dt = tick; // m_ignoreTimeScale ? Time::GetUnscaledDelta() : tick;

    switch (m_state)
    {
    case State::FadingIn:
    case State::FadingOut:
    {
		auto canvas = m_image->GetOwnerCanvas();
        if (canvas)
        {
			canvas->SetCanvasOrder(MAX_CANVAS_ORDER); // 최상위로
        }

        m_elapsed += dt;
        const float tRaw = (m_duration > 0.f) ? std::clamp(m_elapsed / m_duration, 0.f, 1.f) : 1.f;
        const float t = Smooth01(tRaw); // 더 직선적 원하면 t=tRaw 로 바꿔도 됨

        m_currAlpha = Mathf::Lerp(m_fromAlpha, m_toAlpha, t);
        ApplyAlpha(m_currAlpha);

        if (tRaw >= 1.f)
        {
            // 정확히 타겟으로 스냅
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
                // 콜백 재사용 방지
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
            // Hold 완료 후 반대 방향으로 전환(인→아웃 시나리오)
            // 현재 알파가 1이므로 1->0
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
    m_onFadeInComplete = onComplete;      // 인 완료용
    m_onAllComplete = nullptr;            // 전체 완료 콜백 제거
    m_holdRemain = 0.f;

    const float d = duration > 0.f ? duration : m_defaultFadeIn;
    BeginFade(m_currAlpha, 1.f, d, State::Idle);
}

void SceneTransitionUI::FadeOut(float duration, std::function<void()> onComplete)
{
    if (!m_image) return;
    m_onFadeInComplete = nullptr;         // 인 완료 콜백 제거
    m_onAllComplete = onComplete;         // 단일 완료를 AllComplete로 사용
    m_holdRemain = 0.f;

    const float d = duration > 0.f ? duration : m_defaultFadeOut;
    BeginFade(m_currAlpha, 0.f, d, State::Idle);
}

void SceneTransitionUI::FadeInOut(float inDuration, float holdSeconds, float outDuration, std::function<void()> onFadeInComplete, std::function<void()> onAllComplete)
{
    if (!m_image) return;
    m_onFadeInComplete = onFadeInComplete;
    m_onAllComplete = onAllComplete;

    // outDuration을 기본으로 저장(holding 끝난 뒤 사용)
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

    // 방향에 따라 상태 설정
    if (m_toAlpha > m_fromAlpha) m_state = State::FadingIn;
    else                         m_state = State::FadingOut;

    // Holding을 쓸 계획이면 상태 전이용 힌트로 저장
    if (m_holdRemain <= 0.f && nextStateIfHold == State::Holding)
        nextStateIfHold = State::Idle; // hold가 없으면 idle

    // 즉시 반영
    ApplyAlpha(m_fromAlpha);
}

void SceneTransitionUI::ApplyAlpha(float a)
{
    m_image->color.w = std::clamp(a, 0.f, 1.f);
}

float SceneTransitionUI::Smooth01(float x)
{
    // 스무스스텝(가감속) 커브: 3x^2 - 2x^3
    x = std::clamp(x, 0.f, 1.f);
    return x * x * (3.f - 2.f * x);
}