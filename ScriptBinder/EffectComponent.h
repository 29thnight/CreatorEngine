#pragma once
#include "Component.h"
#include "EffectComponent.generated.h"
#include "IRegistableEvent.h"

class EffectComponent : public Component, public RegistableEvent<EffectComponent>
{
public:
    ReflectEffectComponent
    [[Serializable(Inheritance:Component)]]
    GENERATED_BODY(EffectComponent)

    void Awake() override;
    void Update(float tick) override;
    void OnDestroy() override;

    [[Method]]
    void Apply();

    [[Method]]
    void PlayPreview();

    [[Method]]
    void StopEffect();

    [[Method]]
    void PauseEffect();

    [[Method]]
    void ResumeEffect();

    // ���� ����Ʈ ���� ���
    [[Method]]
    void ChangeEffect(const std::string& newEffectName);

    // Ư�� ����Ʈ�� �̸����� ���
    [[Method]]
    void PlayEffectByName(const std::string& effectName);

    // ��Ÿ�ӿ��� ����Ʈ ���� ����
    [[Method]]
    void SetLoop(bool loop);

    [[Method]]
    void SetDuration(float duration);

    [[Method]]
    void SetTimeScale(float timeScale);

    [[Method]]
    void ForceFinishEffect();

    [[Property]]
    std::string m_effectTemplateName = "Null";

    // ����Ʈ ������
    [[Property]]
    bool m_isPlaying = false;

    [[Property]]
    bool m_isPaused = false;

    [[Property]]
    float m_timeScale = 1.0f;

    [[Property]]
    bool m_loop = true;

    [[Property]]
    float m_duration = -1.0f;

    [[Property]]
    bool m_useAbsolutePosition = false;
    
    [[Property]]
    std::string m_effectInstanceName;

private:

    float m_currentTime = 0;

    Mathf::Vector3 m_lastPosition;
    Mathf::Vector3 m_lastRotation;

    void ApplyEffectSettings();
    void DestroyCurrentEffect();
    void ForeceUpdatePosition();
};
