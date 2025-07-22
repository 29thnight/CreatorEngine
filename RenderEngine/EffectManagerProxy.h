#pragma once
#include "EffectCommandType.h"
#include "EffectManager.h"

class EffectManagerProxy
{
public:
    EffectManagerProxy() = default;

    // 복사 생성자 추가
    EffectManagerProxy(const EffectManagerProxy& other) :
        m_executeFunction(other.m_executeFunction),
        m_commandType(other.m_commandType),
        m_effectName(other.m_effectName)
    {
    }

    // 이동 생성자 추가
    EffectManagerProxy(EffectManagerProxy&& other) noexcept :
        m_executeFunction(std::move(other.m_executeFunction)),
        m_commandType(other.m_commandType),
        m_effectName(std::move(other.m_effectName))
    {
    }

    // 이펙트 재생 명령
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

    // 이펙트 정지 명령
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

    // 이펙트 위치 설정 명령
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

    // 이펙트 타임스케일 설정 명령
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

    // 이펙트 회전 설정 명령
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

    // 새 이펙트 생성 명령
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

    // 이펙트 제거 명령
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

    // 이펙트 인스턴스 생성
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

    // 복사 대입 연산자
    EffectManagerProxy& operator=(const EffectManagerProxy& other)
    {
        if (this != &other) {
            m_executeFunction = other.m_executeFunction;
            m_commandType = other.m_commandType;
            m_effectName = other.m_effectName;
        }
        return *this;
    }

    // 이동 대입 연산자
    EffectManagerProxy& operator=(EffectManagerProxy&& other) noexcept
    {
        if (this != &other) {
            m_executeFunction = std::move(other.m_executeFunction);
            m_commandType = other.m_commandType;
            m_effectName = std::move(other.m_effectName);
        }
        return *this;
    }

    // 명령 실행
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