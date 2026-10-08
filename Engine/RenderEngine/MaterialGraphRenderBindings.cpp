#include "../EngineDiagnostics/ProfileScope.h"
#include "MaterialGraphRenderBindings.h"

#include <algorithm>
#include <set>

namespace material_graph
{
namespace
{
bool Fail(std::string& error, std::string message)
{
    error = std::move(message);
    return false;
}

bool Overlaps(std::uint32_t first, std::uint32_t count, std::uint32_t other, std::uint32_t otherCount)
{
    return count && otherCount && std::uint64_t(first) < std::uint64_t(other) + otherCount &&
           std::uint64_t(other) < std::uint64_t(first) + count;
}

bool ResourceCount(std::span<const LX::LXMaterialResource> resources, std::uint32_t limit, std::uint32_t& count)
{
    std::set<std::uint32_t> slots;
    count = 0;
    for (const auto& resource : resources)
    {
        if (resource.slot >= limit || !slots.insert(resource.slot).second)
            return false;
        count = (std::max)(count, resource.slot + 1);
    }
    return true;
}

bool MatchingSlots(const PassLayout& layout)
{
    std::uint32_t next = layout.hostParameterCount;
    for (const auto& [slot, used] : {std::pair{layout.uniformSlot, layout.material.uniformBytes != 0},
                                     std::pair{layout.textureSlot, !layout.material.textures.empty()},
                                     std::pair{layout.samplerSlot, !layout.material.samplers.empty()}})
    {
        if (slot.has_value() != used || (used && *slot != next++))
            return false;
    }
    return next <= 64;
}
} // namespace

bool CreatePassLayout(IRenderRootSignatureCache& cache, const BindingLayout& material,
                      std::span<const RHIPipelineLayoutParam> hostParameters,
                      std::span<const RHIStaticSamplerDesc> hostSamplers, bool inputAssembler, PassLayout& result,
                      std::string& error)
{
    error.clear();
    std::uint32_t textures{}, samplers{};
    if (hostParameters.size() > 64 || material.uniformBytes > 65536 ||
        !ResourceCount(material.textures, 64, textures) || !ResourceCount(material.samplers, 64, samplers))
        return Fail(error, "Invalid material uniform size or texture/sampler slots.");
    const auto constantConflict = [&](const RHIPipelineLayoutParam& parameter) {
        return material.uniformBytes && parameter.shaderRegister == UniformRegister &&
               (parameter.kind == RHILayoutParamKind::ConstantBuffer ||
                parameter.kind == RHILayoutParamKind::Constants);
    };
    for (const auto& parameter : hostParameters)
    {
        if (constantConflict(parameter))
            return Fail(error, "Host constant binding overlaps LX material b2.");
        if (parameter.kind == RHILayoutParamKind::ShaderResourceBuffer &&
            Overlaps(TextureRegister, textures, parameter.shaderRegister, 1))
            return Fail(error, "Host SRV buffer overlaps LX material textures.");
        if (parameter.kind != RHILayoutParamKind::DescriptorTable)
            continue;
        const auto& table = parameter.table;
        if ((table.type == RHIDescriptorType::ShaderResource && textures &&
             Overlaps(TextureRegister, textures, table.baseRegister, table.count)) ||
            (table.type == RHIDescriptorType::Sampler && samplers &&
             Overlaps(SamplerRegister, samplers, table.baseRegister, table.count)))
            return Fail(error, "Host descriptor table overlaps LX material resources.");
    }
    for (const auto& sampler : hostSamplers)
        if (samplers && Overlaps(SamplerRegister, samplers, sampler.shaderRegister, 1))
            return Fail(error, "Host static sampler overlaps LX material samplers.");

    PassLayout candidate;
    candidate.material = material;
    candidate.hostParameterCount = static_cast<std::uint32_t>(hostParameters.size());
    std::vector<RHIPipelineLayoutParam> parameters(hostParameters.begin(), hostParameters.end());
    if (material.uniformBytes)
    {
        candidate.uniformSlot = static_cast<std::uint32_t>(parameters.size());
        parameters.push_back(RHILayout::Cbv(UniformRegister));
    }
    if (textures)
    {
        candidate.textureSlot = static_cast<std::uint32_t>(parameters.size());
        parameters.push_back(RHILayout::SrvTable(textures, TextureRegister));
    }
    if (samplers)
    {
        candidate.samplerSlot = static_cast<std::uint32_t>(parameters.size());
        parameters.push_back(RHILayout::SamplerTable(samplers, SamplerRegister));
    }
    RHIPipelineLayoutDesc description;
    description.params = parameters;
    description.staticSamplers = hostSamplers;
    description.allowInputAssembler = inputAssembler;
    candidate.handle = cache.GetOrCreate(description, error);
    if (!candidate.handle.IsValid())
        return Fail(error, error.empty() ? "Material pass layout creation failed." : error);
    result = std::move(candidate);
    return true;
}

bool RenderBindingCache::Prepare(IRenderDeviceServices& device, IRenderTextureCache& textures,
                                 std::shared_ptr<const Instance> instance, const PassLayout& layout,
                                 std::shared_ptr<const RenderBindings>& result, std::string& error)
{
    ce::profile_scope profile{ce::marker<"MaterialBindingsPrepare">()};
    error.clear();
    if ((device_ && device_ != &device) || !layout.handle.IsValid() || !instance || !instance->generation ||
        layout.material != instance->generation->cooked.product.layout)
        return Fail(error, "Material render binding requires the matching device, instance and pass layout.");
    if (!MatchingSlots(layout))
        return Fail(error, "Material pass layout omits or adds a reflected binding slot.");
    const auto recording = device.GetCurrentUploadRecordingId();
    if (recording == 0)
        return Fail(error, "Material render binding requires an active frame recording.");

    const auto descriptorVersion = device.GetDescriptorVersionToken();
    if (preparedRecording_ != recording || preparedDescriptorVersion_ != descriptorVersion)
    {
        prepared_.clear();
        preparedRecording_ = recording;
        preparedDescriptorVersion_ = descriptorVersion;
    }
    if (const auto found = prepared_.find(instance.get()); found != prepared_.end())
    {
        for (const auto& cached : found->second)
        {
            if (auto packet = cached.lock(); packet && packet->layout == layout && Validate(device, *packet, error))
            {
                result = std::move(packet);
                return true;
            }
        }
    }

    auto candidate = std::make_shared<RenderBindings>();
    candidate->instance = std::move(instance);
    candidate->layout = layout;
    candidate->device = &device;
    candidate->recordingId = recording;
    std::vector<TextureBinding> uploaded;
    for (const auto& texture : candidate->instance->textures)
    {
        if (!texture.owner)
            return Fail(error, "Material render texture has no CPU generation owner.");
        const auto failures = textures.GetUploadFailureCount();
        const auto entry = textures.GetOrUpload(texture.owner.get(), error);
        if (!entry.IsValid() || !error.empty() || textures.GetUploadFailureCount() != failures)
            return Fail(error,
                        error.empty() ? "LX material texture upload failed; neutral substitution rejected." : error);
        uploaded.push_back({texture.slot, entry, texture.owner});
    }
    std::vector<LX::LXMaterialDiagnostic> diagnostics;
    const bool prepared = candidate->instance->generation->cooked.product.materialShader
        ? PrepareResourcesWithUniforms(layout.material, candidate->instance->uniforms, uploaded, candidate->resources, diagnostics)
        : PrepareResources(layout.material, candidate->instance->description.parameters, uploaded, candidate->resources, diagnostics);
    if (!prepared ||
        candidate->resources.uniforms != candidate->instance->uniforms)
        return Fail(error, diagnostics.empty() ? "Material instance differs from its reflected uniform layout."
                                               : diagnostics.front().message);

    if (layout.samplerSlot)
    {
        std::vector<std::string> key(candidate->resources.samplers.size(), "nearest-clamp");
        for (const auto& resource : layout.material.samplers)
            key[resource.slot] = resource.reference;
        if (const auto found = samplers_.find(key); found != samplers_.end())
            candidate->samplers = found->second;
        else
        {
            candidate->samplers = device.CreateSamplers(candidate->resources.samplers);
            if (!candidate->samplers.IsValid())
                return Fail(error, "LX material sampler table allocation failed.");
            samplers_.emplace(std::move(key), candidate->samplers);
            device_ = &device;
        }
    }
    if (layout.textureSlot)
    {
        candidate->textures = device.CreateBindings(candidate->resources.textures);
        if (!candidate->textures.IsValid())
            return Fail(error, "LX material texture descriptor allocation failed.");
    }
    if (layout.uniformSlot)
    {
        candidate->uniforms =
            device.UploadConstants(candidate->resources.uniforms.data(), candidate->resources.uniforms.size());
        if (!candidate->uniforms.IsValid())
            return Fail(error, "LX material uniform upload allocation failed.");
    }
    device_ = &device;
    candidate->descriptorVersion = device.GetDescriptorVersionToken();
    prepared_[candidate->instance.get()].push_back(candidate);
    result = std::move(candidate);
    return true;
}

bool RenderBindingCache::ValidatePass(const IRenderDeviceServices& device, const RenderBindings& source,
                                      const PassLayout& layout, std::string& error)
{
    if (!Validate(device, source, error))
    {
        return false;
    }
    if (!layout.handle.IsValid() || !MatchingSlots(layout) || layout.material != source.layout.material)
    {
        return Fail(error, "Material pass rebind requires the same reflected resources and a valid host layout.");
    }
    return true;
}

bool RenderBindingCache::BindPass(IRenderDeviceServices& device, RHIEncoder& encoder, RHIBindPoint point,
                                  const RenderBindings& source, const PassLayout& layout, std::string& error)
{
    if (!ValidatePass(device, source, layout, error))
    {
        return false;
    }
    if (layout.uniformSlot)
    {
        encoder.SetConstantBuffer(point, *layout.uniformSlot, source.uniforms);
    }
    if (layout.textureSlot)
    {
        encoder.SetBindings(point, *layout.textureSlot, source.textures);
    }
    if (layout.samplerSlot)
    {
        encoder.SetSamplers(point, *layout.samplerSlot, source.samplers);
    }
    return true;
}

bool RenderBindingCache::Validate(const IRenderDeviceServices& device, const RenderBindings& bindings,
                                  std::string& error)
{
    error.clear();
    if (bindings.device != &device || !bindings.instance || !bindings.layout.handle.IsValid() ||
        !MatchingSlots(bindings.layout) || bindings.recordingId == 0 ||
        bindings.recordingId != device.GetCurrentUploadRecordingId() ||
        bindings.descriptorVersion != device.GetDescriptorVersionToken())
        return Fail(error, "Material render bindings belong to a different frame recording.");
    const auto& layout = bindings.layout;
    if ((layout.uniformSlot && !bindings.uniforms.IsValid()) || (layout.textureSlot && !bindings.textures.IsValid()) ||
        (layout.samplerSlot && !bindings.samplers.IsValid()))
        return Fail(error, "Material render bindings contain an invalid upload or descriptor table.");
    return true;
}

bool RenderBindingCache::Bind(IRenderDeviceServices& device, RHIEncoder& encoder, RHIBindPoint point,
                              const RenderBindings& bindings, std::string& error)
{
    if (!Validate(device, bindings, error))
    {
        return false;
    }
    const auto& layout = bindings.layout;
    if (layout.uniformSlot)
        encoder.SetConstantBuffer(point, *layout.uniformSlot, bindings.uniforms);
    if (layout.textureSlot)
        encoder.SetBindings(point, *layout.textureSlot, bindings.textures);
    if (layout.samplerSlot)
        encoder.SetSamplers(point, *layout.samplerSlot, bindings.samplers);
    return true;
}

void RenderBindingCache::Clear()
{
    samplers_.clear();
    prepared_.clear();
    preparedRecording_ = preparedDescriptorVersion_ = 0;
    device_ = nullptr;
}
} // namespace material_graph
