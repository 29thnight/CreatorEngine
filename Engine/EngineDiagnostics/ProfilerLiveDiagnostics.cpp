#include "ProfilerLiveDiagnostics.h"
#include "ProfilerViewerProtocol.h"

#include <bit>
#include <cmath>
#include <limits>
#include <type_traits>
#include <utility>

namespace ce::profiler_viewer::diagnostics
{
    namespace
    {
        constexpr std::uint32_t snapshot_magic = 0x53445043; // CPDS
        constexpr std::uint32_t request_magic = 0x52445043; // CPDR
        // Conservative complete wire budget: 4 KiB for all scalar fields and
        // counts, plus every collection/string cap and the full rendering blob.
        static_assert(4096 + maximum_objects * (56 + maximum_string_bytes) +
            maximum_regions * 22 + maximum_animators * (35 + 2 * maximum_string_bytes) +
            maximum_tasks * (31 + maximum_string_bytes) + maximum_rendering_bytes <=
            maximum_diagnostic_bytes);

        struct writer
        {
            std::vector<std::byte> bytes;

            template<class T> bool scalar(const T& value)
            {
                if constexpr (std::is_enum_v<T>)
                {
                    return scalar(static_cast<std::underlying_type_t<T>>(value));
                }
                else if constexpr (std::is_same_v<T, bool>)
                {
                    return scalar(static_cast<std::uint8_t>(value));
                }
                else if constexpr (std::is_floating_point_v<T>)
                {
                    return std::isfinite(value) && scalar(std::bit_cast<std::uint64_t>(value));
                }
                else
                {
                    if (bytes.size() + sizeof(T) > maximum_diagnostic_bytes)
                    {
                        return false;
                    }
                    const auto bits = static_cast<std::make_unsigned_t<T>>(value);
                    for (std::size_t index = 0; index < sizeof(T); ++index)
                    {
                        bytes.push_back(static_cast<std::byte>((bits >> (index * 8)) & 0xff));
                    }
                    return true;
                }
            }

            bool scalar(const std::string& value)
            {
                if (value.size() > maximum_string_bytes || value.find('\0') != std::string::npos)
                {
                    return false;
                }
                const auto raw = std::as_bytes(std::span(value.data(), value.size()));
                return blob(raw);
            }

            bool blob(std::span<const std::byte> value)
            {
                if (value.size() > maximum_diagnostic_bytes ||
                    bytes.size() + 4 + value.size() > maximum_diagnostic_bytes ||
                    !scalar(static_cast<std::uint32_t>(value.size())))
                {
                    return false;
                }
                bytes.insert(bytes.end(), value.begin(), value.end());
                return true;
            }

            template<class... T> bool operator()(const T&... values)
            {
                return (scalar(values) && ...);
            }
        };

        struct reader
        {
            std::span<const std::byte> bytes;
            std::size_t offset{};

            template<class T> bool scalar(T& value)
            {
                if constexpr (std::is_enum_v<T>)
                {
                    std::underlying_type_t<T> raw{};
                    if (!scalar(raw))
                    {
                        return false;
                    }
                    value = static_cast<T>(raw);
                    return true;
                }
                else if constexpr (std::is_same_v<T, bool>)
                {
                    std::uint8_t raw{};
                    if (!scalar(raw) || raw > 1)
                    {
                        return false;
                    }
                    value = raw != 0;
                    return true;
                }
                else if constexpr (std::is_floating_point_v<T>)
                {
                    std::uint64_t raw{};
                    if (!scalar(raw))
                    {
                        return false;
                    }
                    value = std::bit_cast<double>(raw);
                    return std::isfinite(value);
                }
                else
                {
                    if (bytes.size() - offset < sizeof(T))
                    {
                        return false;
                    }
                    std::make_unsigned_t<T> raw{};
                    for (std::size_t index = 0; index < sizeof(T); ++index)
                    {
                        raw |= static_cast<std::make_unsigned_t<T>>(
                            std::to_integer<std::uint8_t>(bytes[offset++])) << (index * 8);
                    }
                    value = std::bit_cast<T>(raw);
                    return true;
                }
            }

            bool scalar(std::string& value)
            {
                std::span<const std::byte> raw;
                if (!blob(raw, maximum_string_bytes))
                {
                    return false;
                }
                value.assign(reinterpret_cast<const char*>(raw.data()), raw.size());
                return value.find('\0') == std::string::npos;
            }

            bool blob(std::span<const std::byte>& value, std::size_t maximum)
            {
                std::uint32_t size{};
                if (!scalar(size) || size > maximum || size > bytes.size() - offset)
                {
                    return false;
                }
                value = bytes.subspan(offset, size);
                offset += size;
                return true;
            }

