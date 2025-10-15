#pragma once
#ifndef DYNAMICCPP_EXPORTS
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
		/*auto& currFrameQueue = PrepareFrameProxy();
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
		}*/

		const auto frameIndex = m_frame.load(std::memory_order_relaxed);
		const std::array<size_t, 3> queueOrder{
				(frameIndex + 1) % m_proxyFrameCommands.size(),
				frameIndex % m_proxyFrameCommands.size(),
				(frameIndex + 2) % m_proxyFrameCommands.size()
		};

		for (const size_t queueIndex : queueOrder)
		{
			DrainQueue(m_proxyFrameCommands[queueIndex]);
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
	//ImplProxyCommandQueue& PrepareFrameProxy()
	//{
	//	size_t prevFrame = (m_frame.load(std::memory_order_relaxed) + 1) % 3;
	//	return m_proxyFrameCommands[prevFrame];
	//}

	void DrainQueue(ImplProxyCommandQueue& queue)
	{
		ProxyCommand command;
		while (queue.try_pop(command))
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

private:
	ProxyCommandQueueArray m_proxyFrameCommands;
	std::atomic_ullong m_frame{};
};

static auto& ProxyCommandQueue = ProxyCommandQueueController::GetInstance();
#endif // !DYNAMICCPP_EXPORTS