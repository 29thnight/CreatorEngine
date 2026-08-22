#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

enum class RenderBackend : std::uint8_t
{
    DX12,
    Vulkan,
};

inline const char* RenderBackendName(RenderBackend backend) noexcept
{
    return RenderBackend::Vulkan == backend ? "vulkan" : "dx12";
}

inline bool TryParseRenderBackend(std::string_view value,
    RenderBackend& outBackend) noexcept
{
    const auto equalsIgnoreCase = [](std::string_view left,
        std::string_view right) noexcept
    {
        if (left.size() != right.size()) return false;
        for (std::size_t index = 0; index < left.size(); ++index)
        {
            const auto lowerAscii = [](char character) noexcept
            {
                return character >= 'A' && character <= 'Z'
                    ? static_cast<char>(character - 'A' + 'a')
                    : character;
            };
            if (lowerAscii(left[index]) != lowerAscii(right[index])) return false;
        }
        return true;
    };

    if (equalsIgnoreCase(value, "dx12"))
    {
        outBackend = RenderBackend::DX12;
        return true;
    }
    if (equalsIgnoreCase(value, "vulkan"))
    {
        outBackend = RenderBackend::Vulkan;
        return true;
    }
    return false;
}