            template<class... T> bool operator()(T&... values)
            {
                return (scalar(values) && ...);
            }
        };

        template<class Archive, class Value> bool memory_fields(Archive& ar, Value& value)
        {
            if (!ar(value.serial, value.frame, value.capture_ms, value.process_valid,
                value.working_set_bytes, value.private_commit_bytes, value.crt_heap_valid,
                value.crt_live_bytes, value.crt_live_blocks, value.managed_valid,
                value.managed_heap_bytes, value.managed_fragmented_bytes,
                value.managed_total_allocated_bytes, value.vram_valid, value.vram_used_bytes,
                value.vram_budget_bytes, value.texture_cpu_pixel_bytes,
                value.model_upload_payload_bytes, value.committed_private_bytes,
                value.committed_image_bytes, value.committed_mapped_bytes,
                value.reserved_virtual_bytes, value.object_count, value.region_count,
                value.strings_truncated))
            {
                return false;
            }
            for (auto& count : value.object_kind_counts)
            {
                if (!ar(count))
                {
                    return false;
                }
            }
            for (auto& bytes : value.object_kind_cpu_bytes)
            {
                if (!ar(bytes))
                {
                    return false;
                }
            }
            return true;
        }

        template<class Archive, class Value> bool object_fields(Archive& ar, Value& value)
        {
            if (!ar(value.kind, value.name, value.cpu_pixel_bytes,
                value.upload_payload_bytes, value.cpu_size_known, value.shared_alias,
                value.identity_valid) || value.kind > object_kind::sprite_sheet)
            {
                return false;
            }
            for (auto& byte : value.full_name_sha256)
            {
                if (!ar(byte) || (!value.identity_valid && byte != 0))
                {
                    return false;
                }
            }
            return true;
        }

        template<class Archive, class Value> bool region_fields(Archive& ar, Value& value)
        {
            return ar(value.address, value.bytes, value.committed, value.type,
                value.protection) && value.type <= virtual_region::kind::unknown &&
                value.bytes <= (std::numeric_limits<std::uint64_t>::max)() - value.address;
        }

        template<class Archive, class Value> bool animation_fields(Archive& ar, Value& value)
        {
            if (!ar(value.frame, value.registered, value.evaluated, value.degraded,
                value.budgetUs, value.predictedUs, value.measuredUs, value.selectedAnimatorId,
                value.workerPosePool, value.workerCurrentStorage, value.instancePoseStorage,
                value.workerPoseBuffers, value.instancePoseBuffers, value.animator_count,
                value.task_count, value.strings_truncated))
            {
                return false;
            }
            for (auto& count : value.stages)
            {
                if (!ar(count))
                {
                    return false;
                }
            }
            return value.budgetUs >= 0. && value.predictedUs >= 0. && value.measuredUs >= 0.;
        }

        template<class Archive, class Value> bool actor_fields(Archive& ar, Value& value)
        {
            return ar(value.id, value.name, value.stage, value.reason, value.predictedUs,
                value.measuredUs, value.evaluated, value.interpolated) &&
                value.id != 0 && value.stage < 8 && value.predictedUs >= 0. && value.measuredUs >= 0.;
        }

        template<class Archive, class Value> bool task_fields(Archive& ar, Value& value)
        {
            return ar(value.index, value.kind, value.dependencyA, value.dependencyB,
                value.outputSlot, value.clipIndex, value.sampleSlot, value.reachable,
                value.executionOrder, value.bufferOwner) && value.kind <= task_kind::output;
        }

        template<class T, class Visit>
        bool write_rows(writer& ar, const std::vector<T>& rows, std::size_t maximum, Visit visit)
        {
            if (rows.size() > maximum || !ar(static_cast<std::uint32_t>(rows.size())))
            {
                return false;
            }
            for (const auto& row : rows)
            {
                if (!visit(ar, row))
                {
                    return false;
                }
            }
            return true;
        }

        template<class T, class Visit>
        bool read_rows(reader& ar, std::vector<T>& rows, std::size_t maximum, Visit visit)
        {
            std::uint32_t count{};
            if (!ar(count) || count > maximum || count > ar.bytes.size() - ar.offset)
            {
                return false;
            }
            rows.resize(count);
            for (auto& row : rows)
            {
                if (!visit(ar, row))
                {
                    return false;
                }
            }
            return true;
        }

