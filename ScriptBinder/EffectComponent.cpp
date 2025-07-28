#include "ParticleSystem.h"
#include "EffectComponent.h"
#include "EffectManagerProxy.h"
#include "EffectProxyController.h"
#include "EffectRenderProxy.h"

void EffectComponent::Awake()
{
    EffectRenderProxy* proxy = EffectCommandQueue->RegisterProxy(this);

    // Awake������ �⺻ ������ �ϰ� ����Ʈ�� �������� ����
    m_lastPosition = GetOwner()->m_transform.GetWorldPosition();
    auto worldQuat = GetOwner()->m_transform.GetWorldQuaternion();
    float pitch, yaw, roll;
    Mathf::QuaternionToEular(worldQuat, pitch, yaw, roll);
    m_lastRotation = Mathf::Vector3(pitch, yaw, roll);

    if (!m_effectTemplateName.empty() && m_effectTemplateName != "Null")
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
    EffectRenderProxy* proxy = EffectCommandQueue->GetProxy(this);
    if (!proxy) return;

    // ���Ͻÿ��� ���� ������ �ν��Ͻ� �̸��� �����ͼ� ����ȭ
    if (!proxy->GetInstanceName().empty() && m_effectInstanceName != proxy->GetInstanceName())
    {
        m_effectInstanceName = proxy->GetInstanceName();

#ifdef _DEBUG
        std::cout << "Synced instance name: " << m_effectInstanceName << std::endl;
#endif
    }

    // �ν��Ͻ� �̸��� ���� ���� position/rotation ������Ʈ
    if (!m_effectInstanceName.empty())
    {
        auto worldPos = GetOwner()->m_transform.GetWorldPosition();
        auto worldQuat = GetOwner()->m_transform.GetWorldQuaternion();

        Mathf::Vector3 currentPos = Mathf::Vector3(worldPos.m128_f32[0], worldPos.m128_f32[1], worldPos.m128_f32[2]);

        float pitch, yaw, roll;
        Mathf::QuaternionToEular(worldQuat, pitch, yaw, roll);
        Mathf::Vector3 currentRot = Mathf::Vector3(pitch, yaw, roll);

        float posThreshold = 0.01f;
        float posDistance = (m_lastPosition - currentPos).Length();
        if (posDistance > posThreshold)
        {
            proxy->UpdateInstanceName(m_effectInstanceName);
            proxy->UpdatePosition(currentPos);
            proxy->PushCommand(EffectCommandType::SetPosition);
            m_lastPosition = currentPos;

        }

        float rotThreshold = 0.01f;
        float rotDistance = (m_lastRotation - currentRot).Length();
        if (rotDistance > rotThreshold)
        {
            proxy->UpdateInstanceName(m_effectInstanceName);
            proxy->UpdateRotation(currentRot);
            proxy->PushCommand(EffectCommandType::SetRotation);
            m_lastRotation = currentRot;
        }
    }
    else
    {
#ifdef _DEBUG
        static int debugCount = 0;
        if (++debugCount % 60 == 0) {
            std::cout << "Warning: m_effectInstanceName is empty! Proxy instance: '"
                << proxy->GetInstanceName() << "'" << std::endl;
        }
#endif
    }
}

void EffectComponent::OnDestroy()
{
    // ���� �ν��Ͻ��� ������ ����
    DestroyCurrentEffect();
    EffectCommandQueue->UnRegisterProxy(this);
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

    EffectRenderProxy* proxy = EffectCommandQueue->GetProxy(this);
    if (!proxy) return;

    m_effectTemplateName = effectName;

    float templateTimeScale, templateDuration;
    bool templateLoop;

    if (EffectManagerProxy::GetTemplateSettings(m_effectTemplateName, templateTimeScale, templateLoop, templateDuration))
    {
        // JSON���� �ε�� ���� �������� ������Ʈ ���� ������Ʈ
        m_timeScale = templateTimeScale;
        m_loop = templateLoop;
        m_duration = templateDuration;
    }

    // ��ġ�� ȸ�� ����
    auto currentPos = GetOwner()->m_transform.GetWorldPosition();
    auto worldQuat = GetOwner()->m_transform.GetWorldQuaternion();

    float pitch, yaw, roll;
    Mathf::QuaternionToEular(worldQuat, pitch, yaw, roll);
    Mathf::Vector3 currentRot = Mathf::Vector3(pitch, yaw, roll);

    // ���� ����Ʈ�� ������ Replace, ������ ���� ����
    if (!m_effectInstanceName.empty()) {
        // Replace ��� (�ν��Ͻ� ID ����)
        proxy->UpdateInstanceName(m_effectInstanceName);
        proxy->UpdateTempleteName(m_effectTemplateName);
        proxy->UpdatePosition(currentPos);
        proxy->UpdateRotation(currentRot);
        proxy->UpdateTimeScale(m_timeScale);
        proxy->UpdateLoop(m_loop);
        proxy->UpdateDuration(m_duration);

        proxy->PushCommand(EffectCommandType::ReplaceEffect);
    }
    else {
        // ���� ����
        proxy->UpdateTempleteName(m_effectTemplateName);
        proxy->UpdatePosition(currentPos);
        proxy->UpdateRotation(currentRot);
        proxy->UpdateTimeScale(m_timeScale);
        proxy->UpdateLoop(m_loop);
        proxy->UpdateDuration(m_duration);

        proxy->PushCommand(EffectCommandType::Play);
    }

    m_lastPosition = currentPos;
    m_lastRotation = currentRot;
    m_isPlaying = true;
}


