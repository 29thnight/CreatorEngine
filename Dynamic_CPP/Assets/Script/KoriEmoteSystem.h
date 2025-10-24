#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"
#include "TweenManager.h"
#include "KoriEmoteSystem.generated.h"

enum class EKoriEmoteState
{
	Smile,
	Sick,
	Stunned,
	Happy,
};
AUTO_REGISTER_ENUM(EKoriEmoteState);

class KoriEmoteSystem : public ModuleBehavior
{
public:
   ReflectKoriEmoteSystem
    [[ScriptReflectionField]]
	MODULE_BEHAVIOR_BODY(KoriEmoteSystem)
	virtual void Start() override;
	virtual void Update(float tick) override;

public:
	void SetEmoteState(EKoriEmoteState newState, bool forcePlay);
	EKoriEmoteState GetEmoteState() const { return m_currentState; } //Get�� �ʿ����� �ǹ���.

    // ���º� �ִϸ��̼� ������
    [[Method]]
    void PlaySmile();
    [[Method]]
    void PlaySick();
    [[Method]]
    void PlayStunned();
    [[Method]]
    void PlayHappy();

private:
	class ImageComponent*			m_bgComponent{};
	//class ImageComponent*			m_emoteComponent{};
	class RectTransformComponent*	m_emoteRectTransform{};
	TweenManager*                   m_tweenManager{};
	class Camera*                   m_camera{};
	class GameObject*               m_targetObject{};

	EKoriEmoteState					m_currentState{ EKoriEmoteState::Smile };
	EKoriEmoteState					m_previousState{ EKoriEmoteState::Smile };
	Mathf::Vector2                  m_baseAnchoredPos{};
    [[Property]]
	Mathf::Vector2					m_emoteOffset{ 150.f, -150.f };
    [[Property]]
	float							m_emoteChangeInterval{ 5.f };

    // RectTransform ������(�� ���� �Լ��� ���� �ٲ���)
    Mathf::Vector2 GetAnchoredPos() const;
    void SetAnchoredPos(const Mathf::Vector2& p);

    float GetScale() const;
    void SetScale(const float& s);

    float GetRotationZDeg() const;
    void SetRotationZDeg(float deg);

    float GetEmoteAlpha() const;
    void  SetEmoteAlpha(float a);



    // �ݺ� Ʈ�� ���� ��ƿ(�պ� ����)
    void PingPongPosY_Once(float centerY, float amplitude,
        float durUp, float durDown,
        Mathf::Easing::EaseType eUp, Mathf::Easing::EaseType eDown);

    void ShakeX_Cycles(float magnitude, float singleDur, int cycles);

    void PumpScale_Once(float minS, float maxS, float restS,
        float duUp, float duDown, float duRest,
        Mathf::Easing::EaseType eUp, Mathf::Easing::EaseType eDown, Mathf::Easing::EaseType eRest);

    void After(float delaySec, std::function<void()> cb);
    void Disappear(float fadeDur = 0.20f,
        float endScale = 0.0f,
        float dropPixels = 6.0f);

    void PlaySmile_Once();
    void PlaySick_Once();
    void PlayStunned_Once();
    void PlayHappy_Once();

    std::vector<std::weak_ptr<Mathf::ITween>> m_myTweens; // ���� ���� Ʈ���� ����

    // ���� ���� Ʈ���� �����ϰ� Kill
    void KillMyTweens() {
        for (auto it = m_myTweens.begin(); it != m_myTweens.end(); ) {
            if (auto sp = it->lock()) { sp->Kill(); ++it; }
            else { it = m_myTweens.erase(it); }
        }
    }

    // Ʈ�� ���(�Ŵ����� Add + �� ��Ͽ� ����)
    template<typename T>
    std::shared_ptr<Mathf::Tweener<T>> AddMyTween(std::shared_ptr<Mathf::Tweener<T>> tw) {
        if (!tw || !m_tweenManager) return tw;
        m_tweenManager->AddTween(tw);
        m_myTweens.emplace_back(tw);
        return tw;
    }
};
