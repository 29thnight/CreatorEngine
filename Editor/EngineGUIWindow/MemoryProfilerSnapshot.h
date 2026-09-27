#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// On-demand memory snapshots. The presentation thread only requests and reads;
// DataSystem and CoreCLR are sampled by the game thread at a frame boundary.
namespace editor::memory_profiler
{
	enum class object_kind : std::uint8_t
	{
		model, material, texture, ui_texture, sprite_sheet
	};

	struct object_entry
	{
		object_kind kind{};
		std::string name;
		std::uint64_t cpu_pixel_bytes{}; // exact retained CPU image payload, if known
		std::uint64_t upload_payload_bytes{}; // model request bytes; not GPU residency
		bool cpu_size_known{};
		bool shared_alias{};
	};

	struct virtual_region
	{
		std::uint64_t address{};
		std::uint64_t bytes{};
		bool committed{};
		enum class kind : std::uint8_t { private_memory, image, mapped, unknown } type{};
		std::uint32_t protection{};
	};

	struct snapshot
	{
		std::uint64_t serial{};
		std::uint32_t frame{};
		double capture_ms{};
		bool process_valid{};
		std::uint64_t working_set_bytes{};
		std::uint64_t private_commit_bytes{};
		bool crt_heap_valid{};
		std::uint64_t crt_live_bytes{};
		std::uint64_t crt_live_blocks{};
		bool managed_valid{};
		std::uint64_t managed_heap_bytes{};
		std::uint64_t managed_fragmented_bytes{};
		std::uint64_t managed_total_allocated_bytes{};
		bool vram_valid{};
		std::uint64_t vram_used_bytes{};
		std::uint64_t vram_budget_bytes{};
		std::uint64_t texture_cpu_pixel_bytes{};
		std::uint64_t model_upload_payload_bytes{};
		std::uint64_t committed_private_bytes{};
		std::uint64_t committed_image_bytes{};
		std::uint64_t committed_mapped_bytes{};
		std::uint64_t reserved_virtual_bytes{};
		std::vector<object_entry> objects;
		std::vector<virtual_region> regions;
	};

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
