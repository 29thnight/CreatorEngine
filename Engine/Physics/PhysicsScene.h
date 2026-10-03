#pragma once
#include "PhysicsTypes.h"
#include "PhysicsGeometry.h"
#include "PhysicsStep.h"
#include <memory>
#include <mathematics/vector3.hpp>

namespace ce::physics
{
enum class execution_preference : std::uint8_t
{
    cpu,
    prefer_gpu
};

enum class execution_backend : std::uint8_t
{
    cpu,
    gpu
};

enum class gpu_fallback_reason : std::uint8_t
{
    none,
    unsupported_build,
    context_unavailable,
    scene_rejected
};

struct scene_config
{
    math::vector3 gravity{0.f, -9.81f, 0.f};
    std::uint32_t workers = 0; // 0 selects max(1, hardware threads - 4).
    std::uint32_t task_capacity = 1024;
    execution_preference execution = execution_preference::cpu;
    std::uint32_t command_capacity = 1024;
    std::uint32_t event_capacity = 1024;
    std::uint32_t snapshot_capacity = 8; // Includes the currently published, pending and reader-held buffers.
};

enum class scene_phase : std::uint8_t
{
    idle,
    commit,
    query_read,
    simulating,
    publish,
    failed,
    closing
};

struct scene_status
{
    scene_id identity;
    execution_backend backend = execution_backend::cpu;
    bool gpu_requested = false;
    bool gpu_unavailable = false;
    gpu_fallback_reason gpu_fallback = gpu_fallback_reason::none;
    bool gpu_dynamics = false;
    bool gpu_broadphase = false;
    std::uint32_t workers = 0;
    std::uint64_t submitted_tasks = 0;
    std::uint64_t completed_tasks = 0;
    std::uint64_t inline_tasks = 0;
    std::uint32_t sdk_errors = 0;
    tick_id last_tick;
    bool failed = false; // Terminal fetch failure; destroy/recreate the scene to recover.
    scene_phase phase = scene_phase::idle;
};

// Copyable job-side capability. Owns only request/publication storage, never the SDK scene.
// Copies may outlive Scene/PhysicsScene destruction; submissions then fail with wrong_phase.
class PhysicsSceneChannel final
{
  public:
    PhysicsSceneChannel() = default;
    [[nodiscard]] scene_id identity() const noexcept;
    [[nodiscard]] bool is_closed() const noexcept;
    [[nodiscard]] result<void> submit(command value,
                                    std::source_location location = std::source_location::current()) const;
    [[nodiscard]] std::shared_ptr<const tick_snapshot> latest_snapshot() const noexcept;

  private:
    friend class PhysicsScene;
    struct storage;
    explicit PhysicsSceneChannel(std::shared_ptr<storage> value) noexcept : m_storage(std::move(value)) {}
    std::shared_ptr<storage> m_storage;
};

// Owner-thread object. Jobs use channel() copies, not a borrowed PhysicsScene pointer.
// Direct submit_command/latest_snapshot also permit other threads while this object is alive.
// Producers/readers must stop calling this object before owner-thread destruction.
// The SDK implementation is private; engine Scene owns this object via unique_ptr.
class PhysicsScene final
{
  public:
    [[nodiscard]] static result<std::unique_ptr<PhysicsScene>> create(
        const scene_config& config = {}, std::source_location location = std::source_location::current());
    ~PhysicsScene();
    PhysicsScene(const PhysicsScene&) = delete;
    PhysicsScene& operator=(const PhysicsScene&) = delete;
    PhysicsScene(PhysicsScene&&) = delete;
    PhysicsScene& operator=(PhysicsScene&&) = delete;

    [[nodiscard]] result<void> begin_step(float seconds,
                                          std::source_location location = std::source_location::current());
    [[nodiscard]] result<void> finish_step(std::source_location location = std::source_location::current());
    [[nodiscard]] scene_status status() const noexcept;
    [[nodiscard]] result<PhysicsSceneChannel> channel(
        std::source_location location = std::source_location::current()) const;

    [[nodiscard]] result<character_handle> create_character(
        const character_desc& desc, std::source_location location = std::source_location::current());
    [[nodiscard]] result<void> destroy_character(character_handle character,
                                                 std::source_location location = std::source_location::current());
    [[nodiscard]] result<character_state> read_character(
        character_handle character, std::source_location location = std::source_location::current()) const;
    [[nodiscard]] result<character_state> move_character(
        character_handle character, const character_move& move,
        std::source_location location = std::source_location::current());
    [[nodiscard]] result<void> teleport_character(character_handle character, math::vector3 position,
                                                  std::source_location location = std::source_location::current());
    [[nodiscard]] result<void> set_character_filter(character_handle character, std::uint32_t belongs_to,
        std::uint32_t collides_with, std::source_location location = std::source_location::current());

