#pragma once
#include "Core.Minimal.h"
#include "EffectCommandType.h"
#include "ParticleSystem.h"
#include "concurrent_queue.h"

using EffectCommandTypeQueue = concurrent_queue<EffectCommandType>;

class EffectComponent;
class EffectRenderProxy
{
public:
	EffectRenderProxy() = default;
	EffectRenderProxy(EffectComponent* component);

	void PushCommand(const EffectCommandType& type) { m_commandTypeQueue.push(type); }
	void UpdateTempleteName(const std::string_view& templeteName) { m_templateName = templeteName;  }
	void UpdateInstanceName(const std::string_view& instnaceedName) { m_instanceName = instnaceedName; }
	void UpdatePosition(const Mathf::Vector3& pos) { m_commandPosition = pos; }
	void UpdateRotation(const Mathf::Vector3& rot) { m_commandRotation = rot;  }

private:
	EffectCommandTypeQueue	m_commandTypeQueue;
	std::string				m_templateName;
	std::string				m_instanceName;
	Mathf::Vector3			m_commandPosition{ 0.f, 0.f, 0.f };
	Mathf::Vector3			m_commandRotation{ 0.f, 0.f, 0.f };
};