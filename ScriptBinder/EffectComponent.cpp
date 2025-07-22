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
        PlayEffectByName(m_effectTemplateName);
    }
}

void EffectComponent::Update(float tick)
{
    EffectRenderProxy* proxy = EffectCommandQueue->GetProxy(this);
    if (!proxy) return;

    if (!m_effectInstanceName.empty() && m_isPlaying)
    {
        m_currentTime += tick * m_timeScale;

        if (!m_loop && m_duration > 0 && m_currentTime >= m_duration)
        {
            StopEffect();
            return;
        }

        if (m_loop && m_duration > 0 && m_currentTime >= m_duration)
        {
            m_currentTime = 0.0f;
            
            proxy->PushCommand(EffectCommandType::Play);
            //auto playCommand = EffectManagerProxy::CreatePlayCommand(m_effectInstanceName);
            //EffectCommandQueue->PushEffectCommand(std::move(playCommand));
        }
    }

    if (!m_effectInstanceName.empty())
    {
        auto worldPos = GetOwner()->m_transform.GetWorldPosition();
        auto worldQuat = GetOwner()->m_transform.GetWorldQuaternion();

        Mathf::Vector3 currentPos = Mathf::Vector3(worldPos.m128_f32[0], worldPos.m128_f32[1], worldPos.m128_f32[2]);

        float pitch, yaw, roll;
        Mathf::QuaternionToEular(worldQuat, pitch, yaw, roll);
        Mathf::Vector3 currentRot = Mathf::Vector3(pitch, yaw, roll);

        float posDistance = (m_lastPosition - currentPos).Length();
        if (posDistance > 0.001f)
        {
            proxy->UpdatePosition(currentPos);
            proxy->PushCommand(EffectCommandType::SetPosition);
            //auto positionCommand = EffectManagerProxy::CreateSetPositionCommand(m_effectInstanceName, currentPos);
            //EffectCommandQueue->PushEffectCommand(std::move(positionCommand));
            m_lastPosition = currentPos;
        }

        float rotDistance = (m_lastRotation - currentRot).Length();
        if (rotDistance > 0.001f)
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

    // 이펙트 설정 적용
    ApplyEffectSettings();

    // 이펙트 재생
    //auto playCommand = EffectManagerProxy::CreatePlayCommand(m_effectInstanceName);
    //EffectCommandQueue->PushEffectCommand(std::move(playCommand));
    proxy->PushCommand(EffectCommandType::Play);

    // 위치 설정
    auto currentPos = GetOwner()->m_transform.GetWorldPosition();
    //auto positionCommand = EffectManagerProxy::CreateSetPositionCommand(m_effectInstanceName, currentPos);
    //EffectCommandQueue->PushEffectCommand(std::move(positionCommand));
    proxy->UpdatePosition(currentPos);
    proxy->PushCommand(EffectCommandType::SetPosition);

    m_lastPosition = currentPos;
    m_isPlaying = true;
    m_currentTime = 0.0f;
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
        m_currentTime = 0.0f;
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
        m_currentTime = 0.0f;
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

        // 이펙트 기준점을 컴포넌트 위치로 설정
        auto currentPos = GetOwner()->m_transform.GetWorldPosition();
        //auto positionCommand = EffectManagerProxy::CreateSetPositionCommand(m_effectInstanceName, currentPos);
        //EffectCommandQueue->PushEffectCommand(std::move(positionCommand));
        proxy->UpdatePosition(currentPos);
        proxy->PushCommand(EffectCommandType::SetPosition);
    }
}