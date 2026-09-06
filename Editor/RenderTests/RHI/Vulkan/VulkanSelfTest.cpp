#include "VulkanSelfTest.h"
#include "RHI/ShaderReflectionSelfTest.h"
#include "RHI/Vulkan/VulkanDeviceResources.h"
#include "RHI/Vulkan/VulkanEncoder.h"
#include "RHI/Vulkan/VulkanPipelineCache.h"
#include "RHI/Vulkan/VulkanPersistentHeap.h"
#include "RHI/Vulkan/Shaders/VkMeshSpv.h"
#include "RHI/RHIGraphicsPipelineRequest.h"
#include "RHI/RHIShaderBlob.h"
#include "RHI/IRenderDeviceServices.h"
#include "DataSystem.h"
#include "Assets/ModelAssetGeneration.h"
#include "Mesh.h" // Vertex(격리 쿼드)
#include "PathFinder.h"

#include <Windows.h>
#include <DirectXTex.h>

#include <algorithm>
#include <atomic>
#include <array>
#include <limits>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace VulkanApi;

// Vulkan 골격 자가 검증.
//
// ── 5 마무리에서 통째로 다시 썼다 ──
//
// 예전 판은 6단계였고 [3~4/6]이 `VulkanTrianglePass` — 골격 전용 패스,
// 백엔드 전용 인코더 오버로드, 원시 타깃·리드백 — 로 그렸다. 그 검사들이
// 재던 것(파이프라인 캐시 경유 · 상수 도달 · 픽셀)을 지금은 **중립 계약**
// (`IRenderDeviceServices` + `RHIEncoder`)이 전부 잴 수 있고, 실제 패스의
// 대조는 `vk.grid` 가 픽셀 편차 0 으로 하고 있다. 골격 전용 비계는 그래서
// 죽었다 — 비계가 재던 것을 본채가 재게 된 것이 5 의 완료 형태다.
//
// ★ 원시 Vulkan 이 남은 곳은 [4/4] 스왑체인 하나다. 백버퍼는 남(스왑체인)이
//   만든 이미지라 핸들 표에 없고, 그래서 전이를 계약으로 못 건다 — R6 가
//   적어 둔 그 잔여이고, 이 배리어 헬퍼가 남아 있는 이유다.
//
// 판정 줄은 dx12.* 검사와 같은 모양([n/m] 접두)으로 낸다. 반환값이 false 라도
// outLog 는 채워진다 — 어디까지 갔는지가 산출물이다.
namespace
{
    // 유니티 빌드에서 익명 네임스페이스가 파일 간 합쳐지므로 이름을 고유하게 둔다.

    /// 스왑체인 검사용 창 크기.
    constexpr uint32_t kVkTestWidth = 256;
    constexpr uint32_t kVkTestHeight = 256;

    /// 중립 경로가 그리는 크기.
    constexpr uint32_t kVkDrawSize = 64;

