#pragma once

#include "../../Utility_Framework/StandardLibrary.h"
#include <stdint.h>
#include <stdlib.h>

enum class RHICpuExecutor : uint8_t
{
    NativeParallel,
    SoftwareParallel
};

enum class RHIGpuQueuePolicy : uint8_t
{
    PrimaryGraphics,
    OwnedGraphics,
    GraphicsCompute
};

// CPU encoding and GPU placement are independent choices. In particular,
// software recording does not imply async compute or an owned GPU queue.
struct RHIExecutionPolicy
{
    RHICpuExecutor cpu{RHICpuExecutor::NativeParallel};
    RHIGpuQueuePolicy gpu{RHIGpuQueuePolicy::PrimaryGraphics};

    bool operator==(const RHIExecutionPolicy&) const = default;
};

struct RHIExecutionCapabilities
{
    bool ownedGraphics{false};
    bool asyncCompute{false};
};

enum class RHIQueuePolicyFallback : uint8_t
{
    None,
    OwnedQueuesUnavailable,
    ComputeQueueUnavailable
};

struct RHIResolvedExecutionPolicy
{
    RHIExecutionPolicy requested;
    RHIExecutionPolicy effective;
    RHIQueuePolicyFallback fallback{RHIQueuePolicyFallback::None};
};

constexpr RHIResolvedExecutionPolicy ResolveRHIExecutionPolicy(RHIExecutionPolicy requested,
    RHIExecutionCapabilities capabilities)
{
    RHIResolvedExecutionPolicy result{requested, requested};
    if (requested.gpu != RHIGpuQueuePolicy::PrimaryGraphics && !capabilities.ownedGraphics)
    {
        result.effective.gpu = RHIGpuQueuePolicy::PrimaryGraphics;
        result.fallback = RHIQueuePolicyFallback::OwnedQueuesUnavailable;
    }
    else if (requested.gpu == RHIGpuQueuePolicy::GraphicsCompute && !capabilities.asyncCompute)
    {
        result.effective.gpu = RHIGpuQueuePolicy::OwnedGraphics;
        result.fallback = RHIQueuePolicyFallback::ComputeQueueUnavailable;
    }
    return result;
}

constexpr RHIExecutionPolicy ParseRHIExecutionPolicy(std::string_view software, std::string_view queues)
{
    return {software == "1" ? RHICpuExecutor::SoftwareParallel : RHICpuExecutor::NativeParallel,
        queues == "2" ? RHIGpuQueuePolicy::GraphicsCompute : queues == "1" ? RHIGpuQueuePolicy::OwnedGraphics
                                                                        : RHIGpuQueuePolicy::PrimaryGraphics};
}

inline RHIExecutionPolicy ReadRHIExecutionPolicy()
{
#ifdef _WIN32
    char* software{};
    char* queues{};
    size_t softwareLength{};
    size_t queueLength{};
    _dupenv_s(&software, &softwareLength, "CREATOR_RHI_SOFTWARE_COMMANDS");
    _dupenv_s(&queues, &queueLength, "CREATOR_RENDERGRAPH_QUEUE_EXECUTION");
    const auto policy = ParseRHIExecutionPolicy(software ? software : "", queues ? queues : "");
    std::free(software);
    std::free(queues);
    return policy;
#else
    const char* software = std::getenv("CREATOR_RHI_SOFTWARE_COMMANDS");
    const char* queues = std::getenv("CREATOR_RENDERGRAPH_QUEUE_EXECUTION");
    return ParseRHIExecutionPolicy(software ? software : "", queues ? queues : "");
#endif
}
