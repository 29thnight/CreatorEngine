#pragma once
#include "../Utility_Framework/LayerTypes.h"

#include "PhysicsTypes.h"
#include <mathematics/vector3.hpp>
#include <mathematics/quaternion.hpp>
#include <memory>
#include <span>
#include <variant>
#include <utility>
#include <array>

namespace ce::physics
{
// GUID-compatible value layout; opaque subscription metadata, independent of collision filters.
struct contact_role_id
{
    std::uint32_t a = 0;
    std::uint16_t b = 0, c = 0;
    std::array<std::uint8_t, 8> d{};
    auto operator<=>(const contact_role_id&) const = default;
};
static_assert(sizeof(contact_role_id) == 16);

struct pose
{
    math::vector3 position{};
    math::quaternion rotation{};
};

enum class geometry_kind : std::uint8_t
{
    convex,
    triangle_mesh,
    heightfield
};

// Immutable SDK asset. Keeps its SDK alive even after its cooking scene is destroyed.
class CollisionGeometry final
{
  public:
    ~CollisionGeometry();
    CollisionGeometry(const CollisionGeometry&) = delete;
    CollisionGeometry& operator=(const CollisionGeometry&) = delete;

    [[nodiscard]] geometry_kind kind() const noexcept;
    [[nodiscard]] bool gpu_compatible() const noexcept;

  private:
    friend class PhysicsScene;
    struct implementation;
    explicit CollisionGeometry(std::unique_ptr<implementation> state) noexcept;

    std::unique_ptr<implementation> m_state;
};

struct box_geometry
{
    math::vector3 half_extent{.5f, .5f, .5f};
};
struct sphere_geometry
{
    float radius = .5f;
};
// Capsule's longitudinal axis is local +Y; half_height excludes the spherical caps.
struct capsule_geometry
{
    float radius = .5f;
    float half_height = .5f;
};

struct cooked_geometry
{
    std::shared_ptr<const CollisionGeometry> asset;
    math::vector3 scale{1.f, 1.f, 1.f}; // Positive scale; primitive dimensions are already baked.
};

using geometry = std::variant<box_geometry, sphere_geometry, capsule_geometry, cooked_geometry>;

struct material
{
    float static_friction = .5f;
    float dynamic_friction = .5f;
    float restitution = 0.f;
};

struct collision_filter
{
    std::uint32_t belongs_to = 1;
    std::uint32_t collides_with = ~0u;
    std::uint32_t query_layers = ~0u;
};

struct ShapeInstance
{
    shape_id id{1};
    geometry form = box_geometry{};
    pose local_pose{};
    material surface{};
    collision_filter filter{};
    bool sensor = false;
    bool query_enabled = true;
    ce::layers::layer_id layer_override{}; // Host policy metadata; zero inherits the Entity layer.
    contact_role_id contact_role{};
};

enum class body_kind : std::uint8_t
{
    static_body,
    kinematic,
    dynamic
};

enum class axis_lock : std::uint8_t
{
    none = 0,
    x = 1,
    y = 2,
    xy = 3,
    z = 4,
    xz = 5,
    yz = 6,
    all = 7
};

constexpr axis_lock operator|(axis_lock left, axis_lock right) noexcept
{
    return static_cast<axis_lock>(std::to_underlying(left) | std::to_underlying(right));
}

struct body_constraints
{
    axis_lock translation = axis_lock::none;
    axis_lock rotation = axis_lock::none;
};

struct body_desc
{
    body_kind kind = body_kind::static_body;
    pose initial_pose{};
    std::span<const ShapeInstance> shapes;
    float mass = 1.f; // kg; compound inertia uses solid shapes only. Non-static bodies require a solid shape.
    math::vector3 linear_velocity{};
    math::vector3 angular_velocity{};
    float linear_damping = 0.f;
    float angular_damping = .05f;
    body_constraints constraints{};
    bool gravity_enabled = true;
};

struct body_state
{
    body_kind kind;
    pose transform;
    math::vector3 linear_velocity;
    math::vector3 angular_velocity;
    float mass;
    math::vector3 inertia;
    pose center_of_mass{}; // Local COM pose; inertia is expressed in its principal axes.
};

struct triangle_indices
{
    std::uint32_t a, b, c;
};

struct heightfield_desc
{
    std::uint32_t rows = 0;
    std::uint32_t columns = 0;
    std::span<const std::int16_t> heights; // Row-major signed samples; scale is applied at instantiation.
};

struct query_filter
{
    std::uint32_t layers = ~0u;
    bool include_sensors = false;
    body_handle ignore{};
};

struct query_hit
{
    body_handle body;
    shape_id shape;
    math::vector3 position{};
    math::vector3 normal{};
    float distance = 0.f;
    std::uint32_t face = ~0u;
    bool has_location = false; // Overlap hits have identity only.
};

struct ray_query
{
    math::vector3 origin{};
    math::vector3 direction{};
    float distance = 0.f;
};

struct sweep_query
{
    geometry form;
    pose origin{};
    math::vector3 direction{};
    float distance = 0.f;
};

struct overlap_query
{
    geometry form;
    pose origin{};
};

using query_input = std::variant<ray_query, sweep_query, overlap_query>;

struct query_request
{
    query_input input;
    std::span<query_hit> output;
    query_filter filter{};
};

struct query_result
{
    std::size_t written = 0;
    std::size_t required_capacity = 0;
    bool truncated = false;
};
} // namespace ce::physics
