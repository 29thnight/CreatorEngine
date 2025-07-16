#include "EffectComponent.h"
#include "EffectManagerProxy.h"
#include "EffectProxyController.h"

void EffectComponent::Awake()
{
    // Awake������ �⺻ ������ �ϰ� ����Ʈ�� �������� ����
    m_lastPosition = GetOwner()->m_transform.GetWorldPosition();

    // �⺻ ���ø��� �����Ǿ� ������ �ڵ� ���
    if (!m_effectTemplateName.empty())
    {
        PlayEffectByName(m_effectTemplateName);
    }
}

void EffectComponent::Update(float tick)
{
    if (!m_effectInstanceName.empty() && m_isPlaying)
    {
        // �ð� ������Ʈ
        m_currentTime += tick * m_timeScale;

        // ���ӽð� üũ
        if (!m_loop && m_duration > 0 && m_currentTime >= m_duration)
        {
            StopEffect();
            return;
        }

        // ���� ó��
        if (m_loop && m_duration > 0 && m_currentTime >= m_duration)
        {
            m_currentTime = 0.0f;
            auto playCommand = EffectManagerProxy::CreatePlayCommand(m_effectInstanceName);
            EffectProxyController::GetInstance()->PushEffectCommand(std::move(playCommand));
        }
    }

    // ��ġ ����ȭ�� ����Ʈ�� ��� ���� �ƴϾ �׻� üũ
    if (!m_effectInstanceName.empty())
    {
        auto worldPos = GetOwner()->m_transform.GetWorldPosition();
        Mathf::Vector3 currentPos = Mathf::Vector3(worldPos.m128_f32[0], worldPos.m128_f32[1], worldPos.m128_f32[2]);

        // �Ÿ� ��� üũ�� �� ��Ȯ�ϰ� (�ε��Ҽ��� ���� ����)
        float distance = (m_lastPosition - currentPos).Length();
        if (distance > 0.001f)  // 1mm �̻� ���̳� ���� ������Ʈ
        {
            auto positionCommand = EffectManagerProxy::CreateSetPositionCommand(m_effectInstanceName, currentPos);
            EffectProxyController::GetInstance()->PushEffectCommand(std::move(positionCommand));
            m_lastPosition = currentPos;
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

    // ����Ʈ ���� ����
    ApplyEffectSettings();

    // ����Ʈ ���
    auto playCommand = EffectManagerProxy::CreatePlayCommand(m_effectInstanceName);
    EffectProxyController::GetInstance()->PushEffectCommand(std::move(playCommand));

    // ��ġ ����
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
    }
}