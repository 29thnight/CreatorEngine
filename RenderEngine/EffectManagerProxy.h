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
    // ����Ʈ ��� ���
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

    // ����Ʈ ���� ���
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

    // ����Ʈ ��ġ ���� ���
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

    // ����Ʈ ȸ�� ���� ���
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

    // ����Ʈ Ÿ�ӽ����� ���� ���
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

    // ����Ʈ ���� ���� ���
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

    // ����Ʈ ���ӽð� ���� ���
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

    // ����Ʈ �Ͻ����� ���
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

    // ����Ʈ �簳 ���
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

    // ����Ʈ ���� ���
    static EffectManagerProxy CreateRemoveEffectCommand(const std::string& effectName)
    {
        EffectManagerProxy cmd;
        cmd.m_executeFunction = [effectName]() {
            efm->RemoveEffect(effectName);
            };
        return cmd;
    }

    // ����Ʈ ���� �Ϸ� ���
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

    // ����Ʈ �ν��Ͻ� ����
    static EffectManagerProxy CreateEffectInstanceCommand(const std::string& templateName, const std::string& instanceName)
    {
        EffectManagerProxy cmd;
        cmd.m_executeFunction = [templateName, instanceName]() {
            efm->CreateEffectInstance(templateName, instanceName);
            };
        return cmd;
    }

    // ���ø� ���� �������� (���� �Լ��� ���� ó��)
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

    // ��� ����
    void Execute()
    {
        if (m_executeFunction) {
            m_executeFunction();
        }
    }

private:
    std::function<void()> m_executeFunction;
};