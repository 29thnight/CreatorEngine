#pragma once
#include "RHIResourceTypes.h"
#include <memory>

// Backend owns the native heap. Graph retains it until every placed resource
// has been released after GPU completion. Imported/history resources never enter.
struct RHITransientResourceDesc
{
    bool buffer{false};
    RHIBufferDesc bufferDesc{};
    RHITextureDesc textureDesc{};
};

struct RHITransientAllocationInfo
{
    uint64_t bytes{0};
    uint64_t alignment{0};
    uint32_t heapClass{0}; // Backend compatibility class, not a native flag.
};

class RHITransientHeap
{
public:
    virtual ~RHITransientHeap() = default;
};