    /// 스왑체인 백버퍼의 표시 전이 (위 ★ — 핸들이 없어 계약으로 못 건다).
    void VkTestImageBarrier(VkCommandBuffer commandBuffer, VkImage image,
        VkImageLayout oldLayout, VkImageLayout newLayout,
        VkPipelineStageFlags2 sourceStage, VkAccessFlags2 sourceAccess,
        VkPipelineStageFlags2 destinationStage, VkAccessFlags2 destinationAccess)
    {
        VkImageMemoryBarrier2 barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
        barrier.srcStageMask = sourceStage;
        barrier.srcAccessMask = sourceAccess;
        barrier.dstStageMask = destinationStage;
        barrier.dstAccessMask = destinationAccess;
        barrier.oldLayout = oldLayout;
        barrier.newLayout = newLayout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.layerCount = 1;

        VkDependencyInfo dependency{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
        dependency.imageMemoryBarrierCount = 1;
        dependency.pImageMemoryBarriers = &barrier;

        vkCmdPipelineBarrier2(commandBuffer, &dependency);
    }

    /// 스왑체인 검사용 숨김 창.
    ///
    /// ★ 왜 창을 따로 만드는가: 엔진의 창은 DX12 스왑체인이 쥐고 있다(D2).
    ///   한 창에 두 백엔드의 스왑체인을 함께 둘 수 없다 — DXGI 제약이자
    ///   Vulkan 쪽에서도 서피스가 이미 쓰이는 창이면
    ///   VK_ERROR_NATIVE_WINDOW_IN_USE_KHR 가 난다.
    struct VkTestWindow
    {
        HWND handle{ nullptr };
        ATOM atom{ 0 };

        bool Create()
        {
            WNDCLASSEXW cls{};
            cls.cbSize = sizeof(cls);
            cls.lpfnWndProc = ::DefWindowProcW;
            cls.hInstance = ::GetModuleHandleW(nullptr);
            cls.lpszClassName = L"CreatorEngineVulkanSelfTest";
            atom = ::RegisterClassExW(&cls);

            handle = ::CreateWindowExW(0, L"CreatorEngineVulkanSelfTest", L"vk.selftest",
                WS_OVERLAPPEDWINDOW, 0, 0, static_cast<int>(kVkTestWidth),
                static_cast<int>(kVkTestHeight), nullptr, nullptr,
                ::GetModuleHandleW(nullptr), nullptr);
            return nullptr != handle;
        }

        void Destroy()
        {
            if (nullptr != handle) { ::DestroyWindow(handle); handle = nullptr; }
            if (0 != atom)
            {
                ::UnregisterClassW(L"CreatorEngineVulkanSelfTest", ::GetModuleHandleW(nullptr));
                atom = 0;
            }
        }
    };
}

bool RunVulkanSelfTest(const std::string& outputPngPath, const std::string& modelPath, const std::string& texturePath, std::string& outLog)
{
    std::string error;

    if (!RenderTest::RunShaderReflectionSelfTest(texturePath, outLog)) return false;

    // ── [1/4] 로더·인스턴스·디바이스 ──

    if (!VulkanApi::LoadLoader(error))
    {
        outLog += "[1/4] " + error + "\n";
        return false;
    }

    VulkanDeviceResources resources;
    if (!resources.Initialize(kVkTestWidth, kVkTestHeight, true, error))
    {
        outLog += "[1/4] 디바이스 초기화 실패: " + error + "\n";
        return false;
    }

    {
        const uint32_t api = resources.GetApiVersion();
        outLog += "[1/4] 디바이스 생성 통과 — " + resources.GetAdapterName()
            + " (Vulkan " + std::to_string(VK_API_VERSION_MAJOR(api)) + "."
            + std::to_string(VK_API_VERSION_MINOR(api)) + "."
            + std::to_string(VK_API_VERSION_PATCH(api)) + ")"
            + " · 검증 레이어 " + (resources.IsValidationEnabled() ? "켬" : "끔") + "\n";
    }

    if (!resources.IsValidationEnabled())
    {
        outLog += "[1/4] 검증 레이어가 꺼져 있다 — 통과로 세지 않는다\n";
        return false;
    }
    if (!RunVulkanPersistentHeapSelfTest(resources.GetDevice(),
        resources.GetPhysicalDevice(), resources.IsMemoryBudgetSupported(),
        &resources.GetPersistentMemoryBudgetCoordinator(), outLog))
    {
        resources.Shutdown();
        return false;
    }

    VkDevice device = resources.GetDevice();

    VulkanPipelineCache pipelineCache;
    pipelineCache.Initialize(device);
    resources.SetPipelineCache(&pipelineCache);

    VulkanMeshCache meshCache;
    if (!meshCache.Initialize(&resources, error))
    {
        resources.WaitForGpu();
        pipelineCache.Shutdown();
        outLog += "[2/4] VulkanMeshCache 초기화 실패: " + error + "\n";
        return false;
    }

    const file::path scenePath = file::path(modelPath);
    // MBC9 — typed generation이 유일한 모델 지오메트리 출처다(Assimp·legacy Mesh 은퇴).
    const std::shared_ptr<const assets::ModelAssetGeneration> sceneModel =
        DataSystems->LoadModelAssetGenerationByPath(scenePath.string());
    std::vector<RHIModelMeshView> sceneMeshes;
    RHIModelMeshView sceneMesh{};
    math::aabb sceneMeshBounds{};
    uint64_t sceneVertexBytes = 0;
    uint64_t sceneIndexBytes = 0;
    uint64_t sceneUploadBytes = 0;
    if (sceneModel)
    {
        for (std::uint32_t i = 0; i < sceneModel->Meshes().size(); ++i)
        {
            RHIModelMeshView candidate{};
            if (!BuildRHIModelMeshView(*sceneModel, i, candidate) || !candidate.IsComplete())
                continue;
            const uint64_t candidateVertexBytes = candidate.vertexBytes;
            const uint64_t candidateIndexBytes =
                static_cast<uint64_t>(candidate.indexCount) * sizeof(uint32);

            sceneMeshes.push_back(candidate);
            sceneUploadBytes += candidateVertexBytes + candidateIndexBytes;
            if (!sceneMesh.IsComplete() || candidateVertexBytes + candidateIndexBytes >
                sceneVertexBytes + sceneIndexBytes)
            {
                sceneMesh = candidate;
                sceneMeshBounds = sceneModel->Meshes()[i].bounds;
                sceneVertexBytes = candidateVertexBytes;
                sceneIndexBytes = candidateIndexBytes;
            }
        }
    }
    if (!sceneMesh.IsComplete() || sceneUploadBytes <= 16ull * 1024 * 1024)
    {
        meshCache.Shutdown();
        pipelineCache.Shutdown();
        outLog += "[2/4] 입력 모델의 유효 mesh 누적 업로드가 16MiB를 넘지 않는다"
            " (mesh " + std::to_string(sceneMeshes.size()) + "개 · 누적 "
            + std::to_string(sceneUploadBytes) + "B): " + scenePath.string() + "\n";
        return false;
    }

    RHIReadback readback{};

    auto fail = [&](const std::string& line) {
        outLog += line;

        // ★ 실패할 때 검증 레이어가 한 말을 함께 낸다 (5c-4d). 없으면
        //   `VkResult -13` 같은 답만 남는다 — 레이어는 이미 정확히 알고 있다.
        std::string layerMessages;
        if (0 != resources.DrainDebugMessages(layerMessages) || !layerMessages.empty())
        {
            outLog += layerMessages;
        }

        resources.WaitForGpu();
        resources.ReleaseReadback(readback);
        meshCache.Shutdown();
        pipelineCache.Shutdown();
        return false;
    };

    // ── [2/4]·[3/4] 중립 경로 한 프레임 — 프레임 경계와 그리기를 함께 잰다 ──
    //
    // ★ 전부 **계약**(`IRenderDeviceServices` + `RHIEncoder`)으로 부른다.
    //   구체 타입이 남은 곳은 프레임 여닫이(`IRHIDeviceResources` 의 몫)와
    //   `EndRenderTargets`(DX12 에 대응이 없어 계약에 안 넣은 것) 둘이고,
    //   끝에 **미구현 계수 0** 을 판정에 넣는다 — 스텁이 조용히 지나가면
    //   "통과했는데 아무것도 안 그렸다"가 된다.

    const uint64_t completedBefore = resources.GetCompletedFenceValue();
    uint64_t parallelCasRetries = 0;
    uint64_t parallelWorkerCreates = 0;
    RHIBufferHandle parallelHandleForTrim{};
    RHIMeshBinding sceneBinding{};

    {
        IRenderDeviceServices& services = resources;

        if (!resources.BeginFrame(error)) return fail("[2/4] BeginFrame 실패: " + error + "\n");

        // 문제를 일으킨 실제 입력 모델의 모든 유효 Mesh를 같은 recording에
        // cache 계약으로 올린다. 개별 mesh가 아니라 모델 누적 업로드가 기존
        // 16MiB ring 경계를 넘는 실제 자산 구조를 그대로 재현한다.
        meshCache.BeginFrame(0);
        for (const RHIModelMeshView& mesh : sceneMeshes)
        {
            std::string meshError;
            const RHIMeshBinding binding = meshCache.GetOrUploadModel(mesh, meshError);
            const uint64_t vertexBytes = mesh.vertexBytes;
            const uint64_t indexBytes =
                static_cast<uint64_t>(mesh.indexCount) * sizeof(uint32);
            if (!binding.IsValid() || binding.vertices.size != vertexBytes ||
                binding.indices.size != indexBytes)
            {
                return fail("[2/4] 입력 모델 실제 mesh 누적 cache upload가 실패했다: "
                    + meshError + "\n");
            }
            if (mesh.handle == sceneMesh.handle) sceneBinding = binding;
        }
        const VulkanMeshCache::Stats firstMeshStats = meshCache.GetStats();
        if (!sceneBinding.IsValid() || !sceneBinding.vertices.IsValid() ||
            !sceneBinding.indices.IsValid() ||
            sceneBinding.vertices.size != sceneVertexBytes ||
            sceneBinding.indices.size != sceneIndexBytes ||
            sceneMeshes.size() != firstMeshStats.uploads ||
            sceneUploadBytes != firstMeshStats.bytesUploaded ||
            0 != firstMeshStats.failures)
        {
            return fail("[2/4] 입력 모델 실제 mesh 누적 cache 통계가 일치하지 않는다\n");
        }

        // 입력 모델는 여러 regular segment를 누적으로 넘기는 실제 사례다.
        // 단일 요청 dedicated-large 경로는 자산 형상과 섞지 않고 별도로 잰다.
        const RHIBufferSlice largeUpload = services.AllocateUpload(RHIUploadRequest{
            20ull * 1024 * 1024, RHIUploadUsage::BufferCopy, 16 });
        if (!largeUpload.IsWritable() ||
            !resources.GetResourceTable().Resolve(largeUpload.buffer).IsValid())
        {
            return fail("[2/4] Vulkan 단일 20MiB large segment 예약 실패\n");
        }

        // 첫 flush가 mesh copy를 제출하고 이후 두 flush가 wait 없이 이어진다.
        // draw는 같은 queue의 뒤 command buffer라 copy보다 먼저 실행될 수 없다.
        for (uint32_t flush = 0; flush < 3; ++flush)
        {
            if (!resources.FlushCommandList(error))
                return fail("[2/4] 비동기 중간 제출 실패: " + error + "\n");
        }

        // DX12와 같은 병렬 fixture. standby regular 3개(48MiB)를 넘어서는
        // 64MiB를 worker가 예약해 CAS 범위 비중복과 worker slow growth의
        // stable handle 등록을 함께 확인한다.
        constexpr uint32_t kWorkerCount = 8;
        constexpr uint32_t kAllocationsPerWorker = 8;
        constexpr uint64_t kAllocationBytes = 1ull * 1024 * 1024;
        const RHIUploadStats beforeParallel = resources.GetUploadStats();
        std::array<std::array<RHIBufferSlice, kAllocationsPerWorker>,
            kWorkerCount> workerSlices{};
        std::atomic<uint32_t> ready{ 0 };
        std::atomic<bool> go{ false };
        std::vector<std::thread> workers;
        workers.reserve(kWorkerCount);
        for (uint32_t worker = 0; worker < kWorkerCount; ++worker)
        {
            workers.emplace_back([&, worker]() {
                ready.fetch_add(1, std::memory_order_release);
                while (!go.load(std::memory_order_acquire)) std::this_thread::yield();
                for (RHIBufferSlice& slice : workerSlices[worker])
                {
                    slice = services.AllocateUpload(RHIUploadRequest{
                        kAllocationBytes, RHIUploadUsage::BufferCopy, 16 });
                    if (slice.IsWritable())
                    {
                        auto* bytes = static_cast<uint8_t*>(slice.cpuAddress);
                        bytes[0] = static_cast<uint8_t>(worker);
                        bytes[kAllocationBytes - 1] = static_cast<uint8_t>(worker + 1);
                    }
                }
            });
        }
        while (ready.load(std::memory_order_acquire) != kWorkerCount)
            std::this_thread::yield();
        go.store(true, std::memory_order_release);
        for (std::thread& worker : workers) worker.join();

        std::vector<RHIBufferSlice> sorted;
        sorted.reserve(kWorkerCount * kAllocationsPerWorker);
        uint32_t invalidParallel = 0;
        for (const auto& perWorker : workerSlices)
        {
            for (const RHIBufferSlice& slice : perWorker)
            {
                if (!slice.IsWritable() ||
                    !resources.GetResourceTable().Resolve(slice.buffer).IsValid())
                    ++invalidParallel;
                else
                    sorted.push_back(slice);
            }
        }
        std::sort(sorted.begin(), sorted.end(), [](const RHIBufferSlice& a,
            const RHIBufferSlice& b) {
                return a.buffer.id != b.buffer.id
                    ? a.buffer.id < b.buffer.id : a.offset < b.offset;
            });
        uint32_t overlaps = 0;
        for (size_t i = 1; i < sorted.size(); ++i)
        {
            if (sorted[i - 1].buffer.id == sorted[i].buffer.id &&
                sorted[i].offset < sorted[i - 1].offset + sorted[i - 1].size)
                ++overlaps;
        }

        const RHIUploadStats uploadStats = resources.GetUploadStats();
        if (!sorted.empty()) parallelHandleForTrim = sorted.front().buffer;
        parallelCasRetries = uploadStats.casRetries - beforeParallel.casRetries;
        parallelWorkerCreates = uploadStats.workerSegmentCreates
            - beforeParallel.workerSegmentCreates;
        if (0 == uploadStats.largeSegments || 0 != uploadStats.oomFailures ||
            0 != invalidParallel || 0 != overlaps ||
            uploadStats.fastPathReservations <= beforeParallel.fastPathReservations ||
            0 == parallelWorkerCreates)
        {
            return fail("[2/4] Vulkan 병렬 CAS/worker growth 검증 실패"
                " (무효 " + std::to_string(invalidParallel)
                + " · 겹침 " + std::to_string(overlaps)
                + " · worker 생성 " + std::to_string(parallelWorkerCreates) + ")\n");
        }

        RHITextureDesc colorDesc{};
        colorDesc.width = kVkDrawSize;
        colorDesc.height = kVkDrawSize;
        colorDesc.format = RHIFormat::RGBA8Unorm;
        colorDesc.allowRenderTarget = true;
        colorDesc.initialState = RHIResourceState::RenderTarget;
        colorDesc.debugName = L"vk.selftest.color";

        RHITextureHandle color;
        if (!services.CreateTexture(colorDesc, color, error))
            return fail("[3/4] CreateTexture(색) 실패: " + error + "\n");

        RHITextureDesc depthDesc{};
        depthDesc.width = kVkDrawSize;
        depthDesc.height = kVkDrawSize;
        depthDesc.format = RHIFormat::D32Float;
        depthDesc.allowDepthStencil = true;
        depthDesc.initialState = RHIResourceState::DepthWrite;
        depthDesc.debugName = L"vk.selftest.depth";

        RHITextureHandle depthTexture;
        if (!services.CreateTexture(depthDesc, depthTexture, error))
            return fail("[3/4] CreateTexture(깊이) 실패: " + error + "\n");

        // 되묻기 — 만든 값이 그대로 돌아와야 한다(5c-1 이 계약에 둔 이유).
        const RHITextureInfo info = services.DescribeTexture(color);
        if (!info.IsValid() || kVkDrawSize != info.width || kVkDrawSize != info.height
            || RHIFormat::RGBA8Unorm != info.format)
        {
            return fail("[3/4] DescribeTexture 가 만든 값과 다르다\n");
        }

        // 버퍼 칸의 생산자 경로 (5c-4c).
        RHIBufferDesc bufferDesc{};
        bufferDesc.bytes = 4096;
        bufferDesc.debugName = L"vk.selftest.buffer";

        RHIBufferHandle buffer;
        if (!services.CreateBuffer(bufferDesc, buffer, error))
            return fail("[3/4] CreateBuffer 실패: " + error + "\n");

        const RHIDepthTargetDesc depthTarget =
            RHIDepthTargetDesc::Depth(depthTexture, RHIFormat::D32Float);
        const RHITextureHandle colorList[1] = { color };

        const RHIRenderTargetBinding targets = services.CreateRenderTargets(colorList, &depthTarget);
        if (!targets.IsValid() || 1 != targets.colorCount || !targets.HasDepth())
            return fail("[3/4] CreateRenderTargets 가 무효를 줬다\n");

        // ── 파이프라인 — 레이아웃이 그리드의 `RHILayout::Cbv(0)` 와 같다 ──

        const RHIPipelineLayoutParam layoutParams[] = { RHILayout::Cbv(0) };
        RHIPipelineLayoutDesc layoutDesc{};
        layoutDesc.params = layoutParams;

        const RHIPipelineLayoutHandle layout = pipelineCache.GetOrCreate(layoutDesc, error);
        if (!layout.IsValid())
            return fail("[3/4] 레이아웃 생성 실패: " + error + "\n");

        RHIShaderBlob vs;
        RHIShaderBlob ps;
        vs.Assign(kVkMeshVsSpv, sizeof(kVkMeshVsSpv));
        ps.Assign(kVkMeshPsSpv, sizeof(kVkMeshPsSpv));

        const RHIInputElement meshInput[] = {
            { "POSITION", 0, RHIFormat::RGB32Float, 0,
                static_cast<uint32_t>(offsetof(Vertex, position)), 0 },
        };

        RHIGraphicsPipelineDesc pipelineDesc{};
        pipelineDesc.inputElements = meshInput;
        pipelineDesc.inputElementCount = _countof(meshInput);
        pipelineDesc.vsBytecode = vs.Data();
        pipelineDesc.vsSize = vs.Size();
        pipelineDesc.psBytecode = ps.Data();
        pipelineDesc.psSize = ps.Size();
        pipelineDesc.layout = layout;
        pipelineDesc.topologyType = RHITopologyType::Triangle;
        pipelineDesc.cullMode = RHICullMode::None;
        pipelineDesc.depthEnable = false;
        pipelineDesc.numRenderTargets = 1;
        pipelineDesc.rtvFormats[0] = RHIFormat::RGBA8Unorm;

        // ★ 깊이를 안 쓰는데도 포맷을 적어야 한다 — 검증 레이어가 반증했다
        //   (5c-4d). 렌더링에 깊이 타깃을 걸었으면 파이프라인이 같은 포맷을
        //   선언해야 한다. "포맷은 묶음이 아니라 파이프라인이 든다"의 실물.
        pipelineDesc.dsvFormat = RHIFormat::D32Float;

        RHIGraphicsPipelineRequest pipelineRequest;
        if (!pipelineRequest.Create(pipelineCache, pipelineDesc, error))
            return fail("[3/4] 파이프라인 생성 실패: " + error + "\n");
        const RHIPipelineHandle pipeline = pipelineRequest.GetHandle();
        const RHIGraphicsPipelineDesc& ownedPipelineDesc = pipelineRequest.GetDesc();
        const bool requestOwnsSource =
            ownedPipelineDesc.vsBytecode != pipelineDesc.vsBytecode &&
            ownedPipelineDesc.psBytecode != pipelineDesc.psBytecode &&
            ownedPipelineDesc.inputElements != pipelineDesc.inputElements &&
            ownedPipelineDesc.inputElementCount == pipelineDesc.inputElementCount &&
            nullptr != ownedPipelineDesc.inputElements[0].semantic &&
            ownedPipelineDesc.inputElements[0].semantic != meshInput[0].semantic &&
            std::string(ownedPipelineDesc.inputElements[0].semantic) == "POSITION";
        if (!requestOwnsSource)
            return fail("[3/4] owning PSO 요청이 bytecode/input semantic을 깊은 복사하지 않았다\n");

        // 실제 mesh bounds에서 가장 넓은 두 축을 골라 clip 공간 [-0.7, 0.7]에
        // 맞춘다. 특정 모델 좌표계나 카메라에 fixture가 의존하지 않게 한다.
        // 바운드는 generation이 소유한 값(임포터가 계산)이다.
        const math::vector3 meshMin = sceneMeshBounds.min();
        const math::vector3 meshMax = sceneMeshBounds.max();
        const auto component = [](const math::vector3& value, uint32_t axis) {
            return 0 == axis ? value.x : (1 == axis ? value.y : value.z);
        };
        std::array<uint32_t, 3> axes = { 0, 1, 2 };
        std::sort(axes.begin(), axes.end(), [&](uint32_t a, uint32_t b) {
            return component(meshMax, a) - component(meshMin, a) >
                component(meshMax, b) - component(meshMin, b);
        });
        const float extentX = component(meshMax, axes[0]) - component(meshMin, axes[0]);
        const float extentY = component(meshMax, axes[1]) - component(meshMin, axes[1]);
        if (!(extentX > 1e-6f) || !(extentY > 1e-6f))
            return fail("[3/4] 입력 모델 mesh bounds가 투영 가능한 면을 만들지 못한다\n");

        struct alignas(16) MeshConstants
        {
            float axisX[4]{};
            float axisY[4]{};
            float tint[4]{ 0.f, 1.f, 0.f, 1.f };
        } constants;
        const float scaleX = 1.4f / extentX;
        const float scaleY = 1.4f / extentY;
        constants.axisX[axes[0]] = scaleX;
        constants.axisY[axes[1]] = scaleY;
        constants.axisX[3] = -(component(meshMin, axes[0]) +
            component(meshMax, axes[0])) * 0.5f * scaleX;
        constants.axisY[3] = -(component(meshMin, axes[1]) +
            component(meshMax, axes[1])) * 0.5f * scaleY;

        // ── 세그먼트에서 두 번 잘라 겹침·정렬을 함께 잰다 (5c-4d) ──
        const RHIBufferSlice firstSlice = services.UploadConstants(&constants, sizeof(constants));
        const RHIBufferSlice cb = services.UploadConstants(&constants, sizeof(constants));

        if (!firstSlice.IsWritable() || !cb.IsWritable())
            return fail("[3/4] UploadConstants 가 쓸 수 없는 조각을 줬다\n");
        if (cb.offset < firstSlice.offset + firstSlice.size)
            return fail("[3/4] 링에서 자른 두 조각이 겹친다\n");
        const uint64_t uniformAlignment =
            resources.GetRequiredUploadAlignmentForTesting(RHIUploadUsage::ConstantBuffer);
        if (0 != (cb.offset % uniformAlignment))
            return fail("[3/4] 상수 정렬이 장치 하한과 다르다 — "
                + std::to_string(cb.offset) + "/" + std::to_string(uniformAlignment) + "\n");

        // ── 리드백도 계약이다 (5d 가 열었다) ──

        if (!services.CreateReadback(kVkDrawSize, kVkDrawSize, RHIFormat::RGBA8Unorm,
            1, readback, error))
        {
            return fail("[3/4] CreateReadback 실패: " + error + "\n");
        }

        VulkanEncoder& backendEncoder = static_cast<VulkanEncoder&>(services.GetImmediateEncoder());
        RHIEncoder& encoder = backendEncoder;

        // 지운 색은 (0, 0, 0.5) — 삼각형이 안 그려지면 파랑만 남는다.
        const float clearColor[4] = { 0.f, 0.f, 0.5f, 1.f };
        encoder.BindRenderTargets(targets);
        encoder.ClearRenderTargets(targets, clearColor);
        encoder.ClearDepthTarget(targets, 1.f);
        encoder.SetViewportAndScissor(kVkDrawSize, kVkDrawSize);
        encoder.SetPipeline(RHIBindPoint::Graphics, pipeline);
        encoder.SetPrimitiveTopology(RHIPrimitiveTopology::TriangleList);
        encoder.SetConstantBuffer(RHIBindPoint::Graphics, 0, cb);
        encoder.SetVertexBuffer(sceneBinding.vertices, sceneBinding.vertexStride);
        encoder.SetIndexBuffer(sceneBinding.indices, sceneBinding.indexFormat);
        encoder.DrawIndexed(sceneBinding.indexCount, 1);
        backendEncoder.EndRenderTargets();

        // 그래프 밖 전이도 계약으로 건다(V3 가 이 자리를 위해 만든 어휘다).
        const RHITransition transition{
            color, RHIResourceState::RenderTarget, RHIResourceState::CopySource };
        services.TransitionResources({ &transition, 1 });

        encoder.CopyToReadback(readback, color);

        if (!resources.EndFrame(error)) return fail("[3/4] EndFrame 실패: " + error + "\n");

        // M5-C3b2b1: 새 PSO를 먼저 만들고 방금 제출된 command buffer가 참조한
        // 옛 VkPipeline handle 하나만 무효화한다. 객체는 completion까지 보관한다.
        const RHICompletionPoint pipelineRetireAfter{
            resources.GetLastSignaledFenceValue() };
        RHIGraphicsPipelineDesc replacementDesc = pipelineDesc;
        replacementDesc.cullMode = RHICullMode::Back;
        if (!pipelineRequest.Replace(pipelineCache, replacementDesc,
            pipelineRetireAfter, error))
        {
            return fail("[3/4] Vulkan owning PSO 교체 실패: " + error + "\n");
        }
        const RHIPipelineHandle replacementPipeline = pipelineRequest.GetHandle();
        const bool stalePipelineRejected = !pipelineCache.Resolve(pipeline).IsValid();
        const bool replacementPublished = replacementPipeline.IsValid()
            && replacementPipeline != pipeline
            && pipelineCache.Resolve(replacementPipeline).IsValid();

        // 옛 desc의 next-use는 방금 비운 slot의 새 generation을 받아야 한다.
        const RHIPipelineHandle reloadedPipeline =
            pipelineCache.GetOrCreate(pipelineDesc, error);
        const auto reloadStats = pipelineCache.GetStats();
        const bool pipelineGenerationAdvanced = reloadedPipeline.IsValid()
            && RHIHandleBits::SlotOf(reloadedPipeline.id) == RHIHandleBits::SlotOf(pipeline.id)
            && RHIHandleBits::GenerationOf(reloadedPipeline.id) !=
                RHIHandleBits::GenerationOf(pipeline.id)
            && pipelineCache.Resolve(reloadedPipeline).IsValid();
        if (!stalePipelineRejected || !replacementPublished
            || !pipelineGenerationAdvanced || reloadStats.invalidations != 1
            || reloadStats.retiredPipelines != 1)
        {
            return fail("[3/4] Vulkan PSO generation invalidation/next-use 재요청 실패: "
                + error + "\n");
        }
        const VulkanDescriptorRecyclerStats descriptorPending =
            resources.GetDescriptorRecyclerStats();
        const bool descriptorVersionsPassed =
            descriptorPending.allocations > 0 &&
            0 == descriptorPending.allocationFailures &&
            descriptorPending.versions.versions >= 4 &&
            descriptorPending.versions.pending >= 4 &&
            descriptorPending.versions.submissions >= 4;
        if (!descriptorVersionsPassed)
        {
            return fail("[3/4] descriptor pool version 격리 실패"
                " (version " + std::to_string(descriptorPending.versions.versions)
                + " · pending " + std::to_string(descriptorPending.versions.pending)
                + " · 제출 " + std::to_string(descriptorPending.versions.submissions)
                + " · set " + std::to_string(descriptorPending.allocations)
                + " · 실패 " + std::to_string(descriptorPending.allocationFailures) + ")\n");
        }
        resources.WaitForGpu();
        const std::uint32_t collectedPipelines =
            pipelineCache.CollectRetiredPipelines(
                RHICompletionPoint{ resources.GetCompletedFenceValue() });
        const auto collectedPipelineStats = pipelineCache.GetStats();
        if (1 != collectedPipelines || 0 != collectedPipelineStats.retiredPipelines ||
            1 != collectedPipelineStats.retiredCollections)
        {
            return fail("[3/4] Vulkan PSO completion retirement 회수 실패\n");
        }

        // targeted 경로가 전체 invalidation 계약을 약화시키지 않았는지도 backend에서
        // 함께 확인한다. GPU는 이미 idle이고 두 live handle은 즉시 회수 가능하다.
        const RHICompletionPoint completedPipelinePoint{
            resources.GetCompletedFenceValue() };
        const std::uint32_t globallyInvalidated =
            pipelineCache.InvalidatePipelines(completedPipelinePoint);
        const bool globalHandlesRejected =
            !pipelineCache.Resolve(replacementPipeline).IsValid() &&
            !pipelineCache.Resolve(reloadedPipeline).IsValid();
        const std::uint32_t globallyCollected =
            pipelineCache.CollectRetiredPipelines(completedPipelinePoint);
        const auto globalPipelineStats = pipelineCache.GetStats();
        if (2 != globallyInvalidated || !globalHandlesRejected || 2 != globallyCollected ||
            2 != globalPipelineStats.invalidations ||
            0 != globalPipelineStats.retiredPipelines ||
            3 != globalPipelineStats.retiredCollections)
        {
            return fail("[3/4] Vulkan PSO global invalidation 회귀 실패\n");
        }
        const VulkanDescriptorRecyclerStats descriptorCollected =
            resources.GetDescriptorRecyclerStats();
        if (0 != descriptorCollected.versions.pending ||
            descriptorCollected.versions.available != descriptorCollected.versions.versions)
        {
            return fail("[3/4] descriptor pool completion 회수 실패\n");
        }
        outLog += "[3/4] descriptor pool versioned recycler 통과"
            " (무대기 flush 3회 · version "
            + std::to_string(descriptorPending.versions.versions)
            + " · set " + std::to_string(descriptorPending.allocations)
            + " · 완료 회수 " + std::to_string(descriptorCollected.versions.collections)
            + ")\n";

        // ── [2/4] 프레임 경계·타임라인 세마포어 ──

        {
            const uint64_t completedAfter = resources.GetCompletedFenceValue();
            const uint64_t signaled = resources.GetLastSignaledFenceValue();
            const bool advanced = completedAfter > completedBefore && completedAfter >= signaled;
            outLog += "[2/4] 프레임 경계·타임라인 세마포어 "
                + std::string(advanced ? "통과" : "실패")
                + " (완료값 " + std::to_string(completedBefore) + " → "
                + std::to_string(completedAfter) + " · 서명 "
                + std::to_string(signaled)
            + " · 입력 모델 정점 " + std::to_string(sceneVertexBytes / (1024 * 1024))
            + "MiB/인덱스 " + std::to_string(sceneIndexBytes / (1024 * 1024))
            + "MiB 첫 cache upload · 비동기 flush 3회"
            + " · 병렬 64MiB CAS 재시도 " + std::to_string(parallelCasRetries)
            + " · worker 생성 " + std::to_string(parallelWorkerCreates) + ")\n";
            if (!advanced) return fail("");
        }

        // ── 미구현 계수 — 통과 판정의 일부다 ──

        const uint32_t deviceStubs = resources.GetUnimplementedCount();
        const uint32_t encoderStubs = backendEncoder.GetUnimplementedCount();
        if (0 != deviceStubs || 0 != encoderStubs)
        {
            return fail("[3/4] 미구현 호출이 있었다 — 서비스 "
                + std::to_string(deviceStubs) + "건("
                + (nullptr != resources.GetLastUnimplemented()
                    ? resources.GetLastUnimplemented() : "-")
                + ") · 인코더 " + std::to_string(encoderStubs) + "건("
                + (nullptr != backendEncoder.GetLastUnimplemented()
                    ? backendEncoder.GetLastUnimplemented() : "-") + ")\n");
        }

        // ── 픽셀 검증·PNG ──

        RHIReadbackImage image;
        if (!services.MapReadback(readback, image, error))
            return fail("[3/4] MapReadback 실패: " + error + "\n");

        uint32_t greenPixels = 0;
        uint32_t bluePixels = 0;
        for (uint32_t y = 0; y < kVkDrawSize; ++y)
        {
            for (uint32_t x = 0; x < kVkDrawSize; ++x)
            {
                const float r = image.At(x, y, 0);
                const float g = image.At(x, y, 1);
                const float b = image.At(x, y, 2);
                if (g > 0.78f && r < 0.08f && b < 0.08f) ++greenPixels;
                if (b > 0.39f && r < 0.08f && g < 0.08f) ++bluePixels;
            }
        }

        // green은 실제 indexed draw와 b0 tint 도달을 함께 증명한다. bounds를
        // 70% 화면에 맞췄으므로 blue가 남아야 clear도 별도로 검증된다.
        const bool drewSceneMesh = greenPixels >= 16;
        const bool clearedOutside = bluePixels >= 16;
        if (!drewSceneMesh || !clearedOutside)
        {
            char detail[160]{};
            std::snprintf(detail, sizeof(detail),
                "[3/4] 입력 모델 indexed draw 픽셀 검증 실패 — green %u · blue %u\n",
                greenPixels, bluePixels);
            return fail(detail);
        }

        {
            DirectX::Image png{};
            png.width = kVkDrawSize;
            png.height = kVkDrawSize;
            png.format = DXGI_FORMAT_R8G8B8A8_UNORM;
            png.rowPitch = image.rowPitch;
            png.slicePitch = image.data.size();
            png.pixels = image.data.data();

            const std::wstring widePath(outputPngPath.begin(), outputPngPath.end());
            const HRESULT saved = DirectX::SaveToWICFile(png, DirectX::WIC_FLAGS_NONE,
                DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG), widePath.c_str());
            if (FAILED(saved)) return fail("[3/4] PNG 저장 실패\n");
        }

        services.ReleaseReadback(readback);
        services.ReleaseTexture(color);
        services.ReleaseTexture(depthTexture);
        resources.ReleaseBuffer(buffer);

        const auto stats = pipelineCache.GetStats();
        const RHIPersistentHeapStats meshHeap =
            meshCache.GetStats().persistentHeap;
        char line[512]{};
        std::snprintf(line, sizeof(line),
            "[3/4] 입력 모델 중립 mesh 경로 통과 — mesh %zu개 · 누적 %lluB"
            " · draw 정점 %lluB · 인덱스 %lluB"
            " · indexed %u · green %u · blue %u · PSO %u · reload collected %u · segment 사용 %uB"
            " · persistent %u장/%lluB 사용 · pooled %u"
            " · 표 잔량 %u장 · 미구현 0\n",
            sceneMeshes.size(), static_cast<unsigned long long>(sceneUploadBytes),
            static_cast<unsigned long long>(sceneVertexBytes),
            static_cast<unsigned long long>(sceneIndexBytes), sceneBinding.indexCount,
            greenPixels, bluePixels, stats.compiles, stats.retiredCollections,
            static_cast<uint32_t>(resources.GetUploadUsedBytes()),
            meshHeap.activeSegments,
            static_cast<unsigned long long>(meshHeap.allocatedBytes),
            meshHeap.livePooledAllocations,
            static_cast<uint32_t>(resources.GetResourceTable().LiveImageCount()));
        outLog += line;
    }

