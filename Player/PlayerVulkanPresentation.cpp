#include "PlayerPresentation.h"
#include "Render/Temporal/TemporalRuntimeControl.h"
#include "RHI/RHIShaderCompiler.h"
#include "RHI/Vulkan/VulkanDeviceResources.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <exception>
#include <limits>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

namespace Player
{
    namespace
    {
        bool CheckVulkanResult(VkResult result, const char* operation, std::string& outError)
        {
            if (VK_SUCCESS == result)
            {
                return true;
            }
            outError = std::string("Player Vulkan ") + operation + ": " + VulkanApi::ResultToString(result);
            return false;
        }

        bool CreatePresentationShader(VkDevice device, const RHIShaderBlob& blob, uint32_t executionModel,
                                      VkShaderModule& module, std::string& entryPoint, std::string& outError)
        {
            if (blob.Size() < 5 * sizeof(uint32_t) || 0 != blob.Size() % sizeof(uint32_t))
            {
                outError = "Player Vulkan shader is not a complete SPIR-V word stream";
                return false;
            }
            // Keep the Vulkan code pointer aligned, and use the artifact's actual entry name.
            std::vector<uint32_t> words(blob.Size() / sizeof(uint32_t));
            std::memcpy(words.data(), blob.Data(), blob.Size());
            if (0x07230203u != words[0])
            {
                outError = "Player Vulkan shader has an invalid SPIR-V header";
                return false;
            }
            for (size_t offset = 5; offset < words.size();)
            {
                const uint32_t instruction = words[offset];
                const size_t length = instruction >> 16;
                if (0 == length || length > words.size() - offset)
                {
                    outError = "Player Vulkan shader contains a truncated instruction";
                    return false;
                }
                constexpr uint32_t kOpEntryPoint = 15;
                if (kOpEntryPoint == (instruction & 0xffffu))
                {
                    if (length < 4)
                    {
                        outError = "Player Vulkan shader contains a truncated entry point";
                        return false;
                    }
                    if (executionModel == words[offset + 1])
                    {
                        const auto* begin = reinterpret_cast<const char*>(words.data() + offset + 3);
                        const auto* end = static_cast<const char*>(std::memchr(begin, 0, (length - 3) * sizeof(uint32_t)));
                        if (nullptr == end || begin == end || !entryPoint.empty())
                        {
                            outError = "Player Vulkan shader requires one nonempty entry point per stage";
                            return false;
                        }
                        entryPoint.assign(begin, end);
                    }
                }
                offset += length;
            }
            if (entryPoint.empty())
            {
                outError = "Player Vulkan shader has no entry point for its stage";
                return false;
            }
            VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
            info.codeSize = blob.Size();
            info.pCode = words.data();
            return CheckVulkanResult(VulkanApi::vkCreateShaderModule(device, &info, nullptr, &module),
                                     "shader module creation failed", outError);
        }

        class VulkanPresentation final : public Presentation
        {
        public:
            ~VulkanPresentation() override { Shutdown(); }

            bool IsActive() const override { return m_active.load(std::memory_order_acquire); }
            const char* GetName() const override { return "Vulkan (CPU readback/upload bridge)"; }
            bool IsQuarantined() const { return m_quarantined; }

            bool Initialize(HWND window, uint32_t width, uint32_t height, std::string& outError) override
            {
                outError.clear();
                if (IsActive())
                {
                    return true;
                }
                if (m_quarantined)
                {
                    outError = "Player Vulkan presentation is quarantined after unproven GPU completion";
                    return false;
                }
                if (nullptr == window || 0 == width || 0 == height)
                {
                    outError = "Player Vulkan initialization requires a window with a nonzero client size";
                    return false;
                }
#if defined(_DEBUG)
                constexpr bool kEnableValidation = true;
#else
                constexpr bool kEnableValidation = false;
#endif
                m_shutdown = false;
                // This device owns presentation only. Scene rendering has its own RHI device.
                if (!m_resources.Initialize(1, 1, kEnableValidation, outError) ||
                    !m_resources.AttachSwapChain(window, width, height, outError) ||
                    !CreateBackBufferViews(outError) || !CreateDescriptors(outError) || !CreatePipeline(outError))
                {
                    Shutdown();
                    return false;
                }
                VkPhysicalDeviceProperties properties{};
                VulkanApi::vkGetPhysicalDeviceProperties(m_resources.GetPhysicalDevice(), &properties);
                m_maxTextureDimension = properties.limits.maxImageDimension2D;
                m_width = width;
                m_height = height;
                m_active.store(true, std::memory_order_release);
                PublishTemporalState();
                return true;
            }

