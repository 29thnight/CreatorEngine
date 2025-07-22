#pragma once
#include "Core.Minimal.h"
#include "EffectManager.h"
#include "EffectBase.h"

enum class EffectCommandType
{
    Play,
    Stop,
    Pause,
    Resume,
    SetPosition,
    SetTimeScale,
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
    // 이펙트 재생 명령
    static EffectManagerProxy CreatePlayCommand(const std::string& effectName)
    {
        EffectManagerProxy cmd;
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
        cmd.m_executeFunction = [effectName, position]() {
            if (auto* effect = efm->GetEffect(effectName)) {
                effect->SetPosition(position);
            }
            };
        return cmd;
    }

    // 이펙트 회전 설정 명령
    static EffectManagerProxy CreateSetRotationCommand(const std::string& effectName, const Mathf::Vector3& rotation)
    {
        EffectManagerProxy cmd;
        cmd.m_executeFunction = [effectName, rotation]() {
            if (auto* effect = efm->GetEffect(effectName)) {
                effect->SetRotation(rotation);
            }
            };
        return cmd;
    }

    // 이펙트 타임스케일 설정 명령
    static EffectManagerProxy CreateSetTimeScaleCommand(const std::string& effectName, float timeScale)
    {
        EffectManagerProxy cmd;
        cmd.m_executeFunction = [effectName, timeScale]() {
            if (auto* effect = efm->GetEffect(effectName)) {
                effect->SetTimeScale(timeScale);
            }
            };
        return cmd;
    }

    // 이펙트 루프 설정 명령
    static EffectManagerProxy CreateSetLoopCommand(const std::string& effectName, bool loop)
    {
        EffectManagerProxy cmd;
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
        cmd.m_executeFunction = [effectName]() {
            if (auto* effect = efm->GetEffect(effectName)) {
                effect->Resume();
            }
            };
        return cmd;
    }

    // 이펙트 제거 명령
    static EffectManagerProxy CreateRemoveEffectCommand(const std::string& effectName)
    {
        EffectManagerProxy cmd;
        cmd.m_executeFunction = [effectName]() {
            efm->RemoveEffect(effectName);
            };
        return cmd;
    }

    // 이펙트 강제 완료 명령
    static EffectManagerProxy CreateForceFinishCommand(const std::string& effectName)
    {
        EffectManagerProxy cmd;
        cmd.m_executeFunction = [effectName]() {
            if (auto* effect = efm->GetEffect(effectName)) {
                effect->SetLoop(false);
                effect->SetDuration(0.001f);
            }
            };
        return cmd;
    }

    // 이펙트 인스턴스 생성
    static EffectManagerProxy CreateEffectInstanceCommand(const std::string& templateName, const std::string& instanceName)
    {
        EffectManagerProxy cmd;
        cmd.m_executeFunction = [templateName, instanceName]() {
            efm->CreateEffectInstance(templateName, instanceName);
            };
        return cmd;
    }

    // 템플릿 설정 가져오기 (정적 함수로 직접 처리)
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

    // 명령 실행
    void Execute()
    {
        if (m_executeFunction) {
            m_executeFunction();
        }
    }

private:
    std::function<void()> m_executeFunction;
};