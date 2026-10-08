#include "MemoryProfilerSnapshot.h"

#include "ClrHost.h"
#include "DataSystem.h"
#include "ProfileService.h"
#include "Texture.h"

#include <algorithm>
#include <chrono>
#include <crtdbg.h>
#include <mutex>
#include <limits>
#include <unordered_set>
#include <utility>

#include <Windows.h>
#include <Psapi.h>
#include <bcrypt.h>

#pragma comment(lib, "bcrypt.lib")

namespace editor::memory_profiler
{
	namespace
	{
		constexpr std::uint64_t kMegabyte = 1024ull * 1024ull;

		void capture_object_identities(snapshot& out)
		{
			BCRYPT_ALG_HANDLE algorithm{};
			if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
			{
				return; // Default invalid identities stay distinct in the viewer.
			}
			for (auto& object : out.objects)
			{
				if (object.name.size() > (std::numeric_limits<ULONG>::max)())
				{
					continue;
				}
				// Hash the complete original UTF-8 name on the capture owner, before
				// any transport label clipping. Object kind is a separate key field.
				object.identity_valid = BCryptHash(algorithm, nullptr, 0,
					reinterpret_cast<PUCHAR>(object.name.data()),
					static_cast<ULONG>(object.name.size()), object.full_name_sha256.data(),
					static_cast<ULONG>(object.full_name_sha256.size())) >= 0;
				if (!object.identity_valid)
				{
					object.full_name_sha256.fill(0);
				}
			}
			BCryptCloseAlgorithmProvider(algorithm, 0);
		}

		void capture_assets(snapshot& out)
		{
			DataSystem* data = DataSystem::GetInstance();
			if (!data) return;
			for (const auto& generation : data->SnapshotCurrentModelAssetGenerations())
			{
				if (!generation) continue;
				object_entry object;
				object.kind = object_kind::model;
				object.name = generation->Name();
				for (const auto& descriptor : generation->GpuDescriptors())
					object.upload_payload_bytes += descriptor.byteSize;
				out.model_upload_payload_bytes += object.upload_payload_bytes;
				out.objects.push_back(std::move(object));
			}

            for (const auto& [name, material] : data->SnapshotMaterials())
            {
                if (material)
                {
                    out.objects.push_back({ object_kind::material, name });
                }
            }

            using texture_pair = std::pair<std::string, own::shared_owner<const Texture>>;
            std::vector<std::pair<object_kind, texture_pair>> textures;
            const auto copy = [&](DataSystem::TextureFileType type, object_kind kind)
            {
                for (auto& [name, texture] : data->SnapshotTextures(type))
                {
                    textures.emplace_back(kind, texture_pair{ std::move(name), std::move(texture) });
                }
            };
            copy(DataSystem::TextureFileType::Texture, object_kind::texture);
            copy(DataSystem::TextureFileType::UITexture, object_kind::ui_texture);
            copy(DataSystem::TextureFileType::SpriteSheet, object_kind::sprite_sheet);
			std::sort(textures.begin(), textures.end(), [](const auto& left, const auto& right)
			{
				return left.second.first != right.second.first
					? left.second.first < right.second.first
					: left.first < right.first;
			});
			std::unordered_set<const std::byte*> seenPixels;
			for (const auto& [kind, entry] : textures)
			{
				if (!entry.second) continue;
				object_entry object;
				object.kind = kind;
				object.name = entry.first;
				const TextureImageView image = entry.second->GetImageView();
				object.cpu_size_known = !image.IsEmpty();
				if (object.cpu_size_known)
				{
					for (std::uint32_t mipIndex = 0; mipIndex < image.SubresourceCount(); ++mipIndex)
					{
						const TextureSubimage* mip = image.At(mipIndex);
						if (!mip || !mip->pixels) continue;
						if (seenPixels.insert(mip->pixels).second)
							object.cpu_pixel_bytes += mip->slicePitch;
						else object.shared_alias = true;
					}
					out.texture_cpu_pixel_bytes += object.cpu_pixel_bytes;
				}
				out.objects.push_back(std::move(object));
			}
		}