            bool Resize(uint32_t width, uint32_t height, std::string& outError) override
            {
                outError.clear();
                if (!IsActive())
                {
                    outError = "Player Vulkan presentation is inactive";
                    return false;
                }
                if (m_frameOpen)
                {
                    AbortCurrentFrame();
                }
                if (0 == width || 0 == height)
                {
                    // Keep the last completed texture; do not acquire a minimized surface.
                    m_width = width;
                    m_height = height;
                    return true;
                }
                if (m_width == width && m_height == height && !m_resources.NeedsSwapChainRecreation())
                {
                    return true;
                }
                if (!m_resources.DrainForLifecycle(RHILifecycleCommand::SwapChainResize, outError))
                {
                    return false;
                }
                if (m_resources.GetLastLifecycleResult().command == RHILifecycleCommand::UnrecoverableDeviceError)
                {
                    outError = "Player Vulkan resize drain abandoned the device without proving GPU completion";
                    return false;
                }
                DestroyBackBufferViews();
                if (!m_resources.ResizeSwapChain(width, height, outError) || !CreateBackBufferViews(outError))
                {
                    return false;
                }
                // The pipeline remains valid across extent changes. A different UNORM surface
                // format requires a replacement after the same lifecycle drain.
                if (m_pipelineFormat != m_resources.GetBackBufferFormat())
                {
                    VulkanApi::vkDestroyPipeline(m_resources.GetDevice(), m_pipeline, nullptr);
                    m_pipeline = VK_NULL_HANDLE;
                    if (!CreatePipeline(outError))
                    {
                        return false;
                    }
                }
                m_width = width;
                m_height = height;
                return true;
            }

            bool BeginFrame(std::string& outError) override
            {
                outError.clear();
                PublishTemporalState();
                if (!IsActive() || m_frameOpen)
                {
                    outError = "Player Vulkan presentation is inactive or a frame is already open";
                    return false;
                }
                m_frameDeferred = false;
                if (0 == m_width || 0 == m_height)
                {
                    m_frameDeferred = true;
                    return true;
                }
                if (m_resources.NeedsSwapChainRecreation() && !Resize(m_width, m_height, outError))
                {
                    return false;
                }
                bool admitted = m_resources.BeginFrame(outError);
                if (!admitted)
                {
                    m_resources.AbortFrame();
                    // OUT_OF_DATE can occur without WM_SIZE. Rebuild views and the swapchain
                    // together before one fresh acquire; surface/device loss remains an error.
                    if (m_resources.NeedsSwapChainRecreation())
                    {
                        if (!Resize(m_width, m_height, outError))
                        {
                            return false;
                        }
                        admitted = m_resources.BeginFrame(outError);
                        if (!admitted)
                        {
                            m_resources.AbortFrame();
                        }
                    }
                    if (!admitted)
                    {
                        if (outError == "Vulkan host frame admission timed out")
                        {
                            m_frameDeferred = true;
                            outError.clear();
                            return true;
                        }
                        return false;
                    }
                }
                m_frameOpen = true;
                // BeginFrame has waited for the previous presentation GPU fence. Only now
                // may we overwrite the single uploaded image or update its descriptor set.
                {
                    std::lock_guard<std::mutex> lock(m_cpuFrameMutex);
                    m_recordingCpuFrame.swap(m_pendingCpuFrame);
                    outError.swap(m_pendingCpuError);
                }
                if (!outError.empty() || (m_recordingCpuFrame && !UploadCpuFrame(*m_recordingCpuFrame, outError)))
                {
                    AbortCurrentFrame();
                    return false;
                }
                return true;
            }

