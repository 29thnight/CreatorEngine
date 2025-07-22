#pragma once
#include "Core.Minimal.h"
#include "EffectManagerProxy.h"
#include "concurrent_queue.h"

class EffectRenderProxy;
class EffectComponent;
class EffectProxyController: public DLLCore::Singleton<EffectProxyController>
{
private:
	friend class Singleton;
	using ImplEffectCommandQueue = concurrent_queue<EffectManagerProxy>;
	using EffectCommandQueueArray = std::array<ImplEffectCommandQueue, 3>;
	using EffectRenderProxyContainer = std::unordered_map<HashedGuid, EffectRenderProxy*>;

private:
	EffectProxyController() = default;
	~EffectProxyController() = default;

public:
	void PrepareCommandBehavior();
	void ExecuteEffectCommands();

	EffectRenderProxy* RegisterProxy(EffectComponent* component);
	void UnRegisterProxy(EffectComponent* component);
	EffectRenderProxy* GetProxy(EffectComponent* component);


	// ���� ���� �����忡�� ȣ�� - ����Ʈ ��� �߰�
	void PushEffectCommand(EffectManagerProxy&& effectCommand)
	{
		size_t currFrame = m_frame.load(std::memory_order_relaxed) % 3;
		m_effectFrameCommands[currFrame].push(std::move(effectCommand));
	}

	// ������ ī���� ����
	void AddFrame()
	{
		m_frame.fetch_add(1, std::memory_order_relaxed);
	}

	// ��� ���� ��� ���� Ȯ��
	size_t GetPendingCommandCount() const
	{
		size_t prevFrame = (m_frame.load(std::memory_order_relaxed) + 1) % 3;
		return m_effectFrameCommands[prevFrame].unsafe_size();
	}
private:
	ImplEffectCommandQueue& PrepareFrameEffectCommands()
	{
		size_t prevFrame = (m_frame.load(std::memory_order_relaxed) + 1) % 3;
		return m_effectFrameCommands[prevFrame];
	}

	void CommandBehavior(EffectRenderProxy* proxy);

private:
	EffectCommandQueueArray m_effectFrameCommands;
	EffectRenderProxyContainer m_proxyContainer;
	std::atomic_ullong m_frame{};
};

static auto EffectCommandQueue = EffectProxyController::GetInstance();