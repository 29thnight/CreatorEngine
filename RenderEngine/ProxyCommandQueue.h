#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include "Core.Minimal.h"
#include "ProxyCommand.h"
#include "concurrent_queue.h"
#include <deque>
#include <vector>

using namespace concurrency;

class RenderScene;

class ProxyCommandQueueController : public Singleton<ProxyCommandQueueController>
{
private:
	friend class Singleton;
	using ImplProxyCommandQueue = concurrent_queue<ProxyCommand>;

private:
	ProxyCommandQueueController() = default;
	~ProxyCommandQueueController() = default;

public:
	struct Stats
	{
		uint64_t enqueued{ 0 };
		uint64_t applied{ 0 };
		uint64_t dropped{ 0 };
		uint64_t pending{ 0 };
		uint64_t staleEpoch{ 0 };
		uint64_t missingTarget{ 0 };
		uint64_t failed{ 0 };
		uint64_t superseded{ 0 };
		uint64_t shutdownDiscarded{ 0 };
	};
	using Batch = std::vector<ProxyCommand>;

	void Execute(RenderScene& renderScene, uint64_t sceneEpoch)
	{
		ExecuteBatch(renderScene, sceneEpoch, CapturePending());
	}

	// 게임 스레드의 프레임 밀봉 지점에서 staging queue를 값 배치로 떼어 낸다.
	// 이 뒤부터 delta는 frame packet과 같은 bounded RenderThread queue 수명을
	// 가진다. push와 try_pop은 concurrent_queue의 다중 생산자 계약을 따른다.
	Batch CapturePending()
	{
		Batch batch;
		ProxyCommand command;
		while (m_proxyCommands.try_pop(command))
			batch.push_back(std::move(command));
		return batch;
	}

	// 렌더 소비 스레드 전용. future-epoch 보류분을 먼저 원래 순서로 적용하고,
	// 이어서 해당 frame packet과 함께 넘어온 delta를 적용한다.
	void ExecuteBatch(RenderScene& renderScene, uint64_t sceneEpoch, Batch batch)
	{
		const size_t deferredCount = m_deferredCommands.size();
		for (size_t i = 0; i < deferredCount; ++i)
		{
			ProxyCommand command = std::move(m_deferredCommands.front());
			m_deferredCommands.pop_front();
			Consume(std::move(command), renderScene, sceneEpoch);
		}

		for (ProxyCommand& command : batch)
			Consume(std::move(command), renderScene, sceneEpoch);
	}

	void DeferBatch(Batch batch)
	{
		for (ProxyCommand& command : batch)
			m_deferredCommands.push_back(std::move(command));
	}

	void MarkSuperseded(uint64_t count) noexcept
	{
		if (0 == count) return;
		m_superseded.fetch_add(count, std::memory_order_seq_cst);
		m_dropped.fetch_add(count, std::memory_order_seq_cst);
	}

	void MarkShutdownDiscarded(uint64_t count) noexcept
	{
		if (0 == count) return;
		m_shutdownDiscarded.fetch_add(count, std::memory_order_seq_cst);
		m_dropped.fetch_add(count, std::memory_order_seq_cst);
	}

	// RenderThread가 완전히 멈춘 뒤 Scene teardown이 만든 unregister delta는
	// 적용할 저장소가 이미 Finalize된 상태다. 별도 원인으로 계수하며 staging과
	// consumer 보류분을 모두 비워 enqueued = processed + pending을 유지한다.
	uint64_t DiscardPendingForShutdown()
	{
		uint64_t discarded = 0;
		ProxyCommand command;
		while (m_proxyCommands.try_pop(command)) ++discarded;
		discarded += static_cast<uint64_t>(m_deferredCommands.size());
		m_deferredCommands.clear();
		MarkShutdownDiscarded(discarded);
		return discarded;
	}

	void PushProxyCommand(ProxyCommand&& proxyCommand)
	{
		m_enqueued.fetch_add(1, std::memory_order_seq_cst);
		try
		{
			// enqueued를 먼저 발행해야 소비자가 push 직후 pop해도
			// processed가 enqueued보다 앞설 수 없다.
			m_proxyCommands.push(std::move(proxyCommand));
		}
		catch (...)
		{
			m_enqueued.fetch_sub(1, std::memory_order_seq_cst);
			throw;
		}
	}

	Stats GetStats() const noexcept
	{
		// 처리 계수를 먼저 읽고 enqueued를 마지막에 읽는다. enqueue는 큐
		// publish보다 먼저 증가하므로 processed <= enqueued가 유지된다.
		const uint64_t applied = m_applied.load(std::memory_order_seq_cst);
		const uint64_t dropped = m_dropped.load(std::memory_order_seq_cst);
		const uint64_t enqueued = m_enqueued.load(std::memory_order_seq_cst);
		const uint64_t processed = applied + dropped;
		return { enqueued, applied, dropped,
			enqueued >= processed ? enqueued - processed : 0,
			m_staleEpoch.load(std::memory_order_seq_cst),
			m_missingTarget.load(std::memory_order_seq_cst),
			m_failed.load(std::memory_order_seq_cst),
			m_superseded.load(std::memory_order_seq_cst),
			m_shutdownDiscarded.load(std::memory_order_seq_cst) };
	}

private:
	void Consume(ProxyCommand command, RenderScene& renderScene,
		uint64_t sceneEpoch)
	{
		// 아직 소비할 frame packet보다 새 씬의 명령이면 버리지 않고 보류한다.
		// 3-2E에서 packet과 delta를 한 bounded queue로 합치기 전까지 필요한
		// 전환 계약이다.
		if (command.GetSceneEpoch() > sceneEpoch)
		{
			m_deferredCommands.push_back(std::move(command));
			return;
		}

		try
		{
			switch (command.Apply(renderScene, sceneEpoch))
			{
			case ProxyCommand::ApplyResult::Applied:
				m_applied.fetch_add(1, std::memory_order_seq_cst);
				break;
			case ProxyCommand::ApplyResult::StaleEpoch:
				m_staleEpoch.fetch_add(1, std::memory_order_seq_cst);
				m_dropped.fetch_add(1, std::memory_order_seq_cst);
				break;
			case ProxyCommand::ApplyResult::MissingTarget:
				m_missingTarget.fetch_add(1, std::memory_order_seq_cst);
				m_dropped.fetch_add(1, std::memory_order_seq_cst);
				break;
			}
		}
		catch (const std::exception& e)
		{
			m_failed.fetch_add(1, std::memory_order_seq_cst);
			m_dropped.fetch_add(1, std::memory_order_seq_cst);
			Debug->LogWarning(e.what());
		}
	}

private:
	ImplProxyCommandQueue m_proxyCommands;
	// 렌더 소비 스레드만 접근한다.
	std::deque<ProxyCommand> m_deferredCommands;
	std::atomic_ullong m_enqueued{};
	std::atomic_ullong m_applied{};
	std::atomic_ullong m_dropped{};
	std::atomic_ullong m_staleEpoch{};
	std::atomic_ullong m_missingTarget{};
	std::atomic_ullong m_failed{};
	std::atomic_ullong m_superseded{};
	std::atomic_ullong m_shutdownDiscarded{};
};

static auto ProxyCommandQueue = ProxyCommandQueueController::GetInstance();
#endif // !DYNAMICCPP_EXPORTS
