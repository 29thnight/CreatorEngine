#pragma once

#include <compare>
#include <cstdint>
#include <expected>
#include <string_view>

namespace ce::layers
{
enum class error
{
    invalid_definition,
    unknown_layer,
    duplicate,
    reserved_layer,
    capacity_exceeded,
    wrong_owner,
    wrong_scene,
    out_of_memory,
    io_failure,
    corrupt_data
};

template<class T>
using result = std::expected<T, error>;

struct layer_id
{
    std::uint64_t value = 0;
    auto operator<=>(const layer_id&) const = default;
};

inline constexpr layer_id default_layer{1};

class layer_slot final
{
  public:
    layer_slot() = default;

    static constexpr result<layer_slot> Make(std::uint32_t value) noexcept
    {
        if (value >= 32)
            return std::unexpected(error::capacity_exceeded);

        return layer_slot(static_cast<std::uint8_t>(value));
    }

    constexpr std::uint8_t Value() const noexcept { return m_value; }
    constexpr std::uint32_t Mask() const noexcept { return std::uint32_t{1} << m_value; }
    auto operator<=>(const layer_slot&) const = default;

  private:
    explicit constexpr layer_slot(std::uint8_t value) : m_value(value) {}

    std::uint8_t m_value = 0;
};
inline constexpr std::size_t max_layer_name_bytes = 1024;

inline bool ValidLayerName(std::string_view name) noexcept
{
    // Reject NUL/control characters and malformed/overlong UTF-8 before names reach UI/files.
    if (name.empty())
        return false;

    for (std::size_t i = 0; i < name.size();)
    {
        const auto first = static_cast<unsigned char>(name[i++]);
        if (first < 0x20 || first == 0x7f)
            return false;

        if (first < 0x80)
            continue;

        const int continuation = first >= 0xc2 && first <= 0xdf   ? 1
                                 : first >= 0xe0 && first <= 0xef ? 2
                                 : first >= 0xf0 && first <= 0xf4 ? 3
                                                                  : -1;
        if (continuation < 0 || name.size() - i < static_cast<std::size_t>(continuation))
            return false;

        std::uint32_t code = first & ((1u << (6 - continuation)) - 1);
        for (int j = 0; j < continuation; ++j)
        {
            const auto byte = static_cast<unsigned char>(name[i++]);
            if ((byte & 0xc0) != 0x80)
                return false;

            code = (code << 6) | (byte & 0x3f);
        }

        if ((continuation == 2 && code < 0x800) || (continuation == 3 && code < 0x10000) || code > 0x10ffff ||
            (code >= 0xd800 && code <= 0xdfff))
            return false;
    }

    return true;
}

} // namespace ce::layers