            bool Present(uint64_t textureId, std::string& outError) override
            {
                outError.clear();
                if (!IsActive())
                {
                    outError = "Player Vulkan presentation is inactive";
                    return false;
                }
                if (m_frameDeferred || 0 == m_width || 0 == m_height)
                {
                    return true;
                }
                if (!m_frameOpen)
                {
                    outError = "Player Vulkan presentation has no open frame";
                    return false;
                }
                if (0 != textureId && (textureId != m_textureId || !m_textureInitialized))
                {
                    outError = "Player Vulkan presentation received a stale or foreign texture ID";
                    AbortCurrentFrame();
                    return false;
                }
                const uint32_t index = m_resources.GetBackBufferIndex();
                if (index >= m_backBufferViews.size())
                {
                    outError = "Player Vulkan back-buffer index is out of range";
                    AbortCurrentFrame();
                    return false;
                }

                const VkCommandBuffer commandBuffer = m_resources.GetCommandBuffer();
                VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
                // Every frame overwrites the entire back buffer, including the no-scene clear.
                barrier.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
                barrier.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
                barrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
                barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.image = m_resources.GetBackBuffer(index);
                barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                barrier.subresourceRange.levelCount = 1;
                barrier.subresourceRange.layerCount = 1;
                VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
                dependency.imageMemoryBarrierCount = 1;
                dependency.pImageMemoryBarriers = &barrier;
                VulkanApi::vkCmdPipelineBarrier2(commandBuffer, &dependency);

                VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
                color.imageView = m_backBufferViews[index];
                color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
                color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
                color.clearValue.color = {{0.0f, 0.0f, 0.0f, 1.0f}};
                VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
                const VkExtent2D extent = m_resources.GetBackBufferExtent();
                rendering.renderArea.extent = extent;
                rendering.layerCount = 1;
                rendering.colorAttachmentCount = 1;
                rendering.pColorAttachments = &color;
                VulkanApi::vkCmdBeginRendering(commandBuffer, &rendering);
                if (0 != textureId)
                {
                    // Match the shared HLSL/Slang top-left UV convention.
                    VkViewport viewport{};
                    viewport.y = static_cast<float>(extent.height);
                    viewport.width = static_cast<float>(extent.width);
                    viewport.height = -static_cast<float>(extent.height);
                    viewport.maxDepth = 1.0f;
                    const VkRect2D scissor{{0, 0}, extent};
                    VulkanApi::vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
                    VulkanApi::vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
                    VulkanApi::vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
                    VulkanApi::vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                      m_pipelineLayout, 0, 1, &m_descriptorSet, 0, nullptr);
                    VulkanApi::vkCmdDraw(commandBuffer, 3, 1, 0, 0);
                }
                VulkanApi::vkCmdEndRendering(commandBuffer);
                barrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
                barrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
                barrier.dstStageMask = VK_PIPELINE_STAGE_2_NONE;
                barrier.dstAccessMask = 0;
                barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
                VulkanApi::vkCmdPipelineBarrier2(commandBuffer, &dependency);

                if (!m_resources.EndFrame(outError))
                {
                    AbortCurrentFrame();
                    return false;
                }
                m_frameOpen = false;
                m_recordingCpuFrame.reset();
                // This waits for the CPU submission ticket, then calls native Present on
                // the serialized presentation owner. It does not report physical scan-out.
                if (!m_resources.Present(outError))
                {
                    if (!m_resources.NeedsSwapChainRecreation())
                    {
                        return false;
                    }
                    return Resize(m_width, m_height, outError);
                }
                if (0 != textureId)
                {
                    RecordSubmittedGameFrame();
                    TemporalRuntimeControl::Get().PublishPlayer([](auto& snapshot) { ++snapshot.realPresentationCount; });
                }
                return true;
            }

            uint64_t OpenSharedTexture(void*, std::shared_ptr<RHIDisplayConsumerLease>) override
            {
                // A DXGI shared handle is not a Vulkan texture. This backend retains the
                // existing scene readback -> CPU mailbox -> Vulkan upload bridge.
                return 0;
            }