    // GPU 완료 뒤 pressure trim으로 Available segment만 파괴하고, 다음 생성이
    // generation이 오른 stable registry free slot을 재사용하는지 확인한다.
    {
        constexpr uint64_t kForcedSoftBudget = 16ull * 1024 * 1024;
        const RHIUploadStats beforeTrim = resources.GetUploadStats();
        resources.SetUploadBudgetForTesting(kForcedSoftBudget, 0, true);
        if (!resources.BeginFrame(error))
        {
            resources.ClearUploadBudgetOverrideForTesting();
            return fail("[2/4] budget trim BeginFrame 실패: " + error + "\n");
        }

        const RHIUploadStats afterCollect = resources.GetUploadStats();
        const bool staleInvalid = !resources.GetResourceTable().Resolve(
            parallelHandleForTrim).IsValid();

        // BeginFrame의 completion notification 뒤 같은 asset을 다시 물으면
        // 네이티브 buffer를 만들지 않고 기존 Resident binding을 돌려줘야 한다.
        meshCache.BeginFrame(1);
        std::string cacheHitError;
        const RHIMeshBinding cacheHit = meshCache.GetOrUploadModel(
            sceneMesh, cacheHitError);
        const VulkanMeshCache::Stats afterCacheHit = meshCache.GetStats();
        const bool cacheHitPassed = cacheHit.IsValid() &&
            cacheHit.vertices.buffer.id == sceneBinding.vertices.buffer.id &&
            cacheHit.indices.buffer.id == sceneBinding.indices.buffer.id &&
            sceneMeshes.size() == afterCacheHit.uploads && 0 != afterCacheHit.hits &&
            resources.GetResourceTable().Resolve(cacheHit.vertices.buffer).IsValid() &&
            resources.GetResourceTable().Resolve(cacheHit.indices.buffer).IsValid();

        const uint64_t cursorBeforeReject = resources.GetUploadUsedBytes();
        const std::array<RHIUploadRequest, 2> impossible = {{
            { 1, RHIUploadUsage::BufferCopy, 1 },
            { (std::numeric_limits<uint64_t>::max)(), RHIUploadUsage::BufferCopy, 1 }
        }};
        std::array<RHIBufferSlice, 2> rejectedSlices{};
        for (size_t i = 0; i < rejectedSlices.size(); ++i)
        {
            rejectedSlices[i].buffer.id = static_cast<uint32_t>(100 + i);
            rejectedSlices[i].offset = 200 + i;
            rejectedSlices[i].size = 300 + i;
            rejectedSlices[i].cpuAddress = reinterpret_cast<void*>(static_cast<uintptr_t>(400 + i));
        }
        const auto rejectedBefore = rejectedSlices;
        std::string rejectError;
        const bool rejected = !resources.ReserveUploadBatch(impossible,
            rejectedSlices, rejectError);
        bool outputsUnchanged = rejected &&
            cursorBeforeReject == resources.GetUploadUsedBytes();
        for (size_t i = 0; i < rejectedSlices.size(); ++i)
        {
            outputsUnchanged = outputsUnchanged &&
                rejectedSlices[i].buffer.id == rejectedBefore[i].buffer.id &&
                rejectedSlices[i].offset == rejectedBefore[i].offset &&
                rejectedSlices[i].size == rejectedBefore[i].size &&
                rejectedSlices[i].cpuAddress == rejectedBefore[i].cpuAddress;
        }
        const RHIBufferSlice recycled = resources.AllocateUpload(
            RHIUploadRequest{ 4096, RHIUploadUsage::BufferCopy, 16 });
        const RHIUploadStats afterRecreate = resources.GetUploadStats();
        if (!resources.EndFrame(error))
        {
            resources.ClearUploadBudgetOverrideForTesting();
            return fail("[2/4] budget trim EndFrame 실패: " + error + "\n");
        }
        resources.ClearUploadBudgetOverrideForTesting();

        // 실제 draw가 끝난 뒤 cache entry를 completion graveyard로 보내고,
        // 완료값 직전에는 보존·완료값 도달 시 handle generation이 무효화되는지 본다.
        resources.WaitForGpu();
        meshCache.BeginFrame(1 + VulkanMeshCache::kPressureRetireAfterFrames);
        const uint64_t retireCompletion = resources.GetLastSignaledFenceValue();
        RHIAssetEvictionPass pressureEviction = BeginRHIAssetEvictionPass(
            true, sceneUploadBytes);
        const uint64_t retiredBytes = meshCache.RetireUnused(retireCompletion,
            &pressureEviction);
        const bool aliveInGrave =
            resources.GetResourceTable().Resolve(sceneBinding.vertices.buffer).IsValid() &&
            resources.GetResourceTable().Resolve(sceneBinding.indices.buffer).IsValid();
        const uint64_t prematureFreed = meshCache.SweepGraveyard(retireCompletion - 1);
        const uint64_t completedFreed = meshCache.SweepGraveyard(retireCompletion);
        const bool invalidAfterCompletion =
            !resources.GetResourceTable().Resolve(sceneBinding.vertices.buffer).IsValid() &&
            !resources.GetResourceTable().Resolve(sceneBinding.indices.buffer).IsValid();
        const VulkanMeshCache::Stats afterRetire = meshCache.GetStats();
        const RHIPersistentHeapStats& retiredHeap = afterRetire.persistentHeap;
        const bool persistentRetirePassed = 0 == retiredHeap.allocatedBytes &&
            0 == retiredHeap.livePooledAllocations &&
            0 == retiredHeap.liveDedicatedAllocations &&
            retiredHeap.activeSegments <= 1 &&
            retiredHeap.emptySegments == retiredHeap.activeSegments;
        const bool retirePassed = retiredBytes == sceneUploadBytes &&
            aliveInGrave && 0 == prematureFreed && completedFreed == retiredBytes &&
            invalidAfterCompletion && sceneMeshes.size() == afterRetire.retired &&
            sceneMeshes.size() == afterRetire.eviction.pressureRetired &&
            sceneUploadBytes == afterRetire.eviction.pressureRetiredBytes &&
            sceneMeshes.size() == pressureEviction.pressureRetiredCount &&
            0 == afterRetire.residentCount && 0 == afterRetire.graveyardCount &&
            persistentRetirePassed;

        const bool trimPassed = afterCollect.trimmedSegments > beforeTrim.trimmedSegments &&
            afterCollect.segmentBytes < beforeTrim.segmentBytes && staleInvalid &&
            cacheHitPassed && retirePassed && outputsUnchanged && recycled.IsWritable() &&
            afterRecreate.registrySlotReuses > beforeTrim.registrySlotReuses &&
            afterRecreate.registryHighWater == beforeTrim.registryHighWater &&
            afterRecreate.budgetPressureEvents > beforeTrim.budgetPressureEvents &&
            afterRecreate.budgetRetries > beforeTrim.budgetRetries;
        if (!trimPassed)
        {
            return fail("[2/4] pressure trim/slot 재사용 검증 실패"
                " (trim " + std::to_string(afterCollect.trimmedSegments - beforeTrim.trimmedSegments)
                + " · 재사용 " + std::to_string(afterRecreate.registrySlotReuses - beforeTrim.registrySlotReuses)
                + " · high-water " + std::to_string(beforeTrim.registryHighWater)
                + "→" + std::to_string(afterRecreate.registryHighWater)
                + " · cache hit " + std::string(cacheHitPassed ? "보존" : "실패")
                + " · retire " + std::string(retirePassed ? "완료점" : "실패")
                + " · rollback " + std::string(outputsUnchanged ? "보존" : "손상") + ")\n");
        }
        outLog += "[2/4] pressure trim·slot 재사용 통과"
            " (trim " + std::to_string(afterCollect.trimmedSegments - beforeTrim.trimmedSegments)
            + " · 재사용 " + std::to_string(afterRecreate.registrySlotReuses - beforeTrim.registrySlotReuses)
            + " · high-water " + std::to_string(beforeTrim.registryHighWater)
            + "→" + std::to_string(afterRecreate.registryHighWater)
                + " · cache hit 보존 · pressure 3f retire 완료점/persistent 병합"
            + " · rollback " + std::string(outputsUnchanged ? "보존" : "손상") + ")\n";
    }

