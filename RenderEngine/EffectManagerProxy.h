#pragma once
#include "EffectCommandType.h"
#include "EffectManager.h"
#include "EffectBase.h"

class EffectManagerProxy
{
public:
    // 이펙트 재생 명령
    static EffectManagerProxy CreatePlayCommand(const std::string& templateName) {
        EffectManagerProxy cmd;
        cmd.m_executeFunction = [templateName]() {
            EffectManagers->PlayEffect(templateName);
            };
        return cmd;
    }

    // 이펙트 정지 명령
    static EffectManagerProxy CreateStopCommand(const std::string& instanceId)
    {
        EffectManagerProxy cmd;
        cmd.m_executeFunction = [instanceId](){
            if (auto* effect = EffectManagers->GetEffectInstance(instanceId)) {
                effect->Stop();
            }
            };
        return cmd;
    }

    // 이펙트 위치 설정 명령
    static EffectManagerProxy CreateSetPositionCommand(const std::string& instanceId, const Mathf::Vector3& position)
    {
        EffectManagerProxy cmd;
        cmd.m_executeFunction = [instanceId, position](){
            if (auto* effect = EffectManagers->GetEffectInstance(instanceId)) {
                effect->SetPosition(position);
            }
            };
        return cmd;
    }

    // 이펙트 타임스케일 설정 명령
    static EffectManagerProxy CreateSetTimeScaleCommand(const std::string& instanceId, float timeScale)
    {
        EffectManagerProxy cmd;
        cmd.m_executeFunction = [instanceId, timeScale](){
            if (auto* effect = EffectManagers->GetEffectInstance(instanceId)) {
                effect->SetTimeScale(timeScale);
            }
            };
        return cmd;
    }

    // 이펙트 회전 설정 명령
    static EffectManagerProxy CreateSetRotationCommand(const std::string& instanceId, const Mathf::Vector3& rotation)
    {
        EffectManagerProxy cmd;
        cmd.m_executeFunction = [instanceId, rotation](){
            if (auto* effect = EffectManagers->GetEffectInstance(instanceId)) {
                effect->SetRotation(rotation);
            }
            };
        return cmd;
    }

    // 이펙트 인스턴스 제거 명령
    static EffectManagerProxy CreateRemoveEffectCommand(const std::string& instanceId)
    {
        EffectManagerProxy cmd;
        cmd.m_executeFunction = [instanceId](){
            EffectManagers->RemoveEffect(instanceId);
            };
        return cmd;
    }

    static EffectManagerProxy CreateSetLoopCommand(const std::string& instanceId, bool isLoop)
    {
        EffectManagerProxy cmd;
        cmd.m_executeFunction = [instanceId, isLoop](){
            if (auto* effect = EffectManagers->GetEffectInstance(instanceId)) {
                effect->SetLoop(isLoop);
            }
            };
        return cmd;
    }

    static EffectManagerProxy CreateSetDurationCommand(const std::string& instanceId, float duration)
    {
        EffectManagerProxy cmd;
        cmd.m_executeFunction = [instanceId, duration](){
            if (auto* effect = EffectManagers->GetEffectInstance(instanceId)) {
                effect->SetDuration(duration);
            }
            };
        return cmd;
    }

    // 이펙트 강제 완료 명령
    static EffectManagerProxy CreateForceFinishCommand(const std::string& instanceId)
    {
        EffectManagerProxy cmd;
        cmd.m_executeFunction = [instanceId](){
            if (auto* effect = EffectManagers->GetEffectInstance(instanceId)) {
                effect->SetLoop(false);
                effect->SetDuration(0.001f);
            }
            return "";
            };
        return cmd;
    }

    static EffectManagerProxy CreateReplaceEffectCommand(const std::string& instanceId, const std::string& newTemplateName)
    {
        EffectManagerProxy cmd;
        cmd.m_executeFunction = [instanceId, newTemplateName]() {
            return EffectManagers->ReplaceEffect(instanceId, newTemplateName);
            };
        return cmd;
    }

    // 템플릿 설정 가져오기 (정적 함수로 직접 처리)
    static bool GetTemplateSettings(const std::string& templateName,
        float& outTimeScale,
        bool& outLoop,
        float& outDuration)
    {
        return EffectManagers->GetTemplateSettings(templateName, outTimeScale, outLoop, outDuration);
    }

    static EffectManagerProxy CreatePlayWithCustomIdCommand(const std::string& templateName, const std::string& customInstanceId)
    {
        EffectManagerProxy cmd;
        cmd.m_executeFunction = [templateName, customInstanceId]() {
            return EffectManagers->PlayEffectWithCustomId(templateName, customInstanceId);
            };
        return cmd;
    }

    //static uint32_t GetCurrentInstanceCounter() {
    //    return EffectManagers->GetInstanceId();
    //}

    // 명령 실행
    void Execute()
    {
        if (m_executeFunction) {
            return m_executeFunction();
        }
    }


private:
    std::function<void()> m_executeFunction;
};