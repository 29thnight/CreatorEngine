#pragma once
#include <compare>
#include <cstdint>
#include <expected>
#include <limits>
#include <source_location>
#include <string_view>

namespace ce::physics
{
enum class error_code : std::uint8_t
{
    invalid_argument,
    unsupported_geometry,
    stale_handle,
    wrong_scene,
    wrong_phase,
    backend_initialization,
    cooking_failed,
    capacity_exceeded,
    out_of_memory,
    late_command,
    duplicate_command
};

struct error
{
    error_code code;
    std::uint32_t sdk_code = 0;
    std::string_view message; // Static diagnostics; constructing an OOM result must not allocate.
    std::source_location location = std::source_location::current();
};

template<class T>
using result = std::expected<T, error>;

struct tick_id
{
    std::uint64_t value = 0;
    auto operator<=>(const tick_id&) const = default;
};

struct scene_id
{
    std::uint64_t value = 0;
    auto operator<=>(const scene_id&) const = default;
};

template<class Tag>
struct handle
{
    scene_id scene;
    std::uint32_t slot = (std::numeric_limits<std::uint32_t>::max)();
    std::uint32_t generation = 0;
    explicit operator bool() const noexcept
    {
        return scene.value != 0 && generation != 0 && slot != (std::numeric_limits<std::uint32_t>::max)();
    }

    auto operator<=>(const handle&) const = default;
};

struct body_tag;
struct character_tag;
using body_handle = handle<body_tag>;
using character_handle = handle<character_tag>;
struct shape_id
{
    std::uint32_t value = 0;
    auto operator<=>(const shape_id&) const = default;
};

} // namespace ce::physics
