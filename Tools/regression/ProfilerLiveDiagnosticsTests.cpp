// Source-only regression fixture. UNEXECUTED by explicit request: do not build
// or run during this migration. Link the two neutral diagnostics codecs only
// when verification is authorized; no editor/scene/renderer initialization.
#include "../../Engine/EngineDiagnostics/ProfilerLiveDiagnostics.h"
#include "../../Engine/EngineDiagnostics/ProfilerViewerProtocol.h"

#include <cstdio>
#include <limits>
#include <memory>
#include <map>
#include <vector>

namespace live_diagnostics_tests
{
    using namespace ce::profiler_viewer::diagnostics;

    void check(bool condition, const char* label, unsigned& failures)
    {
        if (!condition)
        {
            std::fprintf(stderr, "FAIL: %s\n", label);
            ++failures;
        }
    }

    void reject_snapshot(const std::vector<std::byte>& bytes, const char* label, unsigned& failures)
    {
        snapshot untouched;
        untouched.generation = 999;
        check(!decode_snapshot(bytes, untouched), label, failures);
        check(untouched.generation == 999, "failed decode preserves output", failures);
    }

    int run()
    {
        unsigned failures{};
        snapshot source;
        source.generation = 7;
        source.scene_id = 42;
        source.memory_pending = true;
        source.last_command_id = 9;
        source.last_command_accepted = false;
        auto memory = std::make_shared<memory_snapshot>();
        memory->serial = 2;
        memory->frame = 17;
        memory->capture_ms = 2.5;
        memory->objects.push_back({ object_kind::texture, "Texture A", 4096, 0, true, false });
        memory->regions.push_back({ 0x10000, 0x2000, true, virtual_region::kind::private_memory, 4 });
        memory->object_count = 40000; // Explicit partial-list totals survive the boundary.
        memory->region_count = 9000;
        memory->object_kind_counts[2] = 40000;
        memory->object_kind_cpu_bytes[2] = 1000000;
        source.memory = memory;
        auto animation = std::make_shared<animation_snapshot>();
        animation->frame = 18;
        animation->budgetUs = 1500.;
        animation->predictedUs = 1000.;
        animation->measuredUs = 1050.;
        animation->animators.push_back({ 71, "Actor", 2, "CPU budget", 1000., 1050., true, false });
        animation->animator_count = 1;
        animation->selectedAnimatorId = 71;
        animation->tasks.push_back({ 0, task_kind::sample_clip, invalid_task,
            invalid_task, 0, 1, 0, true, 0, "worker" });
        animation->task_count = 1;
        source.animation = animation;

        std::vector<std::byte> encoded;
        check(encode_snapshot(source, encoded), "snapshot encodes", failures);
        snapshot decoded;
        check(decode_snapshot(encoded, decoded), "snapshot round trip", failures);
        check(decoded.memory && decoded.memory->object_count == 40000 &&
            decoded.memory->objects.size() == 1 && decoded.memory->objects[0].name == "Texture A" &&
            decoded.memory->object_kind_cpu_bytes[2] == 1000000,
            "memory fields and exact totals retained", failures);
        check(decoded.animation && decoded.animation->selectedAnimatorId == 71 &&
            decoded.animation->tasks.size() == 1 &&
            decoded.animation->tasks[0].dependencyA == invalid_task,
            "animation fields retained", failures);

        // Synthetic distinct SHA-256 DTO values test the comparison boundary,
        // not BCrypt itself. Full source names differ after their common prefix;
        // both intentionally have the same bounded display label.
        const std::string full_name_a = std::string(maximum_string_bytes + 100, 'x') + "/A.texture";
        const std::string full_name_b = std::string(maximum_string_bytes + 100, 'x') + "/B.texture";
        object_entry prefix_a{ object_kind::texture, full_name_a.substr(0, maximum_string_bytes), 100, 0, true, false };
        object_entry prefix_b{ object_kind::texture, full_name_b.substr(0, maximum_string_bytes), 200, 0, true, false };
        prefix_a.identity_valid = true;
        prefix_b.identity_valid = true;
        prefix_a.full_name_sha256[0] = 0xa1;
        prefix_b.full_name_sha256[0] = 0xb2;
        const auto original_objects = memory->objects;
        memory->objects = { prefix_a, prefix_b };
        std::vector<std::byte> identity_bytes;
        snapshot identity_roundtrip;
        check(encode_snapshot(source, identity_bytes) && decode_snapshot(identity_bytes, identity_roundtrip),
            "full-name identity codec round trip", failures);
        if (identity_roundtrip.memory && identity_roundtrip.memory->objects.size() == 2)
        {
            const auto& objects = identity_roundtrip.memory->objects;
            check(objects[0].name == objects[1].name &&
                objects[0].full_name_sha256 != objects[1].full_name_sha256 &&
                objects[0].identity_valid && objects[1].identity_valid,
                "duplicate clipped prefixes retain distinct full-name digests", failures);
            std::map<memory_object_key, unsigned> keys;
            for (std::size_t index = 0; index < objects.size(); ++index)
            {
                ++keys[object_comparison_key(objects[index], 1, index)];
                ++keys[object_comparison_key(objects[index], 2, index)];
            }
            check(keys.size() == 2 && keys.begin()->second == 2,
                "same full identity pairs across A/B without collapsing sibling prefixes", failures);
            auto unavailable = objects[0];
            unavailable.identity_valid = false;
            unavailable.full_name_sha256.fill(0);
            keys.clear();
            ++keys[object_comparison_key(unavailable, 1, 0)];
            ++keys[object_comparison_key(unavailable, 1, 1)];
            ++keys[object_comparison_key(unavailable, 2, 0)];
            ++keys[object_comparison_key(unavailable, 2, 1)];
            check(keys.size() == 4, "hash failure keeps all sibling and cross-snapshot rows separate", failures);
        }
        memory->objects[0].identity_valid = false;
        check(!encode_snapshot(source, identity_bytes), "invalid identity must not carry digest bytes", failures);
        memory->objects = original_objects;

        for (std::size_t size = 0; size < encoded.size(); ++size)
        {
            std::vector<std::byte> truncated(encoded.begin(), encoded.begin() + size);
            reject_snapshot(truncated, "every truncated prefix rejected", failures);
        }
        auto malformed = encoded;
        malformed.push_back(std::byte{});
        reject_snapshot(malformed, "trailing byte rejected", failures);
        malformed = encoded;
        malformed[0] ^= std::byte{ 1 };
        reject_snapshot(malformed, "wrong magic rejected", failures);
        malformed = encoded;
        malformed[4] = std::byte{ 0xff };
        reject_snapshot(malformed, "unsupported schema rejected", failures);
        malformed = encoded;
        malformed[18] = std::byte{ 2 }; // memory_pending, after magic/version/generation/scene
        reject_snapshot(malformed, "noncanonical bool rejected", failures);
        malformed.assign(ce::profiler_viewer::maximum_diagnostic_bytes + 1, std::byte{});
        reject_snapshot(malformed, "oversized envelope rejected", failures);

        memory->capture_ms = (std::numeric_limits<double>::quiet_NaN)();
        check(!encode_snapshot(source, malformed), "NaN capture duration rejected", failures);
        memory->capture_ms = 2.5;
        memory->objects[0].name.assign(maximum_string_bytes + 1, 'x');
        check(!encode_snapshot(source, malformed), "oversized text rejected", failures);
        memory->objects[0].name = "Texture A";
        memory->objects[0].kind = static_cast<object_kind>(255);
        check(!encode_snapshot(source, malformed), "invalid object kind rejected", failures);
        memory->objects[0].kind = object_kind::texture;
        animation->animators[0].stage = 8;
        check(!encode_snapshot(source, malformed), "invalid animation quality stage rejected", failures);
        animation->animators[0].stage = 2;
        animation->tasks[0].kind = static_cast<task_kind>(255);
        check(!encode_snapshot(source, malformed), "invalid animation task kind rejected", failures);
        animation->tasks[0].kind = task_kind::sample_clip;
        memory->objects.resize(maximum_objects + 1);
        check(!encode_snapshot(source, malformed), "object count bound enforced", failures);
        memory->objects.resize(1);

        request command;
        command.kind = command_kind::capture_memory;
        command.command_id = 1;
        command.generation = 7;
        command.scene_id = 42;
        command.acknowledge = true;
        check(encode_request(command, encoded), "memory request encodes", failures);
        request decoded_command;
        check(decode_request(encoded, decoded_command) && decoded_command.acknowledge &&
            decoded_command.generation == 7 && decoded_command.scene_id == 42,
            "request identity and acknowledgement preserved", failures);
        malformed = encoded;
        malformed.push_back(std::byte{});
        check(!decode_request(malformed, decoded_command), "request trailing bytes rejected", failures);
        command.value = 1;
        check(!encode_request(command, malformed), "memory request rejects extraneous value", failures);
        command.kind = command_kind::request_animation;
        command.value = 71;
        check(encode_request(command, malformed), "stable animator identity request", failures);
        command.kind = command_kind::rendering;
        command.value = static_cast<std::uint64_t>(rendering_command::open_render_pass);
        check(encode_request(command, malformed), "known rendering request", failures);
        command.value = UINT64_MAX;
        check(!encode_request(command, malformed), "unknown rendering control rejected", failures);
        command.kind = static_cast<command_kind>(255);
        check(!encode_request(command, malformed), "unknown command rejected", failures);

        // Exercise all row and string caps together; this must fit below the
        // outer 2 MiB envelope without silently dropping an entire publication.
        memory->objects.assign(maximum_objects,
            { object_kind::texture, std::string(maximum_string_bytes, 'm'), 1, 0, true, false });
        memory->object_count = memory->objects.size();
        memory->regions.assign(maximum_regions, { 1, 1, true, virtual_region::kind::image, 1 });
        memory->region_count = memory->regions.size();
        animation->animators.assign(maximum_animators,
            { 1, std::string(maximum_string_bytes, 'a'), 1,
                std::string(maximum_string_bytes, 'r'), 1., 1., true, false });
        animation->animator_count = animation->animators.size();
        animation->tasks.assign(maximum_tasks,
            { 0, task_kind::sample_clip, invalid_task, invalid_task, 0, 0, 0, true,
                0, std::string(maximum_string_bytes, 't') });
        animation->task_count = animation->tasks.size();
        source.rendering.passTimings.assign(maximum_rendering_passes,
            { std::string(maximum_rendering_name_bytes, 'p'), 1. });
        source.rendering.validationMessages.assign(maximum_rendering_messages,
            std::string(maximum_rendering_message_bytes, 'v'));
        source.rendering.meshletFallback.assign(maximum_rendering_text_bytes, 'x');
        source.rendering.occlusionFallback.assign(maximum_rendering_text_bytes, 'x');
        source.rendering.skinningFallback.assign(maximum_rendering_text_bytes, 'x');
        source.rendering.lastGpuCollectError.assign(maximum_rendering_text_bytes, 'x');
        source.rendering.lastError.assign(maximum_rendering_text_bytes, 'x');
        check(encode_snapshot(source, encoded) &&
            encoded.size() <= ce::profiler_viewer::maximum_diagnostic_bytes,
            "combined worst-case row caps fit envelope", failures);
        return failures == 0 ? 0 : 1;
    }
}

int main()
{
    return live_diagnostics_tests::run();
}