        bool valid_request(const request& value)
        {
            if (value.command_id == 0 || value.generation == 0)
            {
                return false;
            }
            switch (value.kind)
            {
            case command_kind::capture_memory:
                return value.value == 0;
            case command_kind::request_animation:
                return true; // Stable ID membership is checked by the owner.
            case command_kind::rendering:
                return value.value <= UINT32_MAX &&
                    static_cast<rendering_command>(value.value) == rendering_command::open_render_pass;
            default:
                return false;
            }
        }
    }

    bool encode_snapshot(const snapshot& value, std::vector<std::byte>& bytes)
    {
        bytes.clear();
        writer ar;
        const bool has_memory = static_cast<bool>(value.memory);
        const bool has_animation = static_cast<bool>(value.animation);
        if (!ar(snapshot_magic, schema_version, value.generation, value.scene_id,
            value.memory_pending, value.last_command_id, value.last_command_accepted,
            has_memory, has_animation) || value.generation == 0)
        {
            return false;
        }
        if (has_memory)
        {
            const auto& memory = *value.memory;
            if (!memory_fields(ar, memory) || memory.capture_ms < 0. ||
                !write_rows(ar, memory.objects, maximum_objects, object_fields<writer, const object_entry>) ||
                !write_rows(ar, memory.regions, maximum_regions, region_fields<writer, const virtual_region>))
            {
                return false;
            }
        }
        if (has_animation)
        {
            const auto& animation = *value.animation;
            if (!animation_fields(ar, animation) ||
                !write_rows(ar, animation.animators, maximum_animators, actor_fields<writer, const animation_actor>) ||
                !write_rows(ar, animation.tasks, maximum_tasks, task_fields<writer, const animation_task>))
            {
                return false;
            }
        }
        const auto rendering = encode_rendering(value.rendering);
        if (rendering.empty() || !ar.blob(rendering))
        {
            return false;
        }
        bytes = std::move(ar.bytes);
        return true;
    }

    bool decode_snapshot(std::span<const std::byte> bytes, snapshot& value)
    {
        if (bytes.size() > maximum_diagnostic_bytes)
        {
            return false;
        }
        reader ar{ bytes };
        snapshot decoded;
        std::uint32_t magic{};
        std::uint16_t version{};
        bool has_memory{}, has_animation{};
        if (!ar(magic, version, decoded.generation, decoded.scene_id, decoded.memory_pending,
            decoded.last_command_id, decoded.last_command_accepted, has_memory, has_animation) ||
            magic != snapshot_magic || version != schema_version || decoded.generation == 0)
        {
            return false;
        }
        if (has_memory)
        {
            auto memory = std::make_shared<memory_snapshot>();
            if (!memory_fields(ar, *memory) || memory->serial == 0 || memory->capture_ms < 0. ||
                !read_rows(ar, memory->objects, maximum_objects, object_fields<reader, object_entry>) ||
                !read_rows(ar, memory->regions, maximum_regions, region_fields<reader, virtual_region>) ||
                memory->object_count < memory->objects.size() || memory->region_count < memory->regions.size())
            {
                return false;
            }
            decoded.memory = std::move(memory);
        }
        if (has_animation)
        {
            auto animation = std::make_shared<animation_snapshot>();
            if (!animation_fields(ar, *animation) ||
                !read_rows(ar, animation->animators, maximum_animators, actor_fields<reader, animation_actor>) ||
                !read_rows(ar, animation->tasks, maximum_tasks, task_fields<reader, animation_task>) ||
                animation->animator_count < animation->animators.size() ||
                animation->task_count < animation->tasks.size())
            {
                return false;
            }
            decoded.animation = std::move(animation);
        }
        std::span<const std::byte> rendering;
        if (!ar.blob(rendering, 256 * 1024) || !decode_rendering(rendering, decoded.rendering) ||
            ar.offset != bytes.size())
        {
            return false;
        }
        value = std::move(decoded);
        return true;
    }

    bool encode_request(const request& value, std::vector<std::byte>& bytes)
    {
        bytes.clear();
        writer ar;
        if (!valid_request(value) || !ar(request_magic, schema_version, value.kind,
            value.command_id, value.generation, value.scene_id, value.value, value.acknowledge))
        {
            return false;
        }
        bytes = std::move(ar.bytes);
        return true;
    }

    bool decode_request(std::span<const std::byte> bytes, request& value)
    {
        reader ar{ bytes };
        request decoded;
        std::uint32_t magic{};
        std::uint16_t version{};
        if (!ar(magic, version, decoded.kind, decoded.command_id, decoded.generation,
            decoded.scene_id, decoded.value, decoded.acknowledge) || magic != request_magic || version != schema_version ||
            ar.offset != bytes.size() || !valid_request(decoded))
        {
            return false;
        }
        value = decoded;
        return true;
    }
}
