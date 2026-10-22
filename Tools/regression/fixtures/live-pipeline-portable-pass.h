#pragma once

// CPU feature-contract boundary only. The production frame context carries
// scene/material/native state that this test never accesses. The runner replaces
// EnhancedRenderPass.h in its temporary snapshot with these lifecycle signatures
// and the actual scalar fields consumed by EnhancedLivePipelineDesc.cpp.
#include "../../../Utility_Framework/StandardLibrary.h"

class EnhancedRenderGraph;
struct EnhancedFrameContext
{
    uint32_t width{}, height{}, displayWidth{}, displayHeight{}, viewFlags{};
};

class EnhancedRenderPass
{
  public:
    virtual ~EnhancedRenderPass() = default;
    virtual const char* GetName() const = 0;
    virtual bool Initialize(const EnhancedFrameContext&, std::string&) = 0;
    virtual bool PrepareFrame(const EnhancedFrameContext&, std::string&) { return true; }
    virtual void Declare(EnhancedRenderGraph&, const EnhancedFrameContext&) = 0;
    virtual void Shutdown() {}
};
