#pragma once
#include "Component.h"
#include "EffectComponent.generated.h"
#include "IRegistableEvent.h"

class EffectComponent : public Component, public RegistableEvent<EffectComponent>, public System::IInitializable
{
public:
    ReflectEffectComponent
    [[Serializable(Inheritance:Component)]]
    GENERATED_BODY(EffectComponent)

    void Initialize() override;
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

    // 동적 이펙트 변경 기능
    [[Method]]
    void ChangeEffect(const std::string& newEffectName);

    // 특정 이펙트를 이름으로 재생
    [[Method]]
    void PlayEffectByName(const std::string& effectName);

    // 런타임에서 이펙트 설정 변경
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

    // 이펙트 설정들
    [[Property]]
    float m_timeScale = 1.0f;

    [[Property]]
    float m_duration = -1.0f;

    [[Property]]
    bool m_isPlaying = false;

    [[Property]]
    bool m_isPaused = false;

    [[Property]]
    bool m_loop = true;

    [[Property]]
    bool m_useAbsolutePosition = false;

    [[Property]]
    float m_currentTime = 0;
    
    [[Property]]
    std::string m_effectInstanceName;


private:
    Mathf::Vector3 m_lastPosition;
    Mathf::Vector3 m_lastRotation;
    Mathf::Vector3 m_lastScale;

    void DestroyCurrentEffect();
    void ForeceUpdatePosition();
    //WARN : bitflag 좀 써라!!!
    bool m_timeScaleModified = false;
    bool m_loopModified = false;
    bool m_durationModified = false;
    bool m_pendingPlay;
    bool m_isNewInstance;
};