            void SubmitCpuFrame(uint64_t key, uint32_t width, uint32_t height, const void* rgba, uint32_t rowPitch,
                                const RHIDisplayFrameMetadata& metadata) override
            {
                if (!IsActive())
                {
                    return;
                }
                const uint64_t tightPitch = uint64_t{width} * 4;
                const uint64_t byteCount = tightPitch * height;
                const uint64_t sourceBytes = uint64_t{rowPitch} * height;
                if (0 == key || 0 == width || 0 == height || nullptr == rgba || rowPitch < tightPitch ||
                    width > m_maxTextureDimension || height > m_maxTextureDimension ||
                    byteCount > kMaxCpuFrameBytes || sourceBytes > (std::numeric_limits<size_t>::max)())
                {
                    std::lock_guard<std::mutex> lock(m_cpuFrameMutex);
                    if (IsActive())
                    {
                        m_pendingCpuError = "Player Vulkan CPU frame is invalid or exceeds the 256 MiB frame limit";
                    }
                    return;
                }
                CpuFrame frame{};
                frame.key = key;
                frame.width = width;
                frame.height = height;
                frame.metadata = metadata;
                try
                {
                    frame.rgba.resize(static_cast<size_t>(byteCount));
                }
                catch (const std::exception&)
                {
                    std::lock_guard<std::mutex> lock(m_cpuFrameMutex);
                    if (IsActive())
                    {
                        m_pendingCpuError = "Player Vulkan CPU frame allocation failed";
                    }
                    return;
                }
                const auto* source = static_cast<const uint8_t*>(rgba);
                for (uint32_t y = 0; y < height; ++y)
                {
                    std::memcpy(frame.rgba.data() + static_cast<size_t>(y * tightPitch),
                                source + static_cast<size_t>(y) * rowPitch, static_cast<size_t>(tightPitch));
                }
                std::lock_guard<std::mutex> lock(m_cpuFrameMutex);
                if (IsActive())
                {
                    // Player consumes one game view. Scene changes replace this single
                    // mailbox entry instead of accumulating keys or stale scene allocations.
                    m_pendingCpuFrame = std::move(frame);
                }
            }

            RHIDisplayTexture GetCpuFrameTexture(uint64_t key) override
            {
                if (!IsActive() || !m_frameOpen || key != m_textureKey || !m_textureInitialized ||
                    0 == m_textureMetadata.m_frameId)
                {
                    return {};
                }
                // Only BeginFrame changes these pixels and their metadata; concurrent RT
                // publication cannot mix a newer camera/frame identity with this upload.
                return {m_textureId, m_textureWidth, m_textureHeight, m_textureMetadata};
            }

            void Shutdown() override
            {
                if (m_shutdown)
                {
                    return;
                }
                m_shutdown = true;
                m_active.store(false, std::memory_order_release);
                if (m_frameOpen)
                {
                    m_resources.AbortFrame();
                    m_frameOpen = false;
                }
                const VkDevice device = m_resources.GetDevice();
                if (VK_NULL_HANDLE != device)
                {
                    std::string error;
                    const bool drained = m_resources.DrainForLifecycle(RHILifecycleCommand::BackendShutdown, error);
                    const bool completionProven = drained &&
                        m_resources.GetLastLifecycleResult().command != RHILifecycleCommand::UnrecoverableDeviceError;
                    if (!completionProven)
                    {
                        MarkShutdownFailure();
                        OutputDebugStringA(("[Player Vulkan] Shutdown completion failed: " + error + "\n").c_str());
                        std::string abandonError;
                        const bool abandoned = m_resources.DrainForLifecycle(
                            RHILifecycleCommand::UnrecoverableDeviceError, abandonError);
                        // CPU-owner abandonment alone is not a GPU completion signal.
                        // After queue access stops, only native idle or actual device loss
                        // makes it legal to release objects referenced by submitted work.
                        const VkResult idle = abandoned ? VulkanApi::vkDeviceWaitIdle(device) : VK_NOT_READY;
                        if (VK_SUCCESS != idle && VK_ERROR_DEVICE_LOST != idle)
                        {
                            m_quarantined = true;
                            std::fputs("[Player presentation] FATAL: Vulkan consumer completion unproven; "
                                       "native resources quarantined until process exit\n", stderr);
                            return;
                        }
                    }
                    DestroyBackBufferViews();
                    if (VK_NULL_HANDLE != m_pipeline)
                    {
                        VulkanApi::vkDestroyPipeline(device, m_pipeline, nullptr);
                    }
                    if (VK_NULL_HANDLE != m_pipelineLayout)
                    {
                        VulkanApi::vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);
                    }
                    if (VK_NULL_HANDLE != m_descriptorPool)
                    {
                        VulkanApi::vkDestroyDescriptorPool(device, m_descriptorPool, nullptr);
                    }
                    if (VK_NULL_HANDLE != m_descriptorLayout)
                    {
                        VulkanApi::vkDestroyDescriptorSetLayout(device, m_descriptorLayout, nullptr);
                    }
                    if (VK_NULL_HANDLE != m_sampler)
                    {
                        VulkanApi::vkDestroySampler(device, m_sampler, nullptr);
                    }
                    // The RHI table owns the uploaded image and releases it during device
                    // teardown, including the unrecoverable-device path. No false completion.
                }
                m_pipeline = VK_NULL_HANDLE;
                m_pipelineLayout = VK_NULL_HANDLE;
                m_descriptorPool = VK_NULL_HANDLE;
                m_descriptorLayout = VK_NULL_HANDLE;
                m_descriptorSet = VK_NULL_HANDLE;
                m_sampler = VK_NULL_HANDLE;
                m_pipelineFormat = VK_FORMAT_UNDEFINED;
                m_texture = {};
                m_textureInitialized = false;
                m_textureId = 0;
                m_textureKey = 0;
                m_textureWidth = 0;
                m_textureHeight = 0;
                m_textureMetadata = {};
                m_recordingCpuFrame.reset();
                {
                    std::lock_guard<std::mutex> lock(m_cpuFrameMutex);
                    m_pendingCpuFrame.reset();
                    m_pendingCpuError.clear();
                }
                m_resources.Shutdown();
                m_width = 0;
                m_height = 0;
            }

