#include "ParticleSystem.h"
#include "EffectComponent.h"
#include "EffectManagerProxy.h"
#include "EffectProxyController.h"

void EffectComponent::Awake()
{
    // Awake������ �⺻ ������ �ϰ� ����Ʈ�� �������� ����
    m_lastPosition = GetOwner()->m_transform.GetWorldPosition();
    auto worldQuat = GetOwner()->m_transform.GetWorldQuaternion();
    float pitch, yaw, roll;
    Mathf::QuaternionToEular(worldQuat, pitch, yaw, roll);
    m_lastRotation = Mathf::Vector3(pitch, yaw, roll);

    if (!m_effectTemplateName.empty())
    {
        float templateTimeScale, templateDuration;
        bool templateLoop;

        if (EffectManagerProxy::GetTemplateSettings(m_effectTemplateName, templateTimeScale, templateLoop, templateDuration))
        {
            // JSON���� �ε�� ���� �������� ������Ʈ ���� ������Ʈ
            m_timeScale = templateTimeScale;
            m_loop = templateLoop;
            m_duration = templateDuration;
        }

        // ���� ����ȭ�� �������� Effect ���
        PlayEffectByName(m_effectTemplateName);
    }
}

void EffectComponent::Update(float tick)
{
    if (!m_effectInstanceName.empty())
    {
        auto worldPos = GetOwner()->m_transform.GetWorldPosition();
        auto worldQuat = GetOwner()->m_transform.GetWorldQuaternion();

        Mathf::Vector3 currentPos = Mathf::Vector3(worldPos.m128_f32[0], worldPos.m128_f32[1], worldPos.m128_f32[2]);

        float pitch, yaw, roll;
        Mathf::QuaternionToEular(worldQuat, pitch, yaw, roll);
        Mathf::Vector3 currentRot = Mathf::Vector3(pitch, yaw, roll);

        // threshold�� �� ũ�� �����ϰų� ����
        float posThreshold = 0.01f;  // 0.001f���� 0.01f�� ����
        float posDistance = (m_lastPosition - currentPos).Length();
        if (posDistance > posThreshold)
        {
            auto positionCommand = EffectManagerProxy::CreateSetPositionCommand(m_effectInstanceName, currentPos);
            EffectProxyController::GetInstance()->PushEffectCommand(std::move(positionCommand));
            m_lastPosition = currentPos;
        }

        float rotThreshold = 0.01f;  // ȸ���� threshold ����
        float rotDistance = (m_lastRotation - currentRot).Length();
        if (rotDistance > rotThreshold)
        {
            auto rotationCommand = EffectManagerProxy::CreateSetRotationCommand(m_effectInstanceName, currentRot);
            EffectProxyController::GetInstance()->PushEffectCommand(std::move(rotationCommand));
            m_lastRotation = currentRot;
        }
    }
}

void EffectComponent::OnDestroy()
{
    // ���� �ν��Ͻ��� ������ ����
    DestroyCurrentEffect();
}

void EffectComponent::Apply()
{
    // ���� ������ ���ø����� ���
    PlayEffectByName(m_effectTemplateName);
}

void EffectComponent::PlayPreview()
{
    Apply();
}

void EffectComponent::PlayEffectByName(const std::string& effectName)
{
    if (effectName.empty()) return;

    // ���� ����Ʈ�� ������ ���� ����
    if (!m_effectInstanceName.empty())
    {
        DestroyCurrentEffect();
    }

    // ���ο� ���� �ν��Ͻ� �̸� ����
    m_effectInstanceName = effectName + "_" + std::to_string(GetInstanceID()) + "_" + std::to_string(m_instanceCounter++);

    // ���ø����� �ν��Ͻ� ����
    auto createCommand = EffectManagerProxy::CreateEffectInstanceCommand(effectName, m_effectInstanceName);
    EffectProxyController::GetInstance()->PushEffectCommand(std::move(createCommand));

    // ���� ���� ������Ʈ ������ ����
    ApplyEffectSettings();

    // �� ������ ���ø� �������� ������Ʈ (�ʿ��� ��쿡��)
    float templateTimeScale, templateDuration;
    bool templateLoop;

    if (EffectManagerProxy::GetTemplateSettings(effectName, templateTimeScale, templateLoop, templateDuration))
    {
        // ���ø� ������ ������ �� �ٽ� ����
        m_timeScale = templateTimeScale;
        m_loop = templateLoop;
        m_duration = templateDuration;

        // ����� ������ �ٽ� ����
        ApplyEffectSettings();
    }

    
    // ��ġ ����
    auto currentPos = GetOwner()->m_transform.GetWorldPosition();
    auto positionCommand = EffectManagerProxy::CreateSetPositionCommand(m_effectInstanceName, currentPos);
    EffectProxyController::GetInstance()->PushEffectCommand(std::move(positionCommand));

    // ����Ʈ ���
    auto playCommand = EffectManagerProxy::CreatePlayCommand(m_effectInstanceName);
    EffectProxyController::GetInstance()->PushEffectCommand(std::move(playCommand));

    m_lastPosition = currentPos;
    m_isPlaying = true;
}

