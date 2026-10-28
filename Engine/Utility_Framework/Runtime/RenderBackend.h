#if defined(CE_CONTRACT_STANDALONE)
#include "../../../Tools/build/ModuleAdapters/Creator.Core.Strings.h"
#else
import Creator.Core.Strings;
#endif
#pragma once

#include "../Foundation/StandardLibrary.h"

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


    if (ce::strings::equals_ascii_case_insensitive(value, "dx12"))
    {
        outBackend = RenderBackend::DX12;
        return true;
    }
    if (ce::strings::equals_ascii_case_insensitive(value, "vulkan"))
    {
        outBackend = RenderBackend::Vulkan;
        return true;
    }
    return false;
}
