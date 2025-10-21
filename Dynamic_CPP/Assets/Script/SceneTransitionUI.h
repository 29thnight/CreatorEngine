#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"
#include "SceneTransitionUI.generated.h"

class SceneTransitionUI : public ModuleBehavior
{
public:
   ReflectSceneTransitionUI
    [[ScriptReflectionField]]
	MODULE_BEHAVIOR_BODY(SceneTransitionUI)
	virtual void Awake() override;
	virtual void Update(float tick) override;

    // 전환 API ---------------------------------------------------------
   // 현재 알파에서 target으로 duration동안 보간
    void FadeIn(float duration, std::function<void()> onComplete = nullptr);   // 0 -> 1
    void FadeOut(float duration, std::function<void()> onComplete = nullptr);  // 1 -> 0

    // 연속 전환: 인 -> (hold 유지) -> 아웃
    void FadeInOut(float inDuration, float holdSeconds, float outDuration,
        std::function<void()> onFadeInComplete = nullptr,
        std::function<void()> onAllComplete = nullptr);

    // 즉시 설정
    void SetInstantAlpha(float a);

    // 상태 조회
    bool IsBusy() const { return m_state != State::Idle; }
    float GetAlpha() const { return m_currAlpha; }

private:
    enum class State { Idle, FadingIn, Holding, FadingOut };

    // Components
    ImageComponent* m_image{ nullptr };

    // Fade runtime
    State  m_state{ State::Idle };
    float  m_currAlpha{ 0.f };
    float  m_fromAlpha{ 0.f };
    float  m_toAlpha{ 0.f };
    float  m_elapsed{ 0.f };
    float  m_duration{ 0.f };
    float  m_holdRemain{ 0.f };

    std::function<void()> m_onFadeInComplete;
    std::function<void()> m_onAllComplete;

    // 기본 설정 (인스펙터 노출용)
    [[Property]] 
    float m_defaultFadeIn = 0.3f;
    [[Property]] 
    float m_defaultFadeOut = 0.3f;
    [[Property]] 
    bool  m_ignoreTimeScale = false; // 네가 비스케일 틱을 갖고있다면 연결

private:
    void BeginFade(float from, float to, float duration, State nextStateIfHold = State::Idle);
    void ApplyAlpha(float a);
    float Smooth01(float x);
};
