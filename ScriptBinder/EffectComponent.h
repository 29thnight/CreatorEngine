#pragma once
#include "ParticleSystem.h"
#include "Component.h"
#include "EffectComponent.generated.h"

class EffectComponent : public Component, public IUpdatable, public IAwakable, public IOnDestroy
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

    // 동적 이펙트 변경 기능
    [[Method]]
    void ChangeEffect(const std::string& newEffectName);

    // 특정 이펙트를 이름으로 재생
    [[Method]]
    void PlayEffectByName(const std::string& effectName);

    // 에디터에서 선택할 템플릿 이펙트 이름 (런타임에서 변경 가능)
    [[Property]]
    std::string m_effectTemplateName = "Null";

    // 이펙트 설정들
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
    float m_currentTime = 0.0f;

    [[Property]]
    bool m_useAbsolutePosition = false;
private:
    // 실제 사용되는 고유 인스턴스 이름
    std::string m_effectInstanceName;

    // 인스턴스 카운터 (같은 컴포넌트에서 여러번 이펙트 변경시 고유성 보장)
    inline static int m_instanceCounter = 0;

    Mathf::Vector3 m_lastPosition;
    Mathf::Vector3 m_lastRotation;

    void ApplyEffectSettings();
    void DestroyCurrentEffect();
};
