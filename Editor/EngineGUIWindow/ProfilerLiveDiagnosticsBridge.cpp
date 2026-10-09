#include "ProfilerLiveDiagnosticsBridge.h"

#include "AnimationDiagnostics.h"
#include "AnimationScheduler.h"
#include "MemoryProfilerSnapshot.h"
#include "ProfilerLiveDiagnostics.h"
#include "ProfilerRenderingBridge.h"
#include "ProfilerViewerProcess.h"
#include "Scene.h"
#include "SceneManager.h"

#include <algorithm>
#include <chrono>
#include <memory>
#include <string>
#include <utility>

namespace editor
{
    namespace
    {
        namespace dto = ce::profiler_viewer::diagnostics;
        using clock = std::chrono::steady_clock;

        std::uint32_t current_scene_id()
        {
            const auto* scene = SceneManagers->GetActiveScene();
            return scene ? scene->GetSceneId() : 0;
        }

        std::string bounded_label(const std::string& source, bool& truncated)
        {
            auto size = (std::min)(source.size(), dto::maximum_string_bytes);
            const auto terminator = source.find('\0');
            if (terminator < size)
            {
                size = terminator;
            }
            // Do not split a UTF-8 codepoint in the labels displayed by ImGui.
            while (size < source.size() && size != 0 &&
                (static_cast<unsigned char>(source[size]) & 0xc0) == 0x80)
            {
                --size;
            }
            truncated |= size != source.size();
            return source.substr(0, size);
        }

        std::shared_ptr<const dto::memory_snapshot> memory_copy(const dto::memory_snapshot& source)
        {
            auto result = std::make_shared<dto::memory_snapshot>();
            result->serial = source.serial;
            result->frame = source.frame;
            result->capture_ms = source.capture_ms;
            result->process_valid = source.process_valid;
            result->working_set_bytes = source.working_set_bytes;
            result->private_commit_bytes = source.private_commit_bytes;
            result->crt_heap_valid = source.crt_heap_valid;
            result->crt_live_bytes = source.crt_live_bytes;
            result->crt_live_blocks = source.crt_live_blocks;
            result->managed_valid = source.managed_valid;
            result->managed_heap_bytes = source.managed_heap_bytes;
            result->managed_fragmented_bytes = source.managed_fragmented_bytes;
            result->managed_total_allocated_bytes = source.managed_total_allocated_bytes;
            result->vram_valid = source.vram_valid;
            result->vram_used_bytes = source.vram_used_bytes;
            result->vram_budget_bytes = source.vram_budget_bytes;
            result->texture_cpu_pixel_bytes = source.texture_cpu_pixel_bytes;
            result->model_upload_payload_bytes = source.model_upload_payload_bytes;
            result->committed_private_bytes = source.committed_private_bytes;
            result->committed_image_bytes = source.committed_image_bytes;
            result->committed_mapped_bytes = source.committed_mapped_bytes;
            result->reserved_virtual_bytes = source.reserved_virtual_bytes;
            result->object_kind_counts = source.object_kind_counts;
            result->object_kind_cpu_bytes = source.object_kind_cpu_bytes;
            result->object_count = source.objects.size();
            result->region_count = source.regions.size();
            const auto count = (std::min)(source.objects.size(), dto::maximum_objects);
            result->objects.reserve(count);
            for (std::size_t index = 0; index < count; ++index)
            {
                const auto& item = source.objects[index];
                result->objects.push_back({ item.kind,
                    bounded_label(item.name, result->strings_truncated), item.cpu_pixel_bytes,
                    item.upload_payload_bytes, item.cpu_size_known, item.shared_alias,
                    item.full_name_sha256, item.identity_valid });
            }
            result->regions.assign(source.regions.begin(), source.regions.begin() +
                (std::min)(source.regions.size(), dto::maximum_regions));
            return result;
        }

        std::shared_ptr<const dto::animation_snapshot> animation_copy(const AnimationHudSnapshot& source)
        {
            static_assert(static_cast<unsigned>(animation::task_kind::output) ==
                static_cast<unsigned>(dto::task_kind::output));
            auto result = std::make_shared<dto::animation_snapshot>();
            result->frame = source.frame;
            result->registered = source.registered;
            result->evaluated = source.evaluated;
            result->degraded = source.degraded;
            result->budgetUs = source.budgetUs;
            result->predictedUs = source.predictedUs;
            result->measuredUs = source.measuredUs;
            result->stages = source.stages;
            result->selectedAnimatorId = source.selectedAnimatorId;
            result->workerPosePool = source.workerPosePool;
            result->workerCurrentStorage = source.workerCurrentStorage;
            result->instancePoseStorage = source.instancePoseStorage;
            result->workerPoseBuffers = source.workerPoseBuffers;
            result->instancePoseBuffers = source.instancePoseBuffers;
            result->animator_count = source.animators.size();
            result->task_count = source.tasks.size();
            const auto actors = (std::min)(source.animators.size(), dto::maximum_animators);
            result->animators.reserve(actors);
            for (std::size_t index = 0; index < actors; ++index)
            {
                const auto& actor = source.animators[index];
                result->animators.push_back({ actor.id,
                    bounded_label(actor.name, result->strings_truncated),
                    static_cast<std::uint8_t>(actor.stage),
                    bounded_label(actor.reason, result->strings_truncated),
                    actor.predictedUs, actor.measuredUs, actor.evaluated, actor.interpolated });
            }
            const auto tasks = (std::min)(source.tasks.size(), dto::maximum_tasks);
            result->tasks.reserve(tasks);
            for (std::size_t index = 0; index < tasks; ++index)
            {
                const auto& task = source.tasks[index];
                result->tasks.push_back({ task.index, static_cast<dto::task_kind>(task.kind),
                    task.dependencyA, task.dependencyB, task.outputSlot, task.clipIndex,
                    task.sampleSlot, task.reachable, task.executionOrder,
                    bounded_label(task.bufferOwner, result->strings_truncated) });
            }
            return result;
        }