		void capture_regions(snapshot& out)
		{
			SYSTEM_INFO system{};
			::GetNativeSystemInfo(&system);
			const auto limit = reinterpret_cast<std::uintptr_t>(system.lpMaximumApplicationAddress);
			std::uintptr_t address = reinterpret_cast<std::uintptr_t>(system.lpMinimumApplicationAddress);
			while (address <= limit)
			{
				MEMORY_BASIC_INFORMATION info{};
				if (::VirtualQuery(reinterpret_cast<const void*>(address), &info, sizeof(info)) == 0)
				{
					if (limit - address < system.dwPageSize) break;
					address += system.dwPageSize;
					continue;
				}
				const auto base = reinterpret_cast<std::uintptr_t>(info.BaseAddress);
				const std::uint64_t bytes = static_cast<std::uint64_t>(info.RegionSize);
				if (info.State == MEM_COMMIT)
				{
					if (info.Type == MEM_IMAGE) out.committed_image_bytes += bytes;
					else if (info.Type == MEM_MAPPED) out.committed_mapped_bytes += bytes;
					else out.committed_private_bytes += bytes;
				}
				else if (info.State == MEM_RESERVE) out.reserved_virtual_bytes += bytes;
				if (info.State != MEM_FREE)
				{
					const auto type = info.Type == MEM_IMAGE ? virtual_region::kind::image :
						info.Type == MEM_MAPPED ? virtual_region::kind::mapped :
						info.Type == MEM_PRIVATE ? virtual_region::kind::private_memory :
						virtual_region::kind::unknown;
					out.regions.push_back({ base, bytes, info.State == MEM_COMMIT, type, info.Protect });
				}
				if (bytes == 0 || base > limit || bytes > limit - base) break;
				const auto next = base + static_cast<std::uintptr_t>(bytes);
				if (next <= address) break;
				address = next;
			}
		}

		std::shared_ptr<const snapshot> capture(std::uint64_t serial, std::uint32_t frame)
		{
			const auto started = std::chrono::steady_clock::now();
			auto result = std::make_shared<snapshot>();
			result->serial = serial;
			result->frame = frame;
			PROCESS_MEMORY_COUNTERS_EX process{};
			if (::K32GetProcessMemoryInfo(::GetCurrentProcess(),
				reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&process), sizeof(process)))
			{
				result->process_valid = true;
				result->working_set_bytes = process.WorkingSetSize;
				result->private_commit_bytes = process.PrivateUsage;
			}
#if defined(_DEBUG)
			_CrtMemState crt{};
			_CrtMemCheckpoint(&crt);
			result->crt_heap_valid = true;
			for (int i = 0; i < _MAX_BLOCKS; ++i)
			{
				result->crt_live_bytes += crt.lSizes[i];
				result->crt_live_blocks += crt.lCounts[i];
			}
#endif
			ClrHost::ScriptGcStats gc{};
			if (ClrHost::Get().GetManagedGcStats(gc))
			{
				result->managed_valid = true;
				result->managed_heap_bytes = static_cast<std::uint64_t>((std::max)(gc.heapSizeBytes, std::int64_t{}));
				result->managed_fragmented_bytes = static_cast<std::uint64_t>((std::max)(gc.fragmentedBytes, std::int64_t{}));
				result->managed_total_allocated_bytes = static_cast<std::uint64_t>((std::max)(gc.totalAllocatedBytes, std::int64_t{}));
			}
			const auto [usedMB, budgetMB] = ce::profiler().latest_video_memory();
			result->vram_valid = budgetMB != 0;
			result->vram_used_bytes = usedMB * kMegabyte;
			result->vram_budget_bytes = budgetMB * kMegabyte;
			capture_assets(*result);
			capture_object_identities(*result);
			capture_regions(*result);
			result->object_count = result->objects.size();
			result->region_count = result->regions.size();
			for (const auto& object : result->objects)
			{
				const auto kind = static_cast<std::size_t>(object.kind);
				if (kind < result->object_kind_counts.size())
				{
					++result->object_kind_counts[kind];
					result->object_kind_cpu_bytes[kind] += object.cpu_pixel_bytes;
				}
			}
			result->capture_ms = std::chrono::duration<double, std::milli>(
				std::chrono::steady_clock::now() - started).count();
			return result;
		}
	}

	snapshot_service& snapshot_service::instance()
	{
		static snapshot_service value;
		return value;
	}

	void snapshot_service::request() noexcept
	{
		if (pending()) return;
		m_requested.fetch_add(1, std::memory_order_acq_rel);
	}

	bool snapshot_service::pending() const noexcept
	{
		return m_requested.load(std::memory_order_acquire) !=
			m_published.load(std::memory_order_acquire);
	}

	std::shared_ptr<const snapshot> snapshot_service::latest() const noexcept
	{
		return m_latest.load(std::memory_order_acquire);
	}

	void snapshot_service::poll_game_thread(std::uint32_t frame)
	{
		const std::uint64_t requested = m_requested.load(std::memory_order_acquire);
		if (requested == m_published.load(std::memory_order_relaxed)) return;
		m_latest.store(capture(requested, frame), std::memory_order_release);
		m_published.store(requested, std::memory_order_release);
	}
}
