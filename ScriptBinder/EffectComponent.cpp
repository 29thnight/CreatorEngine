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

    // Awake에서는 기본 설정만 하고 이펙트는 생성하지 않음
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
            // JSON에서 로드된 원본 설정으로 컴포넌트 변수 업데이트
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

    if (!proxy->GetInstanceName().empty() && m_effectInstanceName != proxy->GetInstanceName())
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

        // Component 레벨에서 duration 체크
        if (m_duration > 0 && m_currentTime > m_duration) {
            m_isPlaying = false;
        }
    }
}

void EffectComponent::OnDestroy()
{
    // 현재 인스턴스가 있으면 삭제
    DestroyCurrentEffect();
    EffectCommandQueue->UnRegisterProxy(this);
}

void EffectComponent::Apply()
{
    // 현재 설정된 템플릿으로 재생
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

    // GameObject의 instanceID를 사용해서 고정 인스턴스 이름 생성
    size_t gameObjectId = GetOwner()->GetInstanceID();
    std::string customInstanceId = effectName + "_obj" + std::to_string(gameObjectId);

    // 설정 로드
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

    // 기존 인스턴스 이름과 다르면 새로 생성
    if (m_effectInstanceName != customInstanceId) {
        if (!m_effectInstanceName.empty()) {
            DestroyCurrentEffect();
        }
        m_effectInstanceName = customInstanceId;
        m_isNewInstance = true;  // 새 인스턴스 플래그
    }
    else {
        m_isNewInstance = false;  // Replace 플래그
    }

    // 프록시 설정은 바로 하고
    proxy->UpdateInstanceName(m_effectInstanceName);
    proxy->UpdateTempleteName(m_effectTemplateName);
    proxy->UpdatePosition(currentPos);
    proxy->UpdateRotation(currentRot);
    proxy->UpdateScale(currentSca);
    proxy->UpdateTimeScale(m_timeScale);
    proxy->UpdateLoop(m_loop);
    proxy->UpdateDuration(m_duration);

    // Play 명령만 한 프레임 늦게
    m_pendingPlay = true;

    m_lastPosition = currentPos;
    m_lastRotation = currentRot;
    m_isPlaying = true;
}

void EffectComponent::ChangeEffect(const std::string& newEffectName)
{
    if (newEffectName.empty()) return;

    // 새로운 이펙트 이름 설정
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

    // 기존 이펙트가 있으면 Replace, 없으면 새로 생성
    if (!m_effectInstanceName.empty()) {
        // Replace로 처리 (ID 유지)
        PlayEffectByName(newEffectName);
    }
    else {
        // 새로 생성
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

        // 이것도 추가해야 함!
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

        // threshold 검사 없이 즉시 업데이트
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