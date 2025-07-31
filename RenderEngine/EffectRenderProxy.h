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
	void UpdateLoop(bool isLoop) { m_loop = isLoop; }
	void UpdateDuration(const float& duration) { m_duration = duration; }

	void SetPendingRemoveInstance(const std::string& instanceName) { m_pendingRemoveInstance = instanceName; }
	const std::string& GetPendingRemoveInstance() const { return m_pendingRemoveInstance; }
	void ClearPendingRemoveInstance() { m_pendingRemoveInstance.clear(); }
	void SetInstanceName(const std::string& instanceName) { m_instanceName = instanceName; }

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
	const Mathf::Vector3& GetPosition() const { return m_commandPosition; }
	const Mathf::Vector3& GetRotation() const { return m_commandRotation; }
	const float& GetTimeScale() const { return m_timeScale; }
	bool& GetLoop() { return m_loop; }
	const float& GetDuration() const { return m_duration; }

private:
	EffectCommandTypeQueue	m_commandTypeQueue;
	std::string				m_templateName;
	std::string				m_instanceName;
	Mathf::Vector3			m_commandPosition{ 0.f, 0.f, 0.f };
	Mathf::Vector3			m_commandRotation{ 0.f, 0.f, 0.f };
	float                   m_timeScale{ 1.f };
	bool					m_loop{ false };
	float                   m_duration{ 0.f };
	std::string m_pendingRemoveInstance;
};