        private:
            void PublishTemporalState()
            {
                const auto request = TemporalRuntimeControl::Get().Snapshot();
                TemporalRuntimeControl::Get().PublishPlayer([&](auto& snapshot)
                {
                    snapshot.playerObservedGeneration = request.requestedGeneration;
                    snapshot.presentationTarget = TemporalPresentationTarget::PlayerSwapchain;
                    snapshot.selectedFrameGenerator = snapshot.activeFrameGenerator = TemporalProvider::None;
                    snapshot.lastFrameGenerationResult = {request.settings.enabled &&
                        request.settings.requestedFrameGenerator != TemporalProvider::None ?
                        TemporalStatus::IntegrationRequired : TemporalStatus::Success};
                    snapshot.requestedFrameGenerationResult = snapshot.lastFrameGenerationResult;
                    if (snapshot.lastFrameGenerationResult.status == TemporalStatus::IntegrationRequired)
                        snapshot.diagnostic = "NativePresentationInteropUnavailable: Vulkan Player CPU display bridge has no native multi-resource SDK transport; real-frame fallback";
                });
            }
            struct CpuFrame
            {
                uint64_t key{0};
                uint32_t width{0};
                uint32_t height{0};
                std::vector<uint8_t> rgba;
                RHIDisplayFrameMetadata metadata{};
            };

            void AbortCurrentFrame()
            {
                m_resources.AbortFrame();
                m_frameOpen = false;
                if (m_recordingCpuFrame)
                {
                    // Discard speculative layout/content state. A future upload starts
                    // from UNDEFINED, so aborted copy barriers are never treated as executed.
                    m_textureInitialized = false;
                    m_textureMetadata = {};
                    m_textureId = 0;
                    std::lock_guard<std::mutex> lock(m_cpuFrameMutex);
                    if (!m_pendingCpuFrame)
                    {
                        m_pendingCpuFrame = std::move(m_recordingCpuFrame);
                    }
                    m_recordingCpuFrame.reset();
                }
            }

