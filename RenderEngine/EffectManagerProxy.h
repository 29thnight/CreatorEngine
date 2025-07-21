#pragma once
#include "Core.Minimal.h"
#include "EffectManager.h"

enum class EffectCommandType
{
    Play,
    Stop,
    Pause,
    Resume,
    SetPosition,
    SetTimeScale,
    CreateEffect,
    RemoveEffect,
    UpdateEffectProperty,
    CreateInstance,
    SetRotation,
    SetLoop,
    SetDuration,
    GetState,
    IsFinished,
    ForceFinish
};

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
            if (auto* effect = efm->GetEffect(effectName)) {
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
            if (auto* effect = efm->GetEffect(effectName)) {
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
            if (auto* effect = efm->GetEffect(effectName)) {
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
            if (auto* effect = efm->GetEffect(effectName)) {
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
            if (auto* effect = efm->GetEffect(effectName)) {
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
            efm->RegisterCustomEffect(effectName, emitters);
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
            efm->RemoveEffect(effectName);
            };
        return cmd;
    }

    // 이펙트 루프 설정 명령
    static EffectManagerProxy CreateSetLoopCommand(const std::string& effectName, bool loop)
    {
        EffectManagerProxy cmd;
        cmd.m_commandType = EffectCommandType::SetLoop;
        cmd.m_effectName = effectName;
        cmd.m_executeFunction = [effectName, loop]() {
            if (auto* effect = efm->GetEffect(effectName)) {
                effect->SetLoop(loop);
            }
            };
        return cmd;
    }

    // 이펙트 지속시간 설정 명령
    static EffectManagerProxy CreateSetDurationCommand(const std::string& effectName, float duration)
    {
        EffectManagerProxy cmd;
        cmd.m_commandType = EffectCommandType::SetDuration;
        cmd.m_effectName = effectName;
        cmd.m_executeFunction = [effectName, duration]() {
            if (auto* effect = efm->GetEffect(effectName)) {
                effect->SetDuration(duration);
            }
            };
        return cmd;
    }

    // 이펙트 일시정지 명령
    static EffectManagerProxy CreatePauseCommand(const std::string& effectName)
    {
        EffectManagerProxy cmd;
        cmd.m_commandType = EffectCommandType::Pause;
        cmd.m_effectName = effectName;
        cmd.m_executeFunction = [effectName]() {
            if (auto* effect = efm->GetEffect(effectName)) {
                effect->Pause();
            }
            };
        return cmd;
    }

    // 이펙트 재개 명령
    static EffectManagerProxy CreateResumeCommand(const std::string& effectName)
    {
        EffectManagerProxy cmd;
        cmd.m_commandType = EffectCommandType::Resume;
        cmd.m_effectName = effectName;
        cmd.m_executeFunction = [effectName]() {
            if (auto* effect = efm->GetEffect(effectName)) {
                effect->Resume();
            }
            };
        return cmd;
    }

    // 이펙트 강제 완료 명령 (Finished 상태로 만들기)
    static EffectManagerProxy CreateForceFinishCommand(const std::string& effectName)
    {
        EffectManagerProxy cmd;
        cmd.m_commandType = EffectCommandType::ForceFinish;
        cmd.m_effectName = effectName;
        cmd.m_executeFunction = [effectName]() {
            if (auto* effect = efm->GetEffect(effectName)) {
                // EffectBase에 ForceFinish() 메서드가 있다고 가정
                // 또는 직접 상태를 변경하는 방법
                effect->SetLoop(false);
                effect->SetDuration(0.001f); // 아주 짧은 시간으로 설정해서 바로 종료되도록
            }
            };
        return cmd;
    }

    static bool GetTemplateSettings(const std::string& templateName,
        float& outTimeScale,
        bool& outLoop,
        float& outDuration)
    {
        auto* templateEffect = efm->GetEffect(templateName);
        if (templateEffect) {
            outTimeScale = templateEffect->GetTimeScale();
            outLoop = templateEffect->IsLooping();
            outDuration = templateEffect->GetDuration();
            return true;
        }
        return false;
    }

    // 이펙트 인스턴스 생성
    static EffectManagerProxy CreateEffectInstanceCommand(const std::string& templateName, const std::string& instanceName)
    {
        EffectManagerProxy cmd;
        cmd.m_commandType = EffectCommandType::CreateInstance;
        cmd.m_effectName = instanceName;
        cmd.m_executeFunction = [templateName, instanceName]() {
            efm->CreateEffectInstance(templateName, instanceName);
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