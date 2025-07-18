#include "EffectComponent.h"
#include "EffectManagerProxy.h"
#include "EffectProxyController.h"

void EffectComponent::Awake()
{
    // Awake에서는 기본 설정만 하고 이펙트는 생성하지 않음
    m_lastPosition = GetOwner()->m_transform.GetWorldPosition();
    auto worldQuat = GetOwner()->m_transform.GetWorldQuaternion();
    float pitch, yaw, roll;
    Mathf::QuaternionToEular(worldQuat, pitch, yaw, roll);
    m_lastRotation = Mathf::Vector3(pitch, yaw, roll);

    if (!m_effectTemplateName.empty())
    {
        PlayEffectByName(m_effectTemplateName);
    }
}

void EffectComponent::Update(float tick)
{
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
            auto playCommand = EffectManagerProxy::CreatePlayCommand(m_effectInstanceName);
            EffectProxyController::GetInstance()->PushEffectCommand(std::move(playCommand));
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
            auto positionCommand = EffectManagerProxy::CreateSetPositionCommand(m_effectInstanceName, currentPos);
            EffectProxyController::GetInstance()->PushEffectCommand(std::move(positionCommand));
            m_lastPosition = currentPos;
        }

        float rotDistance = (m_lastRotation - currentRot).Length();
        if (rotDistance > 0.001f)
        {
            auto rotationCommand = EffectManagerProxy::CreateSetRotationCommand(m_effectInstanceName, currentRot);
            EffectProxyController::GetInstance()->PushEffectCommand(std::move(rotationCommand));
            m_lastRotation = currentRot;
        }
    }
}

void EffectComponent::OnDestroy()
{
    // 현재 인스턴스가 있으면 삭제
    DestroyCurrentEffect();
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

    // 기존 이펙트가 있으면 먼저 삭제
    if (!m_effectInstanceName.empty())
    {
        DestroyCurrentEffect();
    }

    // 새로운 고유 인스턴스 이름 생성
    m_effectInstanceName = effectName + "_" + std::to_string(GetInstanceID()) + "_" + std::to_string(m_instanceCounter++);

    // 템플릿에서 인스턴스 생성
    auto createCommand = EffectManagerProxy::CreateEffectInstanceCommand(effectName, m_effectInstanceName);
    EffectProxyController::GetInstance()->PushEffectCommand(std::move(createCommand));

    // 이펙트 설정 적용
    ApplyEffectSettings();

    // 이펙트 재생
    auto playCommand = EffectManagerProxy::CreatePlayCommand(m_effectInstanceName);
    EffectProxyController::GetInstance()->PushEffectCommand(std::move(playCommand));

    // 위치 설정
    auto currentPos = GetOwner()->m_transform.GetWorldPosition();
    auto positionCommand = EffectManagerProxy::CreateSetPositionCommand(m_effectInstanceName, currentPos);
    EffectProxyController::GetInstance()->PushEffectCommand(std::move(positionCommand));

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
        auto stopCommand = EffectManagerProxy::CreateStopCommand(m_effectInstanceName);
        EffectProxyController::GetInstance()->PushEffectCommand(std::move(stopCommand));
        m_isPlaying = false;
        m_currentTime = 0.0f;
    }
}

void EffectComponent::PauseEffect()
{
    if (!m_effectInstanceName.empty() && m_isPlaying && !m_isPaused)
    {
        auto stopCommand = EffectManagerProxy::CreateStopCommand(m_effectInstanceName);
        EffectProxyController::GetInstance()->PushEffectCommand(std::move(stopCommand));
        m_isPaused = true;
    }
}

void EffectComponent::ResumeEffect()
{
    if (!m_effectInstanceName.empty() && m_isPlaying && m_isPaused)
    {
        auto playCommand = EffectManagerProxy::CreatePlayCommand(m_effectInstanceName);
        EffectProxyController::GetInstance()->PushEffectCommand(std::move(playCommand));
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
        m_currentTime = 0.0f;
    }
}

void EffectComponent::ApplyEffectSettings()
{
    if (!m_effectInstanceName.empty())
    {
        auto timeScaleCommand = EffectManagerProxy::CreateSetTimeScaleCommand(m_effectInstanceName, m_timeScale);
        EffectProxyController::GetInstance()->PushEffectCommand(std::move(timeScaleCommand));

        // 이펙트 기준점을 컴포넌트 위치로 설정
        auto currentPos = GetOwner()->m_transform.GetWorldPosition();
        auto positionCommand = EffectManagerProxy::CreateSetPositionCommand(m_effectInstanceName, currentPos);
        EffectProxyController::GetInstance()->PushEffectCommand(std::move(positionCommand));
    }
}