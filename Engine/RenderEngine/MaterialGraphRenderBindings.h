#pragma once

#include "MaterialGraphRuntime.h"
#include "RHI/IRenderDeviceServices.h"
#include "RHI/IRenderTextureCache.h"
#include "RHI/RHIEncoder.h"

#include <optional>

namespace material_graph
{
struct PassLayout
{
    RHIPipelineLayoutHandle handle;
    BindingLayout material;
    std::uint32_t hostParameterCount{};
    std::optional<std::uint32_t> uniformSlot;
    std::optional<std::uint32_t> textureSlot;
    std::optional<std::uint32_t> samplerSlot;
};

// Host ranges are kept intact. Material ranges are appended only when used;
// conflicting host constants, SRVs or static/dynamic samplers are rejected.
bool CreatePassLayout(IRenderRootSignatureCache& cache, const BindingLayout& material,
                      std::span<const RHIPipelineLayoutParam> hostParameters,
                      std::span<const RHIStaticSamplerDesc> hostSamplers, bool inputAssembler, PassLayout& result,
                      std::string& error);

struct RenderBindings
{
    std::shared_ptr<const Instance> instance;
    PassLayout layout;
    ResourcePacket resources;
    RHIBufferSlice uniforms;
    RHIBindingTable textures;
    RHISamplerTable samplers;
    const IRenderDeviceServices* device{};
    std::uint64_t recordingId{};
    std::uint64_t descriptorVersion{};
};

// One render owner per device. Texture cache owns GPU registrations and their
// fence retirement; the packet retains CPU/program owners through submission.
// Rebuild packets every recording so texture-cache use tracking stays current.
// RenderGraph declares shader reads/transitions; Bind does not change states.
class RenderBindingCache
{
  public:
    bool Prepare(IRenderDeviceServices& device, IRenderTextureCache& textures, std::shared_ptr<const Instance> instance,
                 const PassLayout& layout, std::shared_ptr<const RenderBindings>& result, std::string& error);

    // Call after SetPipeline with this packet's layout, before a draw/dispatch.
    // Frame upload/descriptor handles cannot be reused in another recording.
    static bool Bind(IRenderDeviceServices& device, RHIEncoder& encoder, RHIBindPoint point,
                     const RenderBindings& bindings, std::string& error);
    static bool Validate(const IRenderDeviceServices& device, const RenderBindings& bindings, std::string& error);
    // A second pass in the same recording can consume the same material uploads
    // with another host root layout. No cross-frame descriptor reuse is allowed.
    static bool RebindPass(const IRenderDeviceServices& device, const RenderBindings& source,
                           const PassLayout& layout, std::shared_ptr<const RenderBindings>& result,
                           std::string& error);

    // Clear after device idle, before device recreation. Sampler registrations
    // are device-owned; the RHI has no per-table release operation.
    void Clear();
    std::size_t SamplerTableCount() const { return samplers_.size(); }

  private:
    IRenderDeviceServices* device_{};
    std::map<std::vector<std::string>, RHISamplerTable> samplers_;
};
} // namespace material_graph
