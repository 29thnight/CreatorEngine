#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "ProfilerLiveDiagnostics.h"

// On-demand memory snapshots. The presentation thread only requests and reads;
// DataSystem and CoreCLR are sampled by the game thread at a frame boundary.
namespace editor::memory_profiler
{
    using object_kind = ce::profiler_viewer::diagnostics::object_kind;
    using object_entry = ce::profiler_viewer::diagnostics::object_entry;
    using virtual_region = ce::profiler_viewer::diagnostics::virtual_region;
    using snapshot = ce::profiler_viewer::diagnostics::memory_snapshot;

	class snapshot_service final
	{
	public:
		static snapshot_service& instance();
		void request() noexcept;
		[[nodiscard]] bool pending() const noexcept;
		[[nodiscard]] std::shared_ptr<const snapshot> latest() const noexcept;
		void poll_game_thread(std::uint32_t frame);

	private:
		snapshot_service() = default;
		std::atomic<std::uint64_t> m_requested{};
		std::atomic<std::uint64_t> m_published{};
		std::atomic<std::shared_ptr<const snapshot>> m_latest{};
	};
}
