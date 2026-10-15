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
                {
                    return false;
                }
                count = (std::max)(count, resource.slot + 1);
            }
            return true;
        }

        bool MatchesInstanceUniforms(const BindingLayout& layout, std::span<const std::uint8_t> prepared,
            std::span<const std::uint8_t> instance)
        {
            if (prepared.size() != instance.size())
            {
                return false;
            }
            // PrepareResources has already validated these ranges and written
            // them from exact uploaded formats. Every other byte must still
            // match the immutable instance, including padding and parameters.
            for (std::size_t index = 0; index < prepared.size(); ++index)
            {
                if (prepared[index] == instance[index])
                {
                    continue;
                }
                const bool encoding = std::ranges::any_of(layout.textureEncodings, [index](const auto& binding) {
                    return index >= binding.offset && index < static_cast<std::uint64_t>(binding.offset) + 4u;
                });
                if (!encoding)
                {
                    return false;
                }
            }
            return true;
        }

        bool MatchingSlots(const PassLayout& layout)
        {
            std::uint32_t next = layout.hostParameterCount;
            for (const auto& [slot, used] : {std::pair{layout.uniformSlot, layout.material.uniformBytes != 0},
                                             std::pair{layout.textureSlot, !layout.material.textures.empty()},
                                             std::pair{layout.samplerSlot, !layout.material.samplers.empty()},
                                             std::pair{layout.descriptorIndexSlot,
                                                 !layout.material.textures.empty() || !layout.material.samplers.empty()}})
            {
                if (slot.has_value() != used || (used && *slot != next++))
                {
                    return false;
                }
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
        {
            return Fail(error, "Invalid material uniform size or texture/sampler slots.");
        }
        const auto constantConflict = [&](const RHIPipelineLayoutParam& parameter) {
            return ((material.uniformBytes && parameter.shaderRegister == UniformRegister) ||
                    ((textures || samplers) && parameter.shaderRegister == DescriptorIndexRegister)) &&
                   (parameter.kind == RHILayoutParamKind::ConstantBuffer ||
                    parameter.kind == RHILayoutParamKind::Constants);
        };
        for (const auto& parameter : hostParameters)
        {
            if (constantConflict(parameter))
            {
                return Fail(error, "Host constant binding overlaps LX material b2.");
            }
            if (parameter.kind == RHILayoutParamKind::ShaderResourceBuffer &&
                Overlaps(TextureRegister, textures ? BindlessTextureCapacity : 0, parameter.shaderRegister, 1))
            {
                return Fail(error, "Host SRV buffer overlaps LX material textures.");
            }
            if (parameter.kind != RHILayoutParamKind::DescriptorTable)
            {
                continue;
            }
            const auto& table = parameter.table;
            if ((table.type == RHIDescriptorType::ShaderResource && textures &&
                 Overlaps(TextureRegister, BindlessTextureCapacity, table.baseRegister, table.count)) ||
                (table.type == RHIDescriptorType::Sampler && samplers &&
                 Overlaps(SamplerRegister, BindlessSamplerCapacity, table.baseRegister, table.count)))
            {
                return Fail(error, "Host descriptor table overlaps LX material resources.");
            }
        }
        for (const auto& sampler : hostSamplers)
        {
            if (samplers && Overlaps(SamplerRegister, BindlessSamplerCapacity, sampler.shaderRegister, 1))
            {
                return Fail(error, "Host static sampler overlaps LX material samplers.");
            }
        }

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
            parameters.push_back(RHILayout::SrvTable(BindlessTextureCapacity, TextureRegister));
        }
        if (samplers)
        {
            candidate.samplerSlot = static_cast<std::uint32_t>(parameters.size());
            parameters.push_back(RHILayout::SamplerTable(BindlessSamplerCapacity, SamplerRegister));
        }
        if (textures || samplers)
        {
            candidate.descriptorIndexSlot = static_cast<std::uint32_t>(parameters.size());
            parameters.push_back(RHILayout::Cbv(DescriptorIndexRegister));
        }
        RHIPipelineLayoutDesc description;
        description.params = parameters;
        description.staticSamplers = hostSamplers;
        description.allowInputAssembler = inputAssembler;
        candidate.handle = cache.GetOrCreate(description, error);
        if (!candidate.handle.IsValid())
        {
            return Fail(error, error.empty() ? "Material pass layout creation failed." : error);
        }
        result = std::move(candidate);
        return true;
    }

    bool RenderBindingCache::Prepare(IRenderDeviceServices& device, IRenderTextureCache& textures,
                                     own::shared_owner<const Instance> instance, const PassLayout& layout,
                                     own::shared_owner<const RenderBindings>& result, std::string& error,
                                     own::shared_owner<InstanceFramePins> instancePins, const TextureFramePins* imagePins)
    {
        ce::profile_scope profile{ce::marker<"MaterialBindingsPrepare">()};
        error.clear();
        if ((device_ && device_ != &device) || !layout.handle.IsValid() || !instance || !instance->generation ||
            layout.material != instance->generation->cooked.product.layout)
        {
            return Fail(error, "Material render binding requires the matching device, instance and pass layout.");
        }
        if (!MatchingSlots(layout))
        {
            return Fail(error, "Material pass layout omits or adds a reflected binding slot.");
        }
        const auto recording = device.GetCurrentUploadRecordingId();
        if (recording == 0)
        {
            return Fail(error, "Material render binding requires an active frame recording.");
        }

        const auto descriptorVersion = device.GetDescriptorVersionToken();
        if (preparedRecording_ != recording || preparedDescriptorVersion_ != descriptorVersion)
        {
            prepared_.clear();
            preparedRecording_ = recording;
            preparedDescriptorVersion_ = descriptorVersion;
            bindless_ = std::make_shared<BindlessMaterialFrame>();
        }
        const auto identity = InstanceFramePins::Identity(*instance);
        // Both objects remain pinned during the address check. Keep forged or
        // copied representation IDs from bypassing resource validation.
        if (const auto found = prepared_.find(identity); found != prepared_.end())
        {
            for (const auto& cached : found->second)
            {
                if (auto packet = cached.lock(); packet && packet->layout == layout &&
                    std::addressof(*packet->instance) == std::addressof(*instance) && Validate(device, *packet, error))
                {
                    result = std::move(packet);
                    return true;
                }
            }
        }
        error.clear();

        auto candidate = own::make_shared<RenderBindings>();
        if (!instancePins)
        {
            auto pins = own::make_shared<InstanceFramePins>();
            pins->Retain(instance);
            instancePins = std::move(pins);
        }
        candidate->instancePinIndex = instancePins->Find(*instance);
        candidate->instance = instancePins->Borrow(candidate->instancePinIndex);
        if (!candidate->instance || std::addressof(*candidate->instance) != std::addressof(*instance))
        {
            return Fail(error, "Material bindings require the exact instance in the frame pin table.");
        }
        candidate->instancePins = std::move(instancePins);
        candidate->layout = layout;
        candidate->device = &device;
        candidate->recordingId = recording;
        std::vector<TextureBinding> uploaded;
        for (const auto& texture : candidate->instance->textures)
        {
            if (!texture.owner)
            {
                return Fail(error, "Material render texture has no CPU generation owner.");
            }
            const auto failures = textures.GetUploadFailureCount();
            const auto* descriptor = &*texture.owner;
            const auto image = imagePins ? imagePins->Image(descriptor) : descriptor->NonRehydratableImage();
            const auto entry = textures.GetOrUpload(descriptor, image, error);
            if (!entry.IsValid() || !error.empty() || textures.GetUploadFailureCount() != failures)
            {
                return Fail(error,
                            error.empty() ? "LX material texture upload failed; neutral substitution rejected." : error);
            }
            uploaded.push_back({texture.slot, entry, texture.owner});
        }
        std::vector<LX::LXMaterialDiagnostic> diagnostics;
        const bool prepared = candidate->instance->generation->cooked.product.materialShader
            ? PrepareResourcesWithUniforms(layout.material, candidate->instance->uniforms, uploaded, candidate->resources, diagnostics)
            : PrepareResources(layout.material, candidate->instance->description.parameters, uploaded, candidate->resources, diagnostics);
        if (!prepared || !MatchesInstanceUniforms(layout.material,
            candidate->resources.uniforms, candidate->instance->uniforms))
        {
            return Fail(error, diagnostics.empty() ? "Material instance differs from its reflected uniform layout."
                                                   : diagnostics.front().message);
        }

        // The exact immutable instance already owns every texture dependency.
        // A binding packet borrows that closure through its frame table rather
        // than duplicating texture strong references for every draw/pass.
        candidate->resources.owners.clear();
        std::array<std::uint32_t, 128> descriptorIndices{};
        if (layout.samplerSlot)
        {
            // The cooker accepts exactly these 36 filter/address combinations.
            // Stable indices are independent of instance order and frame lifetime.
            const std::array modes{RHIAddressMode::Wrap, RHIAddressMode::Clamp, RHIAddressMode::Mirror};
            if (!bindlessSamplers_.IsValid())
            {
                std::array<RHISamplerDesc, BindlessSamplerCapacity> descriptions;
                for (unsigned filter = 0; filter < 2; ++filter)
                    for (unsigned mip = 0; mip < 2; ++mip)
                        for (unsigned u = 0; u < 3; ++u)
                            for (unsigned v = 0; v < 3; ++v)
                            {
                                auto& desc = descriptions[((filter * 2 + mip) * 3 + u) * 3 + v];
                                desc.minMag = filter ? RHIFilterMode::Linear : RHIFilterMode::Point;
                                desc.mip = mip ? RHIFilterMode::Linear : RHIFilterMode::Point;
                                desc.addressU = desc.addressW = modes[u];
                                desc.addressV = modes[v];
                            }
                bindlessSamplers_ = device.CreateSamplers(descriptions);
                if (!bindlessSamplers_.IsValid())
                {
                    return Fail(error, "LX bindless sampler table allocation failed.");
                }
                device_ = &device;
            }
            candidate->samplers = bindlessSamplers_;
            for (const auto& resource : layout.material.samplers)
            {
                const auto& sampler = candidate->resources.samplers[resource.slot];
                const auto u = std::ranges::find(modes, sampler.addressU) - modes.begin();
                const auto v = std::ranges::find(modes, sampler.addressV) - modes.begin();
                if (u == modes.size() || v == modes.size()) return Fail(error, "Invalid LX bindless sampler.");
                descriptorIndices[64 + resource.slot] = static_cast<std::uint32_t>(
                    (((sampler.minMag == RHIFilterMode::Linear ? 1 : 0) * 2 +
                      (sampler.mip == RHIFilterMode::Linear ? 1 : 0)) * 3 + u) * 3 + v);
            }
        }
        if (layout.textureSlot)
        {
            candidate->bindless = bindless_;
            std::lock_guard lock(bindless_->mutex);
            for (const auto& resource : layout.material.textures)
            {
                const auto& desc = candidate->resources.textures[resource.slot];
                const auto found = std::ranges::find_if(bindless_->descriptions, [&](const auto& existing) {
                    return existing.resource == desc.resource && existing.format == desc.format &&
                        existing.mostDetailedMip == desc.mostDetailedMip && existing.mipLevels == desc.mipLevels;
                });
                const auto index = found - bindless_->descriptions.begin();
                if (index == BindlessTextureCapacity)
                    return Fail(error, "LX frame exceeds bindless texture capacity; no per-material fallback.");
                if (found == bindless_->descriptions.end()) bindless_->descriptions.push_back(desc);
                descriptorIndices[resource.slot] = static_cast<std::uint32_t>(index);
            }
        }
        if (layout.descriptorIndexSlot)
        {
            candidate->descriptorIndices = device.UploadConstants(descriptorIndices.data(), sizeof(descriptorIndices));
            if (!candidate->descriptorIndices.IsValid()) return Fail(error, "LX descriptor index upload failed.");
        }
        if (layout.uniformSlot)
        {
            candidate->uniforms =
                device.UploadConstants(candidate->resources.uniforms.data(), candidate->resources.uniforms.size());
            if (!candidate->uniforms.IsValid())
            {
                return Fail(error, "LX material uniform upload allocation failed.");
            }
        }
        device_ = &device;
        candidate->descriptorVersion = device.GetDescriptorVersionToken();
        prepared_[identity].push_back(candidate);
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
            std::lock_guard lock(source.bindless->mutex);
            auto& frame = *source.bindless;
            if (frame.publishedSize != frame.descriptions.size())
            {
                auto descriptions = frame.descriptions;
                descriptions.resize(BindlessTextureCapacity, RHIBindingDesc::Srv2D({}, RHIFormat::RGBA8Unorm).OrNull());
                frame.textures = device.CreateBindings(descriptions);
                if (!frame.textures.IsValid()) return Fail(error, "LX bindless texture table allocation failed.");
                frame.publishedSize = frame.descriptions.size();
            }
            encoder.SetBindings(point, *layout.textureSlot, frame.textures);
        }
        if (layout.samplerSlot)
        {
            encoder.SetSamplers(point, *layout.samplerSlot, source.samplers);
        }
        if (layout.descriptorIndexSlot)
            encoder.SetConstantBuffer(point, *layout.descriptorIndexSlot, source.descriptorIndices);
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
        {
            return Fail(error, "Material render bindings belong to a different frame recording.");
        }
        const auto& layout = bindings.layout;
        if ((layout.uniformSlot && !bindings.uniforms.IsValid()) || (layout.textureSlot && !bindings.bindless) ||
            (layout.samplerSlot && !bindings.samplers.IsValid()) ||
            (layout.descriptorIndexSlot && !bindings.descriptorIndices.IsValid()))
        {
            return Fail(error, "Material render bindings contain an invalid upload or descriptor table.");
        }
        return true;
    }

    bool RenderBindingCache::Bind(IRenderDeviceServices& device, RHIEncoder& encoder, RHIBindPoint point,
                                  const RenderBindings& bindings, std::string& error)
    {
        return BindPass(device, encoder, point, bindings, bindings.layout, error);
    }

    void RenderBindingCache::Clear()
    {
        prepared_.clear();
        bindless_.reset();
        bindlessSamplers_ = {};
        preparedRecording_ = preparedDescriptorVersion_ = 0;
        device_ = nullptr;
    }
} // namespace material_graph
