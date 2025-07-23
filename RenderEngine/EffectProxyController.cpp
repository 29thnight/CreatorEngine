#include "EffectProxyController.h"
#include "EffectRenderProxy.h"
#include "EffectComponent.h"

void EffectProxyController::PrepareCommandBehavior()
{
	for (auto& [instanceID, Proxy] : m_proxyContainer)
	{
		CommandBehavior(Proxy);
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
		if(proxy->TryPop(commandType))
		{
			switch (commandType)
			{
			case EffectCommandType::Play:
				command = EffectManagerProxy::CreatePlayCommand(proxy->GetName());
				break;
			case EffectCommandType::Stop:
				command = EffectManagerProxy::CreateStopCommand(proxy->GetName());
				break;
			case EffectCommandType::Pause:
				command = EffectManagerProxy::CreateStopCommand(proxy->GetName());
				break;
			case EffectCommandType::Resume:
				command = EffectManagerProxy::CreatePlayCommand(proxy->GetName());
				break;
			case EffectCommandType::SetPosition:
				command = EffectManagerProxy::CreateSetPositionCommand(proxy->GetName(), proxy->GetPostion());
				break;
			case EffectCommandType::SetTimeScale:
				command = EffectManagerProxy::CreateSetTimeScaleCommand(proxy->GetName(), proxy->GetTimeScale());
				break;
			case EffectCommandType::CreateEffect:
				break;
			case EffectCommandType::RemoveEffect:
				command = EffectManagerProxy::CreateRemoveEffectCommand(proxy->GetName());
				break;
			case EffectCommandType::UpdateEffectProperty:
				break;
			case EffectCommandType::CreateInstance:
				command = EffectManagerProxy::CreateEffectInstanceCommand(proxy->GetTempleteName(), proxy->GetInstanceName());
				break;
			case EffectCommandType::SetRotation:
				command = EffectManagerProxy::CreateSetRotationCommand(proxy->GetName(), proxy->GetRotation());
				break;
			case EffectCommandType::SetLoop:
				command = EffectManagerProxy::CreateSetLoopCommand(proxy->GetName(), proxy->GetLoop());
				break;
			case EffectCommandType::SetDuration:
				command = EffectManagerProxy::CreateSetLoopCommand(proxy->GetName(), proxy->GetDuration());
				break;
			default:
				break;
			}

			PushEffectCommand(std::move(command));
		}
	}
}