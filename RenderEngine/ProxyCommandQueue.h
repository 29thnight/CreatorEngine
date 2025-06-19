#pragma once
#include "Core.Minimal.h"
#include "ProxyCommand.h"
#include "concurrent_queue.h"

using namespace concurrency;

class ProxyCommandQueueController : public Singleton<ProxyCommandQueueController>
{
private:
	friend class Singleton;
	using ImplProxyCommandQueue = concurrent_queue<ProxyCommand>;
	using ProxyCommandQueueArray = std::array<ImplProxyCommandQueue, 3>;

private:
	ProxyCommandQueueController() = default;
	~ProxyCommandQueueController() = default;

public:
	void Execute()
	{
		auto& currFrameQueue = PrepareFrameProxy();
		while (!currFrameQueue.empty())
		{
			ProxyCommand command;
			if (currFrameQueue.try_pop(command))
			{
				try
				{
					command.ProxyCommandExecute();
				}
				catch (const std::exception& e)
				{
					Debug->LogWarning(e.what());
					continue;
				}
			}
		}
	}

	void PushProxyCommand(ProxyCommand&& proxyCommand)
	{
		size_t currFrame = m_frame.load(std::memory_order_relaxed) % 3;
		m_proxyFrameCommands[currFrame].push(std::move(proxyCommand));
	}

	void AddFrame()
	{
		m_frame.fetch_add(1, std::memory_order_relaxed);
	}

private:
	ImplProxyCommandQueue& PrepareFrameProxy()
	{
		size_t prevFrame = (m_frame.load(std::memory_order_relaxed) + 1) % 3;
		return m_proxyFrameCommands[prevFrame];
	}

private:
	ProxyCommandQueueArray m_proxyFrameCommands;
	std::atomic_ullong m_frame{};
};

static auto& ProxyCommandQueue = ProxyCommandQueueController::GetInstance();