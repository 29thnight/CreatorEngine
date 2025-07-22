#include "ParticleSystem.h"
#include "EffectComponent.h"
#include "EffectManagerProxy.h"
#include "EffectProxyController.h"
#include "EffectRenderProxy.h"

void EffectComponent::Awake()
{
    EffectRenderProxy* proxy = EffectCommandQueue->RegisterProxy(this);

    // Awake에서는 기본 설정만 하고 이펙트는 생성하지 않음
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
            // JSON에서 로드된 원본 설정으로 컴포넌트 변수 업데이트
            m_timeScale = templateTimeScale;
            m_loop = templateLoop;
            m_duration = templateDuration;
        }

        // 이제 동기화된 설정으로 Effect 재생
        PlayEffectByName(m_effectTemplateName);
    }
}

void EffectComponent::Update(float tick)
{
    EffectRenderProxy* proxy = EffectCommandQueue->GetProxy(this);
    if (!proxy) return;

    if (!m_effectInstanceName.empty())
    {
        auto worldPos = GetOwner()->m_transform.GetWorldPosition();
        auto worldQuat = GetOwner()->m_transform.GetWorldQuaternion();

        Mathf::Vector3 currentPos = Mathf::Vector3(worldPos.m128_f32[0], worldPos.m128_f32[1], worldPos.m128_f32[2]);

        float pitch, yaw, roll;
        Mathf::QuaternionToEular(worldQuat, pitch, yaw, roll);
        Mathf::Vector3 currentRot = Mathf::Vector3(pitch, yaw, roll);

        // threshold를 더 크게 조정하거나 제거
        float posThreshold = 0.01f;  // 0.001f에서 0.01f로 증가
        float posDistance = (m_lastPosition - currentPos).Length();
        if (posDistance > posThreshold)
        {
            proxy->UpdatePosition(currentPos);
            proxy->PushCommand(EffectCommandType::SetPosition);
            //auto positionCommand = EffectManagerProxy::CreateSetPositionCommand(m_effectInstanceName, currentPos);
            //EffectCommandQueue->PushEffectCommand(std::move(positionCommand));
            m_lastPosition = currentPos;
        }

        float rotThreshold = 0.01f;  // 회전도 threshold 조정
        float rotDistance = (m_lastRotation - currentRot).Length();
        if (rotDistance > rotThreshold)
        {
            proxy->UpdateRotation(currentRot);
            proxy->PushCommand(EffectCommandType::SetRotation);
            //auto rotationCommand = EffectManagerProxy::CreateSetRotationCommand(m_effectInstanceName, currentRot);
            //EffectCommandQueue->PushEffectCommand(std::move(rotationCommand));
            m_lastRotation = currentRot;
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

    // 기존 이펙트가 있으면 먼저 삭제
    if (!m_effectInstanceName.empty())
    {
        DestroyCurrentEffect();
    }

    // 새로운 고유 인스턴스 이름 생성
    m_effectInstanceName = effectName + "_" + std::to_string(GetInstanceID()) + "_" + std::to_string(m_instanceCounter++);
	m_effectTemplateName = effectName;

    // 템플릿에서 인스턴스 생성(???)
    //auto createCommand = EffectManagerProxy::CreateEffectInstanceCommand(effectName, m_effectInstanceName);
    //EffectCommandQueue->PushEffectCommand(std::move(createCommand));
	proxy->UpdateTempleteName(m_effectTemplateName);
    proxy->UpdateInstanceName(m_effectInstanceName);
    proxy->PushCommand(EffectCommandType::CreateInstance); //현 로직은 이와 같을 것으로 평가됨.

    // 먼저 현재 컴포넌트 설정을 적용
    ApplyEffectSettings();

    // 이펙트 재생
    //auto playCommand = EffectManagerProxy::CreatePlayCommand(m_effectInstanceName);
    //EffectCommandQueue->PushEffectCommand(std::move(playCommand));
    proxy->PushCommand(EffectCommandType::Play);

    float templateTimeScale, templateDuration;
    bool templateLoop;

    if (EffectManagerProxy::GetTemplateSettings(effectName, templateTimeScale, templateLoop, templateDuration))
    {
        // 템플릿 설정을 가져온 후 다시 적용
        m_timeScale = templateTimeScale;
        m_loop = templateLoop;
        m_duration = templateDuration;

        // 변경된 설정을 다시 적용
        ApplyEffectSettings();
    }

    
    // 위치 설정
    auto currentPos = GetOwner()->m_transform.GetWorldPosition();
    //auto positionCommand = EffectManagerProxy::CreateSetPositionCommand(m_effectInstanceName, currentPos);
    //EffectCommandQueue->PushEffectCommand(std::move(positionCommand));
    proxy->UpdatePosition(currentPos);
    proxy->PushCommand(EffectCommandType::SetPosition);

    // 이펙트 재생
    auto playCommand = EffectManagerProxy::CreatePlayCommand(m_effectInstanceName);
    EffectProxyController::GetInstance()->PushEffectCommand(std::move(playCommand));

    m_lastPosition = currentPos;
    m_isPlaying = true;
}

void EffectComponent::ChangeEffect(const std::string& newEffectName)
{
    if (newEffectName.empty()) return;

    // 기존 이펙트 정지 및 삭제
    DestroyCurrentEffect();

    // 새로운 이펙트로 변경
    m_effectTemplateName = newEffectName;

    // 새 이펙트 재생
    PlayEffectByName(newEffectName);
}

void EffectComponent::StopEffect()
{
    if (!m_effectInstanceName.empty() && m_isPlaying)
    {
        EffectRenderProxy* proxy = EffectCommandQueue->GetProxy(this);
        if (!proxy) return;

        //auto stopCommand = EffectManagerProxy::CreateStopCommand(m_effectInstanceName);
        //EffectCommandQueue->PushEffectCommand(std::move(stopCommand));

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
        //auto removeCommand = EffectManagerProxy::CreateRemoveEffectCommand(m_effectInstanceName);
        //EffectCommandQueue->PushEffectCommand(std::move(removeCommand));
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

void EffectComponent::ApplyEffectSettings()
{
    if (!m_effectInstanceName.empty())
    {
        EffectRenderProxy* proxy = EffectCommandQueue->GetProxy(this);
        if (!proxy) return;
        //auto timeScaleCommand = EffectManagerProxy::CreateSetTimeScaleCommand(m_effectInstanceName, m_timeScale);
        //EffectCommandQueue->PushEffectCommand(std::move(timeScaleCommand));
        proxy->UpdateTimeScale(m_timeScale);
        proxy->PushCommand(EffectCommandType::SetTimeScale);

        // 루프 설정
        //proxy->UpdateTimeScale(m_timeScale);
        proxy->PushCommand(EffectCommandType::SetLoop);

        // 지속시간 설정
        //proxy->UpdateTimeScale(m_timeScale);
        proxy->PushCommand(EffectCommandType::SetDuration);


        // 위치 설정
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