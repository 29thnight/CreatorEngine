#pragma once
#include "EffectCommandType.h"
#include "EffectManager.h"
#include "EffectBase.h"

class EffectManagerProxy
{
public:
    // ����Ʈ ��� ���
    static EffectManagerProxy CreatePlayCommand(const std::string& effectName)
    {
        EffectManagerProxy cmd;
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
        cmd.m_executeFunction = [effectName, rotation]() {
            if (auto* effect = EffectManagers->GetEffect(effectName)) {
                effect->SetRotation(rotation);
            }
            };
        return cmd;
    }

    // ����Ʈ Ÿ�ӽ����� ���� ���
    static EffectManagerProxy CreateSetTimeScaleCommand(const std::string& effectName, float timeScale)
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
        cmd.m_executeFunction = [effectName]() {
            EffectManagers->RemoveEffect(effectName);
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
            EffectManagers->CreateEffectInstance(templateName, instanceName);
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