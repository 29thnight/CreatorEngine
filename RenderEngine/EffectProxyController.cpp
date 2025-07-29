#include "EffectProxyController.h"
#include "EffectRenderProxy.h"
#include "EffectComponent.h"

void EffectProxyController::PrepareCommandBehavior()
{
	for (auto& [instanceID, proxy] : m_proxyContainer)
	{
		CommandBehavior(proxy);
	}
}

void EffectProxyController::ExecuteEffectCommands()
{
	auto& currFrameQueue = PrepareFrameEffectCommands();
	while (!currFrameQueue.empty())
	{
		EffectManagerProxy command;
		if (currFrameQueue.try_pop(command))
		{
			try
			{
				command.Execute();
			}
			catch (const std::exception& e)
			{
				Debug->LogWarning("Effect command execution failed: " + std::string(e.what()));
				continue;
			}
		}
	}
}

EffectRenderProxy* EffectProxyController::RegisterProxy(EffectComponent* component)
{
	HashedGuid instanceID = component->GetInstanceID();
	auto it = m_proxyContainer.find(instanceID);
	if (it == m_proxyContainer.end())
	{
		m_proxyContainer[instanceID] = new EffectRenderProxy;
	}
	return m_proxyContainer[instanceID];
}

void EffectProxyController::UnRegisterProxy(EffectComponent* component)
{
	HashedGuid instanceID = component->GetInstanceID();
	auto it = m_proxyContainer.find(instanceID);
	if (it != m_proxyContainer.end())
	{
		CommandBehavior(it->second);

		delete it->second;
		m_proxyContainer.erase(it);
	}
}

EffectRenderProxy* EffectProxyController::GetProxy(EffectComponent* component)
{
	HashedGuid instanceID = component->GetInstanceID();
	auto it = m_proxyContainer.find(instanceID);
	if (it != m_proxyContainer.end())
	{
		return it->second;
	}
	return nullptr;
}

void EffectProxyController::CommandBehavior(EffectRenderProxy* proxy)
{
	while (!proxy->Empty())
	{
		EffectCommandType commandType;
		EffectManagerProxy command;
		if (proxy->TryPop(commandType))
		{
			switch (commandType)
			{
			case EffectCommandType::Play:
			{
				// PlayEffect 후 반환된 인스턴스 ID를 프록시에 저장
				std::string instanceId = EffectManagers->PlayEffect(proxy->GetTempleteName());
				if (!instanceId.empty())
				{
					proxy->UpdateInstanceName(instanceId);

					// 현재 설정들을 새로운 인스턴스에 적용
					if (auto* effect = EffectManagers->GetEffectInstance(instanceId))
					{
						effect->SetPosition(proxy->GetPosition());
						effect->SetTimeScale(proxy->GetTimeScale());
						effect->SetLoop(proxy->GetLoop());
						effect->SetDuration(proxy->GetDuration());
					}
				}
				break;
			}
			case EffectCommandType::Stop:
				if (!proxy->GetInstanceName().empty())
				{
					command = EffectManagerProxy::CreateStopCommand(proxy->GetInstanceName());
					PushEffectCommand(std::move(command));
				}
				break;
			case EffectCommandType::Pause:
				if (!proxy->GetInstanceName().empty())
				{
					command = EffectManagerProxy::CreateStopCommand(proxy->GetInstanceName());
					PushEffectCommand(std::move(command));
				}
				break;
			case EffectCommandType::Resume:
				// Resume의 경우 새로운 인스턴스를 생성해야 함
				if (!proxy->GetTempleteName().empty())
				{
					std::string instanceId = EffectManagers->PlayEffect(proxy->GetTempleteName());
					if (!instanceId.empty())
					{
						proxy->UpdateInstanceName(instanceId);

						if (auto* effect = EffectManagers->GetEffectInstance(instanceId))
						{
							effect->SetPosition(proxy->GetPosition());
							effect->SetTimeScale(proxy->GetTimeScale());
							effect->SetLoop(proxy->GetLoop());
							effect->SetDuration(proxy->GetDuration());
						}
					}
				}
				break;
			case EffectCommandType::SetPosition:
				if (!proxy->GetInstanceName().empty())
				{
					command = EffectManagerProxy::CreateSetPositionCommand(proxy->GetInstanceName(), proxy->GetPosition());
					PushEffectCommand(std::move(command));
				}
				break;
			case EffectCommandType::SetTimeScale:
				if (!proxy->GetInstanceName().empty())
				{
					command = EffectManagerProxy::CreateSetTimeScaleCommand(proxy->GetInstanceName(), proxy->GetTimeScale());
					PushEffectCommand(std::move(command));
				}
				break;

			case EffectCommandType::RemoveEffect:
			{
				// 삭제 대상 인스턴스 이름 사용
				std::string targetInstance = proxy->GetPendingRemoveInstance();
				if (targetInstance.empty()) {
					targetInstance = proxy->GetInstanceName();  // fallback
				}

				if (!targetInstance.empty()) {
					command = EffectManagerProxy::CreateRemoveEffectCommand(targetInstance);
					PushEffectCommand(std::move(command));
					proxy->ClearPendingRemoveInstance();  // 사용 후 클리어

					// 현재 인스턴스와 같다면 ID도 클리어
					if (targetInstance == proxy->GetInstanceName()) {
						proxy->UpdateInstanceName("");
					}
				}
				break;
			}
			case EffectCommandType::SetRotation:
				if (!proxy->GetInstanceName().empty())
				{
					command = EffectManagerProxy::CreateSetRotationCommand(proxy->GetInstanceName(), proxy->GetRotation());
					PushEffectCommand(std::move(command));
				}
				break;
			case EffectCommandType::SetLoop:
				if (!proxy->GetInstanceName().empty())
				{
					command = EffectManagerProxy::CreateSetLoopCommand(proxy->GetInstanceName(), proxy->GetLoop());
					PushEffectCommand(std::move(command));
				}
				break;
			case EffectCommandType::SetDuration:
				if (!proxy->GetInstanceName().empty())
				{
					command = EffectManagerProxy::CreateSetDurationCommand(proxy->GetInstanceName(), proxy->GetDuration());
					PushEffectCommand(std::move(command));
				}
				break;
			case EffectCommandType::ReplaceEffect:
			{
				std::string newInstanceId = EffectManagers->ReplaceEffect(
					proxy->GetInstanceName(),
					proxy->GetTempleteName()
				);

				if (!newInstanceId.empty()) {
					// 인스턴스 ID는 그대로 유지됨
					if (auto* effect = EffectManagers->GetEffectInstance(newInstanceId)) {
						effect->SetPosition(proxy->GetPosition());
						effect->SetRotation(proxy->GetRotation());
						effect->SetTimeScale(proxy->GetTimeScale());
						effect->SetLoop(proxy->GetLoop());
						effect->SetDuration(proxy->GetDuration());
					}
				}
				break;
			}
			default:
				break;
			}
		}
	}
}