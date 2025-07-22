#pragma once
#include "EffectCommandType.h"
#include "EffectManager.h"

class EffectManagerProxy
{
public:
    EffectManagerProxy() = default;

    // ���� ������ �߰�
    EffectManagerProxy(const EffectManagerProxy& other) :
        m_executeFunction(other.m_executeFunction),
        m_commandType(other.m_commandType),
        m_effectName(other.m_effectName)
    {
    }

    // �̵� ������ �߰�
    EffectManagerProxy(EffectManagerProxy&& other) noexcept :
        m_executeFunction(std::move(other.m_executeFunction)),
        m_commandType(other.m_commandType),
        m_effectName(std::move(other.m_effectName))
    {
    }

    // ����Ʈ ��� ���
    static EffectManagerProxy CreatePlayCommand(const std::string& effectName)
    {
        EffectManagerProxy cmd;
        cmd.m_commandType = EffectCommandType::Play;
        cmd.m_effectName = effectName;
        cmd.m_executeFunction = [effectName]() {
            if (auto* effect = EffectManagers->GetEffect(effectName)) {
                effect->Play();
            }
            };
        return cmd;
    }

    // ����Ʈ ���� ���
    static EffectManagerProxy CreateStopCommand(const std::string& effectName)
    {
        EffectManagerProxy cmd;
        cmd.m_commandType = EffectCommandType::Stop;
        cmd.m_effectName = effectName;
        cmd.m_executeFunction = [effectName]() {
            if (auto* effect = EffectManagers->GetEffect(effectName)) {
                effect->Stop();
            }
            };
        return cmd;
    }

    // ����Ʈ ��ġ ���� ���
    static EffectManagerProxy CreateSetPositionCommand(const std::string& effectName, const Mathf::Vector3& position)
    {
        EffectManagerProxy cmd;
        cmd.m_commandType = EffectCommandType::SetPosition;
        cmd.m_effectName = effectName;
        cmd.m_executeFunction = [effectName, position]() {
            if (auto* effect = EffectManagers->GetEffect(effectName)) {
                effect->SetPosition(position);
            }
            };
        return cmd;
    }

    // ����Ʈ Ÿ�ӽ����� ���� ���
    static EffectManagerProxy CreateSetTimeScaleCommand(const std::string& effectName, float timeScale)
    {
        EffectManagerProxy cmd;
        cmd.m_commandType = EffectCommandType::SetTimeScale;
        cmd.m_effectName = effectName;
        cmd.m_executeFunction = [effectName, timeScale]() {
            if (auto* effect = EffectManagers->GetEffect(effectName)) {
                effect->SetTimeScale(timeScale);
            }
            };
        return cmd;
    }

    // ����Ʈ ȸ�� ���� ���
    static EffectManagerProxy CreateSetRotationCommand(const std::string& effectName, const Mathf::Vector3& rotation)
    {
        EffectManagerProxy cmd;
        cmd.m_commandType = EffectCommandType::SetRotation;
        cmd.m_effectName = effectName;
        cmd.m_executeFunction = [effectName, rotation]() {
            if (auto* effect = EffectManagers->GetEffect(effectName)) {
                effect->SetRotation(rotation);
            }
            };
        return cmd;
    }

    // �� ����Ʈ ���� ���
    static EffectManagerProxy CreateRegisterEffectCommand(const std::string& effectName,
        const std::vector<std::shared_ptr<ParticleSystem>>& emitters)
    {
        EffectManagerProxy cmd;
        cmd.m_commandType = EffectCommandType::CreateEffect;
        cmd.m_effectName = effectName;
        cmd.m_executeFunction = [effectName, emitters]() {
            EffectManagers->RegisterCustomEffect(effectName, emitters);
            };
        return cmd;
    }

    // ����Ʈ ���� ���
    static EffectManagerProxy CreateRemoveEffectCommand(const std::string& effectName)
    {
        EffectManagerProxy cmd;
        cmd.m_commandType = EffectCommandType::RemoveEffect;
        cmd.m_effectName = effectName;
        cmd.m_executeFunction = [effectName]() {
            EffectManagers->RemoveEffect(effectName);
            };
        return cmd;
    }

    // ����Ʈ �ν��Ͻ� ����
    static EffectManagerProxy CreateEffectInstanceCommand(const std::string& templateName, const std::string& instanceName)
    {
        EffectManagerProxy cmd;
        cmd.m_commandType = EffectCommandType::CreateInstance;
        cmd.m_effectName = instanceName;
        cmd.m_executeFunction = [templateName, instanceName]() {
            EffectManagers->CreateEffectInstance(templateName, instanceName);
            };
        return cmd;
    }

    // ���� ���� ������
    EffectManagerProxy& operator=(const EffectManagerProxy& other)
    {
        if (this != &other) {
            m_executeFunction = other.m_executeFunction;
            m_commandType = other.m_commandType;
            m_effectName = other.m_effectName;
        }
        return *this;
    }

    // �̵� ���� ������
    EffectManagerProxy& operator=(EffectManagerProxy&& other) noexcept
    {
        if (this != &other) {
            m_executeFunction = std::move(other.m_executeFunction);
            m_commandType = other.m_commandType;
            m_effectName = std::move(other.m_effectName);
        }
        return *this;
    }

    // ��� ����
    void Execute()
    {
        if (m_executeFunction) {
            m_executeFunction();
        }
    }

    EffectCommandType GetCommandType() const { return m_commandType; }
    const std::string& GetEffectName() const { return m_effectName; }

private:
    std::function<void()> m_executeFunction;
    EffectCommandType m_commandType;
    std::string m_effectName;
};