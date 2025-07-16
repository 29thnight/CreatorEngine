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

    // ���� ����Ʈ ���� ���
    [[Method]]
    void ChangeEffect(const std::string& newEffectName);

    // Ư�� ����Ʈ�� �̸����� ���
    [[Method]]
    void PlayEffectByName(const std::string& effectName);

    // �����Ϳ��� ������ ���ø� ����Ʈ �̸� (��Ÿ�ӿ��� ���� ����)
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
    float m_currentTime = 0.0f;

    [[Property]]
    bool m_useAbsolutePosition = false;
private:
    // ���� ���Ǵ� ���� �ν��Ͻ� �̸�
    std::string m_effectInstanceName;

    // �ν��Ͻ� ī���� (���� ������Ʈ���� ������ ����Ʈ ����� ������ ����)
    inline static int m_instanceCounter = 0;

    Mathf::Vector3 m_lastPosition;
    Mathf::Vector3 m_lastRotation;

    void ApplyEffectSettings();
    void DestroyCurrentEffect();
};
