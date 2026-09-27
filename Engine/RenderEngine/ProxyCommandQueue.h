#pragma once
#include "Core.Minimal.h"
#include "AnimationPaletteArena.h"
#include "ProxyCommand.h"
#include "concurrent_queue.h"
#include <deque>
#include <limits>
#include <memory>
#include <mutex>
#include <span>
#include <stdexcept>
#include <utility>
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
	class Batch final
	{
	public:
		using iterator = std::vector<ProxyCommand>::iterator;
		[[nodiscard]] iterator begin() noexcept { return m_commands.begin(); }
		[[nodiscard]] iterator end() noexcept { return m_commands.end(); }
		[[nodiscard]] size_t size() const noexcept { return m_commands.size(); }
		void reserve(size_t count) { m_commands.reserve(count); }
		void push_back(ProxyCommand&& command) { m_commands.push_back(std::move(command)); }
		[[nodiscard]] ProxyCommand& operator[](size_t index) noexcept { return m_commands[index]; }
		void clear() noexcept
		{
			m_commands.clear();
			m_primaryArena.reset();
			m_extraArenas.clear();
			m_paletteCount = 0;
		}

		// A producer can finish enqueueing after an arena rotates. Associate the
		// transient owner with the drained command, then keep it once per batch.
		void finalize_captures()
		{
			for (auto& command : m_commands) adopt_captured(command);
		}

	private:
		void adopt_captured(ProxyCommand& command)
		{
			const auto [offset, count] = command.GetPaletteSlice();
			auto arena = command.TakeCaptureArena();
			if (count)
			{
				if (!arena) throw std::logic_error("palette capture has no arena");
				std::uint32_t base = 0;
				if (!m_primaryArena)
				{
					m_primaryArena = arena;
					m_paletteCount = checked_size(*arena);
				}
				else if (m_primaryArena != arena)
				{
					bool found = false;
					for (const auto& segment : m_extraArenas)
						if (segment.arena == arena) { base = segment.base; found = true; break; }
					if (!found)
					{
						base = m_paletteCount;
						const auto length = checked_size(*arena);
						checked_add(base, length);
						m_extraArenas.push_back({ base, std::move(arena) });
						m_paletteCount += length;
					}
				}
				if (offset > (std::numeric_limits<std::uint32_t>::max)() - base)
					throw std::length_error("palette command offset overflow");
				command.RebasePaletteOffset(base);
			}
		}
	public:

		// Compaction moves commands but does not change their rebased offsets.
		void take_palettes_from(Batch& source) noexcept
		{
			m_primaryArena = std::move(source.m_primaryArena);
			m_extraArenas = std::move(source.m_extraArenas);
			m_paletteCount = std::exchange(source.m_paletteCount, 0);
		}

		void append(Batch&& source)
		{
			if (m_commands.empty()) { *this = std::move(source); return; }
			const auto base = m_paletteCount;
			checked_add(base, source.m_paletteCount);
			m_commands.reserve(m_commands.size() + source.size());
			m_extraArenas.reserve(m_extraArenas.size() + source.m_extraArenas.size()
				+ (source.m_primaryArena ? 1 : 0));
			if (source.m_primaryArena)
				m_extraArenas.push_back({ base, std::move(source.m_primaryArena) });
			for (auto& segment : source.m_extraArenas)
				m_extraArenas.push_back({ static_cast<std::uint32_t>(base + segment.base),
					std::move(segment.arena) });
			for (auto& command : source.m_commands)
			{
				command.RebasePaletteOffset(base);
				m_commands.push_back(std::move(command));
			}
			m_paletteCount += source.m_paletteCount;
		}

		[[nodiscard]] std::pair<std::shared_ptr<const ce::animation_palette_arena>,
			std::uint32_t> resolve(const ProxyCommand& command) const
		{
			const auto [offset, count] = command.GetPaletteSlice();
			if (!count) return {};
			auto check = [offset, count](
				const std::shared_ptr<const ce::animation_palette_arena>& arena,
				std::uint32_t base, std::uint32_t limit)
				-> std::pair<std::shared_ptr<const ce::animation_palette_arena>, std::uint32_t>
			{
				if (!arena || offset < base || offset - base >= limit) return {};
				const auto local = offset - base;
				if (!arena->resolve(local, count))
					throw std::logic_error("invalid sealed palette slice");
				return { arena, local };
			};
			if (auto found = check(m_primaryArena, 0,
				m_extraArenas.empty() ? m_paletteCount : m_extraArenas.front().base);
				found.first) return found;
			for (size_t index = 0; index < m_extraArenas.size(); ++index)
			{
				const auto& segment = m_extraArenas[index];
				const auto end = index + 1 < m_extraArenas.size()
					? m_extraArenas[index + 1].base : m_paletteCount;
				if (auto found = check(segment.arena, segment.base, end - segment.base);
					found.first) return found;
			}
			throw std::logic_error("palette command has no batch owner");
		}

	private:
		struct PaletteSegment final
		{
			std::uint32_t base{};
			std::shared_ptr<const ce::animation_palette_arena> arena{};
		};
		static std::uint32_t checked_size(const ce::animation_palette_arena& arena)
		{
			if (arena.size() > (std::numeric_limits<std::uint32_t>::max)())
				throw std::length_error("palette arena exceeds command address space");
			return static_cast<std::uint32_t>(arena.size());
		}
		static void checked_add(std::uint32_t a, std::uint32_t b)
		{
			if (b > (std::numeric_limits<std::uint32_t>::max)() - a)
				throw std::length_error("merged palette address space overflow");
		}
		std::vector<ProxyCommand> m_commands{};
		std::shared_ptr<const ce::animation_palette_arena> m_primaryArena{};
		std::vector<PaletteSegment> m_extraArenas{};
		std::uint32_t m_paletteCount{};
	};
	struct PaletteCapture
	{
		std::shared_ptr<const ce::animation_palette_arena> arena{};
		std::uint32_t offset{};
		std::uint32_t count{};
		bool copied{};
		bool allocated{};
	};

	PaletteCapture CaptureAnimationPalette(std::uint64_t animatorKey,
		std::span<const math::matrix4x4> palette)
	{
		std::lock_guard lock(m_paletteMutex);
		bool created = false;
		if (!m_paletteArena)
		{
			for (const auto& candidate : m_paletteArenas)
			{
				if (candidate.use_count() != 1) continue;
				candidate->reset();
				m_paletteArena = candidate;
				break;
			}
			if (!m_paletteArena)
			{
				m_paletteArena = std::make_shared<ce::animation_palette_arena>();
				m_paletteArenas.push_back(m_paletteArena);
				created = true;
			}
		}
		const auto slice = m_paletteArena->capture(animatorKey, palette);
		return { m_paletteArena, slice.offset, slice.count, slice.copied,
			created || slice.grew };
	}

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
		// A command captured before the drain can be pushed afterward; its
		// transient owner survives until a later batch adopts it.
		{
			std::lock_guard lock(m_paletteMutex);
			if (m_paletteArena) m_paletteArena->seal();
			m_paletteArena.reset();
		}
		batch.finalize_captures();
		return batch;
	}

	// 렌더 소비 스레드 전용. future-epoch 보류분을 먼저 원래 순서로 적용하고,
	// 이어서 해당 frame packet과 함께 넘어온 delta를 적용한다.
	void ExecuteBatch(RenderScene& renderScene, uint64_t sceneEpoch, Batch batch)
	{
		const size_t deferredCount = m_deferredCommands.size();
		for (size_t i = 0; i < deferredCount; ++i)
		{
			DeferredCommand deferred = std::move(m_deferredCommands.front());
			m_deferredCommands.pop_front();
			Consume(std::move(deferred.command), renderScene, sceneEpoch,
				std::move(deferred.arena), deferred.localOffset);
		}

		for (ProxyCommand& command : batch)
		{
			auto [arena, localOffset] = batch.resolve(command);
			Consume(std::move(command), renderScene, sceneEpoch,
				std::move(arena), localOffset);
		}
	}

	void DeferBatch(Batch batch)
	{
		for (ProxyCommand& command : batch)
		{
			auto [arena, localOffset] = batch.resolve(command);
			m_deferredCommands.push_back({ std::move(command),
				std::move(arena), localOffset });
		}
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
		{
			std::lock_guard lock(m_paletteMutex);
			if (m_paletteArena) m_paletteArena->seal();
			m_paletteArena.reset();
			m_paletteArenas.clear();
		}
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
	struct DeferredCommand final
	{
		ProxyCommand command{};
		std::shared_ptr<const ce::animation_palette_arena> arena{};
		std::uint32_t localOffset{};
	};
	void Consume(ProxyCommand command, RenderScene& renderScene,
		uint64_t sceneEpoch,
		std::shared_ptr<const ce::animation_palette_arena> arena,
		std::uint32_t localOffset)
	{
		// 아직 소비할 frame packet보다 새 씬의 명령이면 버리지 않고 보류한다.
		// 3-2E에서 packet과 delta를 한 bounded queue로 합치기 전까지 필요한
		// 전환 계약이다.
		if (command.GetSceneEpoch() > sceneEpoch)
		{
			m_deferredCommands.push_back({ std::move(command),
				std::move(arena), localOffset });
			return;
		}

		try
		{
			switch (command.Apply(renderScene, sceneEpoch,
				std::move(arena), localOffset))
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
			Debug::PrintLog(spdlog::level::warn, e.what());
		}
	}

private:
	ImplProxyCommandQueue m_proxyCommands;
	std::mutex m_paletteMutex;
	std::shared_ptr<ce::animation_palette_arena> m_paletteArena;
	std::vector<std::shared_ptr<ce::animation_palette_arena>> m_paletteArenas;
	// 렌더 소비 스레드만 접근한다.
	std::deque<DeferredCommand> m_deferredCommands;
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