void EffectComponent::ChangeEffect(const std::string& newEffectName)
{
    if (newEffectName.empty()) return;

    // ���� ����Ʈ ���� �� ����
    DestroyCurrentEffect();

    // ���ο� ����Ʈ�� ����
    m_effectTemplateName = newEffectName;

    // �� ����Ʈ ���
    PlayEffectByName(newEffectName);
}

void EffectComponent::StopEffect()
{
    if (!m_effectInstanceName.empty() && m_isPlaying)
    {
        auto stopCommand = EffectManagerProxy::CreateStopCommand(m_effectInstanceName);
        EffectProxyController::GetInstance()->PushEffectCommand(std::move(stopCommand));
        m_isPlaying = false;
    }
}

void EffectComponent::PauseEffect()
{
    if (!m_effectInstanceName.empty() && m_isPlaying && !m_isPaused)
    {
        auto pauseCommand = EffectManagerProxy::CreatePauseCommand(m_effectInstanceName);
        EffectProxyController::GetInstance()->PushEffectCommand(std::move(pauseCommand));
        m_isPaused = true;
    }
}


void EffectComponent::ResumeEffect()
{
    if (!m_effectInstanceName.empty() && m_isPlaying && m_isPaused)
    {
        auto resumeCommand = EffectManagerProxy::CreateResumeCommand(m_effectInstanceName);
        EffectProxyController::GetInstance()->PushEffectCommand(std::move(resumeCommand));
        m_isPaused = false;
    }
}

void EffectComponent::DestroyCurrentEffect()
{
    if (!m_effectInstanceName.empty())
    {
        auto removeCommand = EffectManagerProxy::CreateRemoveEffectCommand(m_effectInstanceName);
        EffectProxyController::GetInstance()->PushEffectCommand(std::move(removeCommand));
        m_effectInstanceName.clear();
        m_isPlaying = false;
        m_isPaused = false;
    }
}

void EffectComponent::ForeceUpdatePosition()
{
    if (!m_effectInstanceName.empty())
    {
        auto worldPos = GetOwner()->m_transform.GetWorldPosition();
        Mathf::Vector3 currentPos = Mathf::Vector3(worldPos.m128_f32[0], worldPos.m128_f32[1], worldPos.m128_f32[2]);

        // threshold �˻� ���� ��� ������Ʈ
        auto positionCommand = EffectManagerProxy::CreateSetPositionCommand(m_effectInstanceName, currentPos);
        EffectProxyController::GetInstance()->PushEffectCommand(std::move(positionCommand));
        m_lastPosition = currentPos;
    }
}

void EffectComponent::ApplyEffectSettings()
{
    if (!m_effectInstanceName.empty())
    {
        // Ÿ�ӽ����� ����
        auto timeScaleCommand = EffectManagerProxy::CreateSetTimeScaleCommand(m_effectInstanceName, m_timeScale);
        EffectProxyController::GetInstance()->PushEffectCommand(std::move(timeScaleCommand));

        // ���� ����
        auto loopCommand = EffectManagerProxy::CreateSetLoopCommand(m_effectInstanceName, m_loop);
        EffectProxyController::GetInstance()->PushEffectCommand(std::move(loopCommand));

        // ���ӽð� ����
        auto durationCommand = EffectManagerProxy::CreateSetDurationCommand(m_effectInstanceName, m_duration);
        EffectProxyController::GetInstance()->PushEffectCommand(std::move(durationCommand));

        // ��ġ ����
        auto currentPos = GetOwner()->m_transform.GetWorldPosition();
        auto positionCommand = EffectManagerProxy::CreateSetPositionCommand(m_effectInstanceName, currentPos);
        EffectProxyController::GetInstance()->PushEffectCommand(std::move(positionCommand));
    }
}

void EffectComponent::SetLoop(bool loop)
{
    m_loop = loop;
    if (!m_effectInstanceName.empty())
    {
        auto loopCommand = EffectManagerProxy::CreateSetLoopCommand(m_effectInstanceName, loop);
        EffectProxyController::GetInstance()->PushEffectCommand(std::move(loopCommand));
    }
}

void EffectComponent::SetDuration(float duration)
{
    m_duration = duration;
    if (!m_effectInstanceName.empty())
    {
        auto durationCommand = EffectManagerProxy::CreateSetDurationCommand(m_effectInstanceName, duration);
        EffectProxyController::GetInstance()->PushEffectCommand(std::move(durationCommand));
    }
}

void EffectComponent::SetTimeScale(float timeScale)
{
    m_timeScale = timeScale;
    if (!m_effectInstanceName.empty())
    {
        auto timeScaleCommand = EffectManagerProxy::CreateSetTimeScaleCommand(m_effectInstanceName, timeScale);
        EffectProxyController::GetInstance()->PushEffectCommand(std::move(timeScaleCommand));
    }
}

void EffectComponent::ForceFinishEffect()
{
    if (!m_effectInstanceName.empty())
    {
        auto finishCommand = EffectManagerProxy::CreateForceFinishCommand(m_effectInstanceName);
        EffectProxyController::GetInstance()->PushEffectCommand(std::move(finishCommand));
        m_isPlaying = false;
    }
}