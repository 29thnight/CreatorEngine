#include "PrsmScriptComponent.h"
#include "Entity.h"
#include "LogSystem.h"

void PrsmScriptComponent::OnInitialized()
{
    if (m_typeName.empty() || HasInstance())
    {
        return;
    }
    auto& host = PrsmScriptHost::Get();
    if (!host.IsReady())
    {
        Debug::PrintLog(spdlog::level::warn, "[PrSM] 스크립트 계층이 준비되지 않아 " + m_typeName + " 을 만들지 못했습니다");
        return;
    }
    Entity* owner = GetOwner();
    if (nullptr == owner)
    {
        return;
    }
    m_instance = host.Create(*owner, m_typeName, m_fieldData);
    if (m_instance.IsValid())
    {
        host.Dispatch(m_instance, PrsmLifecycle::Initialized);
    }
}

void PrsmScriptComponent::OnAddedToScene()
{
    auto& host = PrsmScriptHost::Get();
    host.Dispatch(m_instance, PrsmLifecycle::AddedToScene);
    // 엔진은 OnEnable 을 SetEnabled 의 상태 전이에서만 부른다 — 켜진 채 붙은 컴포넌트는
    // 받지 못한다. PrSM 진입 순서(added → enable → begin, D-041)를 여기서 채운다.
    // 호스트의 Enable 은 상태 기준이라 DDOL 재편입으로 다시 와도 두 번 전달되지 않는다.
    if (IsEnabled())
    {
        host.Dispatch(m_instance, PrsmLifecycle::Enable);
    }
}

void PrsmScriptComponent::OnEnable()
{
    PrsmScriptHost::Get().Dispatch(m_instance, PrsmLifecycle::Enable);
}

void PrsmScriptComponent::OnBeginSimulation()
{
    PrsmScriptHost::Get().Dispatch(m_instance, PrsmLifecycle::BeginSimulation);
}

void PrsmScriptComponent::OnEndSimulation()
{
    PrsmScriptHost::Get().Dispatch(m_instance, PrsmLifecycle::EndSimulation);
}

void PrsmScriptComponent::OnDisable()
{
    PrsmScriptHost::Get().Dispatch(m_instance, PrsmLifecycle::Disable);
}

void PrsmScriptComponent::OnRemovingFromScene()
{
    PrsmScriptHost::Get().Dispatch(m_instance, PrsmLifecycle::RemovingFromScene);
}

void PrsmScriptComponent::OnUninitializing()
{
    PrsmScriptHost::Get().Dispatch(m_instance, PrsmLifecycle::Uninitializing);
    Release();
}

void PrsmScriptComponent::Release()
{
    auto& host = PrsmScriptHost::Get();
    if (!host.IsLive(m_instance))
    {
        return;
    }
    // 재생 중 값이 저장 데이터로 남도록 마지막 값을 붙든다(실패한 인스턴스는 그대로 둔다).
    if (!host.IsFailed(m_instance))
    {
        m_fieldData = host.CaptureFields(m_instance);
    }
    host.Destroy(m_instance);
    m_instance = {};
}
