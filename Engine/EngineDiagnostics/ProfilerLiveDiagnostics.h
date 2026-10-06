#pragma once

#include "ProfilerRenderingDiagnostics.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <compare>
#include <memory>
#include <span>
#include <string>
#include <vector>

// Value-only wire model. Addresses are labels from the target, never handles
// that the viewer can dereference or send back as a read request.
namespace ce::profiler_viewer::diagnostics
{
    inline constexpr std::uint16_t schema_version = 2;
    // Includes a full 256-bit resource identity per row while keeping all three
    // diagnostics at their simultaneous row/string caps below the 2 MiB envelope.
    inline constexpr std::size_t maximum_objects = 3840;
    inline constexpr std::size_t maximum_regions = 8192;
    inline constexpr std::size_t maximum_animators = 512;
    inline constexpr std::size_t maximum_tasks = 512;
    inline constexpr std::size_t maximum_string_bytes = 256;
    inline constexpr std::uint32_t invalid_task = UINT32_MAX;

    enum class object_kind : std::uint8_t
    {
        model, material, texture, ui_texture, sprite_sheet
    };

    struct object_entry
    {
        object_kind kind{};
        std::string name;
        std::uint64_t cpu_pixel_bytes{};
        std::uint64_t upload_payload_bytes{};
        bool cpu_size_known{};
        bool shared_alias{};
        std::array<std::uint8_t, 32> full_name_sha256{};
        bool identity_valid{};
    };

    struct memory_object_key
    {
        object_kind kind{};
        std::array<std::uint8_t, 32> full_name_sha256{};
        std::uint64_t unmatched_side{};
        std::uint64_t unmatched_row{};
        auto operator<=>(const memory_object_key&) const = default;
    };

    // Bounded display labels are never identity. Failed hashes receive unique
    // row-local keys, so neither siblings nor different snapshots can collapse.
    inline memory_object_key object_comparison_key(const object_entry& value,
        std::uint64_t snapshot_side, std::uint64_t row_index)
    {
        if (value.identity_valid)
        {
            return { value.kind, value.full_name_sha256, 0, 0 };
        }
        return { value.kind, {}, snapshot_side, row_index + 1 };
    }

    struct virtual_region
    {
        std::uint64_t address{};
        std::uint64_t bytes{};
        bool committed{};
        enum class kind : std::uint8_t { private_memory, image, mapped, unknown } type{};
        std::uint32_t protection{};
    };

    struct memory_snapshot
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
        std::array<std::uint64_t, 5> object_kind_counts{};
        std::array<std::uint64_t, 5> object_kind_cpu_bytes{};
        std::vector<object_entry> objects;
        std::vector<virtual_region> regions;
        // Exact totals before bounded transport; UI must disclose truncation.
        std::uint64_t object_count{};
        std::uint64_t region_count{};
        bool strings_truncated{};
    };

    enum class task_kind : std::uint8_t
    {
        sample_clip, blend, materialize, prepare_composite, blend_masked,
        make_additive, apply_additive, materialize_composite, two_bone_ik,
        bone_transform, output
    };

    struct animation_task
    {
        std::uint32_t index{};
        task_kind kind{};
        std::uint32_t dependencyA{ invalid_task };
        std::uint32_t dependencyB{ invalid_task };
        std::uint32_t outputSlot{ invalid_task };
        std::int32_t clipIndex{ -1 };
        std::uint8_t sampleSlot{};
        bool reachable{};
        std::uint32_t executionOrder{ invalid_task };
        std::string bufferOwner;
    };

    struct animation_actor
    {
        std::uint64_t id{};
        std::string name;
        std::uint8_t stage{};
        std::string reason;
        double predictedUs{};
        double measuredUs{};
        bool evaluated{};
        bool interpolated{};
    };

    struct animation_snapshot
    {
        std::uint64_t frame{};
        std::uint64_t registered{};
        std::uint64_t evaluated{};
        std::uint64_t degraded{};
        double budgetUs{};
        double predictedUs{};
        double measuredUs{};
        std::array<std::uint64_t, 8> stages{};
        std::vector<animation_actor> animators;
        std::uint64_t selectedAnimatorId{};
        std::vector<animation_task> tasks;
        std::uint64_t workerPosePool{};
        std::uint64_t workerCurrentStorage{};
        std::uint64_t instancePoseStorage{};
        std::uint64_t workerPoseBuffers{};
        std::uint64_t instancePoseBuffers{};
        std::uint64_t animator_count{};
        std::uint64_t task_count{};
        bool strings_truncated{};
    };

    struct snapshot
    {
        std::uint64_t generation{};
        std::uint32_t scene_id{};
        bool memory_pending{};
        std::shared_ptr<const memory_snapshot> memory;
        std::shared_ptr<const animation_snapshot> animation;
        rendering_snapshot rendering;
        std::uint64_t last_command_id{};
        bool last_command_accepted{};
    };

    enum class command_kind : std::uint8_t
    {
        capture_memory = 1, request_animation = 2, rendering = 3
    };

    struct request
    {
        command_kind kind{};
        std::uint64_t command_id{};
        std::uint64_t generation{};
        std::uint32_t scene_id{};
        // Animator identity for request_animation; rendering_command for rendering.
        std::uint64_t value{};
        bool acknowledge{};
    };

    bool encode_snapshot(const snapshot& value, std::vector<std::byte>& bytes);
    bool decode_snapshot(std::span<const std::byte> bytes, snapshot& value);
    bool encode_request(const request& value, std::vector<std::byte>& bytes);
    bool decode_request(std::span<const std::byte> bytes, request& value);
}
