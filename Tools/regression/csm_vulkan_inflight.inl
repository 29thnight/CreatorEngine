#ifdef LX_PROBE_VULKAN
void RunCsmVulkanInFlight(RecordingChangeDevice& device)
{
    std::string error;
    device.WaitForGpu();
    const auto signal = reinterpret_cast<PFN_vkSignalSemaphore>(VulkanApi::vkGetDeviceProcAddr(device.GetDevice(), "vkSignalSemaphore"));
    Check(signal != nullptr, "Vulkan timeline signal available");
    VkSemaphoreTypeCreateInfo type{VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO};
    type.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    VkSemaphoreCreateInfo create{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    create.pNext = &type;
    VkSemaphore gate{};
    Check(VulkanApi::vkCreateSemaphore(device.GetDevice(), &create, nullptr, &gate) == VK_SUCCESS, "CSM queue gate");
    struct ReleaseGate
    {
        RecordingChangeDevice& device;
        VkSemaphore gate;
        PFN_vkSignalSemaphore signal;
        bool released{};
        void Release()
        {
            if (released) return;
            VkSemaphoreSignalInfo info{VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO};
            info.semaphore = gate; info.value = 1;
            signal(device.GetDevice(), &info);
            released = true;
            device.WaitForGpu();
        }
        ~ReleaseGate() { Release(); VulkanApi::vkDestroySemaphore(device.GetDevice(), gate, nullptr); }
    } release{device, gate, signal};
    Check(GetRHISubmissionThread().ExecuteAndWait(&device, "CSM test queue gate", [&](std::string&) {
        VkSemaphoreSubmitInfo wait{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
        wait.semaphore = gate; wait.value = 1; wait.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
        submit.waitSemaphoreInfoCount = 1; submit.pWaitSemaphoreInfos = &wait;
        return VulkanApi::vkQueueSubmit2(device.GetQueue(), 1, &submit, VK_NULL_HANDLE) == VK_SUCCESS;
    }, error), "CSM gate submit " + error);
    const auto completed = device.GetCompletedFenceValue();
    std::array<RHITextureHandle, 3> textures{};
    std::array<RHIReadback, 3> readbacks{};
    std::array<std::shared_ptr<EnhancedRenderGraph>, 3> graphs;
    for (unsigned frame = 0; frame < 3; ++frame)
    {
        Check(device.BeginFrame(error), "CSM in-flight begin " + error);
        RHITextureDesc desc;
        desc.width = desc.height = 8; desc.depthOrArraySize = 3;
        desc.format = RHIFormat::D32Float; desc.allowDepthStencil = true;
        Check(device.CreateTexture(desc, textures[frame], error), "CSM in-flight depth");
        Check(device.CreateReadback(8, 8, RHIFormat::D32Float, 3, readbacks[frame], error), "CSM in-flight readback");
        auto graph = std::make_shared<EnhancedRenderGraph>(device);
        graphs[frame] = graph;
        const auto map = graph->ImportTexture(textures[frame], RHIResourceState::Common, "CSM.InFlight");
        graph->AddPass("CSM.Clear", {{map, RHIResourceState::DepthWrite}}, [&, map, frame](const auto& execution) {
            for (unsigned layer = 0; layer < 3; ++layer)
            {
                const auto depth = RHIDepthTargetDesc::DepthSlice(execution.ResolveHandle(map), RHIFormat::D32Float, layer);
                const auto target = device.CreateRenderTargets(std::span<const RHITextureHandle>{}, &depth);
                execution.encoder->BindRenderTargets(target);
                execution.encoder->ClearDepthTarget(target, .1f * float(1 + frame * 3 + layer));
            }
        });
        graph->AddPass("CSM.Read", {{map, RHIResourceState::CopySource}}, [&, map, frame](const auto& execution) {
            for (unsigned layer = 0; layer < 3; ++layer)
                execution.encoder->CopyToReadback(readbacks[frame], execution.ResolveHandle(map), layer, layer);
        }, true);
        Check(graph->Compile(error) && graph->Execute(error) && device.EndFrame(error), "CSM in-flight record " + error);
    }
    Check(GetRHISubmissionThread().DrainSubmissions(&device, error), "CSM three submissions queued");
    const bool stalled = device.GetCompletedFenceValue() == completed;
    release.Release(); // Always release the test gate before any potentially throwing assertion.
    Check(stalled, "CSM exercised three genuinely pending frame slots");
    for (unsigned frame = 0; frame < 3; ++frame)
    {
        RHIReadbackImage mapped;
        Check(device.MapReadback(readbacks[frame], mapped, error), "CSM in-flight map");
        for (unsigned layer = 0; layer < 3; ++layer)
            Near(mapped.At(4, 4, 0, layer), .1f * float(1 + frame * 3 + layer), "CSM retained slice depth");
        device.ReleaseReadback(readbacks[frame]);
        graphs[frame].reset();
        device.ReleaseTexture(textures[frame]);
    }
    std::string messages;
    Check(device.DrainDebugMessages(messages) == 0, "CSM in-flight validation " + messages);
    std::cout << "CSM_VULKAN_INFLIGHT_OK frames=3 layers=9 validation=0\n";
}
#endif
