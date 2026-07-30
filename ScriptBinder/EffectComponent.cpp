#include "ParticleSystem.h"
#include "EffectComponent.h"
#include "EffectManagerProxy.h"
#include "EffectProxyController.h"
#include "EffectRenderProxy.h"

#ifdef _DEBUG
#include "InputManager.h"
#endif


void EffectComponent::Initialize()
{
    EffectRenderProxy* proxy = EffectCommandQueue->RegisterProxy((EffectComponent*)this);

    // Awake������ �⺻ ������ �ϰ� ����Ʈ�� �������� ����
    m_lastPosition = GetOwner()->m_transform.GetWorldPosition();
    m_lastScale = GetOwner()->m_transform.GetWorldScale();
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

        proxy->UpdateInstanceName(m_effectInstanceName);
        proxy->UpdateScale(m_lastScale);
        proxy->PushCommand(EffectCommandType::SetScale);

        proxy->UpdateInstanceName(m_effectInstanceName);
        proxy->UpdatePosition(m_lastPosition);
        proxy->PushCommand(EffectCommandType::SetPosition);

        proxy->UpdateInstanceName(m_effectInstanceName);
        proxy->UpdateRotation(m_lastRotation);
        proxy->PushCommand(EffectCommandType::SetRotation);

    }
}

void EffectComponent::Update(float tick)
{
#ifdef _DEBUG
    if (InputManagement->IsKeyDown('M')) {
        Apply();
    }
#endif

    EffectRenderProxy* proxy = EffectCommandQueue->GetProxy(this);

    if (m_pendingPlay)
    {
        if (proxy) {
            if (m_isNewInstance) {
                proxy->PushCommand(EffectCommandType::PlayWithCustomId);
            }
            else {
                proxy->PushCommand(EffectCommandType::ReplaceEffect);
            }
        }
        m_pendingPlay = false;
    }

    // GetProxy는 등록 전이거나 이미 해제된 컴포넌트에 대해 nullptr을 반환한다.
    // 위 m_pendingPlay 블록은 이를 검사하는데 여기서 빠뜨려 널 역참조가 났다.
    if (proxy && !proxy->GetInstanceName().empty() && m_effectInstanceName != proxy->GetInstanceName())
    {
        m_effectInstanceName = proxy->GetInstanceName();
    }

    if (!m_effectInstanceName.empty())
    {
        Mathf::Vector3 worldPos = GetOwner()->m_transform.GetWorldPosition();
        auto worldQuat = GetOwner()->m_transform.GetWorldQuaternion();
        Mathf::Vector3 worldScale = GetOwner()->m_transform.GetWorldScale();

        float pitch, yaw, roll;
        Mathf::QuaternionToEular(worldQuat, pitch, yaw, roll);
        Mathf::Vector3 currentRot = Mathf::Vector3(-pitch, -yaw, -roll);
        proxy->UpdateInstanceName(m_effectInstanceName);
        proxy->UpdatePosition(worldPos);
        proxy->PushCommand(EffectCommandType::SetPosition);
        proxy->UpdateInstanceName(m_effectInstanceName);
        proxy->UpdateRotation(currentRot);
        proxy->PushCommand(EffectCommandType::SetRotation);
        proxy->UpdateInstanceName(m_effectInstanceName);
        proxy->UpdateScale(worldScale);
        proxy->PushCommand(EffectCommandType::SetScale);
    }

    if (m_isPlaying) {
        m_currentTime += Time->GetElapsedSeconds() * m_timeScale;

        // Component �������� duration üũ
        if (m_duration > 0 && m_currentTime > m_duration) {
            m_isPlaying = false;
        }
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
    m_currentTime = 0.0f;

    // GameObject�� instanceID�� ����ؼ� ���� �ν��Ͻ� �̸� ����
    size_t gameObjectId = GetOwner()->GetInstanceID();
    std::string customInstanceId = effectName + "_obj" + std::to_string(gameObjectId);

    // ���� �ε�
    float templateTimeScale, templateDuration;
    bool templateLoop;
    if (EffectManagerProxy::GetTemplateSettings(m_effectTemplateName, templateTimeScale, templateLoop, templateDuration))
    {
        if (!m_timeScaleModified) {
            m_timeScale = templateTimeScale;
        }
        if (!m_loopModified) {
            m_loop = templateLoop;
        }
        if (!m_durationModified) {
            m_duration = templateDuration;
        }
    }

    auto currentPos = GetOwner()->m_transform.GetWorldPosition();
    auto worldQuat = GetOwner()->m_transform.GetWorldQuaternion();
    auto currentSca = GetOwner()->m_transform.GetWorldScale();
    float pitch, yaw, roll;
    Mathf::QuaternionToEular(worldQuat, pitch, yaw, roll);
    Mathf::Vector3 currentRot = Mathf::Vector3(pitch, yaw, roll);

    // ���� �ν��Ͻ� �̸��� �ٸ��� ���� ����
    if (m_effectInstanceName != customInstanceId) {
        if (!m_effectInstanceName.empty()) {
            DestroyCurrentEffect();
        }
        m_effectInstanceName = customInstanceId;
        m_isNewInstance = true;  // �� �ν��Ͻ� �÷���
    }
    else {
        m_isNewInstance = false;  // Replace �÷���
    }

    // ���Ͻ� ������ �ٷ� �ϰ�
    proxy->UpdateInstanceName(m_effectInstanceName);
    proxy->UpdateTempleteName(m_effectTemplateName);
    proxy->UpdatePosition(currentPos);
    proxy->UpdateRotation(currentRot);
    proxy->UpdateScale(currentSca);
    proxy->UpdateTimeScale(m_timeScale);
    proxy->UpdateLoop(m_loop);
    proxy->UpdateDuration(m_duration);

    // Play ���ɸ� �� ������ �ʰ�
    m_pendingPlay = true;

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
        if (!m_timeScaleModified) {
            m_timeScale = templateTimeScale;
        }
        if (!m_loopModified) {
            m_loop = templateLoop;
        }
        if (!m_durationModified) {
            m_duration = templateDuration;
        }
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
        proxy->PushCommand(EffectCommandType::Pause);
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
        proxy->PushCommand(EffectCommandType::Resume);
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

void EffectComponent::SetLoop(bool loop)
{
    m_loop = loop;
    m_loopModified = true;
    if (!m_effectInstanceName.empty())
    {
        auto loopCommand = EffectManagerProxy::CreateSetLoopCommand(m_effectInstanceName, loop);
        EffectProxyController::GetInstance()->PushEffectCommand(std::move(loopCommand));
    }
}

void EffectComponent::SetDuration(float duration)
{
    m_duration = duration;
    m_durationModified = true;
    if (!m_effectInstanceName.empty())
    {
        auto durationCommand = EffectManagerProxy::CreateSetDurationCommand(m_effectInstanceName, duration);
        EffectProxyController::GetInstance()->PushEffectCommand(std::move(durationCommand));
    }
}

void EffectComponent::SetTimeScale(float timeScale)
{
    m_timeScale = timeScale;
    m_timeScaleModified = true;
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