    [[nodiscard]] result<void> submit_command(command value,
                                              std::source_location location = std::source_location::current());
    [[nodiscard]] std::shared_ptr<const tick_snapshot> latest_snapshot() const noexcept;
    [[nodiscard]] result<void> apply_force(body_handle body, math::vector3 linear, math::vector3 angular,
                                           force_mode mode = force_mode::force,
                                           std::source_location location = std::source_location::current());

    [[nodiscard]] result<std::shared_ptr<const CollisionGeometry>> cook_convex(
        std::span<const math::vector3> points, std::source_location location = std::source_location::current());
    [[nodiscard]] result<std::shared_ptr<const CollisionGeometry>> cook_triangle_mesh(
        std::span<const math::vector3> points, std::span<const triangle_indices> triangles,
        std::source_location location = std::source_location::current());
    [[nodiscard]] result<std::shared_ptr<const CollisionGeometry>> cook_heightfield(
        const heightfield_desc& desc, std::source_location location = std::source_location::current());

    struct convex_cook_input { std::span<const math::vector3> points; };
    struct triangle_cook_input { std::span<const math::vector3> points; std::span<const triangle_indices> triangles; };
    using geometry_cook_input = std::variant<convex_cook_input, triangle_cook_input, heightfield_desc>;

    // Opaque SDK payload. Identity/revision/integrity belong to the artifact codec.
    static std::uint32_t sdk_version() noexcept;
    [[nodiscard]] result<std::vector<std::byte>> cook_geometry_blob(const geometry_cook_input& input);
    [[nodiscard]] result<std::shared_ptr<const CollisionGeometry>> load_geometry_blob(
        geometry_kind kind, std::span<const std::byte> bytes);

    [[nodiscard]] result<body_handle> create_body(const body_desc& desc,
                                                  std::source_location location = std::source_location::current());
    // Transactional complete-definition replacement. Success invalidates the old handle.
    [[nodiscard]] result<body_handle> replace_body(body_handle body, const body_desc& desc,
                                                   std::source_location location = std::source_location::current());
    [[nodiscard]] result<void> destroy_body(body_handle body,
                                            std::source_location location = std::source_location::current());
    [[nodiscard]] result<body_state> read_body(body_handle body,
                                               std::source_location location = std::source_location::current()) const;
    // Idle owner only. Preserve body/shape identities and explicitly refilter existing pairs.
    [[nodiscard]] result<void> set_shape_filter(body_handle body, shape_id shape, collision_filter filter,
                                                 std::source_location location = std::source_location::current());
    [[nodiscard]] result<void> set_pose(body_handle body, const pose& value,
                                        std::source_location location = std::source_location::current());
    [[nodiscard]] result<void> set_velocity(body_handle body, math::vector3 linear, math::vector3 angular,
                                            std::source_location location = std::source_location::current());
    [[nodiscard]] result<void> set_kinematic_target(body_handle body, const pose& value,
                                                    std::source_location location = std::source_location::current());

    // All-hit queries. No-hit succeeds; overflow reports exact required capacity, never silently drops hits.
    // Results are an unordered subset on overflow, not guaranteed to contain the nearest hit.
    [[nodiscard]] result<query_result> raycast(math::vector3 origin, math::vector3 unit_direction, float distance,
                                               std::span<query_hit> output, const query_filter& filter = {},
                                               std::source_location location = std::source_location::current());
    [[nodiscard]] result<query_result> sweep(const geometry& form, const pose& origin, math::vector3 unit_direction,
                                             float distance, std::span<query_hit> output,
                                             const query_filter& filter = {},
                                             std::source_location location = std::source_location::current());
    [[nodiscard]] result<query_result> overlap(const geometry& form, const pose& origin, std::span<query_hit> output,
                                               const query_filter& filter = {},
                                               std::source_location location = std::source_location::current());

    // Owner idle only. Input/output storage is borrowed until return; hit buffers must not alias.
    // Batch-level failure writes nothing. Per-request errors do not cancel later requests.
    [[nodiscard]] result<void> query_batch(
        std::span<const query_request> requests, std::span<result<query_result>> results,
        std::source_location location = std::source_location::current());

  private:
    struct implementation;
    explicit PhysicsScene(std::unique_ptr<implementation> state) noexcept;
    std::unique_ptr<implementation> m_state;
};

} // namespace ce::physics