        struct bridge_state
        {
            dto::snapshot published;
            clock::time_point published_at{};
            clock::time_point last_memory_request{};
            clock::time_point last_rendering_request{};
            std::uint64_t animation_scene_boundary_frame{};
            bool waiting_for_scene_animation{};

            ce::profiler_viewer::engine_process::diagnostic_encoder publish()
            {
                const auto scene_id = current_scene_id();
                const auto animation = SceneManagers->GetAnimationScheduler().GetHudSnapshot();
                if (published.generation != 0 && published.scene_id != scene_id)
                {
                    // A previously published HUD may outlive a scene switch.
                    // Do not relabel that old animation sample with the new scene.
                    animation_scene_boundary_frame = animation ? animation->frame : 0;
                    waiting_for_scene_animation = static_cast<bool>(animation);
                    published.animation.reset();
                }
                ++published.generation;
                published.scene_id = scene_id;
                published.memory_pending = memory_profiler::snapshot_service::instance().pending();
                const auto memory = memory_profiler::snapshot_service::instance().latest();
                if (memory && (!published.memory || memory->serial != published.memory->serial))
                {
                    published.memory = memory_copy(*memory);
                }
                if (waiting_for_scene_animation &&
                    (!animation || animation->frame != animation_scene_boundary_frame))
                {
                    waiting_for_scene_animation = false;
                }
                published.animation = animation && !waiting_for_scene_animation
                    ? animation_copy(*animation) : nullptr;
                published.rendering = capture_rendering_diagnostics();
                published_at = clock::now();
                // Serialization runs only on the transport worker and captures
                // a bounded immutable value, never the bridge or engine services.
                return [frozen = published]
                {
                    std::vector<std::byte> bytes;
                    if (!dto::encode_snapshot(frozen, bytes))
                    {
                        return std::vector<std::byte>{};
                    }
                    return bytes;
                };
            }

            bool apply(std::span<const std::byte> bytes)
            {
                dto::request command;
                if (!dto::decode_request(bytes, command))
                {
                    return false;
                }
                if (command.acknowledge)
                {
                    published.last_command_id = command.command_id;
                    published.last_command_accepted = false;
                }
                const auto now = clock::now();
                // Exact target identity/session are checked by the outer transport.
                // The scene ID survives address reuse and the short generation
                // lease rejects controls from a previously displayed scene/frame.
                if (command.scene_id != current_scene_id() ||
                    command.scene_id != published.scene_id ||
                    command.generation > published.generation ||
                    published.generation - command.generation > 8 ||
                    now - published_at > std::chrono::seconds(2))
                {
                    return false;
                }
                switch (command.kind)
                {
                case dto::command_kind::capture_memory:
                    if (memory_profiler::snapshot_service::instance().pending() ||
                        now - last_memory_request < std::chrono::seconds(1))
                    {
                        return false;
                    }
                    memory_profiler::snapshot_service::instance().request();
                    last_memory_request = now;
                    break;
                case dto::command_kind::request_animation:
                {
                    auto& scheduler = SceneManagers->GetAnimationScheduler();
                    const auto& current = published.animation;
                    if (command.value != 0 && (!current ||
                        std::none_of(current->animators.begin(), current->animators.end(),
                            [&](const auto& actor) { return actor.id == command.value; })))
                    {
                        return false;
                    }
                    // Stable-ID request only. The scheduler resolves it during
                    // its next owner update; removed IDs produce no task capture.
                    scheduler.RequestHudCapture(command.value);
                    break;
                }
                case dto::command_kind::rendering:
                    if (now - last_rendering_request < std::chrono::seconds(1) ||
                        !apply_rendering_command(static_cast<dto::rendering_command>(command.value)))
                    {
                        return false;
                    }
                    last_rendering_request = now;
                    break;
                default:
                    return false;
                }
                if (command.acknowledge)
                {
                    published.last_command_accepted = true;
                }
                return true;
            }
        };
    }

    void install_profiler_live_diagnostics(ce::profiler_viewer::engine_process& process)
    {
        const auto state = std::make_shared<bridge_state>();
        process.set_diagnostic_hooks([state] { return state->publish(); },
            [state](std::span<const std::byte> bytes) { return state->apply(bytes); });
    }
}