            bool UploadCpuFrame(const CpuFrame& frame, std::string& outError)
            {
                if (!m_texture.IsValid() || frame.width != m_textureWidth || frame.height != m_textureHeight)
                {
                    // The previous host GPU frame completed before BeginFrame returned.
                    // There is no unbounded retired-texture list or descriptor cache.
                    if (m_texture.IsValid())
                    {
                        m_resources.ReleaseTexture(m_texture);
                        m_texture = {};
                    }
                    m_textureInitialized = false;
                    m_textureMetadata = {};
                    m_textureId = 0;
                    RHITextureDesc desc{};
                    desc.width = frame.width;
                    desc.height = frame.height;
                    desc.format = RHIFormat::RGBA8Unorm;
                    desc.debugName = L"Player.Vulkan.CpuFrame";
                    if (!m_resources.CreateTexture(desc, m_texture, outError))
                    {
                        return false;
                    }
                    m_textureWidth = frame.width;
                    m_textureHeight = frame.height;
                    const VulkanImageEntry image = m_resources.GetResourceTable().Resolve(m_texture);
                    if (!image.IsValid())
                    {
                        outError = "Player Vulkan uploaded texture handle could not be resolved";
                        return false;
                    }
                    VkDescriptorImageInfo imageInfo{};
                    imageInfo.imageView = image.view;
                    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
                    write.dstSet = m_descriptorSet;
                    write.dstBinding = 0;
                    write.descriptorCount = 1;
                    write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
                    write.pImageInfo = &imageInfo;
                    VulkanApi::vkUpdateDescriptorSets(m_resources.GetDevice(), 1, &write, 0, nullptr);
                }
                const RHIBufferSlice upload = m_resources.AllocateUpload(
                    RHIUploadRequest{frame.rgba.size(), RHIUploadUsage::TextureCopy, 1});
                if (!upload.IsWritable())
                {
                    outError = "Player Vulkan CPU frame upload allocation failed";
                    return false;
                }
                const VulkanBufferEntry source = m_resources.GetResourceTable().Resolve(upload.buffer);
                const VulkanImageEntry destination = m_resources.GetResourceTable().Resolve(m_texture);
                if (!source.IsValid() || !destination.IsValid())
                {
                    outError = "Player Vulkan CPU frame upload handles could not be resolved";
                    return false;
                }
                std::memcpy(upload.cpuAddress, frame.rgba.data(), frame.rgba.size());
                const RHITransition toCopy{m_texture,
                                          m_textureInitialized ? RHIResourceState::PixelShaderResource
                                                               : RHIResourceState::Common,
                                          RHIResourceState::CopyDest};
                m_resources.TransitionResources({&toCopy, 1});
                VkBufferImageCopy copy{};
                copy.bufferOffset = upload.offset;
                copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                copy.imageSubresource.layerCount = 1;
                copy.imageExtent = {frame.width, frame.height, 1};
                VulkanApi::vkCmdCopyBufferToImage(m_resources.GetCommandBuffer(), source.buffer, destination.image,
                                                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
                const RHITransition toRead{m_texture, RHIResourceState::CopyDest, RHIResourceState::PixelShaderResource};
                m_resources.TransitionResources({&toRead, 1});
                m_textureInitialized = true;
                m_textureKey = frame.key;
                m_textureMetadata = frame.metadata;
                m_textureId = ++m_nextTextureId;
                return true;
            }

            void DestroyBackBufferViews()
            {
                for (VkImageView view : m_backBufferViews)
                {
                    if (VK_NULL_HANDLE != view)
                    {
                        VulkanApi::vkDestroyImageView(m_resources.GetDevice(), view, nullptr);
                    }
                }
                m_backBufferViews.clear();
            }

            bool CreateBackBufferViews(std::string& outError)
            {
                const VkFormat format = m_resources.GetBackBufferFormat();
                // The scene bridge already contains display-ready values. Sampling UNORM
                // into UNORM preserves them; an sRGB attachment would encode them twice.
                if (VK_FORMAT_B8G8R8A8_UNORM != format && VK_FORMAT_R8G8B8A8_UNORM != format)
                {
                    outError = "Player Vulkan presentation requires an RGBA8/BGRA8 UNORM surface";
                    return false;
                }
                const uint32_t count = m_resources.GetBackBufferCount();
                if (0 == count)
                {
                    outError = "Player Vulkan swapchain has no images";
                    return false;
                }
                m_backBufferViews.assign(count, VK_NULL_HANDLE);
                for (uint32_t index = 0; index < count; ++index)
                {
                    VkImageViewCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
                    info.image = m_resources.GetBackBuffer(index);
                    info.viewType = VK_IMAGE_VIEW_TYPE_2D;
                    info.format = format;
                    info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                    info.subresourceRange.levelCount = 1;
                    info.subresourceRange.layerCount = 1;
                    if (!CheckVulkanResult(VulkanApi::vkCreateImageView(m_resources.GetDevice(), &info, nullptr,
                                                                       &m_backBufferViews[index]),
                                           "back-buffer view creation failed", outError))
                    {
                        DestroyBackBufferViews();
                        return false;
                    }
                }
                return true;
            }

            bool CreateDescriptors(std::string& outError)
            {
                const VkDevice device = m_resources.GetDevice();
                VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
                sampler.magFilter = VK_FILTER_LINEAR;
                sampler.minFilter = VK_FILTER_LINEAR;
                sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
                sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
                sampler.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
                sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
                if (!CheckVulkanResult(VulkanApi::vkCreateSampler(device, &sampler, nullptr, &m_sampler),
                                       "sampler creation failed", outError))
                {
                    return false;
                }
                VkDescriptorSetLayoutBinding bindings[2]{};
                bindings[0].binding = 0;
                bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
                bindings[0].descriptorCount = 1;
                bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
                bindings[1].binding = 1;
                bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
                bindings[1].descriptorCount = 1;
                bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
                bindings[1].pImmutableSamplers = &m_sampler;
                VkDescriptorSetLayoutCreateInfo layout{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
                layout.bindingCount = 2;
                layout.pBindings = bindings;
                if (!CheckVulkanResult(VulkanApi::vkCreateDescriptorSetLayout(device, &layout, nullptr,
                                                                             &m_descriptorLayout),
                                       "descriptor layout creation failed", outError))
                {
                    return false;
                }
                const VkDescriptorPoolSize sizes[] = {{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1},
                                                     {VK_DESCRIPTOR_TYPE_SAMPLER, 1}};
                VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
                pool.maxSets = 1;
                pool.poolSizeCount = 2;
                pool.pPoolSizes = sizes;
                if (!CheckVulkanResult(VulkanApi::vkCreateDescriptorPool(device, &pool, nullptr, &m_descriptorPool),
                                       "descriptor pool creation failed", outError))
                {
                    return false;
                }
                VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
                allocate.descriptorPool = m_descriptorPool;
                allocate.descriptorSetCount = 1;
                allocate.pSetLayouts = &m_descriptorLayout;
                if (!CheckVulkanResult(VulkanApi::vkAllocateDescriptorSets(device, &allocate, &m_descriptorSet),
                                       "descriptor set allocation failed", outError))
                {
                    return false;
                }
                VkPipelineLayoutCreateInfo pipelineLayout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
                pipelineLayout.setLayoutCount = 1;
                pipelineLayout.pSetLayouts = &m_descriptorLayout;
                return CheckVulkanResult(VulkanApi::vkCreatePipelineLayout(device, &pipelineLayout, nullptr,
                                                                          &m_pipelineLayout),
                                         "pipeline layout creation failed", outError);
            }

            bool CreatePipeline(std::string& outError)
            {
                RHIShaderBlob vertex, pixel;
                const RHIShaderCompiler::ScopedOutput output(RHIShaderBinary::SpirV);
                const RHIShaderCompiler::ModuleReuseScope reuse;
                if (!RHIShaderCompiler::CompileFile("PlayerPresentation.slang", "VSMain", "vs_6_0", vertex, outError) ||
                    !RHIShaderCompiler::CompileFile("PlayerPresentation.slang", "PSMain", "ps_6_0", pixel, outError))
                {
                    return false;
                }
                const VkDevice device = m_resources.GetDevice();
                struct ShaderModules
                {
                    VkDevice device;
                    VkShaderModule vertex{VK_NULL_HANDLE};
                    VkShaderModule pixel{VK_NULL_HANDLE};
                    ~ShaderModules()
                    {
                        if (VK_NULL_HANDLE != vertex)
                        {
                            VulkanApi::vkDestroyShaderModule(device, vertex, nullptr);
                        }
                        if (VK_NULL_HANDLE != pixel)
                        {
                            VulkanApi::vkDestroyShaderModule(device, pixel, nullptr);
                        }
                    }
                } modules{device};
                std::string vertexEntry, pixelEntry;
                if (!CreatePresentationShader(device, vertex, 0, modules.vertex, vertexEntry, outError) ||
                    !CreatePresentationShader(device, pixel, 4, modules.pixel, pixelEntry, outError))
                {
                    return false;
                }
                VkPipelineShaderStageCreateInfo stages[2]{};
                stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
                stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
                stages[0].module = modules.vertex;
                stages[0].pName = vertexEntry.c_str();
                stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
                stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
                stages[1].module = modules.pixel;
                stages[1].pName = pixelEntry.c_str();
                VkPipelineVertexInputStateCreateInfo vertexInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
                VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
                assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
                VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
                viewport.viewportCount = 1;
                viewport.scissorCount = 1;
                VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
                raster.polygonMode = VK_POLYGON_MODE_FILL;
                raster.cullMode = VK_CULL_MODE_NONE;
                raster.frontFace = VK_FRONT_FACE_CLOCKWISE;
                raster.lineWidth = 1.0f;
                VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
                multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
                VkPipelineDepthStencilStateCreateInfo depth{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
                VkPipelineColorBlendAttachmentState attachment{};
                attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
                VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
                blend.attachmentCount = 1;
                blend.pAttachments = &attachment;
                const VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
                VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
                dynamic.dynamicStateCount = 2;
                dynamic.pDynamicStates = dynamicStates;
                const VkFormat format = m_resources.GetBackBufferFormat();
                VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
                rendering.colorAttachmentCount = 1;
                rendering.pColorAttachmentFormats = &format;
                VkGraphicsPipelineCreateInfo pipeline{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
                pipeline.pNext = &rendering;
                pipeline.stageCount = 2;
                pipeline.pStages = stages;
                pipeline.pVertexInputState = &vertexInput;
                pipeline.pInputAssemblyState = &assembly;
                pipeline.pViewportState = &viewport;
                pipeline.pRasterizationState = &raster;
                pipeline.pMultisampleState = &multisample;
                pipeline.pDepthStencilState = &depth;
                pipeline.pColorBlendState = &blend;
                pipeline.pDynamicState = &dynamic;
                pipeline.layout = m_pipelineLayout;
                if (!CheckVulkanResult(VulkanApi::vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline,
                                                                           nullptr, &m_pipeline),
                                       "graphics pipeline creation failed", outError))
                {
                    return false;
                }
                m_pipelineFormat = format;
                return true;
            }

            static constexpr uint64_t kMaxCpuFrameBytes = 256ull * 1024 * 1024;
            std::atomic<bool> m_active{false};
            bool m_frameOpen{false};
            bool m_frameDeferred{false};
            bool m_shutdown{true};
            bool m_quarantined{false};
            uint32_t m_width{0};
            uint32_t m_height{0};
            uint32_t m_maxTextureDimension{0};
            VulkanDeviceResources m_resources;
            std::vector<VkImageView> m_backBufferViews;
            VkSampler m_sampler{VK_NULL_HANDLE};
            VkDescriptorSetLayout m_descriptorLayout{VK_NULL_HANDLE};
            VkDescriptorPool m_descriptorPool{VK_NULL_HANDLE};
            VkDescriptorSet m_descriptorSet{VK_NULL_HANDLE};
            VkPipelineLayout m_pipelineLayout{VK_NULL_HANDLE};
            VkPipeline m_pipeline{VK_NULL_HANDLE};
            VkFormat m_pipelineFormat{VK_FORMAT_UNDEFINED};
            RHITextureHandle m_texture;
            bool m_textureInitialized{false};
            uint64_t m_textureId{0};
            uint64_t m_nextTextureId{0};
            uint64_t m_textureKey{0};
            uint32_t m_textureWidth{0};
            uint32_t m_textureHeight{0};
            RHIDisplayFrameMetadata m_textureMetadata{};
            // The only cross-thread mutable payload: RT writes the newest CPU snapshot,
            // PT consumes it once before command recording. No queue/submission mutex.
            std::mutex m_cpuFrameMutex;
            std::optional<CpuFrame> m_pendingCpuFrame;
            std::optional<CpuFrame> m_recordingCpuFrame;
            std::string m_pendingCpuError;
        };
    }

    std::shared_ptr<Presentation> CreateVulkanPresentation()
    {
        return std::shared_ptr<Presentation>(new VulkanPresentation, [](VulkanPresentation* presentation)
        {
            presentation->Shutdown();
            if (!presentation->IsQuarantined())
            {
                delete presentation;
            }
        });
    }
}