    // ── [4/4] 스왑체인 (숨김 창) ──

    {
        VkTestWindow window;
        if (!window.Create()) return fail("[4/4] 검사용 창 생성 실패\n");

        bool swapChainOk = resources.AttachSwapChain(window.handle,
            kVkTestWidth, kVkTestHeight, error);
        if (!swapChainOk)
        {
            outLog += "[4/4] AttachSwapChain 실패: " + error + "\n";
            window.Destroy();
            return fail("");
        }

        // 두 프레임을 돌린다. 백버퍼를 표시 가능 레이아웃으로만 옮기고 표시한다 —
        // 골격의 목적은 그림이 아니라 프레임 경계·획득·표시가 계약대로 도는지다.
        for (uint32_t frame = 0; frame < 2 && swapChainOk; ++frame)
        {
            if (!resources.BeginFrame(error))
            {
                outLog += "[4/4] BeginFrame 실패: " + error + "\n";
                swapChainOk = false;
                break;
            }

            VkTestImageBarrier(resources.GetCommandBuffer(),
                resources.GetBackBuffer(resources.GetBackBufferIndex()),
                VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0,
                VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, 0);

            if (!resources.EndFrame(error) || !resources.Present(error))
            {
                outLog += "[4/4] 제출·표시 실패: " + error + "\n";
                swapChainOk = false;
                break;
            }
        }

        // 획득 뒤 Abort해도 acquire semaphore/이미지가 남지 않아 다음
        // BeginFrame이 정상 진행되는지 확인한다.
        if (swapChainOk)
        {
            if (!resources.BeginFrame(error))
            {
                outLog += "[4/4] Abort 준비 BeginFrame 실패: " + error + "\n";
                swapChainOk = false;
            }
            else
            {
                const RHIBufferSlice abortedUpload = resources.AllocateUpload(
                    RHIUploadRequest{ 1024, RHIUploadUsage::BufferCopy, 4 });
                if (!abortedUpload.IsWritable()) swapChainOk = false;
                resources.AbortFrame();
            }
        }
        if (swapChainOk)
        {
            if (!resources.BeginFrame(error))
            {
                outLog += "[4/4] Abort 뒤 BeginFrame 실패: " + error + "\n";
                swapChainOk = false;
            }
            else
            {
                VkTestImageBarrier(resources.GetCommandBuffer(),
                    resources.GetBackBuffer(resources.GetBackBufferIndex()),
                    VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                    VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0,
                    VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, 0);
                if (!resources.EndFrame(error) || !resources.Present(error))
                {
                    outLog += "[4/4] Abort 뒤 제출·표시 실패: " + error + "\n";
                    swapChainOk = false;
                }
            }
        }

        if (swapChainOk && !resources.ResizeSwapChain(320, 200, error))
        {
            outLog += "[4/4] ResizeSwapChain 실패: " + error + "\n";
            swapChainOk = false;
        }

        resources.WaitForGpu();
        window.Destroy();

        if (!swapChainOk) return fail("");

        outLog += "[4/4] 스왑체인 통과 — 붙임·획득·표시 2프레임·Abort 복구·크기 변경\n";
    }

    // ── 검증 레이어가 조용해야 진짜 통과다 ──

    resources.WaitForGpu();
    meshCache.Shutdown();
    pipelineCache.Shutdown();

    std::string messages;
    const uint32_t problems = resources.DrainDebugMessages(messages);
    if (0 != problems)
    {
        outLog += "검증 레이어 경고 " + std::to_string(problems) + "건\n" + messages;
        return false;
    }

    outLog += "Vulkan 골격 검증 통과 — 검증 레이어 클린\n";
    return true;
}
