#pragma once
#include "Core.Minimal.h"
#include "EffectCommandType.h"
#include "ParticleSystem.h"
#include "ManagedHeapObject.h"
#include "concurrent_queue.h"

using EffectCommandTypeQueue = concurrent_queue<EffectCommandType>;

class EffectComponent;
class EffectRenderProxy : public ManagedHeapObject
{
public:
	EffectRenderProxy() = default;
	~EffectRenderProxy() = default;

	void PushCommand(const EffectCommandType& type) { m_commandTypeQueue.push(type); }
	void UpdateTempleteName(const std::string_view& templeteName) { m_templateName = templeteName;  }
	void UpdateInstanceName(const std::string_view& instnaceedName) { m_instanceName = instnaceedName; }
	void UpdatePosition(const Mathf::Vector3& pos) { m_commandPosition = pos; }
	void UpdateRotation(const Mathf::Vector3& rot) { m_commandRotation = rot;  }
	void UpdateTimeScale(const float& timeScale) { m_timeScale = timeScale; }

	bool TryPop(EffectCommandType& type)
	{ 
		return m_commandTypeQueue.try_pop(type);
	}

	bool Empty()
	{
		return m_commandTypeQueue.empty();
	}

	const std::string& GetName() const
	{
		return m_instanceName.empty() ? m_templateName : m_instanceName;
	}

	const std::string& GetTempleteName() const { return m_templateName; }
	const std::string& GetInstanceName() const { return m_instanceName; }
	const Mathf::Vector3& GetPostion() const { return m_commandPosition; }
	const Mathf::Vector3& GetRotation() const { return m_commandRotation; }
	const float& GetTimeScale() const { return m_timeScale; }

private:
	EffectCommandTypeQueue	m_commandTypeQueue;
	std::string				m_templateName;
	std::string				m_instanceName;
	Mathf::Vector3			m_commandPosition{ 0.f, 0.f, 0.f };
	Mathf::Vector3			m_commandRotation{ 0.f, 0.f, 0.f };
	float                   m_timeScale{ 1.f };
};