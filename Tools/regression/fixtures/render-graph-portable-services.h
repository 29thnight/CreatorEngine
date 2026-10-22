#pragma once

// Native-device boundary for portable CPU tests. Graph compilation, feature
// declaration and software command handling continue to use production code.
// These services retain descriptors and capture encoder calls; they do not
// allocate GPU memory or claim that native rendering has been verified.
#include "Engine/RenderEngine/RHI/IRenderDeviceServices.h"
#include "Engine/RenderEngine/RHI/RHIEncoder.h"

namespace ce::regression
{
    class PortableRenderEncoder final : public RHIEncoder
    {
      public:
        std::vector<RHITransition> textureTransitions;
        std::vector<RHIBufferTransition> bufferTransitions;
        std::vector<RHITextureHandle> uavTextures, aliasTextures;
        std::vector<RHIBufferHandle> uavBuffers, aliasBuffers;
        uint64_t dispatches{}, draws{}, copies{};

        void Reset()
        {
            textureTransitions.clear();
            bufferTransitions.clear();
            uavTextures.clear();
            uavBuffers.clear();
            aliasTextures.clear();
            aliasBuffers.clear();
            dispatches = draws = copies = 0;
        }
        void SetViewportAndScissor(uint32_t, uint32_t) override {}
        void SetPipeline(RHIBindPoint, RHIPipelineHandle) override {}
        void SetPrimitiveTopology(RHIPrimitiveTopology) override {}
        void SetBindings(RHIBindPoint, uint32_t, const RHIBindingTable&) override {}
        void SetSamplers(RHIBindPoint, uint32_t, const RHISamplerTable&) override {}
        void SetConstantBuffer(RHIBindPoint, uint32_t, const RHIBufferSlice&) override {}
        void SetRootBuffer(RHIBindPoint, uint32_t, const RHIBufferSlice&) override {}
        void SetVertexBuffer(const RHIBufferSlice&, uint32_t) override {}
        void SetIndexBuffer(const RHIBufferSlice&, RHIFormat) override {}
        void Draw(uint32_t, uint32_t, uint32_t = 0, uint32_t = 0) override { ++draws; }
        void DrawIndexed(uint32_t, uint32_t, uint32_t = 0, int32_t = 0, uint32_t = 0) override { ++draws; }
        void Dispatch(uint32_t, uint32_t, uint32_t) override { ++dispatches; }
        uint64_t GetComputeDispatchCount() const override { return dispatches; }
        void BindRenderTargets(const RHIRenderTargetBinding&) override {}
        void ClearRenderTargets(const RHIRenderTargetBinding&, const float[4]) override {}
        void ClearDepthTarget(const RHIRenderTargetBinding&, float) override {}
        void ResourceBarriers(const RHIBarrierBatch& barriers) override
        {
            textureTransitions.insert(textureTransitions.end(), barriers.textureTransitions.begin(),
                                      barriers.textureTransitions.end());
            bufferTransitions.insert(bufferTransitions.end(), barriers.bufferTransitions.begin(),
                                     barriers.bufferTransitions.end());
            uavTextures.insert(uavTextures.end(), barriers.uavTextures.begin(), barriers.uavTextures.end());
            uavBuffers.insert(uavBuffers.end(), barriers.uavBuffers.begin(), barriers.uavBuffers.end());
            aliasTextures.insert(aliasTextures.end(), barriers.aliasTextures.begin(), barriers.aliasTextures.end());
            aliasBuffers.insert(aliasBuffers.end(), barriers.aliasBuffers.begin(), barriers.aliasBuffers.end());
        }
        void UavBarrier(std::span<const RHITextureHandle> handles) override
        {
            uavTextures.insert(uavTextures.end(), handles.begin(), handles.end());
        }
        void UavBarrierBuffers(std::span<const RHIBufferHandle> handles) override
        {
            uavBuffers.insert(uavBuffers.end(), handles.begin(), handles.end());
        }
        void CopyResource(RHITextureHandle, RHITextureHandle) override { ++copies; }
        void ClearUnorderedAccess(const RHIBindingDesc&, const float[4]) override {}
        void CopyToReadback(const RHIReadback&, RHITextureHandle, uint32_t = 0, uint32_t = 0) override { ++copies; }
        void CopyVolumeToReadback(const RHIReadback&, RHITextureHandle, uint32_t = 0) override { ++copies; }
        void CopyPartialToReadback(const RHIReadback&, RHITextureHandle, uint32_t = 0, uint32_t = 0) override
        {
            ++copies;
        }
        void CopyBufferToReadback(const RHIReadback&, RHIBufferHandle, uint64_t = 0, uint64_t = 0) override
        {
            ++copies;
        }
        void CopyTexture(RHITextureHandle, RHITextureHandle, uint32_t = 0, uint32_t = 0) override { ++copies; }
        void ClearRenderTargetRect(const RHIRenderTargetBinding&, const float[4], const RHIRect&) override {}
    };

    class PortableRenderServices : public IRenderDeviceServices
    {
      public:
        PortableRenderEncoder encoder;
        uint64_t recordingId{1};
        uint64_t descriptorVersion{1};
        bool retainTextureStates{true};
        RHISubresourceCapabilities subresourceCapabilities{};
        std::vector<RHITextureDesc> textures{1};
        std::vector<RHIBufferDesc> buffers{1};
        std::vector<IRHIUploadTransactionListener*> listeners;
        size_t texturesCreated{}, buffersCreated{}, texturesReleased{}, buffersReleased{};

