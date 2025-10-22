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

    // ��ȯ API ---------------------------------------------------------
   // ���� ���Ŀ��� target���� duration���� ����
    void FadeIn(float duration, std::function<void()> onComplete = nullptr);   // 0 -> 1
    void FadeOut(float duration, std::function<void()> onComplete = nullptr);  // 1 -> 0

    // ���� ��ȯ: �� -> (hold ����) -> �ƿ�
    void FadeInOut(float inDuration, float holdSeconds, float outDuration,
        std::function<void()> onFadeInComplete = nullptr,
        std::function<void()> onAllComplete = nullptr);

    // ��� ����
    void SetInstantAlpha(float a);

    // ���� ��ȸ
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

    // �⺻ ���� (�ν����� �����)
    [[Property]] 
    float m_defaultFadeIn = 0.3f;
    [[Property]] 
    float m_defaultFadeOut = 0.3f;
    [[Property]] 
    bool  m_ignoreTimeScale = false; // �װ� ������ ƽ�� �����ִٸ� ����

private:
    void BeginFade(float from, float to, float duration, State nextStateIfHold = State::Idle);
    void ApplyAlpha(float a);
    float Smooth01(float x);
};
