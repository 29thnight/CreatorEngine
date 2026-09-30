#pragma once

#include <cstdint>

namespace LX
{
enum class LXMaterialExecutionRoute : uint8_t
{
    Deferred = 1,
    Forward = 2
};

enum class LXMaterialExecutionError : uint8_t
{
    None,
    UnknownFeatures,
    MissingCoreIor,
    SpecialRequiresForward,
    MissingRefraction,
    MissingSubsurface,
    MissingVolume
};

struct LXMaterialExecutionRequirements
{
    uint32_t features{};
    bool forward{};
    bool refraction{};
    bool subsurface{};
    bool volume{};
    LXMaterialExecutionError error{};
};

struct LXMaterialExecutionResources
{
    bool refraction{};
    bool subsurface{};
    bool volume{};
};

// Execution requirements only. Graph feature extraction and the product's
// cook/pass selection consume this contract in MAT-6/MAT-7.
inline LXMaterialExecutionRequirements LXAnalyzeMaterialExecution(uint32_t features)
{
    LXMaterialExecutionRequirements result;
    result.features = features;
    result.refraction = (features & 0x0800u) != 0;
    result.subsurface = (features & 0x1000u) != 0;
    result.volume = (features & 0x2000u) != 0;
    result.forward = result.refraction || result.subsurface || result.volume;
    if ((features & ~0x3FFFu) != 0)
    {
        result.error = LXMaterialExecutionError::UnknownFeatures;
    }
    else if ((features & 0x1F80u) != 0 && (features & 0x0040u) == 0)
    {
        result.error = LXMaterialExecutionError::MissingCoreIor;
    }
    return result;
}

inline LXMaterialExecutionError LXValidateMaterialExecution(const LXMaterialExecutionRequirements& requirements,
                                                            LXMaterialExecutionRoute route,
                                                            const LXMaterialExecutionResources& resources)
{
    if (requirements.error != LXMaterialExecutionError::None)
    {
        return requirements.error;
    }
    if (requirements.forward && route != LXMaterialExecutionRoute::Forward)
    {
        return LXMaterialExecutionError::SpecialRequiresForward;
    }
    if (requirements.refraction && !resources.refraction)
    {
        return LXMaterialExecutionError::MissingRefraction;
    }
    if (requirements.subsurface && !resources.subsurface)
    {
        return LXMaterialExecutionError::MissingSubsurface;
    }
    if (requirements.volume && !resources.volume)
    {
        return LXMaterialExecutionError::MissingVolume;
    }
    return LXMaterialExecutionError::None;
}
} // namespace LX