        bool ReserveUploadBatch(std::span<const RHIUploadRequest>, std::span<RHIBufferSlice>,
                                std::string& error) override
        {
            error = "Portable graph fixture does not emulate uploads.";
            return false;
        }
        RHIBufferSlice AllocateUpload(const RHIUploadRequest&) override { return {}; }
        RHIBufferSlice UploadConstants(const void*, size_t) override { return {}; }
        uint64_t GetCurrentUploadRecordingId() const override { return recordingId; }
        uint64_t GetDescriptorVersionToken() const override { return descriptorVersion; }
        void RegisterUploadTransactionListener(IRHIUploadTransactionListener* listener) override
        {
            listeners.push_back(listener);
        }
        void UnregisterUploadTransactionListener(IRHIUploadTransactionListener* listener) override
        {
            std::erase(listeners, listener);
        }
        RHISamplerTable CreateSamplers(std::span<const RHISamplerDesc>) override { return {}; }
        RHIBindingTable CreateBindings(std::span<const RHIBindingDesc>) override { return {}; }
        RHIEncoder& GetImmediateEncoder() override { return encoder; }
        RHIRenderTargetBinding CreateRenderTargets(std::span<const RHITextureHandle> colors,
                                                   const RHIDepthTargetDesc* depth = nullptr) override
        {
            return {0, static_cast<uint32_t>(colors.size()), depth != nullptr};
        }
        RHIRenderTargetBinding CreateRenderTargets(std::span<const RHIColorTargetDesc> colors,
                                                   const RHIDepthTargetDesc* depth = nullptr) override
        {
            return {0, static_cast<uint32_t>(colors.size()), depth != nullptr};
        }
        bool CreateTexture(const RHITextureDesc& descriptor, RHITextureHandle& handle, std::string& error) override
        {
            error.clear();
            if (!descriptor.width || !descriptor.height || !descriptor.depthOrArraySize || !descriptor.mipLevels ||
                descriptor.format == RHIFormat::Unknown)
            {
                error = "Invalid portable texture descriptor.";
                return false;
            }
            handle = {static_cast<uint32_t>(textures.size())};
            textures.push_back(descriptor);
            ++texturesCreated;
            return true;
        }
        bool CreateBuffer(const RHIBufferDesc& descriptor, RHIBufferHandle& handle, std::string& error) override
        {
            error.clear();
            if (!descriptor.bytes)
            {
                error = "Invalid portable buffer descriptor.";
                return false;
            }
            handle = {static_cast<uint32_t>(buffers.size())};
            buffers.push_back(descriptor);
            ++buffersCreated;
            return true;
        }
        RHITextureInfo DescribeTexture(RHITextureHandle handle) const override
        {
            if (!handle.IsValid() || handle.id >= textures.size())
            {
                return {};
            }
            const RHITextureDesc& descriptor = textures[handle.id];
            return {descriptor.width, descriptor.height, descriptor.depthOrArraySize, descriptor.mipLevels,
                    descriptor.format, retainTextureStates, descriptor.dim == RHITextureDesc::Dim::Texture3D};
        }
        RHIBufferInfo DescribeBuffer(RHIBufferHandle handle) const override
        {
            return handle.IsValid() && handle.id < buffers.size() ? RHIBufferInfo{buffers[handle.id].bytes}
                                                                  : RHIBufferInfo{};
        }
        void ReleaseTexture(RHITextureHandle handle) override
        {
            if (DescribeTexture(handle).IsValid())
            {
                textures[handle.id] = {};
                ++texturesReleased;
            }
        }
        void ReleaseBuffer(RHIBufferHandle handle) override
        {
            if (DescribeBuffer(handle).IsValid())
            {
                buffers[handle.id] = {};
                ++buffersReleased;
            }
        }
        bool IsAtomicMin64Buffer(RHIBufferHandle handle) const override
        {
            return DescribeBuffer(handle).bytes >= 8 && buffers[handle.id].allowUnorderedAccess;
        }
        RHIAtomicCapabilities GetAtomicCapabilities() const override { return {true}; }
        RHISubresourceCapabilities GetSubresourceCapabilities() const override { return subresourceCapabilities; }
        void TransitionResources(std::span<const RHITransition> transitions) override
        {
            encoder.textureTransitions.insert(encoder.textureTransitions.end(), transitions.begin(), transitions.end());
        }
        void TransitionBuffers(std::span<const RHIBufferTransition> transitions) override
        {
            encoder.bufferTransitions.insert(encoder.bufferTransitions.end(), transitions.begin(), transitions.end());
        }
        bool CreateReadback(uint32_t, uint32_t, RHIFormat, uint32_t, RHIReadback&, std::string& error) override
        {
            error = "Portable graph fixture does not emulate GPU readback.";
            return false;
        }
        bool CreateBufferReadback(uint64_t, RHIReadback&, std::string& error) override
        {
            error = "Portable graph fixture does not emulate GPU readback.";
            return false;
        }
        bool MapReadback(const RHIReadback&, RHIReadbackImage&, std::string& error) override
        {
            error = "Portable graph fixture does not emulate GPU readback.";
            return false;
        }
        void ReleaseReadback(RHIReadback& readback) override { readback = {}; }
    };
} // namespace ce::regression