void EffectComponent::ChangeEffect(const std::string& newEffectName)
{
    if (newEffectName.empty()) return;

    // ���ο� ����Ʈ �̸� ����
    m_effectTemplateName = newEffectName;

    float templateTimeScale, templateDuration;
    bool templateLoop;

    if (EffectManagerProxy::GetTemplateSettings(m_effectTemplateName, templateTimeScale, templateLoop, templateDuration))
    {
        m_timeScale = templateTimeScale;
        m_loop = templateLoop;
        m_duration = templateDuration;
    }

    // ���� ����Ʈ�� ������ Replace, ������ ���� ����
    if (!m_effectInstanceName.empty()) {
        // Replace�� ó�� (ID ����)
        PlayEffectByName(newEffectName);
    }
    else {
        // ���� ����
        PlayEffectByName(newEffectName);
    }
}

void EffectComponent::StopEffect()
{
    if (!m_effectInstanceName.empty() && m_isPlaying)
    {
        EffectRenderProxy* proxy = EffectCommandQueue->GetProxy(this);
        if (!proxy) return;

        //auto stopCommand = EffectManagerProxy::CreateStopCommand(m_effectInstanceName);
        //EffectCommandQueue->PushEffectCommand(std::move(stopCommand));
        proxy->UpdateInstanceName(m_effectInstanceName);
        proxy->PushCommand(EffectCommandType::Stop);
        m_isPlaying = false;
    }
}

void EffectComponent::PauseEffect()
{
    if (!m_effectInstanceName.empty() && m_isPlaying && !m_isPaused)
    {
        EffectRenderProxy* proxy = EffectCommandQueue->GetProxy(this);
        if (!proxy) return;
        //auto stopCommand = EffectManagerProxy::CreateStopCommand(m_effectInstanceName);
        //EffectCommandQueue->PushEffectCommand(std::move(stopCommand));
        proxy->UpdateInstanceName(m_effectInstanceName);
        proxy->PushCommand(EffectCommandType::Stop);
        m_isPaused = true;
    }
}


void EffectComponent::ResumeEffect()
{
    if (!m_effectInstanceName.empty() && m_isPlaying && m_isPaused)
    {
        EffectRenderProxy* proxy = EffectCommandQueue->GetProxy(this);
        if (!proxy) return;
        //auto playCommand = EffectManagerProxy::CreatePlayCommand(m_effectInstanceName);
        //EffectCommandQueue->PushEffectCommand(std::move(playCommand));
        proxy->UpdateInstanceName(m_effectInstanceName);
        proxy->PushCommand(EffectCommandType::Play);
        m_isPaused = false;
    }
}

void EffectComponent::DestroyCurrentEffect()
{
    if (!m_effectInstanceName.empty())
    {
        EffectRenderProxy* proxy = EffectCommandQueue->GetProxy(this);
        if (!proxy) return;

        // �̰͵� �߰��ؾ� ��!
        proxy->SetPendingRemoveInstance(m_effectInstanceName);
        proxy->PushCommand(EffectCommandType::RemoveEffect);

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
        EffectRenderProxy* proxy = EffectCommandQueue->GetProxy(this);
        if (!proxy) return;
        proxy->UpdateInstanceName(m_effectInstanceName);
        //auto timeScaleCommand = EffectManagerProxy::CreateSetTimeScaleCommand(m_effectInstanceName, m_timeScale);
        //EffectCommandQueue->PushEffectCommand(std::move(timeScaleCommand));
        proxy->UpdateTimeScale(m_timeScale);

        proxy->PushCommand(EffectCommandType::SetTimeScale);

        // ���� ����
        //proxy->UpdateTimeScale(m_timeScale);
        proxy->UpdateLoop(m_loop);
        proxy->PushCommand(EffectCommandType::SetLoop);

        // ���ӽð� ����
        //proxy->UpdateTimeScale(m_timeScale);
        proxy->UpdateDuration(m_duration);
        proxy->PushCommand(EffectCommandType::SetDuration);


        // ��ġ ����
        auto currentPos = GetOwner()->m_transform.GetWorldPosition();
        //auto positionCommand = EffectManagerProxy::CreateSetPositionCommand(m_effectInstanceName, currentPos);
        //EffectCommandQueue->PushEffectCommand(std::move(positionCommand));
        proxy->UpdatePosition(currentPos);
        proxy->PushCommand(EffectCommandType::SetPosition);
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