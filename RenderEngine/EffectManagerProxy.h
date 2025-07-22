#pragma once
#include "EffectCommandType.h"
#include "EffectManager.h"
#include "EffectBase.h"

class EffectManagerProxy
{
public:
    // 이펙트 재생 명령
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

    // 이펙트 정지 명령
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

    // 이펙트 위치 설정 명령
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
        cmd.m_executeFunction = [effectName, rotation]() {
            if (auto* effect = EffectManagers->GetEffect(effectName)) {
                effect->SetRotation(rotation);
            }
            };
        return cmd;
    }

    // 이펙트 타임스케일 설정 명령
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

    // 이펙트 제거 명령
    static EffectManagerProxy CreateRemoveEffectCommand(const std::string& effectName)
    {
        EffectManagerProxy cmd;
        cmd.m_executeFunction = [effectName]() {
            EffectManagers->RemoveEffect(effectName);
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
            EffectManagers->CreateEffectInstance(templateName, instanceName);
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