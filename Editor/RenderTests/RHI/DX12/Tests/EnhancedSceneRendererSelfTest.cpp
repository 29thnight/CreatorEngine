#include "../../../../../Engine/RenderEngine/RenderAllocator.h"
#include "RHI/DX12/Tests/Geometry/GraphSceneFixture.h"
#include "../../../../../Tools/regression/material_owner_checks.h"
#include "RHI/DX12/DX12Format.h"
#include "RHI/DX12/Tests/DX12SelfTest.h"
#include "RHIQueueContractTests.h"
#include "RHIQueueNativeTests.h"
#include "RHIQueueOwnershipTests.h"
#include "RenderQueueScheduleRg8Tests.h"
#include "RenderQueueExecutionRg8Tests.h"
#include "RHI/ShaderReflectionSelfTest.h"
#include "RHI/DX12/DX12DeviceResources.h"
#include "RHI/DX12/DX12PSOManager.h"
#include "RHI/DX12/DX12RootSignatureCache.h"
#include "RHI/RHIGraphicsPipelineRequest.h"
#include "DX12TestTextureRegistration.h"
#include "Render/Graph/EnhancedRenderGraph.h"
#include "Render/Scene/EnhancedSceneRenderer.h"
#include "Render/Scene/EnhancedCameraReplayInput.h"
#include "Render/Scene/EnhancedDrawReplayInput.h"
#include "Render/Scene/EnhancedLatticeReplayInput.h"
#include "Render/Core/EnhancedLivePipelineDesc.h"
#include "Render/Core/PassResourceRetirement.h"
#include "Render/Passes/Geometry/SceneSurfaceTargets.h"
#include "MaterialGraphSceneHost.h"
#include "MaterialGraphIndirectBatch.h"
#include "MaterialGraphSceneLookup.h"
#include "MaterialGraphSceneRefraction.h"
#include "MaterialGraphSceneSubsurface.h"
#include "MaterialGraphSceneVolume.h"
#include "MaterialGraphSceneAccess.h"
#include "RHI/RHIShaderSource.h"
#include "Render/Passes/Lighting/EnhancedSkyBoxPass.h"
// ★ 자기가 쓰는 것은 자기가 포함한다. 유니티 빌드가 같은 덩어리의
//   다른 파일에서 끌어와 주고 있어서 없어도 빌드가 됐는데, 파일 하나를
//   프로젝트에 더한 것만으로 묶음이 바뀌어 갑자기 터졌다.
#include "Render/Passes/Geometry/EnhancedShadowPass.h"
#include "Geometry/GraphSceneFixture.h"
#include "Render/Passes/Lighting/EnhancedSSGIPass.h"
#include "Render/Passes/Lighting/EnhancedLightTilePass.h"
#include "Render/Passes/Geometry/EnhancedSpritePass.h"
#include "Render/Passes/Lighting/EnhancedSSAOPass.h"
#include "Render/Passes/PostProcess/EnhancedPostChainPass.h"
#include "Render/Passes/UI/EnhancedUIPass.h"
#include "RHI/DX12/DX12GpuProfiler.h"
#include "RHI/DX12/DX12CommandListPool.h"
#include "RHISoftwareNativeTests.h"
#include "RHI/ParallelRecordingJobsSelfTest.h"
#include "RHI/DX12/DX12MeshCache.h"
#include "RHI/DX12/DX12PersistentHeap.h"
#include "RHI/DX12/DX12TextureCache.h"
#include "RHI/RHICompletionRetireQueue.h"
#include "RHI/IRenderDeviceServices.h"
#include "RHI/RHIAssetEvictionPolicy.h"
#include "RHI/RHIDeviceMemoryBudgetCoordinator.h"
#include "RHI/RHISubmissionThread.h"
#include "RHI/RHIPersistentHeapPolicy.h"
#include "RHI/RHIShaderSource.h"
#include "ShaderMeta.h"
#include "ShaderPermutationDomain.h"
#include "DataSystem.h"
#include "Material.h"
#include "RenderScene.h"
#include "Scene.h"
#include "SceneManager.h"
#include "CameraComponent.h"
#include "Render/Core/EnhancedLightPacking.h"
#include "Texture.h"
#include "TextureFramePins.h"
#include "PrimitiveRenderProxy.h"
#include "BoneRegion.h" // kMaxBones
#include "DataSystem.h"
#include "Assets/ModelAssetGeneration.h"
#include <mathematics/transform.hpp>
#include "PathFinder.h"
// 자가 검증이 만드는 쿼드가 Vertex를 쓴다. 유니티 빌드에서는 같은 블롭의
// 앞선 파일이 공급했다.
#include "Mesh.h"

#include <DirectXTex.h>
#include "JobScheduler.h"
#include <stdint.h>
#include "../../../../../Engine/Utility_Framework/StandardLibrary.h"
#include "RHI/RHIShaderCompiler.h"
#include "Render/Passes/Lighting/EnhancedSSSPass.h"
#include "Render/Passes/Lighting/EnhancedSSRPass.h"
#include "EnhancedSpriteRg5Tests.h"
#include "EnhancedScreenRg5Tests.h"
#include "EnhancedFinalRg5Tests.h"

namespace
{
    // 결과를 살려 두려고 옮기는 텍셀 수. 내용은 안 본다 — 소비자가 있다는
    // 사실만으로 그래프가 그 앞을 걷어내지 않는다.
    constexpr uint32_t kProbeTexels = 8;

    // 브링업 셰이더는 파일 의존을 만들지 않으려고 소스에 담는다. 실제 씬 셰이더는
    // PSOManager(3-4)가 ShaderSystem과 함께 관리한다.
    constexpr const char* kTriangleShaderFile = "SelfTest/Triangle.slang";

    // 체커보드 텍스처 파라미터. 픽셀 검증이 같은 상수로 기대값을 계산한다.
    constexpr uint32_t kTexSize = 64;      // 64*4 = 256바이트 행 — 업로드 정렬과 정확히 일치
    constexpr uint32_t kCheckerCells = 4;
    constexpr uint8_t  kColorA[4] = { 230, 40, 200, 255 };  // 마젠타
    constexpr uint8_t  kColorB[4] = { 250, 220, 40, 255 };  // 노랑

    bool ValidateShaderPermutation(std::string& outLog)
    {
        std::string error;
        RHIShaderPermutation first;
        RHIShaderPermutation reordered;
        RHIShaderPermutation changed;

        const bool populated =
            first.Set("TILE_SIZE", "16", error)
            && first.Enable("REFERENCE_PATH", error)
            && reordered.Enable("REFERENCE_PATH", error)
            && reordered.Set("TILE_SIZE", "16", error)
            && changed.Set("TILE_SIZE", "32", error)
            && changed.Enable("REFERENCE_PATH", error);
        if (!populated)
        {
            outLog += "[permutation] 구성 실패: " + error + "\n";
            return false;
        }

        std::string duplicateError;
        RHIShaderPermutation duplicate = first;
        const bool duplicateRejected =
            !duplicate.Set("TILE_SIZE", "32", duplicateError);

        std::string invalidError;
        RHIShaderPermutation invalid;
        const bool invalidRejected = !invalid.Set("1INVALID", "1", invalidError);

        const bool canonical = first.Entries() == reordered.Entries()
            && first.Key() == reordered.Key();
        const bool valueSensitive = !(first.Key() == changed.Key());
        const bool stableWidth = 32 == first.Key().Hex().size();
        if (!canonical || !valueSensitive || !stableWidth
            || !duplicateRejected || !invalidRejected)
        {
            outLog += "[permutation] 정렬/키/검증 계약 불일치\n";
            return false;
        }

        outLog += "[permutation] canonical key " + first.Key().Hex()
            + " · 순서 독립/값 구분/중복 거부 통과\n";
        return true;
    }

    bool ValidateShaderMeta(std::string& outLog)
    {
        const std::filesystem::path metaPath = RHIShaderSource::Resolve(
            "SelfTest/ShaderMetaFixture.shadermeta");
        const FileGuid guid = DataSystems->GetFileGuid(metaPath);
        ShaderMeta meta;
        std::string error;
        if (guid == FileGuid{}
            || !DataSystems->LoadShaderMetaGUID(guid, meta, error))
        {
            outLog += "[shadermeta] fixture load 실패: "
                + (error.empty() ? "asset catalog GUID가 없다" : error) + "\n";
            return false;
        }

        const auto* tint = std::get_if<std::array<float, 4>>(
            &meta.properties[0].defaultValue);
        const auto* roughness = std::get_if<float>(
            &meta.properties[1].defaultValue);
        RHIGraphicsPipelineDesc stateProbe{};
        meta.passes[0].state.ApplyTo(stateProbe);
        const bool fixtureMatches = ShaderMeta::kSchemaVersion == meta.schemaVersion
            && guid == meta.guid
            && "ShaderMetaFixture" == meta.name
            && std::filesystem::path("ShaderMetaFixture.slang") == meta.source
            && 3 == meta.properties.size() && nullptr != tint
            && 1.0f == (*tint)[0] && 0.5f == (*tint)[1]
            && nullptr != roughness && 0.5f == *roughness
            && std::holds_alternative<std::monostate>(
                meta.properties[2].defaultValue)
            && 1 == meta.keywords.size() && 2 == meta.keywords[0].values.size()
            && 1 == meta.passes.size() && meta.passes[0].vertex
            && meta.passes[0].pixel && !meta.passes[0].compute
            && ShaderPassQueue::Transparent == meta.passes[0].queue
            && RHIFillMode::Solid == stateProbe.fillMode
            && RHICullMode::Back == stateProbe.cullMode
            && stateProbe.depthEnable
            && RHICompareOp::LessEqual == stateProbe.depthFunc
            && RHIDepthWrite::Zero == stateProbe.depthWriteMask
            && stateProbe.blendEnable && !stateProbe.independentBlend;
        if (!fixtureMatches)
        {
            outLog += "[shadermeta] fixture 값/PSO state 계약 불일치\n";
            return false;
        }

        // 거부 사례는 저마다 **자기 규칙**으로 거부돼야 한다. 로더는 source 존재를
        // property·pass 해석보다 먼저 보므로, source 가 없으면 아래 사례들은 전부
        // "source 파일이 없다" 로 거부되고 겨냥한 규칙은 한 번도 실행되지 않는다.
        // 9-14 Slang 이관이 Triangle.hlsl 을 .slang 으로 옮기며 실제로 그렇게 됐다
        // (9-17 까지 중복·unknown-field 두 절이 무판정). 그래서 결함만 뺀 같은
        // 문서가 먼저 **받아들여져야** 한다 — 이 대조군이 붉으면 거부 판정은 무효다.
        constexpr std::string_view validControl = R"yaml(
schema: 1
name: ValidControl
source: Triangle.slang
properties:
  - { name: value, type: float, default: 0.0 }
passes:
  - name: Main
    vs: { entry: VSMain }
    ps: { entry: PSMain }
    state: { depthWrite: false }
    queue: opaque
)yaml";
        ShaderMeta invalid;
        std::string controlError;
        if (!ShaderMetaLoader::Parse(validControl, metaPath, guid, invalid, controlError))
        {
            outLog += "[shadermeta] 거부 사례의 정상 대조군이 거부됐다(fixture 전제 붕괴): "
                + controlError + "\n";
            return false;
        }

        constexpr std::string_view duplicateProperty = R"yaml(
schema: 1
name: InvalidDuplicate
source: Triangle.slang
properties:
  - { name: value, type: float, default: 0.0 }
  - { name: value, type: float, default: 1.0 }
passes:
  - { name: Main, vs: { entry: VSMain }, ps: { entry: PSMain }, queue: opaque }
)yaml";
        std::string duplicateError;
        const bool duplicateRejected = !ShaderMetaLoader::Parse(
            duplicateProperty, metaPath, guid, invalid, duplicateError)
            && std::string::npos != duplicateError.find("중복");

        constexpr std::string_view unknownState = R"yaml(
schema: 1
name: InvalidState
source: Triangle.slang
passes:
  - name: Main
    vs: { entry: VSMain }
    ps: { entry: PSMain }
    state: { depthWriet: false }
    queue: opaque
)yaml";
        std::string unknownError;
        const bool unknownRejected = !ShaderMetaLoader::Parse(
            unknownState, metaPath, guid, invalid, unknownError)
            && std::string::npos != unknownError.find("알 수 없는 field");

        constexpr std::string_view escapingSource = R"yaml(
schema: 1
name: InvalidPath
source: ../Triangle.slang
passes:
  - { name: Main, vs: { entry: VSMain }, ps: { entry: PSMain }, queue: opaque }
)yaml";
        std::string pathError;
        const bool pathRejected = !ShaderMetaLoader::Parse(
            escapingSource, metaPath, guid, invalid, pathError)
            && std::string::npos != pathError.find("상위 이동 없는 상대");
        if (!duplicateRejected || !unknownRejected || !pathRejected)
        {
            outLog += "[shadermeta] 중복/unknown-field/source 경계 거부 계약 불일치\n";
            // 어느 사례가 무슨 문장으로 거부됐는지 남긴다. 합친 한 줄만으로는
            // "규칙이 사라졌다" 와 "다른 규칙이 먼저 거부했다" 가 구분되지 않는다.
            outLog += "  duplicate " + std::string(duplicateRejected ? "ok" : "FAIL")
                + ": " + duplicateError + "\n  unknown-field "
                + (unknownRejected ? "ok" : "FAIL") + ": " + unknownError
                + "\n  source-path " + (pathRejected ? "ok" : "FAIL") + ": "
                + pathError + "\n";
            return false;
        }

        ShaderMetaPermutationStats stats;
        std::vector<ShaderMetaPermutation> permutations;
        const bool enumerated = ShaderPermutationDomain::EnumerateForBuild(
            meta, ShaderPermutationDomain::kDefaultBuildCompileLimit,
            permutations, stats, error);
        const bool fixturePermutationMatches = enumerated
            && 2 == stats.variantsPerPass && 2 == stats.compileRequests
            && 2 == permutations.size()
            && 0 == permutations[0].passIndex
            && 1 == permutations[0].selections.size()
            && 1 == permutations[0].defines.Entries().size()
            && 1 == permutations[1].defines.Entries().size()
            && "QUALITY" == permutations[0].selections[0].axis
            && "low" == permutations[0].selections[0].value
            && "0" == permutations[0].defines.Entries()[0].value
            && "high" == permutations[1].selections[0].value
            && "1" == permutations[1].defines.Entries()[0].value
            && !(permutations[0].key == permutations[1].key);

        std::vector<ShaderMetaPermutation> capped;
        ShaderMetaPermutationStats cappedStats;
        std::string capError;
        const bool capRejected = !ShaderPermutationDomain::EnumerateForBuild(
            meta, 1, capped, cappedStats, capError)
            && 2 == cappedStats.compileRequests && capped.empty()
            && std::string::npos != capError.find("상한");

        const std::array<std::uint16_t, 1> highSelection{ 1 };
        ShaderMetaPermutation resolvedHigh;
        std::string resolveError;
        const bool resolveMatches = fixturePermutationMatches
            && ShaderPermutationDomain::Resolve(
            meta, 0, highSelection, resolvedHigh, resolveError)
            && resolvedHigh.key == permutations[1].key
            && resolvedHigh.selections == permutations[1].selections
            && resolvedHigh.defines.Entries() == permutations[1].defines.Entries();

        ShaderMeta authoredOrder = meta;
        authoredOrder.keywords = {
            { "B_MODE", { "off", "on" } },
            { "A_MODE", { "off", "on" } },
        };
        ShaderMeta reorderedAxes = authoredOrder;
        std::ranges::reverse(reorderedAxes.keywords);
        const std::array<std::uint16_t, 2> authoredValues{ 1, 0 };
        const std::array<std::uint16_t, 2> reorderedValues{ 0, 1 };
        ShaderMetaPermutation authoredPermutation;
        ShaderMetaPermutation reorderedPermutation;
        std::string canonicalError;
        const bool canonicalAxes = ShaderPermutationDomain::Resolve(
            authoredOrder, 0, authoredValues, authoredPermutation, canonicalError)
            && ShaderPermutationDomain::Resolve(reorderedAxes, 0,
                reorderedValues, reorderedPermutation, canonicalError)
            && authoredPermutation.key == reorderedPermutation.key
            && authoredPermutation.selections == reorderedPermutation.selections
            && authoredPermutation.defines.Entries()
                == reorderedPermutation.defines.Entries();

        ShaderMeta secondPass = meta;
        ShaderPassDesc copiedPass = secondPass.passes.front();
        copiedPass.name += "Copy";
        secondPass.passes.push_back(std::move(copiedPass));
        ShaderMetaPermutation otherPass;
        const bool passSensitive = ShaderPermutationDomain::Resolve(
            secondPass, 1, highSelection, otherPass, canonicalError)
            && !(resolvedHigh.key == otherPass.key);

        if (!fixturePermutationMatches || !capRejected || !resolveMatches
            || !canonicalAxes || !passSensitive)
        {
            outLog += "[shadermeta permutation] 열거/key/define/cap 계약 불일치: "
                + (error.empty() ? resolveError : error) + "\n";
            return false;
        }

        outLog += "[shadermeta] schema 1 · catalog GUID · property 3 · keyword 1"
            " · pass/state · strict field/path 검증 통과\n";
        outLog += "[shadermeta permutation] variants/pass 2 · requests 2"
            " · GUID/pass/selection key · ordinal define · Build cap 통과\n";
        return true;
    }

    bool ValidateCompletionRetireQueue()
    {
        RHICompletionRetireQueue<uint32_t> queue;
        uint32_t released = 0;
        queue.Enqueue(RHICompletionPoint{ 7 }, 1u, 64);
        queue.Enqueue(RHICompletionPoint{}, 2u, 128); // signal 실패 quarantine

        const auto before = queue.Collect(RHICompletionPoint{ 6 },
            [&](uint32_t payload) { released += payload; });
        const auto exact = queue.Collect(RHICompletionPoint{ 7 },
            [&](uint32_t payload) { released += payload; });
        const auto cannotCollectQuarantine = queue.Collect(
            RHICompletionPoint{ ~uint64_t{ 0 } },
            [&](uint32_t payload) { released += payload; });

        const bool completionBoundary = 0 == before.count && 0 == before.bytes &&
            1 == exact.count && 64 == exact.bytes && 1 == released;
        const bool quarantineHeld = 0 == cannotCollectQuarantine.count &&
            1 == queue.GetPendingCount() && 128 == queue.GetPendingBytes() &&
            1 == queue.GetQuarantinedCount();

        const auto drained = queue.Drain(
            [&](uint32_t payload) { released += payload; });
        return completionBoundary && quarantineHeld &&
            1 == drained.count && 128 == drained.bytes && 3 == released &&
            queue.Empty();
    }

    bool CompileShader(const char* entry, const char* target,
        RHIShaderBlob& outBlob, std::string& outLog)
    {
        std::string error;
        if (!RHIShaderCompiler::CompileFile(kTriangleShaderFile, entry, target, outBlob, error))
        {
            outLog += error + "\n";
            return false;
        }
        return true;
    }

    // R6-b: RenderGraph의 리드백 배관은 backend native 객체를 전혀 요구하지
    // 않아야 한다. 이 encoder는 전달받은 중립 핸들과 인자만 기록한다.
    class R6bFakeReadbackEncoder final : public RHIEncoder
    {
    public:
        struct CopyRecord
        {
            enum class Kind : uint8_t { Texture, Volume, Partial, Buffer } kind;
            RHIBufferHandle  readback;
            RHITextureHandle texture;
            RHIBufferHandle  buffer;
            uint32_t         slice{ 0 };
            uint32_t         subresource{ 0 };
            uint64_t         sourceOffset{ 0 };
            uint64_t         bytes{ 0 };
        };

        void SetViewportAndScissor(uint32_t, uint32_t) override {}
        void SetPipeline(RHIBindPoint, RHIPipelineHandle) override {}
        void SetPrimitiveTopology(RHIPrimitiveTopology) override {}
        void SetBindings(RHIBindPoint, uint32_t, const RHIBindingTable&) override {}
        void SetSamplers(RHIBindPoint, uint32_t, const RHISamplerTable&) override {}
        void SetConstantBuffer(RHIBindPoint, uint32_t, const RHIBufferSlice&) override {}
        void SetRootBuffer(RHIBindPoint, uint32_t, const RHIBufferSlice&) override {}
        void SetVertexBuffer(const RHIBufferSlice&, uint32_t) override {}
        void SetIndexBuffer(const RHIBufferSlice&, RHIFormat) override {}
        void Draw(uint32_t, uint32_t, uint32_t, uint32_t) override {}
        void DrawIndexed(uint32_t, uint32_t, uint32_t, int32_t, uint32_t) override {}
        void Dispatch(uint32_t, uint32_t, uint32_t) override {}
        void BindRenderTargets(const RHIRenderTargetBinding&) override {}
        void ClearRenderTargets(const RHIRenderTargetBinding&, const float[4]) override {}
        void ClearDepthTarget(const RHIRenderTargetBinding&, float) override {}

        void ResourceBarriers(const RHIBarrierBatch& barriers) override
        {
            ++barrierBatches;
            textureTransitions += static_cast<uint32_t>(barriers.textureTransitions.size());
            bufferTransitions += static_cast<uint32_t>(barriers.bufferTransitions.size());
        }

        void UavBarrier(std::span<const RHITextureHandle>) override {}
        void UavBarrierBuffers(std::span<const RHIBufferHandle>) override {}
        void CopyResource(RHITextureHandle, RHITextureHandle) override {}
        void ClearUnorderedAccess(const RHIBindingDesc&, const float[4]) override {}

        void CopyToReadback(const RHIReadback& readback, RHITextureHandle source,
            uint32_t slice, uint32_t sourceSubresource) override
        {
            copies.push_back({ CopyRecord::Kind::Texture, readback.buffer, source, {},
                slice, sourceSubresource });
        }

        void CopyVolumeToReadback(const RHIReadback& readback, RHITextureHandle source,
            uint32_t sourceSubresource) override
        {
            copies.push_back({ CopyRecord::Kind::Volume, readback.buffer, source, {},
                0, sourceSubresource });
        }

        void CopyPartialToReadback(const RHIReadback& readback, RHITextureHandle source,
            uint32_t slice, uint32_t sourceSubresource) override
        {
            copies.push_back({ CopyRecord::Kind::Partial, readback.buffer, source, {},
                slice, sourceSubresource });
        }

        void CopyBufferToReadback(const RHIReadback& readback, RHIBufferHandle source,
            uint64_t sourceOffset, uint64_t bytes) override
        {
            copies.push_back({ CopyRecord::Kind::Buffer, readback.buffer, {}, source,
                0, 0, sourceOffset, bytes });
        }

        void CopyTexture(RHITextureHandle, RHITextureHandle, uint32_t, uint32_t) override {}
        void ClearRenderTargetRect(const RHIRenderTargetBinding&, const float[4],
            const RHIRect&) override {}

        std::vector<CopyRecord> copies;
        uint32_t barrierBatches{ 0 };
        uint32_t textureTransitions{ 0 };
        uint32_t bufferTransitions{ 0 };
    };

    class R6bFakeReadbackServices final : public IRenderDeviceServices
    {
    public:
        bool ReserveUploadBatch(std::span<const RHIUploadRequest>,
            std::span<RHIBufferSlice>, std::string&) override { return false; }
        RHIBufferSlice AllocateUpload(const RHIUploadRequest&) override { return {}; }
        uint64_t GetCurrentUploadRecordingId() const override { return currentRecording; }
        void RegisterUploadTransactionListener(IRHIUploadTransactionListener* listener) override
        {
            listeners.push_back(listener);
        }
        void UnregisterUploadTransactionListener(IRHIUploadTransactionListener* listener) override
        {
            std::erase(listeners, listener);
        }
        void AcceptRecording(RHICompletionPoint completion = {1})
        {
            const auto recording = GetCurrentUploadRecordingId();
            for (auto* listener : listeners)
            {
                listener->OnUploadAccepted(recording, completion);
            }
        }
        std::vector<IRHIUploadTransactionListener*> listeners;
        RHIBufferSlice UploadConstants(const void*, size_t) override { return {}; }
        RHISamplerTable CreateSamplers(std::span<const RHISamplerDesc>) override { return {}; }
        RHIEncoder& GetImmediateEncoder() override { return encoder; }
        RHIBindingTable CreateBindings(std::span<const RHIBindingDesc>) override { return {}; }
        // 이 대역에는 descriptor recycler 가 없다. 0 은 "기록 중인 버전이 없다"이고
        // 인터페이스가 그 뜻을 규정한다 — 판정하는 쪽이 0 을 값으로 읽지 않는다.
        uint64_t GetDescriptorVersionToken() const override { return 0; }
        RHIRenderTargetBinding CreateRenderTargets(
            std::span<const RHITextureHandle>, const RHIDepthTargetDesc*) override { return {}; }
        RHIRenderTargetBinding CreateRenderTargets(
            std::span<const RHIColorTargetDesc>, const RHIDepthTargetDesc*) override { return {}; }
        RHITextureInfo DescribeTexture(RHITextureHandle) const override { return {}; }
        void ReleaseTexture(RHITextureHandle handle) override
        {
            if (textureReleaseFailures)
            {
                --textureReleaseFailures;
                throw 1;
            }
            releasedTextures.push_back(handle);
        }
        void ReleaseBuffer(RHIBufferHandle handle) override { releasedBuffers.push_back(handle); }
        void TransitionResources(std::span<const RHITransition>) override {}
        void TransitionBuffers(std::span<const RHIBufferTransition>) override {}

        bool CreateBuffer(const RHIBufferDesc&, RHIBufferHandle& outHandle,
            std::string&) override
        {
            outHandle = RHIBufferHandle{ RHIHandleBits::Encode(nextHandle++, 1) };
            return true;
        }

        bool CreateTexture(const RHITextureDesc&, RHITextureHandle& outHandle,
            std::string&) override
        {
            outHandle = RHITextureHandle{ RHIHandleBits::Encode(nextHandle++, 1) };
            return true;
        }

        bool CreateReadback(uint32_t width, uint32_t height, RHIFormat format,
            uint32_t sliceCount, RHIReadback& outReadback, std::string&) override
        {
            outReadback = {};
            outReadback.buffer = RHIBufferHandle{ RHIHandleBits::Encode(nextHandle++, 1) };
            outReadback.width = width;
            outReadback.height = height;
            outReadback.rowPitch = width * 4;
            outReadback.format = format;
            outReadback.sliceCount = sliceCount;
            outReadback.sliceBytes = static_cast<size_t>(outReadback.rowPitch) * height;
            ++liveReadbacks;
            return true;
        }

        bool MapReadback(const RHIReadback& readback, RHIReadbackImage& outImage,
            std::string&) override
        {
            if (!readback.IsValid()) return false;
            outImage = {};
            outImage.width = readback.width;
            outImage.height = readback.height;
            outImage.rowPitch = readback.rowPitch;
            outImage.format = readback.format;
            outImage.sliceCount = readback.sliceCount;
            outImage.sliceBytes = readback.sliceBytes;
            outImage.data.resize(readback.sliceBytes * readback.sliceCount);
            return true;
        }

        void ReleaseReadback(RHIReadback& readback) override
        {
            if (readback.IsValid())
            {
                ++releasedReadbacks;
                --liveReadbacks;
            }
            readback = {};
        }

        bool CreateBufferReadback(uint64_t bytes, RHIReadback& outReadback,
            std::string&) override
        {
            outReadback = {};
            outReadback.buffer = RHIBufferHandle{ RHIHandleBits::Encode(nextHandle++, 1) };
            outReadback.width = static_cast<uint32_t>(bytes);
            outReadback.height = 1;
            outReadback.rowPitch = static_cast<uint32_t>(bytes);
            outReadback.sliceBytes = static_cast<size_t>(bytes);
            ++liveReadbacks;
            return true;
        }

        R6bFakeReadbackEncoder encoder;
        uint64_t currentRecording{ 1 };
        std::vector<RHITextureHandle> releasedTextures;
        std::vector<RHIBufferHandle> releasedBuffers;
        uint32_t textureReleaseFailures{};
        uint32_t nextHandle{ 100 };
        uint32_t liveReadbacks{ 0 };
        uint32_t releasedReadbacks{ 0 };
    };

    bool ValidatePassResourceRetirement(std::string& error)
    {
        const auto fail = [&](const char* reason) { error = reason; return false; };
        const auto texture = [](uint32_t slot) { return RHITextureHandle{RHIHandleBits::Encode(slot, 1)}; };
        {
            R6bFakeReadbackServices services;
            PassResourceRetirement retirement;
            retirement.Attach(services);
            retirement.ReserveRetirements(4);
            auto* listener = services.listeners.front();
            retirement.Retire(texture(1));
            services.AcceptRecording({7});
            // The retirement listener has already run, as it does before IBL's
            // acceptance callback. The device can still report the old recording.
            retirement.RetireAccepted(texture(2), {7});
            retirement.Retire(texture(3));
            services.currentRecording = 2;
            retirement.RetireAccepted(texture(4), {7});
            listener->OnUploadCompleted(6);
            if (retirement.PendingCount() != 4 || !services.releasedTextures.empty())
                return fail("accepted targets released before their exact completion");
            listener->OnUploadCompleted(7);
            listener->OnUploadAborted(1);
            listener->OnUploadCompleted(8);
            if (retirement.PendingCount() || services.releasedTextures.size() != 4)
                return fail("post-accept retirement missed its decision or released twice");
            for (uint32_t i = 0; i < 4; ++i)
                if (services.releasedTextures[i] != texture(i + 1))
                    return fail("retirement did not preserve the exact native handles");
            retirement.ClearAfterIdle();
        }
        {
            R6bFakeReadbackServices services;
            PassResourceRetirement retirement;
            retirement.Attach(services);
            auto* listener = services.listeners.front();
            retirement.Retire(texture(1));
            retirement.Retire(texture(2));
            services.AcceptRecording({7});
            services.textureReleaseFailures = 1;
            listener->OnUploadCompleted(7);
            if (retirement.PendingCount() != 1 || services.releasedTextures.size() != 1
                || services.releasedTextures.front() != texture(2))
                return fail("one release exception interrupted collection of other owners");
            listener->OnUploadCompleted(7);
            if (retirement.PendingCount() || services.releasedTextures.size() != 2
                || services.releasedTextures.back() != texture(1))
                return fail("failed release lost its retained handle before retry");
            retirement.ClearAfterIdle();
        }
        {
            R6bFakeReadbackServices services;
            PassResourceRetirement retirement;
            retirement.Attach(services);
            retirement.ReserveRetirements(4);
            auto* listener = services.listeners.front();
            retirement.Retire(texture(1));
            services.AcceptRecording({});
            retirement.RetireAccepted(texture(2), {});
            retirement.Retire(RHIBufferHandle{RHIHandleBits::Encode(3, 1)});
            listener->OnUploadAborted(1);
            listener->OnUploadSubmissionRejected(1, {});
            listener->OnUploadCompleted(UINT64_MAX);
            services.currentRecording = 2;
            retirement.Retire(texture(4));
            listener->OnUploadSubmissionRejected(2, {9});
            listener->OnUploadCompleted(UINT64_MAX);
            if (retirement.PendingCount() != 4 || !services.releasedTextures.empty()
                || !services.releasedBuffers.empty())
                return fail("accepted completion zero escaped idle quarantine");
            retirement.ClearAfterIdle();
            if (retirement.PendingCount() || services.releasedTextures.size() != 3
                || services.releasedBuffers.size() != 1)
                return fail("idle teardown did not release quarantined native owners");
        }
        {
            R6bFakeReadbackServices services;
            PassResourceRetirement retirement;
            retirement.Attach(services);
            auto* listener = services.listeners.front();
            uint32_t accepted = 0, rejected = 0;
            struct AcceptanceProbe final : IRHIUploadTransactionListener
            {
                uint32_t count{};
                void OnUploadSubmitted(uint64_t, RHICompletionPoint) override {}
                void OnUploadCompleted(uint64_t) override {}
                void OnUploadAborted(uint64_t) override {}
                void OnUploadAccepted(uint64_t, RHICompletionPoint) override { ++count; }
            } probe;
            services.RegisterUploadTransactionListener(&probe);
            retirement.WatchRecording([&] { ++rejected; }, [&]
            {
                ++accepted;
                services.currentRecording = 2;
                retirement.WatchRecording([&] { ++rejected; }, [&] { ++accepted; });
                throw 1;
            });
            retirement.WatchRecording([&] { ++rejected; }, [&] { ++accepted; });
            services.AcceptRecording({5});
            listener->OnUploadAborted(1);
            listener->OnUploadSubmissionRejected(1, {5});
            if (accepted != 2 || rejected || probe.count != 1)
                return fail("throwing accepted watch interrupted or rolled back acceptance");
            services.AcceptRecording({6});
            if (accepted != 3 || rejected || probe.count != 2)
                return fail("reentrant watch registration lost its next recording decision");
            services.UnregisterUploadTransactionListener(&probe);
            retirement.ClearAfterIdle();
        }
        {
            R6bFakeReadbackServices services;
            PassResourceRetirement retirement;
            retirement.Attach(services);
            auto* listener = services.listeners.front();
            uint32_t rejected = 0, accepted = 0;
            services.AcceptRecording({3});
            services.currentRecording = 2;
            retirement.Retire(texture(1));
            retirement.WatchRecording([&] { ++rejected; throw 1; }, [&] { ++accepted; });
            retirement.WatchRecording([&] { ++rejected; }, [&] { ++accepted; });
            listener->OnUploadSubmitted(2, {9});
            listener->OnUploadSubmissionRejected(2, {9});
            listener->OnUploadAborted(2);
            listener->OnUploadCompleted(2);
            if (rejected != 2 || accepted || retirement.PendingCount() != 1
                || !services.releasedTextures.empty())
                return fail("genuine rejection did not consume rollback watches exactly once");
            listener->OnUploadCompleted(3);
            if (retirement.PendingCount() || services.releasedTextures.size() != 1)
                return fail("rejected reservation replaced the preceding accepted completion");
            services.currentRecording = 3;
            retirement.WatchRecording([&] { ++rejected; }, [&] { ++accepted; });
            services.AcceptRecording({10});
            if (accepted != 1 || rejected != 2)
                return fail("rejection prevented a clean retry recording");
            retirement.ClearAfterIdle();
        }
        error.clear();
        return true;
    }

    bool ValidateRg1GraphFixtures(std::string& error)
    {
        R6bFakeReadbackServices services;
        const auto desc = [](const char* name) { RGTextureDesc d{}; d.width=4; d.height=4; d.allowRenderTarget=true; d.allowUnorderedAccess=true; d.name=name; return d; };
        using A = RGAccessMode;
        const auto mode = RGSchedulingMode::ExplicitSingleWriter;
        const auto fail = [&](const char* why) { error=why; return false; };
        std::array<int,4> permutation{0,1,2,3};
        do {
            EnhancedRenderGraph g(services,mode);
            const auto a=g.CreateTexture(desc("a")), b=g.CreateTexture(desc("b"));
            for(const auto p:permutation) {
                if(p==0) g.AddPass("A",{{a,RHIResourceState::UnorderedAccess,A::Write}},nullptr);
                if(p==1) g.AddPass("B",{{a,RHIResourceState::UnorderedAccess,A::Read},{b,RHIResourceState::RenderTarget,A::Write}},nullptr);
                if(p==2) g.AddPass("C",{{b,RHIResourceState::ShaderResource,A::Read}},nullptr,true);
                if(p==3) g.AddPass("independent",{},nullptr,true);
            }
            if(!g.Compile(error)) return false;
            const auto& order=g.GetExecuteOrder();
            std::vector<int> chain;
            for(const auto i:order) if(permutation[i]!=3) chain.push_back(permutation[i]);
            if(order.size()!=4 || chain!=std::vector<int>{0,1,2}) return fail("shuffle RAW chain/culling failure");
            // The independent pass is always ready. It must win over every
            // larger authored index while ready, including newly unblocked work.
            const auto independent=std::find(permutation.begin(),permutation.end(),3)-permutation.begin();
            for(const auto i:order) { if(i==independent) break; if(i>independent) return fail("unstable independent tie break"); }
            int nextChain=0;
            for(const auto i:order) {
                if(permutation[i]!=3) { ++nextChain; continue; }
                if(nextChain<3 && std::find(permutation.begin(),permutation.end(),nextChain)-permutation.begin()<independent)
                    return fail("newly ready lower index lost stable tie break");
            }
            EnhancedRenderGraph::DiagnosticSnapshot snap;
            if(!g.CaptureDiagnosticSnapshot(snap) || snap.reachabilityEdges.size()!=2) return fail("explicit UAV read lost dependency");
        } while(std::next_permutation(permutation.begin(),permutation.end()));
        EnhancedRenderGraph g(services,mode);
        auto a=g.CreateTexture(desc("cycle-a")), b=g.CreateTexture(desc("cycle-b"));
        g.AddPass("A",{{a,RHIResourceState::RenderTarget,A::Write},{b,RHIResourceState::ShaderResource,A::Read}},nullptr,true);
        g.AddPass("B",{{b,RHIResourceState::RenderTarget,A::Write},{a,RHIResourceState::ShaderResource,A::Read}},nullptr,true);
        if(g.Compile(error) || error.find("A --cycle-a--> B --cycle-b--> A")==std::string::npos
            || !g.GetExecuteOrder().empty()) return fail("cycle chain not rejected transactionally");
        g.Reset(); a=g.CreateTexture(desc("missing"));
        g.AddPass("missing",{{a,RHIResourceState::ShaderResource,A::Read}},nullptr,true);
        if(g.Compile(error) || error.find("missing writer")==std::string::npos) return fail("missing writer accepted");
        g.Reset(); a=g.ImportTexture(RHITextureHandle{123},RHIResourceState::UnorderedAccess,"history");
        g.AddPass("history",{{a,RHIResourceState::UnorderedAccess,A::ReadWrite}},nullptr);
        if(!g.Compile(error) || g.GetExecuteOrder().size()!=1) return fail("sole imported ReadWrite lost");
        g.AddPass("ambiguous",{{a,RHIResourceState::ShaderResource,A::Read}},nullptr,true);
        if(g.Compile(error) || error.find("ambiguous ReadWrite")==std::string::npos) return fail("ambiguous Modify accepted");
        g.Reset(); a=g.CreateTexture(desc("two-writers"));
        g.AddPass("W1",{{a,RHIResourceState::RenderTarget,A::Write}},nullptr);
        g.AddPass("W2",{{a,RHIResourceState::RenderTarget,A::Write}},nullptr,true);
        if(g.Compile(error) || error.find("multiple writers")==std::string::npos) return fail("multiwriter accepted");
        g.Reset(); a=g.CreateTexture(desc("legacy"));
        g.AddPass("implicit",{{a,RHIResourceState::RenderTarget}},nullptr,true);
        if(g.Compile(error)) return fail("implicit access accepted");
        g.Reset(); g.AddPass("invalid",{{RGHandle{},RHIResourceState::ShaderResource,A::Read}},nullptr,true);
        if(g.Compile(error)) return fail("invalid handle accepted");
        g.Reset(); a=g.CreateTexture(desc("duplicate"));
        g.AddPass("duplicate",{{a,RHIResourceState::RenderTarget,A::Write},{a,RHIResourceState::ShaderResource,A::Read}},nullptr,true);
        if(g.Compile(error)) return fail("duplicate access accepted");
        g.Reset(); a=g.CreateTexture(desc("wrong-state"));
        g.AddPass("wrong-state",{{a,RHIResourceState::ShaderResource,A::Write}},nullptr,true);
        if(g.Compile(error)) return fail("write in read state accepted");
        g.Reset(); a=g.CreateTexture(desc("uninitialized-modify"));
        g.AddPass("uninitialized-modify",{{a,RHIResourceState::UnorderedAccess,A::ReadWrite}},nullptr,true);
        if(g.Compile(error)) return fail("uninitialized Modify accepted");
        error.clear(); return true;
    }

    bool ValidateRg2GraphFixtures(std::string& error)
    {
        R6bFakeReadbackServices services;
        using A=RGAccessMode;
        const auto mode=RGSchedulingMode::ExplicitVersioned;
        const auto fail=[&](const char* why){error=why; return false;};
        RGTextureDesc d{}; d.width=4; d.height=4; d.allowRenderTarget=true; d.name="versioned";
        // Same physical storage: old reads must finish before either Modify or
        // a discard Write overwrites it, regardless of declaration order.
        for (const bool buffer:{false,true})
        {
            std::array<int,5> permutation{0,1,2,3,4};
            do {
                EnhancedRenderGraph g(services,mode);
                const auto initial=buffer ? g.ImportBuffer(RHIBufferHandle{124},RHIResourceState::Common,"buffer") : g.CreateTexture(d);
                const auto a=g.Write(initial), b=g.Modify(a), c=g.Write(b);
                if(!a.IsValid() || !b.IsValid() || !c.IsValid()) return fail("version declaration failed");
                for(const auto p:permutation) {
                    if(p==0) g.AddPass("W0",{{a,RHIResourceState::CopyDest,A::Write}},nullptr);
                    if(p==1) g.AddPass("R0",{{a,RHIResourceState::CopySource,A::Read}},nullptr,true);
                    if(p==2) g.AddPass("Modify",{{b,RHIResourceState::CopyDest,A::ReadWrite}},nullptr);
                    if(p==3) g.AddPass("R1",{{b,RHIResourceState::CopySource,A::Read}},nullptr,true);
                    if(p==4) g.AddPass("W2",{{c,RHIResourceState::CopyDest,A::Write}},nullptr,true);
                }
                if(!g.Compile(error)) return false;
                std::vector<int> actual; for(const auto p:g.GetExecuteOrder()) actual.push_back(permutation[p]);
                if(actual!=std::vector<int>{0,1,2,3,4}) return fail("RAW/WAR/WAW shuffle order");
                EnhancedRenderGraph::DiagnosticSnapshot snap;
                if(!g.CaptureDiagnosticSnapshot(snap)) return fail("version snapshot missing");
                using Reason=EnhancedRenderGraph::DiagnosticSnapshot::VersionEdge::Reason;
                for(const auto reason:{Reason::RAW,Reason::WAR,Reason::WAW})
                    if(std::none_of(snap.versionEdges.begin(),snap.versionEdges.end(),[&](const auto& e){return e.reason==reason;}))
                        return fail("version edge reason missing");
                if(snap.passes[0].usages[0].kind!=(buffer?RGResourceKind::Buffer:RGResourceKind::Texture)) return fail("resource kind lost");
            } while(std::next_permutation(permutation.begin(),permutation.end()));
        }
        EnhancedRenderGraph g(services,mode);
        auto initial=g.CreateTexture(d), a=g.Write(initial), b=g.Write(initial);
        g.AddPass("fork-a",{{a,RHIResourceState::CopyDest,A::Write}},nullptr,true);
        g.AddPass("fork-b",{{b,RHIResourceState::CopyDest,A::Write}},nullptr,true);
        if(g.Compile(error) || error.find("forked")==std::string::npos || !g.GetExecuteOrder().empty()) return fail("fork accepted");
        const auto stale=a; g.Reset(); initial=g.CreateTexture(d); a=g.Write(initial);
        g.AddPass("stale",{{stale,RHIResourceState::CopyDest,A::Write}},nullptr,true);
        if(g.Compile(error) || error.find("stale")==std::string::npos) return fail("Reset stale accepted");
        g.Reset(); initial=g.CreateTexture(d); a=g.Write(initial); auto wrong=a; wrong.kind=RGResourceKind::Buffer;
        g.AddPass("wrong-kind",{{wrong,RHIResourceState::CopyDest,A::Write}},nullptr,true);
        if(g.Compile(error)) return fail("wrong kind accepted");
        g.Reset(); initial=g.CreateTexture(d); a=g.Write(initial);
        EnhancedRenderGraph foreign(services,mode);
        foreign.CreateTexture(d);
        foreign.AddPass("foreign",{{a,RHIResourceState::CopyDest,A::Write}},nullptr,true);
        if(foreign.Compile(error)) return fail("foreign epoch accepted");
        g.AddPass("W0",{{a,RHIResourceState::CopyDest,A::Write}},nullptr);
        auto unissued=a; ++unissued.version;
        g.AddPass("unissued",{{unissued,RHIResourceState::CopySource,A::Read}},nullptr,true);
        if(g.Compile(error)) return fail("unissued version accepted");
        g.Reset(); initial=g.CreateTexture(d); a=g.Write(initial); b=g.Modify(a);
        g.AddPass("W0",{{a,RHIResourceState::CopyDest,A::Write}},nullptr);
        g.AddPass("wrong-modify",{{b,RHIResourceState::CopyDest,A::Write}},nullptr,true);
        if(g.Compile(error)) return fail("Modify represented as Write accepted");
        g.Reset(); initial=g.ImportBuffer(RHIBufferHandle{124},RHIResourceState::Common,"alias-a");
        auto alias=g.ImportBuffer(RHIBufferHandle{124},RHIResourceState::Common,"alias-b");
        g.AddPass("alias",{{initial,RHIResourceState::CopySource,A::Read},{alias,RHIResourceState::CopySource,A::Read}},nullptr,true);
        if(g.Compile(error) || error.find("duplicate physical import")==std::string::npos) return fail("physical alias accepted");
        g.Reset(); initial=g.CreateTexture(d); a=g.Write(initial);
        g.AddPass("missing",{{a,RHIResourceState::CopySource,A::Read}},nullptr,true);
        if(g.Compile(error) || error.find("missing version producer")==std::string::npos) return fail("missing accepted");
        g.Reset(); initial=g.CreateTexture(d);
        if(g.Modify(initial).IsValid()) return fail("uninitialized Modify accepted");
        g.Reset(); initial=g.ImportTexture(RHITextureHandle{123},RHIResourceState::CopySource,"history"); b=g.Modify(initial);
        g.AddPass("Modify",{{b,RHIResourceState::CopyDest,A::ReadWrite}},nullptr);
        g.AddPass("external-read",{{initial,RHIResourceState::CopySource,A::Read}},nullptr,true);
        if(!g.Compile(error) || g.GetExecuteOrder()!=std::vector<uint16_t>{1,0}) return fail("import v0 WAR failure");
        g.Reset(); initial=g.CreateTexture(d); a=g.Write(initial); b=g.Modify(a);
        g.AddPass("cycle-old-read",{{a,RHIResourceState::CopySource,A::Read},{b,RHIResourceState::CopySource,A::Read}},nullptr,true);
        g.AddPass("writer",{{a,RHIResourceState::CopyDest,A::Write}},nullptr);
        g.AddPass("modify",{{b,RHIResourceState::CopyDest,A::ReadWrite}},nullptr);
        // Duplicate physical access is rejected before scheduling; use a second
        // resource to construct a genuine RAW/WAR cycle below.
        if(g.Compile(error)) return fail("duplicate version access accepted");
        g.Reset(); initial=g.CreateTexture(d); a=g.Write(initial); b=g.Modify(a);
        auto token=g.Write(g.CreateTexture(d));
        g.AddPass("old-reader",{{a,RHIResourceState::CopySource,A::Read},{token,RHIResourceState::CopySource,A::Read}},nullptr,true);
        g.AddPass("writer",{{a,RHIResourceState::CopyDest,A::Write}},nullptr);
        g.AddPass("modify",{{b,RHIResourceState::CopyDest,A::ReadWrite},{token,RHIResourceState::CopyDest,A::Write}},nullptr);
        if(g.Compile(error) || error.find("RG2 cycle:")==std::string::npos || error.find(" v0")==std::string::npos) return fail("version cycle accepted");
        error.clear(); return true;
    }

    bool ValidateRg3GraphFixtures(std::string& error)
    {
        R6bFakeReadbackServices services;
        using A=RGAccessMode;
        const auto mode=RGSchedulingMode::ExplicitVersioned;
        const auto fail=[&](const char* why){error=why; return false;};
        RGTextureDesc d{}; d.width=4; d.height=4; d.allowRenderTarget=true; d.allowUnorderedAccess=true; d.name="live";
        std::array<int,5> permutation{0,1,2,3,4};
        do {
            EnhancedRenderGraph g(services,mode);
            const auto a=g.Write(g.CreateTexture(d)), dead=g.Write(a), b=g.Write(dead);
            const auto unused=g.Write(g.CreateTexture(d));
            for(const auto p:permutation) {
                if(p==0) g.AddPass("W0",{{a,RHIResourceState::RenderTarget,A::Write}},nullptr);
                if(p==1) g.AddPass("R0",{{a,RHIResourceState::CopySource,A::Read}},nullptr,true);
                if(p==2) g.AddPass("dead-W1",{{dead,RHIResourceState::UnorderedAccess,A::Write}},nullptr);
                if(p==3) g.AddPass("W2",{{b,RHIResourceState::RenderTarget,A::Write}},nullptr);
                if(p==4) g.AddPass("R2",{{b,RHIResourceState::CopySource,A::Read}},nullptr,true);
            }
            g.AddPass("dead-resource",{{unused,RHIResourceState::RenderTarget,A::Write}},nullptr);
            if(!g.Compile(error)) return false;
            std::vector<int> actual; for(const auto p:g.GetExecuteOrder()) { if(p>=permutation.size()) return fail("dead resource survived"); actual.push_back(permutation[p]); }
            if(actual!=std::vector<int>{0,1,3,4} || g.GetStats().passesCulled!=2) return fail("dead version/order mismatch");
            EnhancedRenderGraph::DiagnosticSnapshot snap;
            if(!g.CaptureDiagnosticSnapshot(snap) || snap.resources[1].used || snap.resources[0].firstUse!=0 || snap.resources[0].lastUse!=3)
                return fail("sorted lifetime/dead allocation mismatch");
            uint32_t first=99,last=99;
            if(!g.GetTransientLifetime(b,first,last) || first!=0 || last!=3 || g.GetTransientLifetime(unused,first,last)) return fail("lifetime query mismatch");
            const auto index=[&](int role){return static_cast<size_t>(std::find(permutation.begin(),permutation.end(),role)-permutation.begin());};
            if(!snap.passes[index(2)].culled || !snap.passes[index(2)].barriers.empty() || !snap.passes[5].barriers.empty()) return fail("dead barriers retained");
            const auto transition=[&](int role,RHIResourceState before,RHIResourceState after){
                const auto& barriers=snap.passes[index(role)].barriers;
                return barriers.size()==1 && !barriers[0].uav && barriers[0].before==before && barriers[0].after==after;
            };
            if(!transition(0,RHIResourceState::Common,RHIResourceState::RenderTarget) ||
                !transition(1,RHIResourceState::RenderTarget,RHIResourceState::CopySource) ||
                !transition(3,RHIResourceState::CopySource,RHIResourceState::RenderTarget) ||
                !transition(4,RHIResourceState::RenderTarget,RHIResourceState::CopySource)) return fail("sorted transition mismatch");
            using Reason=EnhancedRenderGraph::DiagnosticSnapshot::VersionEdge::Reason;
            const auto& edges=snap.versionEdges;
            if(std::none_of(edges.begin(),edges.end(),[&](const auto& e){return e.producer==index(1) && e.consumer==index(3) && e.reason==Reason::WAR;}))
                return fail("WAR bridge lost across culled version");
            if(std::any_of(edges.begin(),edges.end(),[&](const auto& e){return e.producer==index(2) || e.consumer==index(2);})) return fail("culled edge retained");
        } while(std::next_permutation(permutation.begin(),permutation.end()));
        EnhancedRenderGraph g(services,mode);
        auto initial=g.ImportTexture(RHITextureHandle{123},RHIResourceState::ShaderResource,"output");
        auto a=g.Write(initial), b=g.Write(a);
        g.AddPass("discarded",{{a,RHIResourceState::RenderTarget,A::Write}},nullptr);
        g.AddPass("output",{{b,RHIResourceState::RenderTarget,A::Write}},nullptr);
        if(!g.Compile(error) || g.GetExecuteOrder()!=std::vector<uint16_t>{1}) return fail("import final version root mismatch");
        g.Reset(); a=g.Write(g.CreateTexture(d)); b=g.Write(a);
        g.AddPass("output-read",{{b,RHIResourceState::CopySource,A::Read}},nullptr,true);
        g.AddPass("dead-input",{{a,RHIResourceState::RenderTarget,A::Write}},nullptr);
        g.AddPass("dead-reader",{{a,RHIResourceState::CopySource,A::Read}},nullptr);
        g.AddPass("output-write",{{b,RHIResourceState::RenderTarget,A::Write}},nullptr);
        if(!g.Compile(error) || g.GetExecuteOrder()!=std::vector<uint16_t>{3,0}) return fail("WAR kept dead reader/producer alive");
        g.Reset(); a=g.Write(g.CreateTexture(d));
        g.AddPass("dead-invalid-access",{{a,RHIResourceState::RenderTarget}},nullptr);
        if(g.Compile(error)) return fail("dead invalid declaration bypassed validation");
        g.Reset(); initial=g.CreateTexture(d); a=g.Write(initial); b=g.Modify(a);
        g.AddPass("Modify",{{b,RHIResourceState::UnorderedAccess,A::ReadWrite}},nullptr,true);
        g.AddPass("input",{{a,RHIResourceState::UnorderedAccess,A::Write}},nullptr);
        if(!g.Compile(error) || g.GetExecuteOrder()!=std::vector<uint16_t>{1,0}) return fail("Modify input culled");
        for(const bool buffer:{false,true}) {
            g.Reset(); initial=buffer ? g.ImportBuffer(RHIBufferHandle{124},RHIResourceState::UnorderedAccess,"uav-buffer") : g.ImportTexture(RHITextureHandle{123},RHIResourceState::UnorderedAccess,"uav-texture");
            a=g.Modify(initial); b=g.Modify(a);
            g.AddPass("W1",{{a,RHIResourceState::UnorderedAccess,A::ReadWrite}},nullptr);
            g.AddPass("R1",{{a,RHIResourceState::UnorderedAccess,A::Read}},nullptr,true);
            g.AddPass("R1-again",{{a,RHIResourceState::UnorderedAccess,A::Read}},nullptr,true);
            g.AddPass("W2",{{b,RHIResourceState::UnorderedAccess,A::ReadWrite}},nullptr);
            if(!g.Compile(error) || g.GetExecuteOrder()!=std::vector<uint16_t>{0,1,2,3}) return fail("UAV access order mismatch");
            for(uint16_t p=0;p<4;++p) if(g.GetPassBarrierCount({p})!=(p==2?0u:1u)) return fail("UAV write/read hazard mismatch");
        }
        const auto stale=a; g.Reset(); initial=g.Write(g.CreateTexture(d));
        g.AddPass("new",{{initial,RHIResourceState::RenderTarget,A::Write}},nullptr,true);
        uint32_t first,last;
        if(!g.Compile(error) || g.GetTransientLifetime(stale,first,last)) return fail("stale lifetime accepted");
        error.clear(); return true;
    }

    bool ValidateRg4GraphFixtures(std::string& error, std::string& dump)
    {
        R6bFakeReadbackServices services;
        using A=RGAccessMode;
        RGTextureDesc d{}; d.width=4; d.height=4; d.allowRenderTarget=true; d.name="wave";
        std::array<int,4> permutation{0,1,2,3};
        do {
            EnhancedRenderGraph g(services,RGSchedulingMode::ExplicitVersioned);
            const auto a=g.Write(g.CreateTexture(d)), b=g.Write(g.CreateTexture(d));
            for(const auto role:permutation) {
                if(role==0) g.AddPass("A",{{a,RHIResourceState::RenderTarget,A::Write}},nullptr);
                if(role==1) g.AddPass("B",{{a,RHIResourceState::CopySource,A::Read},{b,RHIResourceState::CopyDest,A::Write}},nullptr);
                if(role==2) g.AddPass("C",{{b,RHIResourceState::CopySource,A::Read}},nullptr,true);
                if(role==3) g.AddPass("D",{},nullptr,true);
            }
            const auto dead=g.Write(g.CreateTexture(d));
            g.AddPass("dead",{{dead,RHIResourceState::RenderTarget,A::Write}},nullptr);
            EnhancedRenderGraph::DiagnosticSnapshot s;
            if(!g.Compile(error) || !g.CaptureDiagnosticSnapshot(s)) return false;
            for(size_t p=0;p<4;++p) if(s.dependencyWaves[p]!=(permutation[p]==3 ? 0 : permutation[p])) { error="dependency wave mismatch"; return false; }
            std::vector<int> path; for(const auto p:s.criticalPath) path.push_back(permutation[p]);
            if(path!=std::vector<int>{0,1,2} || s.dependencyWaves[4]!=-1 || g.GetStats().dependencyWaveCount!=3)
            { error="critical path/culled wave mismatch"; return false; }
            if(dump.empty()) {
                dump="RG4_DIAGNOSTIC waves=";
                for(size_t p=0;p<s.dependencyWaves.size();++p) dump+=s.passes[p].name+":"+std::to_string(s.dependencyWaves[p])+",";
                dump+=" criticalPath="; for(const auto p:s.criticalPath) dump+=s.passes[p].name+",";
                for(const auto& e:s.versionEdges) dump+=" edge="+s.passes[e.producer].name+"->"+s.passes[e.consumer].name+
                    ":resource"+std::to_string(e.resource)+":v"+std::to_string(e.version)+":reason"+std::to_string(static_cast<int>(e.reason));
                dump+="\n";
            }
            const auto validViewerGraph = [](const EnhancedRenderGraph::DiagnosticSnapshot& snapshot) {
                if (snapshot.passes.size() != 5 || snapshot.versionEdges.size() != 2 || snapshot.executeOrder.size() != 4)
                {
                    return false;
                }
                for (const auto& edge : snapshot.versionEdges)
                {
                    if (edge.producer >= snapshot.passes.size() || edge.consumer >= snapshot.passes.size() ||
                        edge.reason != EnhancedRenderGraph::DiagnosticSnapshot::VersionEdge::Reason::RAW)
                    {
                        return false;
                    }
                    const auto producer = std::ranges::find(snapshot.executeOrder, edge.producer);
                    const auto consumer = std::ranges::find(snapshot.executeOrder, edge.consumer);
                    if (producer == snapshot.executeOrder.end() || consumer == snapshot.executeOrder.end() || producer >= consumer)
                    {
                        return false;
                    }
                }
                return true;
            };
            if (!validViewerGraph(s))
            {
                error = "RG-V snapshot lost compiled ordering/edges";
                return false;
            }
            auto missingEdge = s;
            missingEdge.versionEdges.pop_back();
            auto wrongOrder = s;
            std::reverse(wrongOrder.executeOrder.begin(), wrongOrder.executeOrder.end());
            if (validViewerGraph(missingEdge) || validViewerGraph(wrongOrder))
            {
                error = "RG-V mutation accepted";
                return false;
            }
            s.viewId = 17;
            s.frameId = 3;
            s.historyRevision = 8;
            s.sceneEpoch = 19;
            s.width = 640;
            s.height = 480;
            EnhancedLiveDisplayEntrySnapshot view;
            view.active = view.ready = true;
            view.key.viewId = 17;
            view.key.historyRevision = 8;
            view.completedSceneEpoch = 19;
            view.completedWidth = 640;
            view.completedHeight = 480;
            if (!EnhancedGraphSnapshotMatchesView(s, view))
            {
                error = "RG-V matching view rejected";
                return false;
            }
            auto stale = s;
            stale.sceneEpoch++;
            if (EnhancedGraphSnapshotMatchesView(stale, view))
            {
                error = "RG-V stale scene accepted";
                return false;
            }
            stale = s;
            stale.historyRevision++;
            if (EnhancedGraphSnapshotMatchesView(stale, view))
            {
                error = "RG-V stale history accepted";
                return false;
            }
            stale = s;
            stale.width++;
            if (EnhancedGraphSnapshotMatchesView(stale, view))
            {
                error = "RG-V stale resize accepted";
                return false;
            }
            view.key.viewId++;
            if (EnhancedGraphSnapshotMatchesView(s, view))
            {
                error = "RG-V wrong view accepted";
                return false;
            }
            const auto bytes = s.StorageBytes();
            g.Reset();
            EnhancedRenderGraph::DiagnosticSnapshot unavailable;
            if (g.CaptureDiagnosticSnapshot(unavailable) || !validViewerGraph(s) || s.StorageBytes() != bytes)
            {
                error = "RG-V snapshot mutated after graph reset";
                return false;
            }
        } while(std::next_permutation(permutation.begin(),permutation.end()));
        error.clear(); return true;
    }

    bool ValidateRg5LookupDeclarations(DX12DeviceResources& resources, std::string& error)
    {
        DX12PSOManager psoManager;
        DX12RootSignatureCache rootSignatures;
        if (!psoManager.Initialize(&resources, L"dx12_rg5_lookup.cache", error) ||
            !rootSignatures.Initialize(&resources, error) || !resources.BeginFrame(error))
        {
            return false;
        }
        material_graph::SceneLookupCache cache;
        struct Scope
        {
            DX12DeviceResources& resources;
            material_graph::SceneLookupCache& cache;
            ~Scope()
            {
                resources.AbortFrame();
                cache.ShutdownAfterIdle();
            }
        } scope{resources, cache};
        EnhancedFrameContext frame{};
        frame.resources = &resources;
        frame.psoManager = &psoManager;
        frame.rootSignatures = &rootSignatures;
        frame.width = 4;
        frame.height = 4;
        frame.frameId = 1;
        for (const auto policy : {RGOrderPolicy::DependencyOrder, RGOrderPolicy::PreserveDeclarationOrder})
        {
            own::shared_owner<const material_graph::SceneLookupFrame> owner;
            if (!cache.Prepare(frame, 1, {}, {}, {}, 0, 1024 * 1024, owner, error, {}, {}, true))
            {
                return false;
            }
            EnhancedRenderGraph graph(resources, RGSchedulingMode::ExplicitVersioned, policy);
            SceneSurfaceTargets gbuffer;
            gbuffer.Declare(graph, frame);
            owner->DeclareInputs(graph);
            bool rejected = false;
            try
            {
                owner->DeclareCaptureOutputs(graph, 10, 2);
            }
            catch (const std::runtime_error&)
            {
                rejected = true;
            }
            if (!rejected)
            {
                error = "RG5 lookup accepted invalid capture range";
                return false;
            }
            for (unsigned iteration = 0; iteration < 2; ++iteration)
            {
                for (unsigned part = 0; part < 2; ++part)
                {
                    const unsigned first = part ? 8 : 0;
                    const unsigned count = part ? 3 : 8;
                    const auto targets = owner->DeclareCaptureOutputs(graph, first, count);
                    std::vector<EnhancedRenderGraph::RGPassUsage> uses;
                    for (unsigned i = first; i < first + count; ++i)
                    {
                        if (targets[i].version != iteration + 1)
                        {
                            error = "RG5 lookup capture owner returned stale version";
                            return false;
                        }
                        uses.push_back({targets[i], RHIResourceState::RenderTarget, RGAccessMode::Write});
                    }
                    graph.AddPass("RG5.LookupCapture", uses, nullptr);
                }
                owner->DeclareBake(graph, gbuffer.GetOutputs().bitmask);
                if (owner->GraphSamples(graph).version != iteration + 1 ||
                    owner->GraphStatistics(graph).version != iteration + 1)
                {
                    error = "RG5 lookup bake outputs did not advance";
                    return false;
                }
                owner->DeclareReady(graph);
            }
            EnhancedRenderGraph::DiagnosticSnapshot snapshot;
            if (!graph.Compile(error) || !graph.CaptureDiagnosticSnapshot(snapshot))
            {
                return false;
            }
            for (const auto& pass : snapshot.passes)
            {
                for (const auto& usage : pass.usages)
                {
                    if (usage.access == RGAccessMode::LegacyState)
                    {
                        error = "RG5 lookup owner still declares legacy access";
                        return false;
                    }
                }
            }
            bool hasWar = false;
            for (const auto& edge : snapshot.versionEdges)
            {
                if (edge.reason == EnhancedRenderGraph::DiagnosticSnapshot::VersionEdge::Reason::WAR)
                {
                    hasWar = true;
                }
            }
            if (!hasWar)
            {
                error = "RG5 lookup repeated capture/bake lost old-reader dependency";
                return false;
            }
            ++frame.frameId;
        }
        error.clear();
        return true;
    }
    bool ValidateRg5SpecialDeclarations(DX12DeviceResources& resources, std::string& error)
    {
        using namespace material_graph;
        DX12PSOManager psoManager;
        DX12RootSignatureCache rootSignatures;
        if (!psoManager.Initialize(&resources, L"dx12_rg5_special.cache", error) ||
            !rootSignatures.Initialize(&resources, error) || !resources.BeginFrame(error))
        {
            return false;
        }
        SceneLookupCache cache;
        SceneRefractionResources refractions;
        SceneSubsurfaceResources subsurfaces;
        SceneVolumeResources volumes;
        struct Scope
        {
            DX12DeviceResources& resources;
            SceneLookupCache& cache;
            SceneRefractionResources& refractions;
            SceneSubsurfaceResources& subsurfaces;
            SceneVolumeResources& volumes;
            ~Scope()
            {
                resources.AbortFrame();
                cache.ShutdownAfterIdle();
                refractions.ShutdownAfterIdle();
                subsurfaces.ShutdownAfterIdle();
                volumes.ShutdownAfterIdle();
            }
        } scope{resources, cache, refractions, subsurfaces, volumes};
        EnhancedFrameContext frame{};
        frame.resources = &resources;
        frame.psoManager = &psoManager;
        frame.rootSignatures = &rootSignatures;
        frame.width = 4;
        frame.height = 4;
        frame.frameId = 1;
        // A closed tetrahedron and retained synthetic material identify two media.
        // These fixtures compile declarations; they do not dispatch material coefficients.
        Generation generationValue;
        generationValue.generation = 1;
        generationValue.cooked.product.program.volume = true;
        const auto generation = own::make_shared<const Generation>(std::move(generationValue));
        Instance instanceValue;
        instanceValue.generation = generation;
        instanceValue.description.graphId = generation->assetId;
        const auto instance = own::make_shared<const Instance>(std::move(instanceValue));
        EnhancedDrawItem draw{};
        draw.geometryKey = 1;
        draw.materialGraphInstance = instance;
        draw.worldMatrix = math::matrix4x4::identity();
        draw.coverage.flags = EnhancedMaterialCoverage::Enabled;
        GraphSceneFixture geometryFixture;
        std::vector<::Vertex> vertices(4);
        const std::array<math::vector3, 4> positions{{{0,0,0}, {1,0,0}, {0,1,0}, {0,0,1}}};
        for (size_t i = 0; i < vertices.size(); ++i)
        {
            vertices[i].position = positions[i];
            vertices[i].normal = {0,0,1};
            vertices[i].tangent = {1,0,0};
        }
        const auto fixtureGeometry = geometryFixture.Draw(geometryFixture.AddMesh(vertices, {0,2,1,0,1,3,0,3,2,1,2,3}), draw.worldMatrix);
        draw.modelMeshView = fixtureGeometry.modelMeshView;
        auto descriptorPins = own::make_shared<assets::ModelAssetGenerationPins>();
        assets::ModelGeometryPreparationPins geometryPins;
        geometryFixture.BuildPins(*descriptorPins, geometryPins);
        auto second = draw;
        second.geometryKey = 2;
        const std::array<EnhancedDrawItem, 2> draws{draw, second};
        SceneInputView view{1, 1, 1, 1, 4, 4};
        view.camera.view = math::matrix4x4::identity();
        view.camera.projection = math::matrix4x4::identity();
        own::shared_owner<const SceneViewInput> input;
        if (!SceneViewInput::Seal(view, draws, {}, input, error, descriptorPins, {}, &geometryPins))
        {
            return false;
        }
        LX::Runtime::CompiledCompute shader;
        if (!LX::Runtime::CompileCompute(RHIShaderSource::Resolve("MaterialGraphSceneVolumeBake.slang").string(),
                                        "LXSceneVolumeCompositeCS", {}, {}, shader, error))
        {
            return false;
        }
        const RHIPipelineLayoutParam parameters[]{RHILayout::Cbv(1), RHILayout::SrvTable(2, 0), RHILayout::Srv(17),
            RHILayout::Srv(18), RHILayout::SrvTable(2, 19), RHILayout::UavTable(1, 0)};
        const RHIStaticSamplerDesc samplers[]{
            {RHISampler::Linear(RHIAddressMode::Clamp), 2},
            {RHISampler::Comparison(RHICompareOp::LessEqual, RHIAddressMode::Border, RHIBorderColor::OpaqueWhite), 3}};
        RHIComputePipelineDesc desc{};
        desc.layout = rootSignatures.GetOrCreate({parameters, samplers}, error);
        desc.csBytecode = shader.stage.bytecode.Data();
        desc.csSize = shader.stage.bytecode.Size();
        LX::Runtime::ComputePipeline coefficientFixture;
        if (!desc.layout.IsValid() || !coefficientFixture.Create(psoManager, desc, std::move(shader.description), error))
        {
            return false;
        }
        auto bindings = own::make_shared<RenderBindings>();
        auto instancePins = own::make_shared<InstanceFramePins>();
        bindings->instancePinIndex = instancePins->Retain(instance);
        bindings->instance = instancePins->Borrow(bindings->instancePinIndex);
        bindings->instancePins = std::move(instancePins);
        const std::array<float, 3> normal{0, 0, 1};
        const auto constants = resources.UploadConstants(normal.data(), sizeof(normal));
        const SceneVolumeBinding binding{bindings, coefficientFixture.GetGeneration(), constants};
        const std::array<SceneVolumeBinding, 2> coefficientBindings{binding, binding};
        LX::Runtime::CompiledCompute transform;
        MeshSurfaceEvaluator geometry;
        if (!LX::Runtime::CompileCompute(RHIShaderSource::Resolve("MaterialGraphMeshSurface.slang").string(),
                                        "LXTransformMesh", {}, {}, transform, error) ||
            !geometry.Initialize(resources, rootSignatures, psoManager, transform.stage.bytecode, error,
                                 std::move(transform.description)))
        {
            return false;
        }
        for (const auto policy : {RGOrderPolicy::DependencyOrder, RGOrderPolicy::PreserveDeclarationOrder})
        {
            own::shared_owner<const SceneLookupFrame> lookup;
            own::shared_owner<const SceneVolumeFrame> volume;
            own::shared_owner<const SceneRefractionFrame> refraction;
            own::shared_owner<const SceneSubsurfaceFrame> subsurface;
            if (!cache.Prepare(frame, 1, {}, {}, {}, 0, 1024 * 1024, lookup, error, {}, {}, true) ||
                !volumes.Prepare(frame, *input, {}, {}, 1024 * 1024, volume, error) ||
                !refractions.Prepare(frame, input->ViewProjection(), {}, 1024 * 1024, refraction, error, volume) ||
                !subsurfaces.Prepare(frame, {}, 1024 * 1024, subsurface, error))
            {
                return false;
            }
            EnhancedRenderGraph graph(resources, RGSchedulingMode::ExplicitVersioned, policy);
            own::shared_owner<const MeshSurfaceBatch> meshBatch;
            if (!geometry.Prepare(resources, input->Draws()[0].geometry->Chunks()[0].input, meshBatch, error, true) ||
                !meshBatch->Declare(graph, error) || meshBatch->GraphOutput(graph).version != 1)
            {
                error = "RG5 mesh world version declaration failed: " + error;
                return false;
            }
            SceneSurfaceTargets gbuffer;
            gbuffer.Declare(graph, frame);
            lookup->DeclareInputs(graph);
            refraction->DeclareInputs(graph);
            subsurface->DeclareInputs(graph);
            volume->DeclareCoefficients(graph, coefficientBindings);
            if (volume->GraphCoefficients(graph).version != 1)
            {
                error = "RG5 Volume coefficients lost v1";
                return false;
            }
            RGTextureDesc colorDesc{4, 4};
            colorDesc.format = RHIFormat::RGBA16Float;
            colorDesc.name = "RG5.SpecialLighting";
            auto lighting = graph.Write(graph.CreateTexture(colorDesc));
            graph.AddPass("RG5.SpecialLighting", {{lighting, RHIResourceState::RenderTarget, RGAccessMode::Write}}, nullptr);
            for (unsigned iteration = 0; iteration < 2; ++iteration)
            {
                std::vector<EnhancedRenderGraph::RGPassUsage> captures;
                const auto lookupTargets = lookup->DeclareCaptureOutputs(graph, 0, 11);
                const auto refractionTargets = refraction->DeclareCaptureOutputs(graph);
                const auto subsurfaceTargets = subsurface->DeclareCaptureOutputs(graph);
                for (const auto targets : {std::span<const RGHandle>(lookupTargets),
                                          std::span<const RGHandle>(refractionTargets),
                                          std::span<const RGHandle>(subsurfaceTargets)})
                {
                    for (const auto target : targets)
                    {
                        if (target.version != iteration + 1)
                        {
                            error = "RG5 Special capture did not advance";
                            return false;
                        }
                        captures.push_back({target, RHIResourceState::RenderTarget, RGAccessMode::Write});
                    }
                }
                graph.AddPass("RG5.SpecialCapture", captures, nullptr);
                lookup->DeclareBake(graph, gbuffer.GetOutputs().bitmask);
                lookup->DeclareReady(graph);
                refraction->DeclareBackground(graph, lighting, gbuffer.GetOutputs().depth);
                refraction->DeclareBake(graph, *lookup, gbuffer.GetOutputs().bitmask, {});
                subsurface->DeclareReflection(graph, *lookup, gbuffer.GetOutputs().bitmask);
                subsurface->DeclareFilter(graph, gbuffer.GetOutputs().bitmask);
                if (refraction->GraphSamples(graph).version != iteration + 1 ||
                    refraction->GraphBackgroundColor(graph).version != iteration ||
                    refraction->GraphBackgroundDepth(graph).version != iteration ||
                    subsurface->GraphReflection(graph).version != iteration + 1 ||
                    subsurface->GraphIrradiance(graph).version != iteration + 1)
                {
                    error = "RG5 Special outputs did not advance";
                    return false;
                }
                graph.AddPass("RG5.SpecialReader",
                    {{refraction->GraphSamples(graph), RHIResourceState::ShaderResource, RGAccessMode::Read},
                     {subsurface->GraphReflection(graph), RHIResourceState::ShaderResource, RGAccessMode::Read},
                     {subsurface->GraphIrradiance(graph), RHIResourceState::ShaderResource, RGAccessMode::Read}}, nullptr, true);
            }
            const auto output = volume->DeclareComposite(graph, lighting, gbuffer.GetOutputs().depth, {});
            graph.AddPass("RG5.VolumeReader", {{output, RHIResourceState::ShaderResource, RGAccessMode::Read}},
                          nullptr, true);
            bool rejected = false;
            try
            {
                volume->DeclareComposite(graph, lighting, gbuffer.GetOutputs().depth, {});
            }
            catch (const std::runtime_error&)
            {
                rejected = true;
            }
            if (output.version != 0 || !rejected || !graph.Compile(error))
            {
                error = "RG5 Volume output/duplicate contract failed: " + error;
                return false;
            }
            EnhancedRenderGraph::DiagnosticSnapshot snapshot;
            if (!graph.CaptureDiagnosticSnapshot(snapshot))
            {
                error = "RG5 Special snapshot missing";
                return false;
            }
            bool war = false;
            unsigned refractionReads = 0, reflectionReads = 0;
            for (const auto& pass : snapshot.passes)
            {
                const bool refractionPass = pass.name == "METEOR.Scene.RefractionBake";
                const bool reflectionPass = pass.name == "METEOR.Scene.SubsurfaceReflection";
                const auto expectedLookupVersion = refractionPass ? ++refractionReads :
                    reflectionPass ? ++reflectionReads : 0;
                unsigned lookupReads = 0;
                for (const auto& use : pass.usages)
                {
                    if (expectedLookupVersion)
                    {
                        for (const auto input : lookup->GraphInputs(graph))
                        {
                            if (use.resource == input.index)
                            {
                                ++lookupReads;
                                if (use.version != expectedLookupVersion || use.access != RGAccessMode::Read)
                                {
                                    error = "RG5 Special consumer read stale Lookup version";
                                    return false;
                                }
                            }
                        }
                    }
                    if (use.access == RGAccessMode::LegacyState)
                    {
                        error = "RG5 Special owner still uses LegacyState";
                        return false;
                    }
                }
                if (expectedLookupVersion && lookupReads != 11)
                {
                    error = "RG5 Special consumer missing Lookup reads";
                    return false;
                }
            }
            if (refractionReads != 2 || reflectionReads != 2)
            {
                error = "RG5 Special consumer repetitions missing";
                return false;
            }
            for (const auto& edge : snapshot.versionEdges)
            {
                war |= edge.reason == EnhancedRenderGraph::DiagnosticSnapshot::VersionEdge::Reason::WAR;
            }
            if (!war)
            {
                error = "RG5 Special repeated owners lost WAR";
                return false;
            }
            ++frame.frameId;
        }
        error.clear();
        return true;
    }

    bool ValidateRg5SurfaceDeclarations(DX12DeviceResources& resources, std::string& error)
    {
        using namespace material_graph;
        {
            ce::render_arena_resource arena;
            const auto exercise = [&]()
            {
                ce::render_arena_buffer<uint64_t> values{arena};
                values.assign({1, 2, 3});
                const auto* first = values.data();
                for (uint64_t value = 4; value != 130; ++value)
                {
                    values.push_back(value);
                }
                values.push_back(values[0]);
                values.pop_back();
                values.erase_if([](uint64_t value) { return value % 2 == 0; });
                if (values.size() != 65 || values[0] != 1 || values[64] != 129)
                {
                    throw std::runtime_error("Declaration arena growth or compaction lost values.");
                }
                return first;
            };
            const auto* first = exercise();
            const auto retained = arena.stats().retainedBytes;
            arena.reset();
            if (exercise() != first || arena.stats().retainedBytes != retained)
            {
                error = "Declaration arena did not reuse its retained blocks";
                return false;
            }
        }
        {
            struct Program
            {
                bool hasSurface{true}, hasTransmission{};
            } program;
            struct Draw
            {
                const Program* program{};
                const RenderBindings* bindings{};
                bool blended{}, shadowEnabled{}, staticShadow{};
            };
            std::array<RHIBindingDesc, 2> textures{};
            textures[0].resource.id = 11;
            textures[1].resource.id = 11;
            RenderBindings bindings;
            bindings.resources.textures = textures;
            std::array<Draw, 2> draws{{{&program, &bindings}, {&program, &bindings}}};
            std::array<Draw, 2> shadows{{{&program, &bindings, false, true, true},
                                         {&program, &bindings, false, true, false}}};
            const PassReadPlan plan(draws, shadows);
            for (const auto pass : {PassReadPlan::Pass::Surface, PassReadPlan::Pass::StaticShadow,
                                    PassReadPlan::Pass::DynamicShadow})
            {
                const auto& reads = plan.Get(pass);
                for (size_t variant = 0; variant != 2; ++variant)
                {
                    const auto& usages = reads.usageTemplates[variant];
                    if (usages.size() != reads.geometry.size() * 2 + reads.textures.size())
                    {
                        error = "Pass access template does not match its source slots";
                        return false;
                    }
                    for (size_t index = 0; index < usages.size(); ++index)
                    {
                        const auto expected = index < reads.geometry.size() * 2 && !variant ?
                            RHIResourceState::ShaderResource : RHIResourceState::PixelShaderResource;
                        if (usages[index].handle.IsValid() || usages[index].state != expected ||
                            usages[index].access != RGAccessMode::Read)
                        {
                            error = "Pass access template retained a graph handle or changed its access contract";
                            return false;
                        }
                    }
                }
            }
            if (!plan.Matches(draws, shadows) || plan.Get(PassReadPlan::Pass::Surface).geometry.size() != 2 ||
                plan.Get(PassReadPlan::Pass::Surface).textures.size() != 1 ||
                plan.Get(PassReadPlan::Pass::StaticShadow).geometry.size() != 1 ||
                plan.Get(PassReadPlan::Pass::DynamicShadow).geometry.size() != 1)
            {
                error = "Pass read recipe lost shared textures or caster selection";
                return false;
            }
            textures[1].resource.id = 12;
            if (plan.Matches(draws, shadows))
            {
                error = "Pass read recipe accepted a replaced texture";
                return false;
            }
            textures[1].resource.id = 11;
            draws[0].blended = true;
            if (plan.Matches(draws, shadows))
            {
                error = "Pass read recipe accepted changed pass membership";
                return false;
            }
            draws[0].blended = false;
            shadows[0].staticShadow = false;
            if (plan.Matches(draws, shadows) || plan.Matches(std::span(draws).first(1), shadows))
            {
                error = "Pass read recipe accepted changed caster membership or draw count";
                return false;
            }
        }
        for (const auto policy : {RGOrderPolicy::DependencyOrder, RGOrderPolicy::PreserveDeclarationOrder})
        {
            EnhancedRenderGraph graph(resources, RGSchedulingMode::ExplicitVersioned, policy);
            RGTextureDesc desc{};
            desc.width = 4;
            desc.height = 4;
            desc.format = RHIFormat::RGBA16Float;
            desc.allowRenderTarget = true;
            desc.name = "RG5.SurfaceColor";
            auto lighting = graph.Write(graph.CreateTexture(desc));
            SceneSurfaceTargets::Outputs surface;
            for (auto* color : {&surface.diffuse, &surface.metalRough, &surface.normal, &surface.emissive, &surface.bitmask})
            {
                *color = graph.CreateTexture(desc);
            }
            desc.format = RHIFormat::D32Float;
            desc.allowRenderTarget = false;
            desc.allowDepthStencil = true;
            desc.name = "RG5.SurfaceDepth";
            surface.depth = graph.CreateTexture(desc);
            const auto opaqueDepth = graph.Write(graph.CreateTexture(desc));
            graph.AddPass("RG5.SurfaceInitial",
                {{lighting, RHIResourceState::RenderTarget, RGAccessMode::Write},
                 {opaqueDepth, RHIResourceState::DepthWrite, RGAccessMode::Write}}, nullptr);
            for (unsigned iteration = 0; iteration < 3; ++iteration)
            {
                const auto copiedDepth = graph.Write(surface.depth);
                graph.AddPass("METEOR.Blended.DepthCopy",
                    {{opaqueDepth, RHIResourceState::CopySource, RGAccessMode::Read},
                     {copiedDepth, RHIResourceState::CopyDest, RGAccessMode::Write}}, nullptr);
                surface.depth = copiedDepth;
                surface = AdvanceSceneSurface(graph, surface, RGAccessMode::Write);
                if (surface.diffuse.version != iteration || surface.depth.version != 2 * iteration + 1)
                {
                    error = "RG5 Surface output lineage lost copy/depth Modify";
                    return false;
                }
                std::vector<EnhancedRenderGraph::RGPassUsage> uses{
                    {surface.depth, RHIResourceState::DepthWrite, RGAccessMode::ReadWrite}};
                for (const auto color : {surface.diffuse, surface.metalRough, surface.normal, surface.emissive, surface.bitmask})
                {
                    uses.push_back({color, RHIResourceState::RenderTarget, RGAccessMode::Write});
                }
                graph.AddPass("METEOR.Blended.Surface", uses, nullptr);
                // Simulate three consecutive blended draws using the production
                // color version function, including the returned handle handoff.
                for (const auto* name : {"METEOR.Blended.Color0", "METEOR.Blended.Color1", "METEOR.Blended.Color2"})
                {
                    const auto previous = lighting;
                    lighting = AdvanceSceneColor(graph, lighting);
                    if (lighting.version != previous.version + 1)
                    {
                        error = "RG5 Surface color output handoff stayed stale";
                        return false;
                    }
                    uses = {{lighting, RHIResourceState::RenderTarget, RGAccessMode::ReadWrite},
                            {surface.depth, RHIResourceState::DepthRead, RGAccessMode::Read},
                            {surface.bitmask, RHIResourceState::PixelShaderResource, RGAccessMode::Read},
                            {surface.bitmask, RHIResourceState::ShaderResource, RGAccessMode::Read}};
                    NormalizeSceneReads(graph, uses);
                    if (uses.size() != 3 || uses[2].state != RHIResourceState::ShaderResource)
                    {
                        error = "RG5 Surface failed to combine shared reads";
                        return false;
                    }
                    graph.AddPass(name, uses, nullptr, true);
                }
            }
            bool writeRejected = false, stateRejected = false;
            try
            {
                std::vector<EnhancedRenderGraph::RGPassUsage> uses{
                    {lighting, RHIResourceState::RenderTarget, RGAccessMode::ReadWrite},
                    {lighting, RHIResourceState::RenderTarget, RGAccessMode::ReadWrite}};
                NormalizeSceneReads(graph, uses);
            }
            catch (const std::runtime_error&)
            {
                writeRejected = true;
            }
            try
            {
                std::vector<EnhancedRenderGraph::RGPassUsage> uses{
                    {opaqueDepth, RHIResourceState::CopySource, RGAccessMode::Read},
                    {opaqueDepth, RHIResourceState::DepthRead, RGAccessMode::Read}};
                NormalizeSceneReads(graph, uses);
            }
            catch (const std::runtime_error&)
            {
                stateRejected = true;
            }
            EnhancedRenderGraph::DiagnosticSnapshot snapshot;
            if (!writeRejected || !stateRejected || !graph.Compile(error) || !graph.CaptureDiagnosticSnapshot(snapshot))
            {
                error = "RG5 Surface plan/rejection gate failed: " + error;
                return false;
            }
            bool war = false;
            for (const auto& edge : snapshot.versionEdges)
            {
                war |= edge.reason == EnhancedRenderGraph::DiagnosticSnapshot::VersionEdge::Reason::WAR;
            }
            if (!war || lighting.version != 9 || surface.depth.version != 5)
            {
                error = "RG5 Surface old-reader hazards or final lineage missing";
                return false;
            }
        }
        error.clear();
        return true;
    }

    bool ValidateMeteorTransformDispatch(DX12DeviceResources& resources, std::string& error, std::string& log)
    {
        DX12PSOManager pipelines;
        DX12RootSignatureCache roots;
        if (!pipelines.Initialize(&resources, L"dx12_lx_transform.cache", error) ||
            !roots.Initialize(&resources, error)) return false;
        EnhancedFrameContext context{};
        context.resources = &resources; context.psoManager = &pipelines; context.rootSignatures = &roots;
        context.width = context.height = 32;
        GraphSceneFixture fixture;
        if (!fixture.Initialize(context, error)) return false;
        LX::Runtime::CompiledCompute shader;
        material_graph::MeshSurfaceEvaluator evaluator;
        if (!LX::Runtime::CompileCompute(RHIShaderSource::Resolve("MaterialGraphMeshSurface.slang").string(),
                "LXTransformMesh", {}, {}, shader, error) ||
            !evaluator.Initialize(resources, roots, pipelines, shader.stage.bytecode, error,
                                  std::move(shader.description))) return false;
        for (const auto dimensions : std::array<std::array<uint32_t, 2>, 6>{{
                 {1,33}, {16,33}, {64,33}, {256,33}, {512,33}, {512,4095}}})
        {
            const uint32_t count = dimensions[0], pointCount = dimensions[1];
            std::vector<Vertex> vertices(pointCount), skinned(pointCount);
            std::vector<uint32_t> indices(pointCount);
            for (uint32_t i = 0; i < pointCount; ++i)
            {
                vertices[i].position = {float(i / 3) * .01f, float(i % 3) * .01f, .25f};
                vertices[i].normal = {0, 0, 1}; vertices[i].tangent = {1, 0, 0}; vertices[i].bitangent = {0, 1, 0};
                vertices[i].uv0 = {float(i) / 64.f, float(i) / 128.f};
                skinned[i] = vertices[i]; skinned[i].boneWeights = {1, 0, 0, 0};
                indices[i] = i;
            }
            const auto staticMesh = fixture.AddMesh(vertices, indices), skinMesh = fixture.AddMesh(skinned, indices);
            if (!resources.BeginFrame(error)) return false;
            std::vector<own::shared_owner<const material_graph::MeshSurfaceBatch>> owners;
            std::vector<const material_graph::MeshSurfaceBatch*> batches;
            std::vector<RHIReadback> readbacks(count);
            for (uint32_t i = 0; i < count; ++i)
            {
                auto world = math::matrix4x4::identity();
                world.m[0][0] = 1.f + float(i % 5) * .1f; world.m[1][1] = 1.25f;
                world.m[3][0] = float(i); world.m[3][1] = -float(i) * .5f;
                auto draw = fixture.Draw(i % 2 ? skinMesh : staticMesh, world);
                auto bone = math::matrix4x4::identity(); bone.m[3][0] = float(i % 7) * .1f;
                if (i % 2) { draw.bonePalette = &bone; draw.boneCount = 1; }
                const std::vector<float> lods(pointCount, float(i % 4) * .5f);
                own::shared_owner<const material_graph::MeshSurfaceInput> input;
                const material_graph::SurfaceView view{{0, 0, 1000, 0}, 1, 1, uint64_t(i + 1)};
                own::shared_owner<const material_graph::MeshSurfaceBatch> batch;
                if (!material_graph::MeshSurfaceInput::Seal(draw, view, lods, input, error) ||
                    !evaluator.Prepare(resources, input, batch, error) ||
                    !resources.CreateBufferReadback(pointCount * sizeof(material_graph::SurfacePoint), readbacks[i], error))
                    return false;
                batches.push_back(batch.operator->()); owners.push_back(std::move(batch));
            }
            EnhancedRenderGraph graph(resources, RGSchedulingMode::ExplicitVersioned, RGOrderPolicy::DependencyOrder);
            if (!material_graph::MeshSurfaceBatch::DeclareBatch(graph, batches, error)) return false;
            for (uint32_t i = 0; i < count; ++i)
            {
                const auto output = owners[i]->GraphOutput(graph);
                graph.AddPass("METEOR.Transform.Readback", {{output, RHIResourceState::CopySource, RGAccessMode::Read}},
                    [output, readback = readbacks[i]](const auto& execution) {
                        execution.encoder->CopyBufferToReadback(readback, execution.graph->ResolveBufferHandle(output));
                    }, true);
            }
            EnhancedRenderGraph::DiagnosticSnapshot snapshot;
            if (!graph.Compile(error) || !graph.CaptureDiagnosticSnapshot(snapshot)) return false;
            const auto named = [&](const char* name) {
                return std::ranges::count_if(snapshot.passes, [&](const auto& pass) { return !pass.culled && pass.name == name; });
            };
            if (named("METEOR.TransformMeshWorld") != 1 || named("METEOR.MeshWorldReady") != 1) {
                error = "METEOR first-transform pass count grew with job count"; return false;
            }
            const auto before = resources.GetImmediateEncoder().GetComputeDispatchCount();
            if (!graph.Execute(error)) return false;
            if (resources.GetImmediateEncoder().GetComputeDispatchCount() != before + 1) {
                error = "METEOR first-transform native dispatch count is not exactly one"; return false;
            }
            if (!resources.EndFrame(error)) return false;
            resources.WaitForGpu();
            for (uint32_t i = 0; i < count; ++i)
            {
                RHIReadbackImage image;
                if (!resources.MapReadback(readbacks[i], image, error)) return false;
                const auto actual = image.Elements<material_graph::SurfacePoint>();
                if (image.ElementCount<material_graph::SurfacePoint>() != pointCount ||
                    !owners[i]->ValidateReadback({actual, pointCount}, error)) return false;
                for (uint32_t v = 0; v < pointCount; ++v)
                {
                    const double shift = i % 2 ? double(float(i % 7) * .1f) : 0.;
                    const double x = (double(vertices[v].position.x) + shift) * double(1.f + float(i % 5) * .1f) + i;
                    const double y = double(vertices[v].position.y) * 1.25 - double(i) * .5;
                    if (std::abs(actual[v].position[0] - x) > 1e-4 ||
                        std::abs(actual[v].position[1] - y) > 1e-4 || std::abs(actual[v].position[2] - .25) > 1e-6 ||
                        actual[v].uvLod[0] != vertices[v].uv0.x || actual[v].uvLod[1] != vertices[v].uv0.y ||
                        actual[v].uvLod[3] != float(i % 4) * .5f || std::abs(actual[v].normal[2] - 1.f) > 1e-6f ||
                        std::abs(actual[v].tangent[0] - (1.f + float(i % 5) * .1f) / std::max(1.25f, 1.f + float(i % 5) * .1f)) > 1e-6f ||
                        std::abs(actual[v].bitangent[1] - 1.25f / std::max(1.25f, 1.f + float(i % 5) * .1f)) > 1e-6f) {
                        char detail[512]{};
                        std::snprintf(detail, sizeof(detail), "METEOR transform reference mismatch job=%u vertex=%u position=(%.9g,%.9g,%.9g) expected=(%.9g,%.9g,.25) uv=(%.9g,%.9g) expected=(%.9g,%.9g) lod=%.9g normalZ=%.9g tangentX=%.9g bitangentY=%.9g",
                            i, v, actual[v].position[0], actual[v].position[1], actual[v].position[2], x, y,
                            actual[v].uvLod[0], actual[v].uvLod[1], vertices[v].uv0.x, vertices[v].uv0.y,
                            actual[v].uvLod[3], actual[v].normal[2], actual[v].tangent[0], actual[v].bitangent[1]);
                        error = detail; return false;
                    }
                }
                resources.ReleaseReadback(readbacks[i]);
            }
            log += "METEOR_TRANSFORM_GPU_OK jobs=" + std::to_string(count) + " points=" + std::to_string(count * pointCount) +
                   " transformPasses=1 readyPasses=1 nativeDispatches=1 mixedStaticSkinned=" + std::to_string(count > 1 ? 1 : 0) + " oracle=independent\n";
        }
        resources.WaitForGpu();
        fixture.Shutdown();
        return true;
    }

    bool ValidateMeteorTransformResidency(DX12DeviceResources& resources, std::string& error, std::string& log)
    {
        DX12PSOManager pipelines;
        DX12RootSignatureCache roots;
        if (!pipelines.Initialize(&resources, L"dx12_lx_residency.cache", error) ||
            !roots.Initialize(&resources, error)) return false;
        EnhancedFrameContext context{};
        context.resources = &resources; context.psoManager = &pipelines; context.rootSignatures = &roots;
        context.width = context.height = 32;
        GraphSceneFixture fixture;
        if (!fixture.Initialize(context, error)) return false;
        LX::Runtime::CompiledCompute shader;
        material_graph::MeshSurfaceEvaluator evaluator;
        if (!LX::Runtime::CompileCompute(RHIShaderSource::Resolve("MaterialGraphMeshSurface.slang").string(),
                "LXTransformMesh", {}, {}, shader, error) ||
            !evaluator.Initialize(resources, roots, pipelines, shader.stage.bytecode, error,
                                  std::move(shader.description))) return false;
        constexpr uint32_t count = 512, points = 1026;
        std::vector<Vertex> vertices(points);
        std::vector<uint32_t> indices(points);
        for (uint32_t i = 0; i < points; ++i) {
            vertices[i].position = {float(i / 3) * .01f, float(i % 3) * .01f, .25f};
            vertices[i].normal = {0,0,1}; vertices[i].tangent = {1,0,0}; vertices[i].bitangent = {0,1,0};
            indices[i] = i;
        }
        const auto mesh = fixture.AddMesh(vertices, indices);
        std::vector<own::shared_owner<const material_graph::MeshSurfaceInput>> inputs(count);
        const std::vector<float> lods(points, 0.f);
        const material_graph::SurfaceView view{{0,0,1000,0},1,1,1};
        for (uint32_t i = 0; i < count; ++i) {
            auto world = math::matrix4x4::identity(); world.m[3][0] = float(i);
            if (!material_graph::MeshSurfaceInput::Seal(fixture.Draw(mesh, world), view, lods, inputs[i], error)) return false;
        }
        // Delayed evaluator completion publication must not age out an active
        // immutable key before it first becomes eligible for reuse.
        own::shared_owner<const material_graph::MeshSurfaceInput> pendingInput;
        auto pendingWorld = math::matrix4x4::identity(); pendingWorld.m[3][0] = -3.f;
        if (!material_graph::MeshSurfaceInput::Seal(fixture.Draw(mesh, pendingWorld), view, lods, pendingInput, error)) return false;
        for (uint32_t round = 0; round < 5; ++round) {
            if (round == 4) evaluator.NotifyCompleted(resources.GetCompletedFenceValue());
            if (!resources.BeginFrame(error)) return false;
            own::shared_owner<const material_graph::MeshSurfaceBatch> owner;
            if (!evaluator.Prepare(resources, pendingInput, owner, error, true)) return false;
            const material_graph::MeshSurfaceBatch* batch = owner.operator->();
            EnhancedRenderGraph graph(resources, RGSchedulingMode::ExplicitVersioned, RGOrderPolicy::DependencyOrder);
            if (!material_graph::MeshSurfaceBatch::DeclareBatch(graph, {&batch,1}, error)) return false;
            graph.AddPass("METEOR.Pending.Consumer", {{owner->GraphOutput(graph), RHIResourceState::ShaderResource, RGAccessMode::Read}}, nullptr, true);
            const auto before = resources.GetImmediateEncoder().GetComputeDispatchCount();
            if (!graph.Compile(error) || !graph.Execute(error) ||
                resources.GetImmediateEncoder().GetComputeDispatchCount() != before + (round < 4 ? 1 : 0)) {
                if (error.empty()) error = "METEOR delayed completion did not gate reuse";
                return false;
            }
            if (!resources.EndFrame(error)) return false;
            owner->MarkSubmitted({resources.GetLastSignaledFenceValue()});
            resources.WaitForGpu();
            if (round == 4 && evaluator.CacheStats().cachedOutputs != 4) {
                error = "METEOR active pending key aged out before completion publication"; return false;
            }
        }
        pendingInput.reset();
        log += "LX_RESIDENCY_PENDING_OK recordings=5 pendingOwnersPreserved=4 completedNativeDispatches=0\n";
        own::shared_owner<const material_graph::MeshSurfaceBatch> pinned;
        for (uint32_t round = 0; round < 4; ++round) {
            if (round == 3) {
                auto world = math::matrix4x4::identity(); world.m[3][0] = 3.f;
                if (!material_graph::MeshSurfaceInput::Seal(fixture.Draw(mesh, world), view, lods, inputs[0], error)) return false;
            }
            if (!resources.BeginFrame(error)) return false;
            std::vector<own::shared_owner<const material_graph::MeshSurfaceBatch>> owners;
            std::vector<const material_graph::MeshSurfaceBatch*> batches;
            std::vector<RHIReadback> readbacks(count);
            for (uint32_t i = 0; i < count; ++i) {
                own::shared_owner<const material_graph::MeshSurfaceBatch> batch;
                if (!evaluator.Prepare(resources, inputs[i], batch, error, true) ||
                    !resources.CreateBufferReadback(points * sizeof(material_graph::SurfacePoint), readbacks[i], error)) return false;
                batches.push_back(batch.operator->()); owners.push_back(std::move(batch));
            }
            if (!round) pinned = owners.front();
            EnhancedRenderGraph graph(resources, RGSchedulingMode::ExplicitVersioned, RGOrderPolicy::DependencyOrder);
            if (!material_graph::MeshSurfaceBatch::DeclareBatch(graph, batches, error)) return false;
            for (uint32_t i = 0; i < count; ++i) {
                const auto output = owners[i]->GraphOutput(graph);
                graph.AddPass("METEOR.Residency.Readback", {{output, RHIResourceState::CopySource, RGAccessMode::Read}},
                    [output, readback = readbacks[i]](const auto& execution) {
                        execution.encoder->CopyBufferToReadback(readback, execution.graph->ResolveBufferHandle(output));
                    }, true);
                graph.AddPass("METEOR.Residency.Restore", {{output, RHIResourceState::ShaderResource, RGAccessMode::Read}}, nullptr, true);
            }
            EnhancedRenderGraph::DiagnosticSnapshot snapshot;
            if (!graph.Compile(error) || !graph.CaptureDiagnosticSnapshot(snapshot)) return false;
            const auto named = [&](const char* name) {
                return std::ranges::count_if(snapshot.passes, [&](const auto& pass) {return !pass.culled && pass.name == name;});
            };
            const uint32_t expected = round == 0 || round == 3 ? 1 : 0;
            const auto before = resources.GetImmediateEncoder().GetComputeDispatchCount();
            if (named("METEOR.TransformMeshWorld") != expected || named("METEOR.MeshWorldReady") != expected ||
                !graph.Execute(error) || resources.GetImmediateEncoder().GetComputeDispatchCount() != before + expected) {
                if (error.empty()) error = "METEOR active residency transformed unchanged outputs";
                return false;
            }
            if (!resources.EndFrame(error)) return false;
            for (const auto& owner : owners) owner->MarkSubmitted({resources.GetLastSignaledFenceValue()});
            resources.WaitForGpu(); evaluator.NotifyCompleted(resources.GetCompletedFenceValue());
            for (uint32_t i = 0; i < count; ++i) {
                RHIReadbackImage image;
                if (!resources.MapReadback(readbacks[i], image, error)) return false;
                const auto actual = image.Elements<material_graph::SurfacePoint>();
                if (image.ElementCount<material_graph::SurfacePoint>() != points ||
                    !owners[i]->ValidateReadback({actual, points}, error)) return false;
                for (uint32_t v = 0; v < points; ++v) {
                    const auto x = vertices[v].position.x + float(round == 3 && i == 0 ? 3 : i);
                    if (std::abs(actual[v].position[0] - x) > 1e-4f ||
                        std::abs(actual[v].position[1] - vertices[v].position.y) > 1e-6f ||
                        std::abs(actual[v].position[2] - .25f) > 1e-6f) {
                        error = "METEOR resident output differs from independent world reference"; return false;
                    }
                }
                resources.ReleaseReadback(readbacks[i]);
            }
            const auto stats = evaluator.CacheStats();
            if (stats.cachedOutputs < count || stats.cachedOutputBytes <= (32ull << 20) || stats.entries < count) {
                error = "METEOR active scene was restricted by former cache budgets"; return false;
            }
            log += "LX_RESIDENCY_GPU_OK round=" + std::to_string(round) + " jobs=512 points=525312 transformPasses=" +
                std::to_string(expected) + " readyPasses=" + std::to_string(expected) + " nativeDispatches=" +
                std::to_string(expected) + " oracle=independent\n";
        }
        inputs.clear();
        for (uint32_t round = 0; round < 3; ++round) {
            if (!resources.BeginFrame(error)) return false;
            evaluator.BeginFrame(resources.GetCurrentUploadRecordingId());
            if (!resources.EndFrame(error)) return false;
            resources.WaitForGpu(); evaluator.NotifyCompleted(resources.GetCompletedFenceValue());
        }
        if (evaluator.CacheStats().cachedOutputs != 1) {
            error = "METEOR unused output retirement released pinned owner or retained idle scene"; return false;
        }
        pinned.reset();
        if (!resources.BeginFrame(error)) return false;
        evaluator.BeginFrame(resources.GetCurrentUploadRecordingId());
        if (!resources.EndFrame(error)) return false;
        resources.WaitForGpu(); evaluator.NotifyCompleted(resources.GetCompletedFenceValue());
        if (evaluator.CacheStats().cachedOutputs != 0 || evaluator.CacheStats().cachedOutputBytes != 0 ||
            evaluator.CacheStats().entries > 256 || evaluator.CacheStats().residentBytes > (128ull << 20)) {
            error = "METEOR output retirement leaked idle outputs"; return false;
        }
        log += "LX_RESIDENCY_RETIRE_OK activeOver32MiB=1 uniqueInputs=512 pinnedPreserved=1 idleOutputs=0\n";
        fixture.Shutdown();
        return true;
    }

    // Light tiles are one compute writer of both tile buffers; blended shading
    // reads them. No legacy access survives in the versioned declaration.
    bool ValidateRg5LightTileDeclarations(DX12DeviceResources& resources, std::string& error)
    {
        DX12PSOManager psoManager;
        DX12RootSignatureCache rootSignatures;
        if (!psoManager.Initialize(&resources, L"dx12_rg5_lighttiles.cache", error) ||
            !rootSignatures.Initialize(&resources, error))
        {
            return false;
        }
        EnhancedFrameContext frame{};
        frame.resources = &resources;
        frame.psoManager = &psoManager;
        frame.rootSignatures = &rootSignatures;
        frame.width = 4;
        frame.height = 4;
        std::vector<EnhancedLight> lights;
        frame.lights = &lights;
        EnhancedLightTilePass tiles;
        if (!tiles.Initialize(frame, error) || !tiles.PrepareFrame(frame, error))
        {
            return false;
        }
        if (!resources.BeginFrame(error))
        {
            return false;
        }
        struct RecordingScope
        {
            DX12DeviceResources& resources;
            ~RecordingScope()
            {
                resources.AbortFrame();
            }
        } recording{resources};
        for (const auto policy : {RGOrderPolicy::DependencyOrder, RGOrderPolicy::PreserveDeclarationOrder})
        {
            EnhancedRenderGraph graph(resources, RGSchedulingMode::ExplicitVersioned, policy);
            SceneSurfaceTargets gbuffer;
            gbuffer.Declare(graph, frame);
            tiles.SetDepth(gbuffer.GetOutputs().depth);
            tiles.Declare(graph, frame);
            const auto& lighting = tiles.GetLighting();
            if (!lighting.graphCounts.IsValid() || !lighting.graphIndices.IsValid() || !lighting.lights.IsValid())
            {
                error = "RG5 light tiles did not publish their buffers";
                return false;
            }
            graph.AddPass("RG5.LightTileConsumer",
                {{lighting.graphCounts, RHIResourceState::ShaderResource, RGAccessMode::Read},
                 {lighting.graphIndices, RHIResourceState::ShaderResource, RGAccessMode::Read}}, nullptr, true);
            EnhancedRenderGraph::DiagnosticSnapshot snapshot;
            if (!graph.Compile(error) || !graph.CaptureDiagnosticSnapshot(snapshot))
            {
                return false;
            }
            size_t tileWrites = 0;
            for (const auto& pass : snapshot.passes)
            {
                for (const auto& usage : pass.usages)
                {
                    if (usage.access == RGAccessMode::LegacyState)
                    {
                        error = "RG5 light tile chain still uses legacy access";
                        return false;
                    }
                    if (pass.name == "METEOR.Scene.LightTiles" && usage.kind == RGResourceKind::Buffer)
                    {
                        if (usage.access != RGAccessMode::Write || usage.version != 1)
                        {
                            error = "RG5 light tile buffers lack Write/v1";
                            return false;
                        }
                        ++tileWrites;
                    }
                }
            }
            if (tileWrites != 2)
            {
                error = "RG5 light tile writer identity mismatch";
                return false;
            }
        }
        tiles.Shutdown();
        error.clear();
        return true;
    }
    bool ValidateRg5IndirectDeclarations(DX12DeviceResources& resources, std::string& error)
    {
        DX12PSOManager psoManager;
        DX12RootSignatureCache rootSignatures;
        if (!psoManager.Initialize(&resources, L"dx12_rg5_indirect.cache", error) ||
            !rootSignatures.Initialize(&resources, error))
        {
            return false;
        }
        EnhancedFrameContext frame{};
        frame.resources = &resources;
        frame.psoManager = &psoManager;
        frame.rootSignatures = &rootSignatures;
        frame.width = 4;
        frame.height = 4;
        EnhancedSSAOPass ao;
        EnhancedSSGIPass gi;
        if (!ao.Initialize(frame, error) || !gi.Initialize(frame, error) ||
            !ao.PrepareFrame(frame, error) || !gi.PrepareFrame(frame, error))
        {
            return false;
        }
        for (const auto policy : {RGOrderPolicy::DependencyOrder, RGOrderPolicy::PreserveDeclarationOrder})
        {
            for (const bool optionalInputs : {false, true})
            {
                for (uint32_t iteration = 0; iteration < 2; ++iteration)
                {
                    EnhancedRenderGraph graph(resources, RGSchedulingMode::ExplicitVersioned, policy);
                    SceneSurfaceTargets gbuffer;
                    EnhancedShadowPass shadow;
                    RGHandle lighting;
                    gbuffer.Declare(graph, frame);
                    shadow.DeclareGraphTargets(graph, frame);
                    const auto inputs = gbuffer.GetOutputs();
                    ao.SetInputs({inputs.depth, inputs.normal});
                    ao.Declare(graph, frame);



                    lighting = material_graph::SceneHost::DeclareHdrTarget(graph, frame);
                    EnhancedSSGIPass::Inputs giInputs{};
                    giInputs.depth = inputs.depth;
                    if (optionalInputs)
                    {
                        giInputs.normal = inputs.normal;
                        giInputs.diffuse = inputs.diffuse;
                        giInputs.metalRough = inputs.metalRough;
                        giInputs.lighting = lighting;
                        giInputs.ambientOcclusion = ao.GetOutput();
                    }
                    gi.SetInputs(giInputs);
                    gi.Declare(graph, frame);
                    graph.AddPass("RG5.IndirectConsumer",
                        {{gi.GetOutput(), RHIResourceState::CopySource, RGAccessMode::Read}}, nullptr, true);
                    EnhancedRenderGraph::DiagnosticSnapshot snapshot;
                    if (!graph.Compile(error) || !graph.CaptureDiagnosticSnapshot(snapshot))
                    {
                        return false;
                    }
                    bool foundAo = false;
                    bool foundStore = false;
                    for (const auto& pass : snapshot.passes)
                    {
                        if (pass.name == "SSAO.Filter")
                        {
                            foundAo = true;
                        }
                        if (pass.name == "SSGI.StoreHistory")
                        {
                            foundStore = true;
                            size_t destinations = 0;
                            for (const auto& usage : pass.usages)
                            {
                                if (usage.state == RHIResourceState::CopyDest)
                                {
                                    if (usage.access != RGAccessMode::Write || usage.version != 1)
                                    {
                                        error = "RG5 history destination lacks imported-v0 to written-v1 lineage";
                                        return false;
                                    }
                                    ++destinations;
                                }
                            }
                            if (destinations != 2)
                            {
                                error = "RG5 history color/depth destinations missing";
                                return false;
                            }
                        }
                        for (const auto& usage : pass.usages)
                        {
                            if (usage.access == RGAccessMode::LegacyState)
                            {
                                error = "RG5 indirect chain still uses legacy access";
                                return false;
                            }
                        }
                    }
                    if (!foundAo || !foundStore || !ao.GetOutput().IsValid() || !gi.GetOutput().IsValid())
                    {
                        error = "RG5 indirect outputs or history store missing";
                        return false;
                    }
                }
            }
        }
        error.clear();
        return true;
    }
    bool ValidateRg5ConsumerDeclarations(DX12DeviceResources& resources, std::string& error)
    {
        DX12PSOManager psoManager;
        DX12RootSignatureCache rootSignatures;
        if (!psoManager.Initialize(&resources, L"dx12_rg5.cache", error) ||
            !rootSignatures.Initialize(&resources, error))
        {
            return false;
        }
        EnhancedFrameContext frame{};
        frame.resources = &resources;
        frame.psoManager = &psoManager;
        frame.rootSignatures = &rootSignatures;
        frame.width = 4;
        frame.height = 4;
        EnhancedSkyBoxPass sky;
        if (!sky.Initialize(frame, error) || !sky.PrepareFrame(frame, error))
        {
            return false;
        }
        for (const auto policy : {RGOrderPolicy::DependencyOrder, RGOrderPolicy::PreserveDeclarationOrder})
        {
            for (const bool standalone : {false, true})
            {
                EnhancedRenderGraph graph(resources, RGSchedulingMode::ExplicitVersioned, policy);
                SceneSurfaceTargets gbuffer;
                EnhancedShadowPass shadow;
                RGHandle lighting;
                if (!standalone)
                {
                    gbuffer.Declare(graph, frame);
                    shadow.DeclareGraphTargets(graph, frame);


                    lighting = material_graph::SceneHost::DeclareHdrTarget(graph, frame);
                    // Keep an old-version reader alive to require WAR before SkyBox modifies color.
                    graph.AddPass("RG5.OldLighting", {{lighting, RHIResourceState::CopySource, RGAccessMode::Read}},
                                  nullptr, true);
                    sky.SetInputs({lighting, gbuffer.GetOutputs().depth});
                }
                else
                {
                    sky.SetInputs({});
                }
                sky.Declare(graph, frame);
                const auto output = sky.GetOutput();
                if (!output.IsValid() || output.version != (standalone ? 0 : 1))
                {
                    error = "RG5 SkyBox output version mismatch";
                    return false;
                }
                graph.AddPass("RG5.ColorConsumer", {{output, RHIResourceState::CopySource, RGAccessMode::Read}},
                              nullptr, true);
                EnhancedRenderGraph::DiagnosticSnapshot snapshot;
                if (!graph.Compile(error) || !graph.CaptureDiagnosticSnapshot(snapshot))
                {
                    return false;
                }
                const auto expectedPasses = standalone ? 2u : 5u;
                if (snapshot.executeOrder.size() != expectedPasses)
                {
                    error = "RG5 consumer chain was unexpectedly culled";
                    return false;
                }
                for (const auto& pass : snapshot.passes)
                {
                    for (const auto& usage : pass.usages)
                    {
                        if (usage.access == RGAccessMode::LegacyState)
                        {
                            error = "RG5 consumer chain still uses legacy access";
                            return false;
                        }
                    }
                }
                if (!standalone)
                {
                    uint32_t reader = UINT32_MAX, skyPass = UINT32_MAX;
                    size_t hdrClears = 0;
                    for (uint32_t index = 0; index < snapshot.passes.size(); ++index)
                    {
                        const auto& pass = snapshot.passes[index];
                        if (pass.name == "RG5.OldLighting") reader = index;
                        if (pass.name == "SkyBox") skyPass = index;
                        if (pass.name == "METEOR.Scene.HdrClear")
                        {
                            ++hdrClears;
                            if (pass.usages.size() != 1 || pass.usages[0].resource != lighting.index ||
                                pass.usages[0].access != RGAccessMode::Write)
                            {
                                error = "METEOR HDR must have one initial writer without lighting inputs";
                                return false;
                            }
                        }
                        if (pass.name == "Deferred")
                        {
                            error = "Removed Deferred pass reappeared in METEOR consumer chain";
                            return false;
                        }
                    }
                    if (hdrClears != 1)
                    {
                        error = "METEOR HDR first writer is missing or duplicated";
                        return false;
                    }
                    bool hasWar = false;
                    for (const auto& edge : snapshot.versionEdges)
                    {
                        if (edge.resource == lighting.index && edge.producer == reader && edge.consumer == skyPass &&
                            edge.reason == EnhancedRenderGraph::DiagnosticSnapshot::VersionEdge::Reason::WAR)
                        {
                            hasWar = true;
                        }
                    }
                    if (!hasWar || output.index != lighting.index || sky.GetDepth().version != 0)
                    {
                        error = "RG5 SkyBox Modify lost old-color dependency or depth identity";
                        return false;
                    }
                }
            }
        }
        error.clear();
        return true;
    }
    bool ValidateRg5OrderPolicy(std::string& error)
    {
        R6bFakeReadbackServices services;
        for (const auto policy : {RGOrderPolicy::DependencyOrder, RGOrderPolicy::PreserveDeclarationOrder})
        {
            for (const bool reverse : {false, true})
            {
                EnhancedRenderGraph graph(services, RGSchedulingMode::ExplicitVersioned, policy);
                RGTextureDesc desc{};
                desc.width = 4;
                desc.height = 4;
                desc.allowRenderTarget = true;
                desc.name = "RG5.Policy";
                const auto output = graph.Write(graph.CreateTexture(desc));
                const auto producer = [&] {
                    graph.AddPass("producer", {{output, RHIResourceState::RenderTarget, RGAccessMode::Write}}, nullptr);
                };
                const auto consumer = [&] {
                    graph.AddPass("consumer", {{output, RHIResourceState::CopySource, RGAccessMode::Read}}, nullptr,
                                  true);
                };
                if (reverse)
                {
                    consumer();
                    producer();
                }
                else
                {
                    producer();
                    consumer();
                }
                const bool compiled = graph.Compile(error);
                if (policy == RGOrderPolicy::PreserveDeclarationOrder && reverse)
                {
                    if (compiled || error.find("Declaration order violates dependency") == std::string::npos)
                    {
                        error = "RG5 authored order accepted a reversed RAW";
                        return false;
                    }
                    continue;
                }
                EnhancedRenderGraph::DiagnosticSnapshot snapshot;
                if (!compiled || !graph.CaptureDiagnosticSnapshot(snapshot))
                {
                    return false;
                }
                const std::vector<uint16_t> expected =
                    reverse ? std::vector<uint16_t>{1, 0} : std::vector<uint16_t>{0, 1};
                if (snapshot.executeOrder != expected || snapshot.versionEdges.size() != 1)
                {
                    error = "RG5 order policy changed version DAG";
                    return false;
                }
            }
        }
        error.clear();
        return true;
    }

    bool ValidateRg5ProducerDeclarations(std::string& error)
    {
        if (!ValidateRg5OrderPolicy(error))
        {
            return false;
        }
        R6bFakeReadbackServices services;
        for (const auto mode : {RGSchedulingMode::DeclarationOrder, RGSchedulingMode::ExplicitSingleWriter,
                                RGSchedulingMode::ExplicitVersioned})
        {
            for (const bool reverse : {false, true})
            {
                EnhancedRenderGraph graph(services, mode);
                SceneSurfaceTargets gbuffer;
                EnhancedShadowPass shadow;
                EnhancedFrameContext frame{};
                frame.width = 4;
                frame.height = 4;
                if (reverse)
                {
                    gbuffer.Declare(graph, frame);
                    shadow.DeclareGraphTargets(graph, frame);
                }
                else
                {
                    shadow.DeclareGraphTargets(graph, frame);
                    gbuffer.Declare(graph, frame);
                }
                const auto output = gbuffer.GetOutputs();
                const RGHandle handles[] = {output.diffuse, output.metalRough, output.normal,        output.emissive,
                                            output.bitmask, output.depth, output.visibility, shadow.GetShadowMap()};
                std::vector<EnhancedRenderGraph::RGPassUsage> reads;
                for (const auto handle : handles)
                {
                    if (!handle.IsValid() || (mode == RGSchedulingMode::ExplicitVersioned &&
                                              (handle.version != 0 || handle.epoch != graph.ResourceEpoch() ||
                                               handle.kind != RGResourceKind::Texture)))
                    {
                        error = "RG5 product output version identity mismatch";
                        return false;
                    }
                    reads.push_back(
                        {handle, RHIResourceState::CopySource,
                         mode == RGSchedulingMode::DeclarationOrder ? RGAccessMode::LegacyState : RGAccessMode::Read});
                }
                graph.AddPass("RG5.OutputConsumer", reads, nullptr, true);
                EnhancedRenderGraph::DiagnosticSnapshot snapshot;
                if (!graph.Compile(error) || !graph.CaptureDiagnosticSnapshot(snapshot))
                {
                    return false;
                }
                // Visibility and surface colors now have independent clear producers.
                if (snapshot.executeOrder != std::vector<uint16_t>{0, 1, 2, 3} || snapshot.resources.size() != std::size(handles) ||
                    graph.GetStats().passesExecuted != 4)
                {
                    error = "RG5 product producer/consumer plan mismatch";
                    return false;
                }
                if (mode != RGSchedulingMode::DeclarationOrder)
                {
                    for (size_t pass = 0; pass < 3; ++pass)
                    {
                        for (const auto& usage : snapshot.passes[pass].usages)
                        {
                            if (usage.access != RGAccessMode::Write)
                            {
                                error = "RG5 product producer lacks explicit Write";
                                return false;
                            }
                        }
                    }
                }
                if (mode == RGSchedulingMode::ExplicitVersioned && snapshot.versionEdges.size() != std::size(handles))
                {
                    error = "RG5 product output RAW edges missing";
                    return false;
                }
            }
        }
        error.clear();
        return true;
    }

    bool ValidateBase0GraphFixtures(std::string& error)
    {
        R6bFakeReadbackServices services;
        const auto fail = [&](const char* message) { error = message; return false; };
        const auto texture = [](const char* name)
        {
            RGTextureDesc desc{};
            desc.width = 4; desc.height = 4; desc.allowRenderTarget = true;
            desc.name = name;
            return desc;
        };
        EnhancedRenderGraph graph(services);
        EnhancedRenderGraph::DiagnosticSnapshot snapshot;
        if (graph.CaptureDiagnosticSnapshot(snapshot)) return fail("uncompiled snapshot accepted");
        const auto a = graph.CreateTexture(texture("raw"));
        const auto dead = graph.CreateTexture(texture("culled"));
        const auto history = graph.ImportTexture(RHITextureHandle{123},
            RHIResourceState::ShaderResource, "history");
        graph.AddPass("producer", {{a, RHIResourceState::RenderTarget}}, nullptr);
        graph.AddPass("independent", {{history, RHIResourceState::ShaderResource}}, nullptr, true);
        graph.AddPass("dead", {{dead, RHIResourceState::RenderTarget}}, nullptr);
        graph.AddPass("consumer", {{a, RHIResourceState::ShaderResource}}, nullptr, true);
        if (!graph.Compile(error) || !graph.CaptureDiagnosticSnapshot(snapshot)) return false;
        if (snapshot.executeOrder != std::vector<uint16_t>{0, 1, 3}
            || !snapshot.passes[2].culled || snapshot.passes[2].compiledIndex != -1
            || snapshot.resources[0].firstUse != 0 || snapshot.resources[0].lastUse != 2
            || snapshot.resources[1].used || snapshot.reachabilityEdges.size() != 1
            || snapshot.reachabilityEdges[0].producer != 0
            || snapshot.reachabilityEdges[0].consumer != 3
            || snapshot.passes[3].barriers.size() != 1)
            return fail("RAW/independent/culling/history snapshot mismatch");
        graph.Reset();
        if (graph.CaptureDiagnosticSnapshot(snapshot)) return fail("reset snapshot is stale");
        EnhancedRenderGraph missing(services);
        const auto uninitialized = missing.CreateTexture(texture("missing"));
        missing.AddPass("read", {{uninitialized, RHIResourceState::ShaderResource}}, nullptr, true);
        if (missing.Compile(error)) return fail("missing producer mutation accepted");
        EnhancedRenderGraph reversed(services);
        const auto output = reversed.CreateTexture(texture("reversed"));
        reversed.AddPass("consumer", {{output, RHIResourceState::ShaderResource}}, nullptr, true);
        reversed.AddPass("producer", {{output, RHIResourceState::RenderTarget}}, nullptr);
        if (reversed.Compile(error)) return fail("reversed RAW mutation accepted");
        EnhancedRenderGraph legacy(services);
        const auto blend = legacy.CreateTexture(texture("blend"));
        legacy.AddPass("clear", {{blend, RHIResourceState::RenderTarget}}, nullptr);
        legacy.AddPass("blend", {{blend, RHIResourceState::RenderTarget}}, nullptr, true);
        if (!legacy.Compile(error) || !legacy.CaptureDiagnosticSnapshot(snapshot)) return false;
        // Legacy state cannot express Modify: this known gap is frozen, not claimed solved.
        if (!snapshot.passes[0].culled || !snapshot.passes[1].usages[0].inferredWrite)
            return fail("legacy multi-writer/implicit Modify baseline changed");
        error.clear();
        return true;
    }

    bool ValidateR6bNeutralReadbackGraph(std::string& outError)
    {
        R6bFakeReadbackServices services;
        EnhancedRenderGraph graph(services);

        const RHITextureHandle texture{ RHIHandleBits::Encode(10, 3) };
        const RHIBufferHandle buffer{ RHIHandleBits::Encode(11, 4) };
        RHIResourceState textureFinal = RHIResourceState::Common;
        RHIResourceState bufferFinal = RHIResourceState::Common;
        const RGHandle graphTexture = graph.ImportTexture(texture, RHIResourceState::Common,
            "fake.readback.texture", &textureFinal);
        const RGHandle graphBuffer = graph.ImportBuffer(buffer, RHIResourceState::Common,
            "fake.readback.buffer", &bufferFinal);

        RHIReadback textureReadback{};
        RHIReadback bufferReadback{};
        if (!services.CreateReadback(8, 4, RHIFormat::RGBA8Unorm, 3,
                textureReadback, outError) ||
            !services.CreateBufferReadback(128, bufferReadback, outError))
            return false;

        graph.AddPass("fake.handle.readback",
            { { graphTexture, RHIResourceState::CopySource },
              { graphBuffer, RHIResourceState::CopySource } },
            [&](const EnhancedRenderGraph::ExecuteContext& context)
            {
                const RHITextureHandle resolved = context.ResolveHandle(graphTexture);
                context.encoder->CopyToReadback(textureReadback, resolved, 1, 2);
                context.encoder->CopyVolumeToReadback(textureReadback, resolved, 3);
                context.encoder->CopyPartialToReadback(textureReadback, resolved, 2, 4);
                context.encoder->CopyBufferToReadback(bufferReadback, buffer, 16, 64);
            }, true);

        if (!graph.Compile(outError) || !graph.Execute(outError)) return false;

        const auto& copies = services.encoder.copies;
        const bool copied = 4 == copies.size() &&
            R6bFakeReadbackEncoder::CopyRecord::Kind::Texture == copies[0].kind &&
            texture == copies[0].texture && textureReadback.buffer == copies[0].readback &&
            1 == copies[0].slice && 2 == copies[0].subresource &&
            R6bFakeReadbackEncoder::CopyRecord::Kind::Volume == copies[1].kind &&
            texture == copies[1].texture && 3 == copies[1].subresource &&
            R6bFakeReadbackEncoder::CopyRecord::Kind::Partial == copies[2].kind &&
            texture == copies[2].texture && 2 == copies[2].slice &&
            4 == copies[2].subresource &&
            R6bFakeReadbackEncoder::CopyRecord::Kind::Buffer == copies[3].kind &&
            buffer == copies[3].buffer && bufferReadback.buffer == copies[3].readback &&
            16 == copies[3].sourceOffset && 64 == copies[3].bytes;
        const bool unsubmitted = RHIResourceState::Common == textureFinal &&
            RHIResourceState::Common == bufferFinal;
        services.AcceptRecording();
        const bool transitioned = unsubmitted && 1 == services.encoder.barrierBatches &&
            1 == services.encoder.textureTransitions &&
            1 == services.encoder.bufferTransitions &&
            RHIResourceState::CopySource == textureFinal &&
            RHIResourceState::CopySource == bufferFinal;

        services.ReleaseReadback(textureReadback);
        services.ReleaseReadback(bufferReadback);
        const bool released = 0 == services.liveReadbacks && 2 == services.releasedReadbacks;

        if (!copied || !transitioned || !released)
        {
            outError = "handle copy/transition/readback lifetime 계약 불일치";
            return false;
        }
        return true;
    }
}

bool DX12Test::RunSelfTest(const std::string& outputPngPath,
    uint32_t frameCount, const std::string& texturePath, std::string& outLog)
{
    using Microsoft::WRL::ComPtr;

    RHIShaderCompiler::ResetStats();

    if (!ValidateShaderPermutation(outLog)) return false;
    if (!ValidateShaderMeta(outLog)) return false;
    if (!RenderTest::RunShaderReflectionSelfTest(texturePath, outLog)) return false;

    // 별도 진단 명령을 늘리지 않는다. 기존 DX12 종단 selftest의 가장 앞에서
    // backend-neutral 파이프라인 기술 계약을 GPU 없이 독립 검증한다.
    std::string pipelineLog;
    if (!LivePipelineDesc::RunSelfTest(pipelineLog))
    {
        outLog += "[pipeline] 실패\n" + pipelineLog;
        return false;
    }
    outLog += "[pipeline] backend-neutral descriptor 통과\n" + pipelineLog;

    constexpr uint32_t kWidth = 640;
    constexpr uint32_t kHeight = 360;

    DX12DeviceResources resources;
    std::string error;
    if (!resources.Initialize(kWidth, kHeight, error))
    {
        outLog += "[1/4] 디바이스 초기화 실패: " + error + "\n";
        return false;
    }
    DX12TestTextureRegistration renderTargetRegistration(
        resources, resources.GetRenderTarget());
    if (!renderTargetRegistration.IsValid())
    {
        outLog += "[1/4] 렌더 타깃 핸들 등록 실패\n";
        resources.Shutdown();
        return false;
    }
    outLog += "[1/4] 디바이스·큐·펜스·타깃 생성 완료\n";

    // ── 루트 시그니처(비어 있음)와 PSO ──
    RHIShaderBlob vsBlob;
    RHIShaderBlob psBlob;
    if (!CompileShader("VSMain", "vs_5_0", vsBlob, outLog)) return false;
    if (!CompileShader("PSMain", "ps_5_0", psBlob, outLog)) return false;

    // 루트 시그니처는 캐시를 통해 얻는다(PHASE 3-4). 식별자를 손으로 붙이지 않는
    // 것이 요점 — 두 패스가 같은 번호를 다른 레이아웃에 붙이면 PSO 캐시가 엉뚱한
    // 파이프라인을 돌려주는데, 원인이 '캐시 히트'라 추적이 어렵다.
    DX12RootSignatureCache rootSignatureCache;
    if (!rootSignatureCache.Initialize(&resources, error))
    {
        outLog += "[2/4] 루트 시그니처 캐시 초기화 실패: " + error + "\n";
        return false;
    }

    RHIPipelineLayoutHandle triangleRoot;
    {
        RHIPipelineLayoutDesc desc{};
        triangleRoot = rootSignatureCache.GetOrCreate(desc, error);
        if (!triangleRoot.IsValid())
        {
            outLog += "[2/4] 루트 시그니처 생성 실패: " + error + "\n";
            return false;
        }
    }

    // PSO는 매니저를 통해 얻는다(PHASE 3-4). 자가 검증도 실전과 같은 경로를 타야
    // 캐시·해시가 실제로 동작하는지 확인된다.
    DX12PSOManager psoManager;
    if (!psoManager.Initialize(&resources, L"dx12_pso_selftest.cache", error))
    {
        outLog += "[2/4] PSO 매니저 초기화 실패: " + error + "\n";
        return false;
    }

    RHIGraphicsPipelineDesc triangleDesc{};
    triangleDesc.vsBytecode = vsBlob.Data();
    triangleDesc.vsSize = vsBlob.Size();
    triangleDesc.psBytecode = psBlob.Data();
    triangleDesc.psSize = psBlob.Size();
    triangleDesc.layout = triangleRoot;

    RHIPipelineHandle pso = psoManager.GetOrCreate(triangleDesc, error);
    if (!pso.IsValid())
    {
        outLog += "[2/4] 삼각형 PSO 생성 실패: " + error + "\n";
        return false;
    }
    // ── 텍스처 블릿 파이프라인: SRV 힙 + 정적 샘플러 루트 시그니처 + 쿼드 PSO ──
    RHIShaderBlob quadVsBlob;
    RHIShaderBlob quadPsBlob;
    if (!CompileShader("VSQuad", "vs_5_0", quadVsBlob, outLog)) return false;
    if (!CompileShader("PSQuad", "ps_5_0", quadPsBlob, outLog)) return false;

    // 메모리 표를 비운 뒤 같은 요청을 다시 보내도 디스크 콘텐츠 캐시가 받아야
    // 한다. 프로세스 안에서 검증하지만 실제 파일을 다시 읽으므로 두 번째 실행
    // 컴파일 0건 계약과 같은 경로다.
    const auto shaderStatsBeforeProbe = RHIShaderCompiler::GetStats();
    RHIShaderCompiler::ClearMemoryCache();
    RHIShaderBlob shaderCacheProbe;
    if (!CompileShader("VSMain", "vs_5_0", shaderCacheProbe, outLog)) return false;
    const auto shaderStatsAfterProbe = RHIShaderCompiler::GetStats();
    if (shaderStatsAfterProbe.compiles != shaderStatsBeforeProbe.compiles
        || shaderStatsAfterProbe.diskHits != shaderStatsBeforeProbe.diskHits + 1)
    {
        outLog += "[shader] 콘텐츠 캐시 재사용 실패 — 메모리 표를 비운 뒤 재컴파일됨\n";
        return false;
    }
    outLog += "[shader] Slang SM6 + 디스크 콘텐츠 캐시 통과 — 컴파일 "
        + std::to_string(shaderStatsAfterProbe.compiles) + " · 디스크 히트 "
        + std::to_string(shaderStatsAfterProbe.diskHits) + " · 메모리 히트 "
        + std::to_string(shaderStatsAfterProbe.memoryHits) + "\n";

    // SRV는 recording descriptor page에서, 샘플러는 샘플러 힙에서 얻는다(PHASE 3-4).
    //
    // 예전에는 단발 SRV 힙 하나를 만들고 샘플러를 루트에 정적으로 박아 두었다.
    // 그건 브링업에서 '텍스처가 보인다'를 증명하기 위한 최소 구성이었고, 실제
    // 패스 이식에는 못 쓴다 — 패스마다 힙을 만들면 힙 교체가 패스 경계마다
    // 일어나고, 정적 샘플러는 머티리얼마다 다른 필터를 감당하지 못한다.
    RHIPipelineLayoutHandle quadRoot;
    {
        // 테이블 둘: SRV 하나, 샘플러 하나. 샘플러가 루트에서 빠지면서
        // 파라미터가 하나 늘었고, 그만큼 레이아웃이 달라져 루트 시그니처 캐시의
        // id도 달라진다(그래서 PSO 캐시도 자동으로 새 키를 쓴다).
        const RHIPipelineLayoutParam params[] = {
            RHILayout::SrvTable(1, 0, RHIShaderVisibility::Pixel),
            RHILayout::SamplerTable(1, 0, RHIShaderVisibility::Pixel),
        };

        RHIPipelineLayoutDesc desc{};
        desc.params = params;

        quadRoot = rootSignatureCache.GetOrCreate(desc, error);
        if (!quadRoot.IsValid())
        {
            outLog += "[2/4] 쿼드 루트 시그니처 생성 실패: " + error + "\n";
            return false;
        }
    }

    // 샘플러는 프레임마다 바뀌지 않으므로 한 번만 만들어 둔다.
    D3D12_GPU_DESCRIPTOR_HANDLE samplerHandle{};
    {
        const RHISamplerDesc sampler = RHISampler::Point(RHIAddressMode::Clamp);

        samplerHandle = resources.GetSamplerHeap().GetOrCreate(sampler);
        if (0 == samplerHandle.ptr)
        {
            outLog += "[2/4] 샘플러 생성 실패\n";
            return false;
        }
    }

    RHIGraphicsPipelineDesc quadDesc{};
    quadDesc.vsBytecode = quadVsBlob.Data();
    quadDesc.vsSize = quadVsBlob.Size();
    quadDesc.psBytecode = quadPsBlob.Data();
    quadDesc.psSize = quadPsBlob.Size();
    quadDesc.layout = quadRoot;

    const RHIPipelineHandle quadPso = psoManager.GetOrCreate(quadDesc, error);
    if (!quadPso.IsValid())
    {
        outLog += "[2/4] 쿼드 PSO 생성 실패: " + error + "\n";
        return false;
    }

    // 같은 desc를 다시 요청하면 메모리 캐시가 받아야 한다 — 중복 제거 확인.
    if (psoManager.GetOrCreate(triangleDesc, error) != pso)
    {
        outLog += "[2/4] 메모리 캐시 실패 — 같은 desc가 다른 PSO를 돌려줬다\n";
        return false;
    }

    {
        const auto stats = psoManager.GetStats();
        outLog += "[2/4] 루트 시그니처·PSO 생성 완료(삼각형 + 텍스처 쿼드) — 컴파일 "
            + std::to_string(stats.compiles) + " · 라이브러리 히트 "
            + std::to_string(stats.libraryHits) + " · 메모리 히트 "
            + std::to_string(stats.memoryHits) + "\n";
    }

    // ── 체커보드 텍스처 생성·업로드 ──
    //
    // 스테이징은 업로드 링에서 잘라 쓴다. 예전에는 텍스처마다 업로드 버퍼를
    // 새로 만들고 GPU가 다 읽을 때까지 살려 뒀는데(그래서 여기 ComPtr가 하나
    // 더 있었다), 씬을 이식하면 그 방식이 프레임당 수십~수백 건이 된다.
    ComPtr<ID3D12Resource> texture;
    {
        D3D12_HEAP_PROPERTIES defaultHeap{};
        defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_RESOURCE_DESC texDesc{};
        texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        texDesc.Width = kTexSize;
        texDesc.Height = kTexSize;
        texDesc.DepthOrArraySize = 1;
        texDesc.MipLevels = 1;
        texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        texDesc.SampleDesc.Count = 1;

        if (FAILED(resources.GetDevice()->CreateCommittedResource(&defaultHeap,
            D3D12_HEAP_FLAG_NONE, &texDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
            IID_PPV_ARGS(&texture))))
        {
            outLog += "[2/4] 텍스처 생성 실패\n";
            return false;
        }

        // 64px * 4바이트 = 256 = D3D12_TEXTURE_DATA_PITCH_ALIGNMENT — 패딩 불필요.
        constexpr uint32_t rowPitch = kTexSize * 4;
        constexpr uint64_t uploadBytes = static_cast<uint64_t>(rowPitch) * kTexSize;

        // 업로드는 전용 사이클로 제출 — 렌더 프레임과 섞지 않아 실패 지점이 분리된다.
        // BeginFrame이 업로드 링의 이 프레임 구간을 되감아 주므로, 링에서 잘라내는
        // 것은 그 뒤여야 한다.
        if (!resources.BeginFrame(error))
        {
            outLog += "[2/4] 업로드 Begin 실패: " + error + "\n";
            return false;
        }

        // 텍스처 스테이징은 512바이트 정렬이 필요하다(CopyTextureRegion의 요구).
        const auto staging = resources.AllocateUpload(
            RHIUploadRequest{ uploadBytes, RHIUploadUsage::TextureCopy, 1 });
        if (!staging.IsValid())
        {
            outLog += "[2/4] 업로드 링 할당 실패(구간 부족)\n";
            return false;
        }

        auto* dst = static_cast<uint8_t*>(staging.cpuAddress);
        constexpr uint32_t cellSize = kTexSize / kCheckerCells;
        for (uint32_t y = 0; y < kTexSize; ++y)
        {
            for (uint32_t x = 0; x < kTexSize; ++x)
            {
                const bool isA = (((x / cellSize) + (y / cellSize)) % 2) == 0;
                const uint8_t* color = isA ? kColorA : kColorB;
                memcpy(&dst[y * rowPitch + x * 4], color, 4);
            }
        }

        D3D12_TEXTURE_COPY_LOCATION src{};
        src.pResource = resources.Resolve(staging.buffer);
        src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        src.PlacedFootprint.Offset = staging.offset;
        src.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        src.PlacedFootprint.Footprint.Width = kTexSize;
        src.PlacedFootprint.Footprint.Height = kTexSize;
        src.PlacedFootprint.Footprint.Depth = 1;
        src.PlacedFootprint.Footprint.RowPitch = rowPitch;

        D3D12_TEXTURE_COPY_LOCATION dstLoc{};
        dstLoc.pResource = texture.Get();
        dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;

        resources.GetCommandList()->CopyTextureRegion(&dstLoc, 0, 0, 0, &src, nullptr);

        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = texture.Get();
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        resources.GetCommandList()->ResourceBarrier(1, &barrier);

        if (!resources.EndFrame(error))
        {
            outLog += "[2/4] 업로드 End 실패: " + error + "\n";
            return false;
        }
        resources.WaitForGpu();

    }
    outLog += "[2/4] 체커보드 텍스처 업로드 완료\n";

    // ── 프레임 루프: 얼로케이터 3개가 frameCount 동안 회전한다 ──
    for (uint32_t frame = 0; frame < frameCount; ++frame)
    {
        if (!resources.BeginFrame(error))
        {
            outLog += "[3/4] 프레임 " + std::to_string(frame) + " Begin 실패: " + error + "\n";
            return false;
        }

        auto* commandList = resources.GetCommandList();

        const D3D12_VIEWPORT viewport{ 0.f, 0.f,
            static_cast<float>(kWidth), static_cast<float>(kHeight), 0.f, 1.f };
        const D3D12_RECT scissor{ 0, 0,
            static_cast<LONG>(kWidth), static_cast<LONG>(kHeight) };
        commandList->RSSetViewports(1, &viewport);
        commandList->RSSetScissorRects(1, &scissor);

        const auto rtvHandle = resources.GetRtvHandle();
        commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

        // 클리어 색은 생성 시 힌트와 반드시 일치해야 검증 레이어가 조용하다.
        // (프레임 번호를 배경에 실어 봤다가 경고 6건을 실측하고 고정으로 돌렸다 —
        //  얼로케이터 회전은 BeginFrame의 펜스 대기 6사이클이 이미 증명한다.)
        commandList->ClearRenderTargetView(rtvHandle, DX12DeviceResources::kClearColor, 0, nullptr);

        // ★ 이 검사는 인코더를 안 타고 원시 커맨드 리스트에 직접 건다 —
        //   그것이 검사의 목적(디바이스·PSO·루트가 날것으로 도는가)이라
        //   그대로 둔다. 대신 핸들을 스스로 푼다.
        const DX12PipelineEntry triangleEntry = resources.Resolve(pso);
        commandList->SetGraphicsRootSignature(triangleEntry.signature);
        commandList->SetPipelineState(triangleEntry.pipeline);
        commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        commandList->DrawInstanced(3, 1, 0, 0);

        // 텍스처 쿼드 — SRV를 현재 recording descriptor page에서 잘라 쓴다.
        //
        // 프레임마다 새로 자르고 뷰를 다시 만드는 것이 요점이다. 실제 씬에서는
        // 프레임마다 보이는 텍스처 집합이 달라지므로 고정 힙으로는 감당이 안 된다.
        // (뷰 생성 대신 CPU 전용 힙에서 CopyDescriptorsSimple로 가져오는 최적화가
        //  있지만, 그건 뷰가 재사용될 때의 얘기라 이식 뒤에 판단한다.)
        const auto srvSlot = resources.GetDescriptorRecycler().Allocate(1);
        if (!srvSlot.IsValid())
        {
            outLog += "[3/4] descriptor page 할당 실패(구간 부족)\n";
            return false;
        }
        resources.GetDevice()->CreateShaderResourceView(texture.Get(), nullptr, srvSlot.cpu);

        // 힙 바인딩은 루트 테이블 설정보다 먼저(검증 레이어 규칙).
        // CBV/SRV/UAV 힙과 샘플러 힙은 종류가 달라 동시에 하나씩 바인딩된다.
        ID3D12DescriptorHeap* heaps[] = {
            resources.GetDescriptorRecycler().GetHeap(),
            resources.GetSamplerHeap().GetHeap() };
        commandList->SetDescriptorHeaps(2, heaps);
        const DX12PipelineEntry quadEntry = resources.Resolve(quadPso);
        commandList->SetGraphicsRootSignature(quadEntry.signature);
        commandList->SetPipelineState(quadEntry.pipeline);
        commandList->SetGraphicsRootDescriptorTable(0, srvSlot.gpu);
        commandList->SetGraphicsRootDescriptorTable(1, samplerHandle);
        commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        commandList->DrawInstanced(4, 1, 0, 0);

        // 마지막 프레임만 리드백으로 복사 — RT ↔ COPY_SOURCE 상태 전이 검증을 겸한다.
        if (frame == frameCount - 1)
        {
            D3D12_RESOURCE_BARRIER barrier{};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = resources.GetRenderTarget();
            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            commandList->ResourceBarrier(1, &barrier);

            resources.GetImmediateEncoder().CopyToReadback(
                resources.GetFrameReadback(), renderTargetRegistration.Handle());

            std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
            commandList->ResourceBarrier(1, &barrier);
        }

        if (!resources.EndFrame(error))
        {
            outLog += "[3/4] 프레임 " + std::to_string(frame) + " End 실패: " + error + "\n";
            return false;
        }
    }

    resources.WaitForGpu();
    outLog += "[3/4] " + std::to_string(frameCount) + "프레임 제출·완료(얼로케이터 "
        + std::to_string(DX12DeviceResources::kFrameCount) + "개 회전)\n";

    // ── 리드백 → 픽셀 검증 → PNG ──
    {
        RHIReadbackImage captured{};
        {
            std::string readbackError;
            if (!resources.MapReadback(resources.GetFrameReadback(), captured, readbackError))
            {
                outLog += "[4/4] 리드백 Map 실패: " + readbackError + "\n";
                return false;
            }
        }

        const uint8_t* pixels = captured.data.data();
        const uint32_t rowPitch = captured.rowPitch;

        // 중앙(삼각형 내부)은 클리어 색이 아니어야 하고, 좌상단 구석은 클리어 색이어야 한다.
        auto pixelAt = [&](uint32_t x, uint32_t y)
        {
            return &pixels[static_cast<size_t>(y) * rowPitch + static_cast<size_t>(x) * 4];
        };
        const uint8_t* center = pixelAt(kWidth / 2, kHeight / 2);
        const uint8_t* corner = pixelAt(4, 4);

        // 구석은 클리어 색과 정확히 일치해야 한다. (처음엔 '파랑 우세'라는 느슨한
        // 단정을 썼다가 렌더는 맞는데 검사가 틀리는 오탐을 냈다 — 기대값 비교로 교체.)
        const auto expected = static_cast<uint8_t>(DX12DeviceResources::kClearColor[0] * 255.f + 0.5f);
        const auto expectedBlue = static_cast<uint8_t>(DX12DeviceResources::kClearColor[2] * 255.f + 0.5f);
        // 주의: near/far는 windef.h 매크로라 식별자로 못 쓴다.
        auto isNear = [](uint8_t a, uint8_t b) { return (a > b ? a - b : b - a) <= 2; };

        const bool centerIsTriangle = center[0] != corner[0] || center[1] != corner[1] || center[2] != corner[2];
        const bool cornerIsClear = isNear(corner[0], expected) && isNear(corner[1], expected)
            && isNear(corner[2], expectedBlue);

        // 텍스처 쿼드: NDC [-0.9,-0.3]²이 640x360에서 (32,234)-(224,342)로 맵핑된다.
        // 4x4 체커의 (0,0)·(1,0) 셀 중앙을 찍는다 — 포인트 샘플링이라 결정적이다.
        auto matches = [&isNear](const uint8_t* pixel, const uint8_t* expectedColor)
        {
            return isNear(pixel[0], expectedColor[0]) && isNear(pixel[1], expectedColor[1])
                && isNear(pixel[2], expectedColor[2]);
        };
        const uint8_t* checkerA = pixelAt(56, 247);
        const uint8_t* checkerB = pixelAt(104, 247);
        const bool quadIsTextured = matches(checkerA, kColorA) && matches(checkerB, kColorB);

        if (!centerIsTriangle || !cornerIsClear || !quadIsTextured)
        {
            outLog += "[4/4] 픽셀 검증 실패 — 중앙("
                + std::to_string(center[0]) + "," + std::to_string(center[1]) + "," + std::to_string(center[2])
                + ") 구석(" + std::to_string(corner[0]) + "," + std::to_string(corner[1]) + "," + std::to_string(corner[2])
                + ") 체커A(" + std::to_string(checkerA[0]) + "," + std::to_string(checkerA[1]) + "," + std::to_string(checkerA[2])
                + ") 체커B(" + std::to_string(checkerB[0]) + "," + std::to_string(checkerB[1]) + "," + std::to_string(checkerB[2]) + ")\n";
            return false;
        }

        DirectX::Image image{};
        image.width = kWidth;
        image.height = kHeight;
        image.format = DXGI_FORMAT_R8G8B8A8_UNORM;
        image.rowPitch = rowPitch;
        image.slicePitch = static_cast<size_t>(rowPitch) * kHeight;
        image.pixels = const_cast<uint8_t*>(pixels);

        const std::wstring widePath(outputPngPath.begin(), outputPngPath.end());
        const HRESULT hr = DirectX::SaveToWICFile(image, DirectX::WIC_FLAGS_NONE,
            DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG), widePath.c_str());

        if (FAILED(hr))
        {
            outLog += "[4/4] PNG 저장 실패\n";
            return false;
        }
    }

    // 검증 레이어에 WARNING 이상이 없어야 진짜 통과다(INFO는 로그에만 남는다).
    std::string debugMessages;
    const uint32_t problems = resources.DrainDebugMessages(debugMessages);
    if (problems > 0)
    {
        outLog += "[4/4] 검증 레이어 문제 " + std::to_string(problems) + "건:\n" + debugMessages;
        return false;
    }

    // PSO 캐시를 남긴다 — 다음 실행/다음 매니저가 컴파일 없이 복원해야 한다.
    if (!psoManager.SaveCache(error))
    {
        outLog += "[4/4] PSO 캐시 저장 실패: " + error + "\n";
        return false;
    }

    {
        const auto stats = psoManager.GetStats();
        outLog += "[4/4] PSO 캐시 저장 — 컴파일 " + std::to_string(stats.compiles)
            + " · 라이브러리 히트 " + std::to_string(stats.libraryHits) + "\n";
    }

    psoManager.Shutdown();
    outLog += "[4/4] 픽셀 검증·PNG 저장·검증 레이어 클린 — 통과\n";
    renderTargetRegistration.Reset();
    resources.Shutdown();
    return true;
}

namespace
{
    // Occupy a real scheduler worker so queued-input lifetime and drain are deterministic.
    class PsoWorkerGate
    {
    public:
        explicit PsoWorkerGate(job_scheduler& scheduler)
        {
            m_job = scheduler.submit([this]
            {
                std::unique_lock lock(m_mutex);
                m_entered = true;
                m_condition.notify_all();
                m_condition.wait(lock, [this] { return m_released; });
            });
        }
        ~PsoWorkerGate()
        {
            Release();
            m_job.wait();
        }
        bool WaitUntilEntered()
        {
            std::unique_lock lock(m_mutex);
            return m_condition.wait_for(lock, std::chrono::seconds(10), [this] { return m_entered; });
        }
        void Release()
        {
            std::lock_guard lock(m_mutex);
            m_released = true;
            m_condition.notify_all();
        }

    private:
        std::mutex m_mutex;
        std::condition_variable m_condition;
        bool m_entered{}, m_released{};
        job_handle m_job;
    };

    bool VerifyPsoJobs(DX12DeviceResources& resources, const RHIGraphicsPipelineDesc& base,
        std::string& outLog)
    {
        thread_pool pool;
        job_scheduler scheduler(pool);
        DX12PSOManager manager(scheduler);
        std::string error;
        if (!manager.Initialize(&resources, L"", error))
        {
            outLog += "PSO jobs initialization failed: " + error + "\n";
            return false;
        }
        using State = DX12PSOManager::RequestState;
        ID3D12PipelineState* result = nullptr;
        if (manager.Request(base, &result) != State::Failed || result
            || manager.GetStats().m_asyncSubmissions != 0)
        {
            outLog += "Stopped scheduler accepted a PSO request\n";
            return false;
        }
        scheduler.start(1);
        {
            PsoWorkerGate gate(scheduler);
            if (!gate.WaitUntilEntered()) return false;
            const auto* vs = static_cast<const std::uint8_t*>(base.vsBytecode);
            const auto* ps = static_cast<const std::uint8_t*>(base.psBytecode);
            std::vector<std::uint8_t> ownedVs(vs, vs + base.vsSize);
            std::vector<std::uint8_t> ownedPs(ps, ps + base.psSize);
            RHIGraphicsPipelineDesc temporary = base;
            temporary.vsBytecode = ownedVs.data();
            temporary.psBytecode = ownedPs.data();
            for (int i = 0; i < 8; ++i)
            {
                if (manager.Request(temporary, &result) != State::Pending || result) return false;
            }
            if (manager.GetStats().m_asyncSubmissions != 1
                || manager.GetStats().m_asyncWorkerExecutions != 0)
            {
                outLog += "PSO request duplicated work or bypassed the occupied scheduler\n";
                return false;
            }
            std::fill(ownedVs.begin(), ownedVs.end(), 0);
            std::fill(ownedPs.begin(), ownedPs.end(), 0);
            // Both source arrays die before the queued compile starts.
        }
        auto waitReady = [&](const RHIGraphicsPipelineDesc& desc)
        {
            for (int i = 0; i < 1000; ++i)
            {
                const auto state = manager.Request(desc, &result);
                if (state == State::Ready) return result != nullptr;
                if (state == State::Failed) return false;
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            return false;
        };
        if (!waitReady(base))
        {
            outLog += "Queued PSO did not retain shader bytes\n";
            return false;
        }
        const auto ready = manager.GetStats();
        if (ready.m_asyncSubmissions != 1 || ready.m_asyncWorkerExecutions != 1
            || ready.compiles + ready.libraryHits != 1 || ready.failures != 1) return false;

        // A driver compile failure must become Failed, preserve existing cache entries,
        // and leave neither an exception nor a permanently pending result for the caller.
        const std::array<std::uint8_t, 16> invalidBytecode{};
        RHIGraphicsPipelineDesc invalid = base;
        invalid.psBytecode = invalidBytecode.data();
        invalid.psSize = invalidBytecode.size();
        if (manager.Request(invalid, &result) != State::Pending) return false;
        State failed = State::Pending;
        for (int i = 0; i < 1000 && failed == State::Pending; ++i)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            failed = manager.Request(invalid, &result);
        }
        if (failed != State::Failed || result || manager.GetStats().failures != ready.failures + 1
            || !waitReady(base))
        {
            outLog += "Failed PSO compile lost its result or damaged a cached pipeline\n";
            return false;
        }

        RHIGraphicsPipelineDesc variant = base;
        variant.blendEnable = !base.blendEnable;
        auto drainQueued = [&](bool shutdown)
        {
            PsoWorkerGate gate(scheduler);
            if (!gate.WaitUntilEntered()) return false;
            const auto before = manager.GetStats();
            if (manager.Request(variant, &result) != State::Pending) return false;
            std::atomic<bool> rejectedDuringDrain{ false };
            std::jthread release([&]
            {
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
                while (std::chrono::steady_clock::now() < deadline)
                {
                    if (manager.Request(variant, nullptr) == State::Failed)
                    {
                        rejectedDuringDrain = true;
                        break;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                gate.Release();
            });
            if (shutdown) manager.Shutdown();
            else manager.InvalidatePipelines();
            release.join();
            const auto after = manager.GetStats();
            if (!rejectedDuringDrain || after.m_asyncSubmissions != before.m_asyncSubmissions + 1
                || after.m_asyncWorkerExecutions != before.m_asyncWorkerExecutions + 1) return false;
            const auto next = manager.Request(variant, &result);
            return shutdown ? next == State::Failed && !result
                            : next == State::Pending && !result && waitReady(variant);
        };
        if (!drainQueued(false))
        {
            outLog += "PSO invalidation did not drain/reject/discard/reopen correctly\n";
            return false;
        }
        variant.cullMode = RHICullMode::Front;
        if (!drainQueued(true))
        {
            outLog += "PSO shutdown did not drain or kept admitting work\n";
            return false;
        }
        if (!scheduler.is_running()) return false;
        scheduler.shutdown();

        // A cache drains only its own jobs. An unrelated blocked job stays outstanding.
        scheduler.start(2);
        if (!manager.Initialize(&resources, L"", error)) return false;
        {
            PsoWorkerGate unrelated(scheduler);
            if (!unrelated.WaitUntilEntered()) return false;
            if (manager.Request(base, &result) != State::Pending || !waitReady(base)) return false;
            std::atomic<bool> finished{ false }, timedOut{ false };
            std::jthread watchdog([&]
            {
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
                while (!finished && std::chrono::steady_clock::now() < deadline)
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                if (!finished) timedOut = true;
                unrelated.Release();
            });
            manager.Shutdown();
            finished = true;
            watchdog.join();
            if (timedOut || !scheduler.is_running())
            {
                outLog += "PSO cache waited for unrelated work or stopped the engine scheduler\n";
                return false;
            }
        }
        outLog += "PSO jobs: stopped rejection, owned bytes, duplicate coalescing, worker execution, "
            "compile failure, invalidation/shutdown drain, admission closure, reopen and independent wait passed\n";
        return true;
    }
}

bool DX12Test::RunPsoCacheTest(const std::string& cacheFilePath, std::string& outLog)
{
    using Microsoft::WRL::ComPtr;

    // 렌더는 하지 않는다 — 디바이스만 있으면 PSO 생성·캐시는 검증된다.
    DX12DeviceResources resources;
    std::string error;
    if (!resources.Initialize(64, 64, error))
    {
        outLog += "디바이스 초기화 실패: " + error + "\n";
        return false;
    }

    RHIShaderBlob vsBlob;
    RHIShaderBlob psBlob;
    if (!CompileShader("VSMain", "vs_5_0", vsBlob, outLog)) return false;
    if (!CompileShader("PSMain", "ps_5_0", psBlob, outLog)) return false;

    DX12RootSignatureCache rootSignatureCache;
    if (!rootSignatureCache.Initialize(&resources, error))
    {
        outLog += "루트 시그니처 캐시 초기화 실패: " + error + "\n";
        return false;
    }

    RHIPipelineLayoutHandle emptyRoot;
    {
        RHIPipelineLayoutDesc desc{};
        emptyRoot = rootSignatureCache.GetOrCreate(desc, error);
        if (!emptyRoot.IsValid())
        {
            outLog += "루트 시그니처 준비 실패: " + error + "\n";
            return false;
        }
    }

    // ── 루트 시그니처 캐시 자체 검증 ──
    //
    // PSO 캐시의 키에 rootSignatureId가 들어가므로, 이 식별자가 레이아웃을
    // 제대로 구분하지 못하면 PSO 캐시가 엉뚱한 파이프라인을 돌려준다.
    // 예전에는 호출부가 번호를 손으로 붙였고(1, 2, 10), 그 규율이 깨지는 순간
    // 원인이 '캐시 히트'인 버그가 된다. 구조가 막는지 여기서 확인한다.
    {
        // 같은 레이아웃을 다시 요청하면 같은 객체·같은 id여야 한다.
        RHIPipelineLayoutDesc sameDesc{};
        const auto again = rootSignatureCache.GetOrCreate(sameDesc, error);

        // 레이아웃이 다르면 id가 달라야 한다 — 이것이 손번호가 못 하던 일이다.
        const RHIPipelineLayoutParam params[] = {
            RHILayout::SrvTable(1, 0, RHIShaderVisibility::Pixel),
        };

        RHIPipelineLayoutDesc tableDesc{};
        tableDesc.params = params;
        const auto tableRoot = rootSignatureCache.GetOrCreate(tableDesc, error);

        // 같은 내용을 다른 주소에 담아도 같은 id여야 한다. 설명을 통째로
        // 바이트 해시하면 span이 든 포인터를 해시하게 되어 여기서 걸린다 —
        // 그러면 캐시가 통째로 놀고, 증상은 '왜인지 매번 컴파일한다'로만
        // 보인다. V4로 설명이 중립 타입이 됐어도 이 함정은 그대로다.
        const RHIPipelineLayoutParam paramsCopy[] = { params[0] };
        RHIPipelineLayoutDesc tableCopyDesc{};
        tableCopyDesc.params = paramsCopy;
        const auto tableCopyRoot = rootSignatureCache.GetOrCreate(tableCopyDesc, error);

        // ★ 핸들 비교가 곧 "같은 객체인가"다(A-1). 예전에는 id 와 signature 를
        //   따로 비교했는데, 같은 레이아웃이면 같은 핸들이 나오는 것이 캐시의
        //   약속이므로 비교가 하나로 준다 — 그리고 그 약속이 깨지면 표가
        //   자라고 PSO 캐시가 논다.
        const bool sameReused = again.IsValid() && again == emptyRoot;
        const bool differentSeparated = tableRoot.IsValid() && tableRoot != emptyRoot;
        const bool contentHashed = tableCopyRoot.IsValid() && tableCopyRoot == tableRoot;

        if (!sameReused || !differentSeparated || !contentHashed)
        {
            outLog += "루트 시그니처 캐시 검증 실패 (재사용 ";
            outLog += sameReused ? "O" : "X";
            outLog += " · 구분 ";
            outLog += differentSeparated ? "O" : "X";
            outLog += " · 내용해시 ";
            outLog += contentHashed ? "O" : "X";
            outLog += ")\n";
            return false;
        }

        const auto rootStats = rootSignatureCache.GetStats();
        outLog += "루트 시그니처 캐시 검증 통과 — 생성 " + std::to_string(rootStats.creates)
            + " · 히트 " + std::to_string(rootStats.hits)
            + " · 보관 " + std::to_string(rootSignatureCache.GetCachedCount()) + "\n";
    }

    // 상태만 다른 변형 3종 — 해시가 상태를 실제로 구분하는지 확인한다.
    // (셰이더가 같아도 다른 PSO여야 한다)
    RHIGraphicsPipelineDesc base{};
    base.vsBytecode = vsBlob.Data();
    base.vsSize = vsBlob.Size();
    base.psBytecode = psBlob.Data();
    base.psSize = psBlob.Size();
    base.layout = emptyRoot;

    if (!VerifyPsoJobs(resources, base, outLog)) return false;

    RHIGraphicsPipelineDesc variants[3] = { base, base, base };
    variants[1].cullMode = RHICullMode::Back;
    variants[2].blendEnable = true;

    // ★ 예전에는 여기서 `desc.ComputeHash()` 를 직접 불러 셋이 다른지 봤다.
    //   A-1 에서 `ComputeHash` 가 desc 의 멤버가 아니게 됐다 — 레이아웃 핸들을
    //   안정 해시로 풀어야 해서 표를 봐야 하고, 그래서 매니저의 private 이다.
    //
    //   대신 아래 1회차에서 **관측 가능한 결과**로 잰다: 상태가 다르면 캐시가
    //   서로 다른 핸들을 줘야 한다. 해시를 직접 보는 것보다 이쪽이 재려는
    //   것에 가깝다 — 해시는 수단이고 판정 대상은 '구분되는가'다.

    // 캐시 파일을 지우고 시작 — 1회차의 '컴파일 N건'을 결정적으로 만든다.
    std::remove(cacheFilePath.c_str());
    const std::wstring widePath(cacheFilePath.begin(), cacheFilePath.end());

    // ── 1회차: 캐시 없음 → 전부 컴파일 ──
    {
        DX12PSOManager manager;
        if (!manager.Initialize(&resources, widePath, error))
        {
            outLog += "1회차 초기화 실패: " + error + "\n";
            return false;
        }

        RHIGraphicsPipelineRequest request;
        RHIPipelineHandle handles[3]{};
        for (size_t i = 0; i < 3; ++i)
        {
            if (0 == i)
            {
                if (!request.Create(manager, variants[i], error))
                {
                    outLog += "1회차 owning PSO 요청 생성 실패: " + error + "\n";
                    return false;
                }
                handles[i] = request.GetHandle();
            }
            else
            {
                handles[i] = manager.GetOrCreate(variants[i], error);
            }
            if (!handles[i].IsValid())
            {
                outLog += "1회차 PSO 생성 실패: " + error + "\n";
                return false;
            }
        }

        // ★ A-1 이전에는 이 검사가 desc.ComputeHash() 를 직접 비교했다.
        //   상태만 다른 셋이 서로 다른 파이프라인이어야 한다 — 빠뜨리면
        //   서로 다른 PSO 가 같은 키를 갖고 먼저 만들어진 쪽이 조용히 재사용된다.
        if (handles[0] == handles[1] || handles[0] == handles[2] || handles[1] == handles[2])
        {
            outLog += "캐시가 상태 차이를 구분하지 못한다 — 셋이 같은 파이프라인이다\n";
            return false;
        }

        const auto stats = manager.GetStats();
        if (stats.compiles != 3 || stats.libraryHits != 0)
        {
            outLog += "1회차 기대와 다름 — 컴파일 " + std::to_string(stats.compiles)
                + "(기대 3) · 라이브러리 히트 " + std::to_string(stats.libraryHits) + "(기대 0)\n";
            return false;
        }

        const RHIGraphicsPipelineDesc& ownedBase = request.GetDesc();
        if (ownedBase.vsBytecode == variants[0].vsBytecode ||
            ownedBase.psBytecode == variants[0].psBytecode ||
            ownedBase.vsSize != variants[0].vsSize || ownedBase.psSize != variants[0].psSize)
        {
            outLog += "owning PSO 요청이 shader bytecode를 깊은 복사하지 않았다\n";
            return false;
        }

        // M5-C3b2b1: 후보 PSO를 먼저 준비한 뒤 옛 handle 하나만 stale로 만든다.
        // 이미 캐시에 있는 variant[1]로 교체해도 variant[2] lookup은 보존되어야 한다.
        constexpr RHICompletionPoint kTargetRetireAfter{ 7 };
        if (!request.Replace(manager, variants[1], kTargetRetireAfter, error))
        {
            outLog += "owning PSO 요청 교체 실패: " + error + "\n";
            return false;
        }
        const bool oldHandleRejected = !resources.Resolve(handles[0]).IsValid();
        const bool replacementPublished = request.GetHandle() == handles[1]
            && resources.Resolve(request.GetHandle()).IsValid();
        const bool unrelatedPreserved = resources.Resolve(handles[2]).IsValid();
        const auto targetedStats = manager.GetStats();
        if (!oldHandleRejected || !replacementPublished || !unrelatedPreserved
            || targetedStats.invalidations != 1 || targetedStats.retiredPipelines != 1)
        {
            outLog += "PSO targeted invalidation/보존 계약 불일치\n";
            return false;
        }

        const std::uint32_t targetedCollectedEarly =
            manager.CollectRetiredPipelines(RHICompletionPoint{ 6 });
        const std::uint32_t targetedCollectedReady =
            manager.CollectRetiredPipelines(kTargetRetireAfter);
        if (0 != targetedCollectedEarly || 1 != targetedCollectedReady)
        {
            outLog += "targeted PSO completion retirement 경계 불일치\n";
            return false;
        }

        // M5-C3b1의 전체 무효화와 generation 재발급 계약도 그대로 보존한다.
        const RHIPipelineHandle reloaded = manager.GetOrCreate(variants[0], error);
        const bool generationAdvanced = reloaded.IsValid()
            && reloaded != handles[0]
            && RHIHandleBits::GenerationOf(reloaded.id) !=
                RHIHandleBits::GenerationOf(handles[0].id)
            && resources.Resolve(reloaded).IsValid();
        constexpr RHICompletionPoint kGlobalRetireAfter{ 9 };
        const std::uint32_t invalidated = manager.InvalidatePipelines(kGlobalRetireAfter);
        const auto reloadStats = manager.GetStats();
        if (invalidated != 3 || !generationAdvanced
            || reloadStats.invalidations != 2 || reloadStats.retiredPipelines != 3)
        {
            outLog += "PSO generation invalidation/next-use 재요청 계약 불일치: "
                + error + "\n";
            return false;
        }

        const std::uint32_t collectedEarly =
            manager.CollectRetiredPipelines(RHICompletionPoint{ 8 });
        const std::uint32_t collectedReady =
            manager.CollectRetiredPipelines(kGlobalRetireAfter);
        const auto collectedStats = manager.GetStats();
        if (0 != collectedEarly || 3 != collectedReady ||
            0 != collectedStats.retiredPipelines ||
            4 != collectedStats.retiredCollections)
        {
            outLog += "PSO completion retirement 경계 불일치\n";
            return false;
        }

        const RHIPipelineHandle quarantineHandle =
            manager.GetOrCreate(variants[0], error);
        if (!quarantineHandle.IsValid())
        {
            outLog += "PSO quarantine 준비 실패: " + error + "\n";
            return false;
        }
        const std::uint32_t quarantined = manager.InvalidatePipelines();
        const std::uint32_t quarantineCollected = manager.CollectRetiredPipelines(
            RHICompletionPoint{ ~std::uint64_t{ 0 } });
        const auto quarantineStats = manager.GetStats();
        if (1 != quarantined || 0 != quarantineCollected ||
            1 != quarantineStats.retiredPipelines)
        {
            outLog += "PSO completion 0 quarantine 계약 불일치\n";
            return false;
        }

        if (!manager.SaveCache(error))
        {
            outLog += "1회차 캐시 저장 실패: " + error + "\n";
            return false;
        }
        manager.Shutdown();
        outLog += "1회차: 컴파일 3 · owning bytecode · targeted stale 1/나머지 보존"
            " · completion 6 보존/7 회수 1 · global stale 3/generation 재발급"
            " · completion 8 보존/9 회수 3 · completion 0 shutdown quarantine 1"
            " · 캐시 저장 완료\n";
    }

    // ── 2회차: 캐시 복원 → 컴파일 0 ──
    {
        DX12PSOManager manager;
        if (!manager.Initialize(&resources, widePath, error))
        {
            outLog += "2회차 초기화 실패: " + error + "\n";
            return false;
        }

        if (!manager.IsLibraryLoaded())
        {
            outLog += "2회차: 캐시 파일을 라이브러리로 복원하지 못했다\n";
            return false;
        }

        for (auto& variant : variants)
        {
            if (!manager.GetOrCreate(variant, error).IsValid())
            {
                outLog += "2회차 PSO 취득 실패: " + error + "\n";
                return false;
            }
        }

        // 메모리 캐시 확인 — 같은 요청을 반복해도 컴파일이 늘지 않아야 한다.
        for (auto& variant : variants)
        {
            manager.GetOrCreate(variant, error);
        }

        const auto stats = manager.GetStats();
        if (stats.compiles != 0 || stats.libraryHits != 3 || stats.memoryHits != 3)
        {
            outLog += "2회차 기대와 다름 — 컴파일 " + std::to_string(stats.compiles)
                + "(기대 0) · 라이브러리 히트 " + std::to_string(stats.libraryHits)
                + "(기대 3) · 메모리 히트 " + std::to_string(stats.memoryHits) + "(기대 3)\n";
            return false;
        }

        manager.Shutdown();
        outLog += "2회차: 컴파일 0 · 라이브러리 히트 3 · 메모리 히트 3 — 캐시가 컴파일을 없앴다\n";
    }

    // ── 비동기 + 폴백: 컴파일 중에도 프레임이 그릴 것을 갖는다 ──
    {
        DX12PSOManager manager;
        if (!manager.Initialize(&resources, L"", error))
        {
            outLog += "비동기 초기화 실패: " + error + "\n";
            return false;
        }

        // 폴백을 먼저 세운다(동기). 이후 다른 desc를 비동기 요청하면 그 사이
        // 프레임은 폴백으로 그려져야 한다.
        if (!manager.SetFallback(variants[0], error))
        {
            outLog += "폴백 PSO 준비 실패: " + error + "\n";
            return false;
        }

        ID3D12PipelineState* resolved = nullptr;
        const auto first = manager.Resolve(variants[1], &resolved);
        if (first != DX12PSOManager::DrawDecision::UseFallback || nullptr == resolved)
        {
            outLog += "컴파일 중인데 폴백이 나오지 않았다 — 프레임이 그릴 것을 잃는다\n";
            return false;
        }

        // 완료까지 폴링. 실전에서는 이 사이의 프레임이 폴백으로 그려진다.
        DX12PSOManager::DrawDecision decision = DX12PSOManager::DrawDecision::UseFallback;
        for (int attempt = 0; attempt < 500 && decision != DX12PSOManager::DrawDecision::UseRequested; ++attempt)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            decision = manager.Resolve(variants[1], &resolved);
        }

        if (decision != DX12PSOManager::DrawDecision::UseRequested || nullptr == resolved)
        {
            outLog += "비동기 컴파일이 완료되지 않았다\n";
            return false;
        }

        const auto stats = manager.GetStats();
        if (stats.fallbackDraws == 0)
        {
            outLog += "폴백 카운터가 0이다 — 통계가 실제 동작을 반영하지 않는다\n";
            return false;
        }
        if (stats.skippedDraws != 0)
        {
            outLog += "폴백이 있는데 Skip이 발생했다\n";
            return false;
        }

        outLog += "비동기+폴백: 컴파일 중 폴백 " + std::to_string(stats.fallbackDraws)
            + "회 → 완료 후 요청 PSO로 전환 · 스킵 0\n";

        // ── 셰이더 리로드: 메모리 캐시가 비워지는가 ──
        manager.OnShaderReloaded();
        ID3D12PipelineState* afterReload = nullptr;
        // 리로드 직후에는 메모리 캐시가 비어 있으므로 다시 Pending이어야 한다.
        if (manager.Resolve(variants[1], &afterReload) == DX12PSOManager::DrawDecision::UseRequested)
        {
            outLog += "리로드 후에도 옛 캐시가 살아 있다\n";
            return false;
        }
        outLog += "셰이더 리로드: 메모리 캐시 비움 확인(디스크 라이브러리는 유지)\n";

        manager.Shutdown();
    }

    // ── 컴퓨트 PSO: 같은 캐시 2층을 타는가 ──
    {
        RHIShaderBlob csBlob;
        if (!CompileShader("CSMain", "cs_5_0", csBlob, outLog)) return false;

        RHIPipelineLayoutHandle computeRoot;
        {
            const RHIPipelineLayoutParam params[] = { RHILayout::UavTable(1, 0) };

            RHIPipelineLayoutDesc desc{};
            desc.params = params;

            computeRoot = rootSignatureCache.GetOrCreate(desc, error);
            if (!computeRoot.IsValid())
            {
                outLog += "컴퓨트 루트 시그니처 준비 실패: " + error + "\n";
                return false;
            }
        }

        RHIComputePipelineDesc computeDesc{};
        computeDesc.csBytecode = csBlob.Data();
        computeDesc.csSize = csBlob.Size();
        computeDesc.layout = computeRoot;

        const std::string computeCachePath = cacheFilePath + ".compute";
        std::remove(computeCachePath.c_str());
        const std::wstring computeWidePath(computeCachePath.begin(), computeCachePath.end());

        {
            DX12PSOManager manager;
            if (!manager.Initialize(&resources, computeWidePath, error))
            {
                outLog += "컴퓨트 1회차 초기화 실패: " + error + "\n";
                return false;
            }
            if (!manager.GetOrCreateCompute(computeDesc, error).IsValid())
            {
                outLog += "컴퓨트 PSO 생성 실패: " + error + "\n";
                return false;
            }
            if (manager.GetStats().compiles != 1)
            {
                outLog += "컴퓨트 1회차 컴파일이 1이 아니다\n";
                return false;
            }
            if (!manager.SaveCache(error))
            {
                outLog += "컴퓨트 캐시 저장 실패: " + error + "\n";
                return false;
            }
            manager.Shutdown();
        }

        {
            DX12PSOManager manager;
            if (!manager.Initialize(&resources, computeWidePath, error))
            {
                outLog += "컴퓨트 2회차 초기화 실패: " + error + "\n";
                return false;
            }
            if (!manager.GetOrCreateCompute(computeDesc, error).IsValid())
            {
                outLog += "컴퓨트 2회차 취득 실패: " + error + "\n";
                return false;
            }
            const auto stats = manager.GetStats();
            if (stats.compiles != 0 || stats.libraryHits != 1)
            {
                outLog += "컴퓨트 2회차 기대와 다름 — 컴파일 " + std::to_string(stats.compiles)
                    + "(기대 0) · 라이브러리 히트 " + std::to_string(stats.libraryHits) + "(기대 1)\n";
                return false;
            }
            manager.Shutdown();
        }

        outLog += "컴퓨트 PSO: 1회차 컴파일 1 → 2회차 컴파일 0 · 라이브러리 히트 1\n";
    }

    resources.Shutdown();
    outLog += "PSO 캐시 검증 통과\n";
    return true;
}

bool DX12Test::RunUploadSegmentTest(const std::string& modelPath, std::string& outLog)
{
    std::string retirementError;
    if (!ValidatePassResourceRetirement(retirementError))
    {
        outLog += "[common] pass resource retirement failed: " + retirementError + "\n";
        return false;
    }
    outLog += "[common] pass retirement acceptance, quarantine, and retry contracts passed\n";
    if (!ValidateCompletionRetireQueue())
    {
        outLog += "[공통] completion retire queue 경계 검증 실패\n";
        return false;
    }

    outLog += "[공통] completion retire queue 경계·quarantine 검증 통과\n";
    if (!RunRHIDeviceMemoryBudgetCoordinatorContractTest(outLog)) return false;
    if (!RunRHIAssetEvictionPolicyContractTest(outLog)) return false;
    if (!RunRHIPersistentHeapPolicyContractTest(outLog)) return false;

    DX12DeviceResources resources;
    std::string error;

    if (!resources.Initialize(64, 64, error))
    {
        outLog += "[1/7] 디바이스 초기화 실패: " + error + "\n";
        return false;
    }
    if (!RunDX12PersistentHeapSelfTest(resources.GetDevice(),
        resources.GetAdapter(), outLog))
    {
        resources.Shutdown();
        return false;
    }

    DX12UploadSegmentAllocator& ring = resources.GetUploadAllocator();
    const uint64_t bytesPerFrame = ring.GetBytesPerFrame();
    const uint32_t frameCount = ring.GetFrameCount();
    bool passed = true;
    RHIBufferHandle parallelHandleForTrim{};

    // ── [1/7] 정렬 ──
    //
    // 상수 버퍼 뷰는 256 정렬이 아니면 생성 자체가 실패하고, 텍스처 복사는
    // 512 정렬이 아니면 검증 레이어가 잡는다. 어긋난 크기를 일부러 섞어
    // 요청해도 반환 오프셋이 정렬돼 있어야 한다.
    {
        if (!resources.BeginFrame(error)) { outLog += "[1/7] Begin 실패: " + error + "\n"; return false; }

        const uint64_t sizes[] = { 1, 17, 100, 255, 257, 1000 };
        uint32_t misaligned = 0;
        for (const uint64_t size : sizes)
        {
            const auto cbv = ring.Allocate(size, DX12UploadSegmentAllocator::kConstantBufferAlignment);
            const auto tex = ring.Allocate(size, DX12UploadSegmentAllocator::kTexturePlacementAlignment);
            if (!cbv.IsValid() || !tex.IsValid()) { ++misaligned; continue; }
            if (0 != (cbv.offset % DX12UploadSegmentAllocator::kConstantBufferAlignment)) ++misaligned;
            if (0 != (tex.offset % DX12UploadSegmentAllocator::kTexturePlacementAlignment)) ++misaligned;

            // GPU 주소도 같은 정렬이어야 한다 — 오프셋만 맞고 기준 주소가
            // 어긋나면 상수 버퍼 뷰가 런타임에 거절된다.
            if (0 != (cbv.gpuAddress % DX12UploadSegmentAllocator::kConstantBufferAlignment)) ++misaligned;
        }

        if (!resources.EndFrame(error)) { outLog += "[1/7] End 실패: " + error + "\n"; return false; }

        if (0 != misaligned) { passed = false; }
        outLog += "[1/7] 정렬 " + std::string(0 == misaligned ? "통과" : "실패")
            + " (어긋남 " + std::to_string(misaligned) + "건)\n";
    }

    // ── [2/7] 제출 완료 전 재사용 금지 ──
    // 중간 제출로 첫 recording을 Pending으로 만든 뒤, 같은 프레임의 새
    // recording이 같은 세그먼트를 받지 않는지 본다. GPU가 빨리 끝나도
    // Collect 전이라 결과가 기계 속도에 흔들리지 않는다.
    {
        if (!resources.BeginFrame(error)) { outLog += "[2/7] Begin 실패\n"; return false; }
        const auto beforeSubmit = ring.Allocate(1024,
            DX12UploadSegmentAllocator::kConstantBufferAlignment);
        if (!resources.FlushCommandList(error))
        {
            outLog += "[2/7] Flush 실패: " + error + "\n";
            return false;
        }
        const auto afterSubmit = ring.Allocate(1024,
            DX12UploadSegmentAllocator::kConstantBufferAlignment);
        if (!resources.EndFrame(error)) { outLog += "[2/7] End 실패\n"; return false; }

        const bool isolated = beforeSubmit.IsValid() && afterSubmit.IsValid() &&
            beforeSubmit.segment != afterSubmit.segment &&
            beforeSubmit.resource != afterSubmit.resource;
        if (!isolated) passed = false;
        outLog += "[2/7] 제출 완료 전 재사용 금지 "
            + std::string(isolated ? "통과" : "실패") + "\n";
    }

    // ── [3/7] 되감기 ──
    //
    // BeginFrame이 커서를 되감지 않으면 몇 프레임 만에 구간이 차서 할당이
    // 거절되기 시작한다. 같은 크기를 여러 프레임 요청해 사용량이 누적되지
    // 않는지 본다.
    {
        uint64_t firstUsed = 0;
        uint64_t lastUsed = 0;
        for (uint32_t frame = 0; frame < frameCount * 2; ++frame)
        {
            if (!resources.BeginFrame(error)) { outLog += "[3/7] Begin 실패\n"; return false; }
            ring.Allocate(4096, DX12UploadSegmentAllocator::kConstantBufferAlignment);
            lastUsed = ring.GetFrameUsedBytes();
            if (0 == frame) firstUsed = lastUsed;
            if (!resources.EndFrame(error)) { outLog += "[3/7] End 실패\n"; return false; }
        }

        const bool rewound = (firstUsed == lastUsed) && (0 != firstUsed);
        if (!rewound) { passed = false; }
        outLog += "[3/7] 되감기 " + std::string(rewound ? "통과" : "실패")
            + " (첫 프레임 " + std::to_string(firstUsed)
            + "B · " + std::to_string(frameCount * 2) + "번째 " + std::to_string(lastUsed) + "B)\n";
    }

    // ── [4/7] 첫 대형 요청 즉시 성공 ──
    // 기본 세그먼트보다 큰 요청도 다음 프레임 성장을 기다리지 않는다.
    {
        if (!resources.BeginFrame(error)) { outLog += "[4/7] Begin 실패\n"; return false; }

        const auto tooBig = ring.Allocate(bytesPerFrame + 1, DX12UploadSegmentAllocator::kConstantBufferAlignment);
        const auto normal = ring.Allocate(256, DX12UploadSegmentAllocator::kConstantBufferAlignment);

        if (!resources.EndFrame(error)) { outLog += "[4/7] End 실패\n"; return false; }

        const auto stats = ring.GetStats();
        const bool immediate = tooBig.IsValid() && normal.IsValid() &&
            stats.largeSegments >= 1 && stats.oomFailures == 0;
        if (!immediate) { passed = false; }
        outLog += "[4/7] 첫 대형 요청 즉시 성공 "
            + std::string(immediate ? "통과" : "실패") + "\n";
    }

    // ── [5/7] 제출 전 Abort 즉시 반환 ──
    {
        if (!resources.BeginFrame(error)) { outLog += "[5/7] Begin 실패\n"; return false; }
        const RHIBufferSlice abandoned = resources.AllocateUpload(
            RHIUploadRequest{ 4096, RHIUploadUsage::BufferCopy, 4 });
        const RHIUploadStats duringAbort = ring.GetStats();
        resources.AbortFrame();
        const RHIUploadStats afterAbort = ring.GetStats();

        if (!resources.BeginFrame(error))
        {
            outLog += "[5/7] Abort 뒤 Begin 실패: " + error + "\n";
            return false;
        }
        const RHIBufferSlice recovered = resources.AllocateUpload(
            RHIUploadRequest{ 4096, RHIUploadUsage::BufferCopy, 4 });
        if (!resources.EndFrame(error)) { outLog += "[5/7] End 실패\n"; return false; }

        // 동일 크기의 Available 세그먼트가 여러 개면 다음 할당이 꼭 abandoned와
        // 같은 handle일 필요는 없다. Abort의 불변식은 제출되지 않은 Active가
        // 즉시 Available로 돌아오고 후속 할당이 성공하는 것이다.
        const bool returned = abandoned.IsWritable() && recovered.IsWritable()
            && duringAbort.activeSegments == afterAbort.activeSegments + 1
            && afterAbort.availableSegments >= duringAbort.availableSegments + 1;
        if (!returned) passed = false;
        outLog += "[5/7] 제출 전 Abort 즉시 반환 "
            + std::string(returned ? "통과" : "실패") + "\n";
    }

    // ── [6/7] 실제 GPU 도달 ──
    //
    // 위 넷은 전부 CPU 쪽 계산이다. 링을 거친 데이터가 정말 GPU 리소스에
    // 닿는지는 복사해서 되읽어야만 알 수 있다.
    {
        constexpr uint32_t kBytes = 1024;
        ComPtr<ID3D12Resource> destination;

        D3D12_HEAP_PROPERTIES defaultHeap{};
        defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_RESOURCE_DESC bufferDesc{};
        bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        bufferDesc.Width = kBytes;
        bufferDesc.Height = 1;
        bufferDesc.DepthOrArraySize = 1;
        bufferDesc.MipLevels = 1;
        bufferDesc.SampleDesc.Count = 1;
        bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        // 버퍼는 초기 상태를 지정해도 무시되고 COMMON으로 만들어진다(검증 레이어가
        // 경고로 알려 준다). 첫 사용 시 암묵 승격으로 COPY_DEST가 되므로 동작은
        // 같지만, 힌트를 사실과 맞춰 두어야 경고가 쌓이지 않는다.
        if (FAILED(resources.GetDevice()->CreateCommittedResource(&defaultHeap,
            D3D12_HEAP_FLAG_NONE, &bufferDesc, D3D12_RESOURCE_STATE_COMMON, nullptr,
            IID_PPV_ARGS(&destination))))
        {
            outLog += "[6/7] 대상 버퍼 생성 실패\n";
            return false;
        }

        DX12TestBufferRegistration destinationRegistration(resources, destination.Get());
        if (!destinationRegistration.IsValid())
        {
            outLog += "[6/7] 대상 버퍼 핸들 등록 실패\n";
            return false;
        }

        RHIReadback readback{};
        {
            std::string readbackError;
            if (!resources.CreateBufferReadback(kBytes, readback, readbackError))
            {
                outLog += "[6/7] 리드백 버퍼 생성 실패: " + readbackError + "\n";
                return false;
            }
        }

        if (!resources.BeginFrame(error)) { outLog += "[6/7] Begin 실패\n"; return false; }

        const auto staging = ring.Allocate(kBytes, DX12UploadSegmentAllocator::kConstantBufferAlignment);
        if (!staging.IsValid()) { outLog += "[6/7] 세그먼트 할당 실패\n"; return false; }

        auto* bytes = static_cast<uint8_t*>(staging.cpuAddress);
        for (uint32_t i = 0; i < kBytes; ++i)
        {
            bytes[i] = static_cast<uint8_t>((i * 7 + 13) & 0xFF);
        }

        resources.GetCommandList()->CopyBufferRegion(destination.Get(), 0,
            staging.resource, staging.offset, kBytes);

        D3D12_RESOURCE_BARRIER toSource{};
        toSource.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        toSource.Transition.pResource = destination.Get();
        toSource.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;  // 복사 대상으로 암묵 승격된 상태
        toSource.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        toSource.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        resources.GetCommandList()->ResourceBarrier(1, &toSource);

        resources.GetImmediateEncoder().CopyBufferToReadback(
            readback, destinationRegistration.Handle());

        if (!resources.EndFrame(error)) { outLog += "[6/7] End 실패\n"; return false; }
        resources.WaitForGpu();

        RHIReadbackImage captured{};
        {
            std::string readbackError;
            if (!resources.MapReadback(readback, captured, readbackError))
            {
                outLog += "[6/7] 리드백 Map 실패: " + readbackError + "\n";
                return false;
            }
        }

        const uint8_t* readBytes = captured.Elements<uint8_t>();
        uint32_t mismatches = 0;
        for (uint32_t i = 0; i < kBytes; ++i)
        {
            if (nullptr == readBytes ||
                readBytes[i] != static_cast<uint8_t>((i * 7 + 13) & 0xFF)) ++mismatches;
        }

        if (0 != mismatches) { passed = false; }
        outLog += "[6/7] GPU 도달 " + std::string(0 == mismatches ? "통과" : "실패")
            + " (" + std::to_string(kBytes) + "바이트 중 불일치 "
            + std::to_string(mismatches) + ")\n";
        destinationRegistration.Reset();
    }

    // ── [7/7] 병렬 CAS와 worker slow growth ──
    // 3개 standby(48MiB)를 넘는 64MiB를 worker들이 동시에 예약한다. 기존
    // 범위가 겹치지 않아야 하고, 네 번째 regular segment는 worker가 만든 뒤
    // stable buffer registry에서 lock-free로 resolve되어야 한다.
    {
        constexpr uint32_t kWorkerCount = 8;
        constexpr uint32_t kAllocationsPerWorker = 8;
        constexpr uint64_t kAllocationBytes = 1ull * 1024 * 1024;
        if (!resources.BeginFrame(error))
        {
            outLog += "[7/7] Begin 실패: " + error + "\n";
            return false;
        }

        const RHIUploadStats before = ring.GetStats();
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
                    slice = resources.AllocateUpload(RHIUploadRequest{
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
        uint32_t invalid = 0;
        for (const auto& perWorker : workerSlices)
        {
            for (const RHIBufferSlice& slice : perWorker)
            {
                if (!slice.IsWritable() || nullptr == resources.Resolve(slice.buffer))
                    ++invalid;
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

        const RHIUploadStats after = ring.GetStats();
        if (!sorted.empty()) parallelHandleForTrim = sorted.front().buffer;
        if (!resources.EndFrame(error))
        {
            outLog += "[7/7] End 실패: " + error + "\n";
            return false;
        }

        const bool parallelPassed = 0 == invalid && 0 == overlaps &&
            after.fastPathReservations > before.fastPathReservations &&
            after.workerSegmentCreates > before.workerSegmentCreates;
        if (!parallelPassed) passed = false;
        outLog += "[7/7] 병렬 CAS·worker growth "
            + std::string(parallelPassed ? "통과" : "실패")
            + " (무효 " + std::to_string(invalid)
            + " · 겹침 " + std::to_string(overlaps)
            + " · CAS 재시도 " + std::to_string(after.casRetries - before.casRetries)
            + " · worker 생성 "
            + std::to_string(after.workerSegmentCreates - before.workerSegmentCreates)
            + ")\n";
    }

    // ── budget trim과 stable registry slot 재사용 ──
    // GPU 완료 뒤 강제 pressure를 걸어 Available segment만 전부 trim한다.
    // 다음 생성은 high-water를 늘리지 않고 세대가 오른 free slot을 재사용해야 한다.
    {
        resources.WaitForGpu();
        const RHIUploadStats beforeTrim = ring.GetStats();
        ring.SetBudgetForTesting(bytesPerFrame, 0, true);

        if (!resources.BeginFrame(error))
        {
            ring.ClearBudgetOverrideForTesting();
            outLog += "[정책] budget trim Begin 실패: " + error + "\n";
            return false;
        }

        const RHIUploadStats afterCollect = ring.GetStats();
        const bool staleInvalid = nullptr == resources.Resolve(parallelHandleForTrim);

        const uint64_t cursorBeforeReject = ring.GetRecordingUsedBytes();
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
            cursorBeforeReject == ring.GetRecordingUsedBytes();
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
        const RHIUploadStats afterRecreate = ring.GetStats();

        if (!resources.EndFrame(error))
        {
            ring.ClearBudgetOverrideForTesting();
            outLog += "[정책] budget trim End 실패: " + error + "\n";
            return false;
        }
        ring.ClearBudgetOverrideForTesting();

        const bool trimPassed = afterCollect.trimmedSegments > beforeTrim.trimmedSegments &&
            afterCollect.segmentBytes < beforeTrim.segmentBytes && staleInvalid &&
            outputsUnchanged && recycled.IsWritable() &&
            afterRecreate.registrySlotReuses > beforeTrim.registrySlotReuses &&
            afterRecreate.registryHighWater == beforeTrim.registryHighWater &&
            afterRecreate.budgetPressureEvents > beforeTrim.budgetPressureEvents &&
            afterRecreate.budgetRetries > beforeTrim.budgetRetries;
        if (!trimPassed) passed = false;
        outLog += "[정책] pressure trim·slot 재사용 "
            + std::string(trimPassed ? "통과" : "실패")
            + " (trim " + std::to_string(afterCollect.trimmedSegments - beforeTrim.trimmedSegments)
            + " · 재사용 " + std::to_string(afterRecreate.registrySlotReuses - beforeTrim.registrySlotReuses)
            + " · high-water " + std::to_string(beforeTrim.registryHighWater)
            + "→" + std::to_string(afterRecreate.registryHighWater)
            + " · rollback " + std::string(outputsUnchanged ? "보존" : "손상") + ")\n";
    }

    // ── Slice D: 입력 모델 메시 캐시 persistent heap ──
    // 버퍼 두 개를 모두 실제 copy queue에 제출한 뒤, 완료점 직전에는
    // placed resource를 보존하고 정확히 도달한 뒤에만 block을 병합한다.
    // 공통 policy 자가 검증과 adapter 자가 검증이 놓치는 실제 cache 소유권,
    // external handle, upload transaction 연동을 한 번에 검증한다.
    {
        DX12MeshCache meshCache;
        if (!meshCache.Initialize(&resources, error))
        {
            passed = false;
            outLog += "[Slice D/DX12] mesh cache 초기화 실패: " + error + "\n";
        }
        else
        {
            const file::path scenePath = file::path(modelPath);
            // MBC9 — typed generation이 유일한 모델 지오메트리 출처다(Assimp·legacy Mesh 은퇴).
            const assets::ModelAssetGeneration::Shared sceneModel =
                DataSystems->LoadModelAssetGenerationByPath(scenePath.string());
            std::vector<RHIModelMeshView> sceneMeshes;
            uint64_t sceneUploadBytes = 0;
            if (sceneModel)
            {
                for (std::uint32_t i = 0; i < sceneModel->Meshes().size(); ++i)
                {
                    RHIModelMeshView view{};
                    if (!BuildRHIModelMeshView(*sceneModel, i, view) || !view.IsComplete()) continue;
                    sceneUploadBytes += view.vertexBytes
                        + static_cast<uint64_t>(view.indexCount) * sizeof(uint32);
                    sceneMeshes.push_back(view);
                }
            }

            bool scenePassed = !sceneMeshes.empty() && sceneUploadBytes > 16ull * 1024 * 1024;
            RHIMeshBinding firstBinding{};
            if (!scenePassed)
            {
                outLog += "[Slice D/DX12] 입력 모델 유효 mesh 누적이 16MiB를 넘지 않음"
                    " (mesh " + std::to_string(sceneMeshes.size()) + "개 · "
                    + std::to_string(sceneUploadBytes) + "B): " + scenePath.string() + "\n";
            }
            else if (!resources.BeginFrame(error))
            {
                scenePassed = false;
                outLog += "[Slice D/DX12] BeginFrame 실패: " + error + "\n";
            }
            else
            {
                meshCache.BeginFrame(1);
                for (const RHIModelMeshView& mesh : sceneMeshes)
                {
                    std::string uploadError;
                    const RHIMeshBinding binding = meshCache.GetOrUploadModel(mesh, uploadError);
                    if (!binding.vertices.IsValid() || !binding.indices.IsValid())
                    {
                        scenePassed = false;
                        outLog += "[Slice D/DX12] 입력 모델 mesh upload 실패: "
                            + uploadError + "\n";
                        break;
                    }
                    if (!firstBinding.vertices.IsValid()) firstBinding = binding;
                }

                if (!scenePassed)
                {
                    resources.AbortFrame();
                }
                else if (!resources.EndFrame(error))
                {
                    scenePassed = false;
                    outLog += "[Slice D/DX12] EndFrame 실패: " + error + "\n";
                }
            }

            if (scenePassed)
            {
                resources.WaitForGpu();
                const DX12MeshCache::Stats resident = meshCache.GetStats();
                const RHIPersistentHeapStats residentHeap = resident.persistentHeap;
                const uint64_t completion = resources.GetLastSignaledFenceValue();
                const uint32_t expectedAllocations =
                    static_cast<uint32_t>(sceneMeshes.size() * 2);
                const bool residentPassed = resident.uploads == sceneMeshes.size() &&
                    0 == resident.failures && resident.residentCount == sceneMeshes.size() &&
                    resident.residentBytes == sceneUploadBytes &&
                    residentHeap.activeSegments >= 1 &&
                    residentHeap.allocatedBytes >= sceneUploadBytes &&
                    residentHeap.livePooledAllocations > 0 &&
                    residentHeap.livePooledAllocations +
                        residentHeap.liveDedicatedAllocations == expectedAllocations &&
                    0 != completion;

                meshCache.BeginFrame(1 + DX12TextureCache::kPressureRetireAfterFrames);
                RHIAssetEvictionPass pressureEviction = BeginRHIAssetEvictionPass(
                    true, sceneUploadBytes);
                const uint64_t retiredBytes = meshCache.RetireUnused(completion,
                    &pressureEviction);
                const DX12MeshCache::Stats grave = meshCache.GetStats();
                const bool handleInvalidAtRetire =
                    nullptr == resources.Resolve(firstBinding.vertices.buffer) &&
                    nullptr == resources.Resolve(firstBinding.indices.buffer);
                const uint64_t prematureFreed = meshCache.SweepGraveyard(completion - 1);
                const DX12MeshCache::Stats beforeCompletion = meshCache.GetStats();
                const uint64_t completedFreed = meshCache.SweepGraveyard(completion);
                const DX12MeshCache::Stats released = meshCache.GetStats();
                const RHIPersistentHeapStats& releasedHeap = released.persistentHeap;

                const bool lifetimePassed = retiredBytes == sceneUploadBytes &&
                    handleInvalidAtRetire && 0 == prematureFreed &&
                    completedFreed == sceneUploadBytes &&
                    grave.graveyardCount == sceneMeshes.size() &&
                    grave.eviction.pressureRetired == sceneMeshes.size() &&
                    grave.eviction.pressureRetiredBytes == sceneUploadBytes &&
                    pressureEviction.pressureRetiredCount == sceneMeshes.size() &&
                    grave.persistentHeap.allocatedBytes == residentHeap.allocatedBytes &&
                    beforeCompletion.persistentHeap.allocatedBytes == residentHeap.allocatedBytes &&
                    0 == released.residentCount && 0 == released.graveyardCount &&
                    0 == releasedHeap.allocatedBytes &&
                    0 == releasedHeap.livePooledAllocations &&
                    0 == releasedHeap.liveDedicatedAllocations &&
                    releasedHeap.activeSegments <= 1 &&
                    releasedHeap.emptySegments == releasedHeap.activeSegments;
                scenePassed = residentPassed && lifetimePassed;

                outLog += "[Slice D/DX12] 입력 모델 persistent mesh "
                    + std::string(scenePassed ? "통과" : "실패")
                    + " (mesh " + std::to_string(sceneMeshes.size())
                    + "개 · 원본 " + std::to_string(sceneUploadBytes)
                    + "B · segment " + std::to_string(residentHeap.activeSegments)
                    + "장/" + std::to_string(residentHeap.segmentBytes)
                    + "B · 할당 " + std::to_string(residentHeap.allocatedBytes)
                    + "B · pooled/dedicated "
                    + std::to_string(residentHeap.livePooledAllocations) + "/"
                    + std::to_string(residentHeap.liveDedicatedAllocations)
                    + " · pressure 3f 퇴출 · 완료 전 보존 · 병합 후 standby "
                    + std::to_string(releasedHeap.activeSegments) + "장)\n";
            }

            if (!scenePassed) passed = false;
            meshCache.Shutdown();
        }
    }

    std::string messages;
    const uint32_t problems = resources.DrainDebugMessages(messages);
    if (0 != problems)
    {
        passed = false;
        outLog += "검증 레이어 문제 " + std::to_string(problems) + "건\n" + messages;
    }

    const auto stats = ring.GetStats();
    outLog += "업로드 세그먼트 통계: 할당 " + std::to_string(stats.allocations)
        + "건 · 누적 " + std::to_string(stats.bytesAllocated)
        + "B · 최대 프레임 사용 " + std::to_string(stats.peakFrameBytes)
        + "B / 구간 " + std::to_string(bytesPerFrame)
        + "B · fast/slow " + std::to_string(stats.fastPathReservations)
        + "/" + std::to_string(stats.slowPathReservations)
        + " · CAS 재시도 " + std::to_string(stats.casRetries)
        + " · worker 생성 " + std::to_string(stats.workerSegmentCreates)
        + " · trim " + std::to_string(stats.trimmedSegments)
        + " · slot 재사용 " + std::to_string(stats.registrySlotReuses) + "\n";

    resources.Shutdown();

    outLog += passed ? "업로드 세그먼트 검증 통과\n" : "업로드 세그먼트 검증 실패\n";
    return passed;
}

bool DX12Test::RunDescriptorHeapTest(std::string& outLog)
{
    // ── [1/6] 공통 version 상태 계약 ──
    // completion 직전에는 회수하지 않고, 정확히 도달한 뒤 generation을 올려
    // 재사용한다. Abort는 즉시 반환하고 completion 0은 quarantine한다.
    {
        RHIDescriptorVersionPolicy policy;
        policy.Reset(1);
        const auto first = policy.BeginRecording(1);
        const bool firstSubmitted = first.IsValid() &&
            policy.OnSubmitted(1, RHICompletionPoint{ 7 });
        const auto beforeCompletion = policy.BeginRecording(2);
        const bool isolated = beforeCompletion.IsValid() &&
            beforeCompletion.handle.slot != first.handle.slot &&
            0 == policy.Collect(RHICompletionPoint{ 6 });
        const bool aborted = policy.AbortRecording(2);
        const auto afterAbort = policy.BeginRecording(3);
        const bool abortReused = afterAbort.IsValid() &&
            afterAbort.handle.slot == beforeCompletion.handle.slot &&
            afterAbort.handle.generation != beforeCompletion.handle.generation;
        policy.AbortRecording(3);
        const bool collected = 1 == policy.Collect(RHICompletionPoint{ 7 });
        const auto afterCompletion = policy.BeginRecording(4);
        const bool completionReused = afterCompletion.IsValid() &&
            afterCompletion.handle.slot == first.handle.slot &&
            afterCompletion.handle.generation != first.handle.generation;
        const bool quarantined = policy.OnSubmitted(4, RHICompletionPoint{}) &&
            0 == policy.Collect(RHICompletionPoint{ UINT64_MAX }) &&
            1 == policy.GetStats().quarantined;

        if (!firstSubmitted || !isolated || !aborted || !abortReused ||
            !collected || !completionReused || !quarantined)
        {
            outLog += "[1/6] 공통 descriptor version 상태 계약 실패\n";
            return false;
        }
        outLog += "[1/6] 공통 completion/Abort/quarantine/generation 계약 통과\n";
    }

    DX12DeviceResources resources;
    std::string error;

    if (!resources.Initialize(64, 64, error))
    {
        outLog += "[2/6] 디바이스 초기화 실패: " + error + "\n";
        return false;
    }

    DX12DescriptorRecycler& recycler = resources.GetDescriptorRecycler();
    const uint32_t perPage = recycler.GetDescriptorsPerPage();
    bool passed = true;

    // ── [2/6] page 안의 핸들 연속성 ──
    //
    // 디스크립터 테이블은 연속이어야 한다. 구간 안의 i번째 핸들이 increment 크기
    // 간격으로 정확히 떨어지지 않으면, 테이블 두 번째 원소부터 엉뚱한 리소스를
    // 가리키게 된다 — 화면에는 '텍스처 하나만 틀리게' 나와서 원인을 찾기 어렵다.
    {
        if (!resources.BeginFrame(error)) { outLog += "[2/6] Begin 실패\n"; return false; }

        const auto range = recycler.Allocate(4);
        uint32_t broken = 0;
        if (!range.IsValid() || 4 != range.count || 0 == range.incrementSize)
        {
            ++broken;
        }
        else
        {
            for (uint32_t i = 0; i < range.count; ++i)
            {
                const auto handle = range.CpuAt(i);
                if (handle.ptr != range.cpu.ptr + static_cast<SIZE_T>(i) * range.incrementSize)
                {
                    ++broken;
                }
            }

            // 같은 recording으로 복구 진입해도 cursor를 되감지 않아야 한다.
            // 다음 할당은 앞 구간 바로 뒤에 붙어야 한다(겹치지도, 비지도 않게).
            if (!recycler.BeginRecording(
                resources.GetCurrentUploadRecordingId(), error))
            {
                ++broken;
            }
            const auto next = recycler.Allocate(1);
            if (!next.IsValid() ||
                next.cpu.ptr != range.cpu.ptr + static_cast<SIZE_T>(4) * range.incrementSize ||
                next.gpu.ptr != range.gpu.ptr + static_cast<UINT64>(4) * range.incrementSize ||
                next.version != range.version ||
                !recycler.IsCurrentVersion(range.version))
            {
                ++broken;
            }
        }

        if (!resources.EndFrame(error)) { outLog += "[2/6] End 실패\n"; return false; }

        if (0 != broken) { passed = false; }
        outLog += "[2/6] page 핸들 연속성 " + std::string(0 == broken ? "통과" : "실패")
            + " (어긋남 " + std::to_string(broken) + "건)\n";
    }

    // ── [3/6] 중간 제출 뒤 새 page/version ──
    {
        if (!resources.BeginFrame(error))
        {
            outLog += "[3/6] Begin 실패\n";
            return false;
        }
        const auto beforeSubmit = recycler.Allocate(1);
        ID3D12DescriptorHeap* const beforeHeap = recycler.GetHeap();
        if (!resources.FlushCommandList(error))
        {
            outLog += "[3/6] Flush 실패: " + error + "\n";
            return false;
        }
        const auto afterSubmit = recycler.Allocate(1);
        ID3D12DescriptorHeap* const afterHeap = recycler.GetHeap();
        const bool isolated = beforeSubmit.IsValid() && afterSubmit.IsValid() &&
            nullptr != beforeHeap && nullptr != afterHeap && beforeHeap != afterHeap &&
            beforeSubmit.version != afterSubmit.version &&
            !recycler.IsCurrentVersion(beforeSubmit.version) &&
            recycler.IsCurrentVersion(afterSubmit.version);
        if (!resources.EndFrame(error)) { outLog += "[3/6] End 실패\n"; return false; }

        if (!isolated) passed = false;
        outLog += "[3/6] 중간 제출 page/version 격리 "
            + std::string(isolated ? "통과" : "실패") + "\n";
    }

    // ── [4/6] 완료 뒤 generation 재사용 ──
    {
        resources.WaitForGpu();
        const uint64_t reusesBefore = recycler.GetVersionStats().reuses;
        if (!resources.BeginFrame(error))
        {
            outLog += "[4/6] Begin 실패\n";
            return false;
        }
        const auto reused = recycler.Allocate(16);
        const bool generationReused = reused.IsValid() &&
            recycler.GetVersionStats().reuses > reusesBefore &&
            16 == recycler.GetRecordingUsed();
        if (!resources.EndFrame(error)) { outLog += "[4/6] End 실패\n"; return false; }

        if (!generationReused) passed = false;
        outLog += "[4/6] completion 뒤 generation 재사용 "
            + std::string(generationReused ? "통과" : "실패") + "\n";
    }

    // ── [5/6] page 넘침 거절 ──
    {
        if (!resources.BeginFrame(error)) { outLog += "[5/6] Begin 실패\n"; return false; }

        const uint64_t before = recycler.GetStats().overflows;
        const auto tooMany = recycler.Allocate(perPage + 1);
        const uint64_t after = recycler.GetStats().overflows;
        const auto normal = recycler.Allocate(1);

        if (!resources.EndFrame(error)) { outLog += "[5/6] End 실패\n"; return false; }

        const bool rejected = !tooMany.IsValid() && (after == before + 1) && normal.IsValid();
        if (!rejected) { passed = false; }
        outLog += "[5/6] page 넘침 거절 " + std::string(rejected ? "통과" : "실패") + "\n";
    }

    // ── [6/6] 샘플러 중복 제거 ──
    //
    // 샘플러 힙은 상한이 2048로 작다. 같은 설정을 머티리얼마다 새로 만들면
    // 큰 씬에서 상한에 먼저 부딪히고, 그때 증상은 '어느 순간부터 샘플러가
    // 안 만들어진다'라 원인이 멀다.
    {
        DX12SamplerHeap& samplers = resources.GetSamplerHeap();

        const RHISamplerDesc linear = RHISampler::Linear(RHIAddressMode::Wrap);
        const RHISamplerDesc point = RHISampler::Point(RHIAddressMode::Wrap);

        const auto a = samplers.GetOrCreate(linear);
        const auto again = samplers.GetOrCreate(linear);   // 같은 설정 → 같은 핸들
        const auto b = samplers.GetOrCreate(point);        // 다른 설정 → 다른 핸들

        const bool deduped = (0 != a.ptr) && (a.ptr == again.ptr);
        const bool separated = (0 != b.ptr) && (b.ptr != a.ptr);

        if (!deduped || !separated) { passed = false; }

        const auto samplerStats = samplers.GetStats();
        outLog += "[6/6] 샘플러 중복 제거 "
            + std::string((deduped && separated) ? "통과" : "실패")
            + " (생성 " + std::to_string(samplerStats.creates)
            + " · 히트 " + std::to_string(samplerStats.hits)
            + " · 보관 " + std::to_string(samplers.GetCachedCount()) + ")\n";
    }

    std::string messages;
    const uint32_t problems = resources.DrainDebugMessages(messages);
    if (0 != problems)
    {
        passed = false;
        outLog += "검증 레이어 문제 " + std::to_string(problems) + "건\n" + messages;
    }

    const auto recyclerStats = recycler.GetStats();
    outLog += "descriptor recycler 통계: 할당 "
        + std::to_string(recyclerStats.allocations)
        + "건 · 디스크립터 " + std::to_string(recyclerStats.descriptors)
        + "개 · 최대 recording 사용 "
        + std::to_string(recyclerStats.peakRecordingDescriptors)
        + " / page " + std::to_string(perPage)
        + " · version 생성/재사용 "
        + std::to_string(recyclerStats.versions.creates) + "/"
        + std::to_string(recyclerStats.versions.reuses)
        + " · pending/quarantine "
        + std::to_string(recyclerStats.versions.pending) + "/"
        + std::to_string(recyclerStats.versions.quarantined) + "\n";

    resources.Shutdown();

    outLog += passed ? "디스크립터 힙 검증 통과\n" : "디스크립터 힙 검증 실패\n";
    return passed;
}

// ★ RunSharedTextureTest를 은퇴시켰다 (D4, 2026-08-09).
//
//   DX12가 만든 공유 텍스처를 DX11에서 열어 SRV로 표시하는 "병존 출력
//   경로"를 실증하던 검사다. 그 경로가 D4 관문에서 사라졌다 - 씬 뷰는
//   이제 셸(DX12)이 공유 핸들로 직결하고, DX11은 그 사이에 없다.
//
//   재는 대상이 없어진 검사를 남기면 "통과"가 아무것도 뜻하지 않는다
//   (T6에서 리사이즈 [1/3]을 은퇴시킨 것과 같은 판단).

static bool ValidateBase0CameraReplay(std::string& error)
{
    EnhancedCameraReplayInput source;
    source.width = 332; source.height = 202; source.target = 0; source.viewFlags = 7;
    source.totalSeconds = 17.25f; source.deltaSeconds = 1.f / 60.f;
    source.camera.view = math::matrix4x4::identity();
    source.camera.projection = math::matrix4x4::identity();
    source.camera.inverseView = math::matrix4x4::identity();
    source.camera.inverseProjection = math::matrix4x4::identity();
    source.camera.eyePosition = {1.25f, -2.f, 3.f};
    source.camera.forward = {0.f, 0.f, 1.f};
    source.camera.right = {1.f, 0.f, 0.f}; source.camera.up = {0.f, 1.f, 0.f};
    source.camera.fov = 60.f; source.camera.nearPlane = 0.1f; source.camera.farPlane = 1000.f;
    const auto bytes = source.Encode();
    EnhancedCameraReplayInput accepted;
    if (!EnhancedCameraReplayInput::Decode(bytes, accepted, error) || accepted.Encode() != bytes)
    { error = "camera replay lost input bits"; return false; }
    const auto reject = [&](std::vector<uint8_t> bad, bool reseal) {
        if (reseal)
        {
            const auto hash = EnhancedCameraReplayInput::Checksum(std::span(bad).first(bad.size() - 8));
            for (unsigned i = 0; i < 8; ++i) bad[bad.size() - 8 + i] = static_cast<uint8_t>(hash >> (8 * i));
        }
        std::string why;
        return !EnhancedCameraReplayInput::Decode(bad, accepted, why)
            && !why.empty() && accepted.Encode() == bytes;
    };
    auto bad = bytes; bad.pop_back();
    if (!reject(bad, false)) { error = "truncated input accepted"; return false; }
    bad = bytes; bad.push_back(0);
    if (!reject(bad, false)) { error = "trailing input accepted"; return false; }
    bad = bytes; bad[0] ^= 1;
    if (!reject(bad, true)) { error = "bad magic accepted"; return false; }
    bad = bytes; bad[80] ^= 1;
    if (!reject(bad, false)) { error = "bad checksum accepted"; return false; }
    bad = bytes; bad[8] = 2;
    if (!reject(bad, true)) { error = "future version accepted"; return false; }
    bad = bytes; bad[12] = bad[13] = bad[14] = bad[15] = 0;
    if (!reject(bad, true)) { error = "zero extent accepted"; return false; }
    bad = bytes; bad[20] = 2;
    if (!reject(bad, true)) { error = "preview target accepted"; return false; }
    bad = bytes; bad[24] = 16;
    if (!reject(bad, true)) { error = "unknown flags accepted"; return false; }
    bad = bytes; bad[28] = 2;
    if (!reject(bad, true)) { error = "invalid boolean accepted"; return false; }
    bad = bytes; bad[44] = 0; bad[45] = 0; bad[46] = 192; bad[47] = 127;
    if (!reject(bad, true)) { error = "NaN camera accepted"; return false; }
    source.camera.nearPlane = source.camera.farPlane;
    if (!reject(source.Encode(), false)) { error = "invalid clip range accepted"; return false; }
    error.clear();
    return true;
}

static bool ValidateBase0DrawReplay(std::string& error)
{
    std::array<float, assets::StrideOf(assets::kCoreVertexAttributes) / sizeof(float)> vertex{};
    std::array<uint32_t, 3> indices{0,0,0};
    auto bone=math::matrix4x4::identity();
    EnhancedDrawItem item;
    assets::TryParseCanonicalUuidV8("11111111-1111-8111-8111-111111111111", item.modelMeshView.handle.modelId);
    assets::TryParseCanonicalUuidV8("22222222-2222-8222-8222-222222222222", item.modelMeshView.handle.meshId);
    item.modelMeshView.handle.generation=1;
    item.modelMeshView.vertexData=vertex.data(); item.modelMeshView.vertexBytes=sizeof(vertex);
    item.modelMeshView.vertexStride=sizeof(vertex);
    item.modelMeshView.vertexAttributeMask=assets::kCoreVertexAttributes;
    item.modelMeshView.vertexLayoutHash=assets::VertexLayoutHash(assets::kCoreVertexAttributes);
    item.modelMeshView.indexData=indices.data(); item.modelMeshView.indexCount=3;
    item.worldMatrix=math::matrix4x4::identity(); item.bonePalette=&bone; item.boneCount=1;
    std::array<EnhancedDrawItem,1> opaque{item},forward{item},graph{item};
    EnhancedDrawReplayInput sealed;
    if (!EnhancedDrawReplayInput::Seal(opaque,forward,graph,sealed,error)) return false;
    bone=math::matrix4x4{}; // Producer storage can change after sealing.
    const auto bytes=sealed.Encode();
    EnhancedDrawReplayInput accepted;
    if (!EnhancedDrawReplayInput::Decode(bytes,accepted,error) || accepted.Encode()!=bytes)
    { error="draw replay roundtrip lost bits"; return false; }
    const auto reject=[&](std::vector<uint8_t> bad, bool reseal) {
        if (reseal)
        {
            const auto hash=EnhancedCameraReplayInput::Checksum(std::span(bad).first(bad.size()-8));
            for(unsigned i=0;i<8;++i) bad[bad.size()-8+i]=static_cast<uint8_t>(hash>>(8*i));
        }
        std::string why;
        return !EnhancedDrawReplayInput::Decode(bad,accepted,why) && !why.empty() && accepted.Encode()==bytes;
    };
    auto bad=bytes; bad.pop_back(); if(!reject(bad,false)) return false;
    bad=bytes; bad.push_back(0); if(!reject(bad,false)) return false;
    bad=bytes; bad[0]^=1; if(!reject(bad,true)) return false;
    bad=bytes; bad[70]^=1; if(!reject(bad,false)) return false;
    bad=bytes; bad[8]=2; if(!reject(bad,true)) return false;
    bad=bytes; bad[12]=bad[13]=bad[14]=bad[15]=255; if(!reject(bad,true)) return false;
    bad=bytes; bad[16]=3; if(!reject(bad,true)) return false;
    bad=bytes; bad[60]=bad[61]=bad[62]=bad[63]=255; if(!reject(bad,true)) return false;
    bad=bytes; bad[64]=0;bad[65]=0;bad[66]=192;bad[67]=127; if(!reject(bad,true)) return false;
    bad=bytes; bad[128]=0;bad[129]=0;bad[130]=128;bad[131]=127; if(!reject(bad,true)) return false;
    // Late closure mismatch must not partially replace the first draw or pose.
    accepted.draws.back().assetIds[0]^=1;
    if (accepted.Apply(opaque,forward,graph,error) || opaque[0].bonePalette!=&bone)
    {
        return false;
    }
    accepted=sealed;
    vertex[0]=1.f;
    if (accepted.Apply(opaque,forward,graph,error) || opaque[0].bonePalette!=&bone)
    {
        return false;
    }
    vertex[0]=0.f;
    opaque[0].boneCount=0;
    if (accepted.Apply(opaque,forward,graph,error))
    {
        return false;
    }
    opaque[0].boneCount=1;
    if(!accepted.Apply(opaque,forward,graph,error)
        || opaque[0].bonePalette==&bone || graph[0].bonePalette!=accepted.draws[2].bones.data()
        || std::memcmp(opaque[0].bonePalette,&accepted.draws[0].bones[0],sizeof(bone))!=0
        || opaque[0].animatorKey==forward[0].animatorKey)
    {
        return false;
    }
    error.clear(); return true;
}

static bool ValidateBase0LatticeReplay(std::string& error)
{
    std::array<float, assets::StrideOf(assets::kCoreVertexAttributes) / sizeof(float)> vertex{}; std::array<uint32_t,3> indices{0,0,0};
    EnhancedDrawItem item;
    assets::TryParseCanonicalUuidV8("11111111-1111-8111-8111-111111111111",item.modelMeshView.handle.modelId);
    assets::TryParseCanonicalUuidV8("22222222-2222-8222-8222-222222222222",item.modelMeshView.handle.meshId);
    item.modelMeshView.handle.generation=1;
    item.modelMeshView.vertexData=vertex.data(); item.modelMeshView.vertexBytes=sizeof(vertex); item.modelMeshView.vertexStride=sizeof(vertex);
    item.modelMeshView.vertexAttributeMask=assets::kCoreVertexAttributes;
    item.modelMeshView.vertexLayoutHash=assets::VertexLayoutHash(assets::kCoreVertexAttributes);
    item.modelMeshView.indexData=indices.data(); item.modelMeshView.indexCount=3; item.materialGraphSlot=7;
    material_graph::Generation generationValue;
    experiment::TryParseCanonicalAssetId("11111111-1111-4111-8111-111111111111", generationValue.assetId);
    generationValue.generation = 1;
    {
        auto& product = generationValue.cooked.product;
        product.program.semanticKey = "base0-native-lattice-fixture";
        const auto parameter = [&](LX::Id id, LX::PinType type, LX::LXSocketValue value,
                                   uint32_t offset, uint32_t bytes) {
            LX::LXMaterialParameter p;
            p.id = id;
            p.type = type;
            p.value = std::move(value);
            p.identifier = "param" + std::to_string(id);
            p.exposed = true;
            product.program.parameters.push_back(p);
            product.layout.parameters.push_back({p, offset, bytes});
        };
        parameter(900, LX::PinType::Float, 0.5, 0, 4);
        parameter(902, LX::PinType::Bool, true, 4, 4);
        parameter(903, LX::PinType::Int, int64_t(7), 8, 4);
        parameter(904, LX::PinType::Vector, std::array<double, 3>{1, 2, 3}, 12, 12);
        parameter(905, LX::PinType::Color, std::array<double, 4>{1, .5, .25, 1}, 24, 16);
        product.layout.uniformBytes = 40;
        experiment::AssetId textureId;
        experiment::TryParseCanonicalAssetId("22222222-2222-4222-8222-222222222222", textureId);
        LX::LXMaterialResource resource;
        resource.slot = 0;
        resource.reference = Uuid::ToString(textureId.value);
        resource.colorSpace = LX::LXColorSpace::Data;
        product.layout.textures.push_back(resource);
    }
    const auto generation = own::make_shared<const material_graph::Generation>(std::move(generationValue));
    const uint32_t pixel=0xff4080c0;
    auto texture=Texture::CreateFromPixels(1,1,"base0-lattice-owner",RHIFormat::RGBA8Unorm,&pixel);
    material_graph::InstanceDescription description; description.graphId=generation->assetId;
    own::shared_owner<const material_graph::Instance> source;
    if(!texture || !material_graph::BuildInstance(generation,description,[&](const auto&,auto,std::string&){return texture;},source,error)) return false;
    item.materialGraphInstance=source;
    std::array<EnhancedDrawItem,2> graph{item,item};
    EnhancedLatticeReplayInput sealed;
    if(!EnhancedLatticeReplayInput::Seal(graph,sealed,error)) return false;
    auto bytes=sealed.Encode(); EnhancedLatticeReplayInput accepted;
    if(!EnhancedLatticeReplayInput::Decode(bytes,accepted,error) || accepted.Encode()!=bytes) return false;
    const auto reject=[&](std::vector<uint8_t> bad,bool reseal) {
        if(reseal) {
            const auto hash=EnhancedCameraReplayInput::Checksum(std::span(bad).first(bad.size()-8));
            for(unsigned i=0;i<8;++i) bad[bad.size()-8+i]=static_cast<uint8_t>(hash>>(8*i));
        }
        std::string why;
        return !EnhancedLatticeReplayInput::Decode(bad,accepted,why) && !why.empty() && accepted.Encode()==bytes;
    };
    auto bad=bytes; bad.pop_back(); if(!reject(bad,false)) return false;
    bad=bytes; bad.push_back(0); if(!reject(bad,false)) return false;
    bad=bytes; bad[0]^=1; if(!reject(bad,true)) return false;
    bad=bytes; bad[70]^=1; if(!reject(bad,false)) return false;
    bad=bytes; bad[8]=2; if(!reject(bad,true)) return false;
    bad=bytes; bad[12]=bad[13]=bad[14]=bad[15]=255; if(!reject(bad,true)) return false;
    bad=bytes; bad[72]=6; if(!reject(bad,true)) return false;
    bad=bytes; bad[84]=bad[85]=bad[86]=bad[87]=255; if(!reject(bad,true)) return false;
    bad=bytes; bad[96]=99; if(!reject(bad,true)) return false;
    bad=bytes; bad[106]=248; bad[107]=127; if(!reject(bad,true)) return false;
    bad=bytes; bad[96]=0; if(!reject(bad,true)) return false;
    bad=bytes; bad[96]=1; std::fill_n(bad.begin()+100,8,255); bad[107]=127; if(!reject(bad,true)) return false;
    bad=bytes; std::fill_n(bad.begin()+48,16,0); if(!reject(bad,true)) return false;
    const auto rejectApply=[&](EnhancedLatticeReplayInput badInput) {
        std::string why;
        return !badInput.Apply(graph,why) && !why.empty() && material_graph_test::SamePinnedObject(graph[0].materialGraphInstance, source) && material_graph_test::SamePinnedObject(graph[1].materialGraphInstance, source);
    };
    auto mutation=sealed; mutation.draws[0].program^=1; if(!rejectApply(mutation)) return false;
    mutation=sealed; mutation.draws[0].textures[0].content^=1; if(!rejectApply(mutation)) return false;
    mutation=sealed; mutation.draws[0].description.parameters[0].id=999; if(!rejectApply(mutation)) return false;
    mutation=sealed; mutation.draws[0].description.parameters[0].value=true; if(!rejectApply(mutation)) return false;
    mutation=sealed; mutation.draws[0].description.parameters[0].value=.25; if(!rejectApply(mutation)) return false; // shared slot conflict
    mutation=sealed; mutation.draws[0].description.parameters[0].value=.25;
    mutation.draws[1].description.graphId.value.data[0]^=1; if(!rejectApply(mutation)) return false; // late rejection is atomic
    for(auto& d:accepted.draws) d.description.parameters[0].value=.25;
    if(!accepted.Apply(graph,error) || material_graph_test::SamePinnedObject(graph[0].materialGraphInstance, source)
        || !material_graph_test::SamePinnedObject(graph[0].materialGraphInstance, graph[1].materialGraphInstance)
        || !material_graph_test::SamePinnedObject(graph[0].materialGraphInstance->generation, generation)
        || !graph[0].materialGraphInstance->textures[0].owner
        || graph[0].materialGraphInstance->textures[0].owner->m_assetId != texture->m_assetId)
    {
        return false;
    }
    float replayed{}; std::memcpy(&replayed,graph[0].materialGraphInstance->uniforms.data(),sizeof(replayed));
    if(replayed!=.25f || source->uniforms==graph[0].materialGraphInstance->uniforms) return false;
    error.clear(); return true;
}

#include "EnhancedTransientRg7Tests.h"

bool DX12Test::RunRenderGraphTest(std::string& outLog, bool replayExtensions)
{
    char* softwareProbe{};
    size_t softwareProbeLength{};
    const bool softwareProbeEnabled = _dupenv_s(&softwareProbe, &softwareProbeLength, "CREATOR_SOFTWARE_RHI_PROBE") == 0 &&
        softwareProbe && std::string_view{softwareProbe} == "1";
    std::free(softwareProbe);
    if (softwareProbeEnabled)
    {
        std::string softwareError;
        if (!ValidateSoftwareNativeCommands(softwareError, outLog))
        {
            outLog += "RHI_SOFTWARE_NATIVE_FAILED " + softwareError + "\n";
            return false;
        }
    }
    std::string rg1Error;
    if(!ValidateRg1GraphFixtures(rg1Error)) { outLog += "RG1_DAG_FAILED " + rg1Error + "\n"; return false; }
    outLog += "RG1_DAG_OK shuffles=24 stable RAW UAV-read cycle missing multiwriter Modify invalid duplicate access-state\n";
    if(!ValidateRg2GraphFixtures(rg1Error)) { outLog += "RG2_DAG_FAILED " + rg1Error + "\n"; return false; }
    outLog += "RG2_DAG_OK shuffles=240 texture buffer RAW WAR WAW Modify fork stale kind missing import cycle\n";
    if(!ValidateRg3GraphFixtures(rg1Error)) { outLog += "RG3_PLAN_FAILED " + rg1Error + "\n"; return false; }
    outLog += "RG3_PLAN_OK shuffles=120 dead-version WAR-bridge sorted-lifetime transitions UAV-read-write import-final Modify stale\n";
    std::string rg4Dump;
    if(!ValidateRg4GraphFixtures(rg1Error,rg4Dump)) { outLog += "RG4_WAVES_FAILED "+rg1Error+"\n"; return false; }
    outLog += "RG4_WAVES_OK shuffles=24 critical-path culled\n"+rg4Dump;
    outLog += "RGV_READER_OK shuffles=24 missing-edge wrong-order stale-scene stale-history stale-resize wrong-view immutable-reset\n";
    if (!ValidateRg5ProducerDeclarations(rg1Error))
    { outLog += "RG5_PRODUCERS_FAILED " + rg1Error + "\n"; return false; }
    outLog += "RG5_PRODUCERS_OK passes=GBuffer/Shadow modes=3 orders=2 outputs=7 RAW=7\n";
    if (replayExtensions)
    {
        std::string latticeError;
        if(!ValidateBase0LatticeReplay(latticeError))
        { outLog += "BASE0_LATTICE_REPLAY_FAILED " + latticeError + "\n"; return false; }
        outLog += "BASE0_LATTICE_REPLAY_OK roundtrip=1 rejection=13 closure=6 atomic=1 types=5 shared-slot=1 exact-owners=1\n";
        std::string drawError;
        if (!ValidateBase0DrawReplay(drawError))
        { outLog += "BASE0_DRAW_REPLAY_FAILED " + drawError + "\n"; return false; }
        outLog += "BASE0_DRAW_REPLAY_OK roundtrip=1 rejection=10 closure=3 atomic=1 owned-pose=1 routes=3\n";
        std::string replayError;
        if (!ValidateBase0CameraReplay(replayError))
        { outLog += "BASE0_CAMERA_REPLAY_FAILED " + replayError + "\n"; return false; }
        outLog += "BASE0_CAMERA_REPLAY_OK roundtrip=1 rejection=11 atomic=1\n";
    }
    std::string baselineError;
    if (!ValidateBase0GraphFixtures(baselineError))
    { outLog += "BASE0_GRAPH_FIXTURES_FAILED " + baselineError + "\n"; return false; }
    outLog += "BASE0_GRAPH_FIXTURES_OK RAW independent culling history missing reversed multiwriter-modify stale\n";

    std::string neutralReadbackError;
    const bool neutralReadbackPassed =
        ValidateR6bNeutralReadbackGraph(neutralReadbackError);
    outLog += "[R6-b] 가짜 backend handle readback "
        + std::string(neutralReadbackPassed ? "통과" : "실패")
        + (neutralReadbackPassed ? " (texture 3 · buffer 1 · transition 2 · release 2)\n"
            : (": " + neutralReadbackError + "\n"));
    if (!neutralReadbackPassed) return false;

    DX12DeviceResources resources;
    std::string error;
    if (!resources.Initialize(64, 64, error))
    {
        outLog += "[1/7] 디바이스 초기화 실패: " + error + "\n";
        return false;
    }

    if (RenderContainerReuseEnabled())
    {
        material_graph::IndirectMaterialBatchPool batches;
        auto first = batches.Acquire();
        auto& bucket = first->AppendBucket();
        bucket.roots.assign({1u, 3u, 5u});
        const auto* roots = bucket.roots.data();
        first->packetWork = own::make_unique<material_graph::PacketDispatchWork>();
        first->packetWork->levels.resize(3);
        first->packetWork->slices.resize(4);
        auto* packet = first->packetWork.operator->();
        const auto* levels = packet->levels.data();
        const auto* slices = packet->slices.data();
        const auto* identity = first.operator->();
        auto pending = first;
        first.reset();
        auto second = batches.Acquire();
        if (second.operator->() == identity || pending->buckets.front().roots.size() != 3)
        {
            outLog += "MATERIAL_BATCH_REUSE_FAILED pending owner was recycled\n";
            return false;
        }
        pending.reset();
        auto reused = batches.Acquire();
        auto& reusedBucket = reused->AppendBucket();
        if (reused.operator->() != identity || reusedBucket.roots.data() != roots || !reusedBucket.roots.empty() ||
            reused->packetWork.operator->() != packet || packet->levels.data() != levels || packet->slices.data() != slices ||
            !packet->levels.empty() || !packet->slices.empty() || reused->PacketMode())
        {
            outLog += "MATERIAL_BATCH_REUSE_FAILED nested capacity or mode was lost\n";
            return false;
        }
        outLog += "MATERIAL_BATCH_REUSE_OK pending-owner roots packet scan slices reset\n";
    }
    else
    {
        material_graph::IndirectMaterialBatchPool batches;
        auto previous = batches.Acquire();
        const auto weak = own::weak_owner<material_graph::IndirectMaterialBatch>(previous);
        previous.reset();
        if (!weak.expired())
        {
            outLog += "MATERIAL_BATCH_FRESH_FAILED pool retained the control object\n";
            return false;
        }
        outLog += "MATERIAL_BATCH_FRESH_OK object-retention=off\n";
    }
    outLog +=
        RenderContainerReuseEnabled() ? "RENDER_CONTAINER_REUSE_POLICY=ON\n" : "RENDER_CONTAINER_REUSE_POLICY=OFF\n";

    if (!ValidateRg5ConsumerDeclarations(resources, error))
    {
        outLog += "RG5_CONSUMERS_FAILED " + error + "\n";
        return false;
    }
    outLog += "RG5_CONSUMERS_OK LX-HDR/SkyBox policies=2 paths=2 Modify/WAR/depth\n";
    if (!ValidateRg5IndirectDeclarations(resources, error))
    {
        outLog += "RG5_INDIRECT_FAILED " + error + "\n";
        return false;
    }
    outLog += "RG5_INDIRECT_OK SSAO/SSGI policies=2 optional=2 frames=2 history-v1\n";
    {
        char* transformCases{}; size_t length{};
        const bool enabled = _dupenv_s(&transformCases, &length, "CREATOR_METEOR_TRANSFORM_CASES") == 0 &&
                             transformCases && transformCases[0] == '1';
        std::free(transformCases);
        if (enabled && (!ValidateMeteorTransformDispatch(resources, error, outLog) ||
                        !ValidateMeteorTransformResidency(resources, error, outLog)))
        {
            outLog += "METEOR_TRANSFORM_GPU_FAILED " + error + "\n";
            return false;
        }
    }
    if (!ValidateRg5LightTileDeclarations(resources, error))
    {
        outLog += "RG5_LIGHTTILES_FAILED " + error + "\n";
        return false;
    }
    outLog += "RG5_LIGHTTILES_OK policies=2 writer=1 tile-v1\n";
    if (!ValidateRg5LookupDeclarations(resources, error))
    {
        outLog += "RG5_LOOKUP_FAILED " + error + "\n";
        return false;
    }
    outLog += "RG5_LOOKUP_OK policies=2 capture=11 iterations=2 outputs-v2 WAR range-rejection\n";
    if (!ValidateRg5SpecialDeclarations(resources, error))
    {
        outLog += "RG5_SPECIAL_FAILED " + error + "\n";
        return false;
    }
    outLog += "RG5_SPECIAL_OK policies=2 Refraction/SSS-v2 Volume-coeff-v1/output-v0 WAR duplicate-rejection\n";
    if (!ValidateRg5SurfaceDeclarations(resources, error))
    {
        outLog += "RG5_SURFACE_FAILED " + error + "\n";
        return false;
    }
    outLog += "RG5_SURFACE_OK policies=2 iterations=3 depth-v5 color-v9 dedup WAR writable/state-rejection\n";
    if (!ValidateRg5SpriteGpu(resources, error))
    {
        outLog += "RG5_SPRITE_FAILED " + error + "\n";
        return false;
    }
    outLog += "RG5_SPRITE_GPU_OK policies=3 frames=18 maxError=0 shared-source depth empty-output\n";
    if (!ValidateRg5ScreenGpu(resources, error))
    {
        outLog += "RG5_SCREEN_FAILED " + error + "\n";
        return false;
    }
    outLog += "RG5_SCREEN_GPU_OK policies=3 frames=48 maxError=0 SSS/SSR enabled bypass mask missing sealed-inputs\n";
    if (!ValidateRg5FinalGpu(resources, error))
    {
        outLog += "RG5_FINAL_FAILED " + error + "\n";
        return false;
    }
    outLog += "RG5_FINAL_GPU_OK policies=3 frames=129 stages=9 maxError=0 Fog/PostChain/UI/Grid/Wire/Icon/Line capture-present history-reset missing owned-inputs\n";
    DX12TestTextureRegistration backbufferRegistration(
        resources, resources.GetRenderTarget());
    if (!backbufferRegistration.IsValid())
    {
        outLog += "[1/7] 백버퍼 핸들 등록 실패\n";
        resources.Shutdown();
        return false;
    }

    bool passed = true;

    // ── [1/7] 실행 순서 = 선언 순서 ──
    //
    // 이 검사는 legacy DeclarationOrder 호환 계약만 검사한다. 명시 모드의 DAG 정렬은 별도 검사한다.
    // 기존 설명: 재정렬하면 프레임이 실행마다
    // 달라질 수 있고, 그러면 픽셀 대조(3-6의 정확성 검증 수단)가 흔들린다.
    // 컬링된 것만 빠져야 한다.
    {
        EnhancedRenderGraph graph(resources);

        RGTextureDesc desc{};
        desc.width = 64; desc.height = 64;
        desc.allowRenderTarget = true;
        desc.name = "gbuffer";
        const RGHandle gbuffer = graph.CreateTexture(desc);

        desc.name = "lit";
        const RGHandle lit = graph.CreateTexture(desc);

        const RGPassId producer = graph.AddPass("gbuffer",
            { { gbuffer, RHIResourceState::RenderTarget } }, nullptr);
        const RGPassId consumer = graph.AddPass("lighting",
            { { gbuffer, RHIResourceState::ShaderResource }, { lit, RHIResourceState::RenderTarget } },
            nullptr, true);

        if (!graph.Compile(error))
        {
            outLog += "[1/7] Compile 실패: " + error + "\n";
            return false;
        }

        const auto& order = graph.GetExecuteOrder();
        const bool correct = (2 == order.size())
            && (order[0] == producer.index) && (order[1] == consumer.index);
        if (!correct) { passed = false; }

        outLog += "[1/7] 실행 순서 = 선언 순서 " + std::string(correct ? "통과" : "실패")
            + " (실행 " + std::to_string(order.size()) + "개)\n";
    }

    // ── [2/7] 선언 순서와 데이터 흐름의 불일치 검출 ──
    //
    // 그래프가 만든 리소스를 아무도 쓰기 전에 읽으면 초기화되지 않은 메모리를
    // 읽는 것이다. 증상은 검은 화면이 아니라 '이전 프레임 내용이 보인다'라서
    // 알아채기 어렵다 — 컴파일에서 잡아야 한다.
    //
    // 임포트한 리소스는 검사하지 않는다는 것도 함께 확인한다. 지난 프레임 결과를
    // 읽는 것(히스토리 버퍼)이 정상 사용이라 막으면 안 된다.
    {
        EnhancedRenderGraph graph(resources);

        RGTextureDesc desc{};
        desc.width = 64; desc.height = 64; desc.allowRenderTarget = true;
        desc.name = "transient";
        const RGHandle transient = graph.CreateTexture(desc);

        // 쓰기 없이 읽기만 하는 패스 — 잡혀야 한다.
        graph.AddPass("readsUnwritten",
            { { transient, RHIResourceState::ShaderResource } }, nullptr, true);

        std::string flowError;
        const bool detected = !graph.Compile(flowError);

        // 임포트 리소스를 먼저 읽는 것은 정상이어야 한다.
        EnhancedRenderGraph importedGraph(resources);
        const RGHandle imported = importedGraph.ImportTexture(backbufferRegistration.Handle(),
            RHIResourceState::RenderTarget, "external");
        importedGraph.AddPass("readsImported",
            { { imported, RHIResourceState::ShaderResource } }, nullptr, true);

        std::string importedError;
        const bool importedOk = importedGraph.Compile(importedError);

        const bool correct = detected && importedOk;
        if (!correct) { passed = false; }

        outLog += "[2/7] 흐름 불일치 검출 " + std::string(correct ? "통과" : "실패")
            + (detected ? (" (" + flowError + ")") : " (transient 미검출)")
            + (importedOk ? " · 임포트는 허용" : " · 임포트를 잘못 막음") + "\n";
    }

    // ── [3/7] 텍스처 배리어 유도 ──
    //
    // 그래프를 두는 이유의 절반이다. 상태가 바뀌는 곳에만 정확히 하나씩 나와야
    // 하고, 같은 상태로 이어지는 곳에는 나오면 안 된다(불필요한 배리어는
    // 파이프라인을 끊는다).
    {
        EnhancedRenderGraph graph(resources);

        RGTextureDesc desc{};
        desc.width = 64; desc.height = 64; desc.allowRenderTarget = true;
        desc.name = "color";
        const RGHandle color = graph.CreateTexture(desc);

        // COMMON → RENDER_TARGET (전이 1)
        const RGPassId draw = graph.AddPass("draw",
            { { color, RHIResourceState::RenderTarget } }, nullptr);
        // RENDER_TARGET 유지 (전이 0이어야 한다)
        const RGPassId drawMore = graph.AddPass("draw2",
            { { color, RHIResourceState::RenderTarget } }, nullptr);
        // RENDER_TARGET → PIXEL_SHADER_RESOURCE (전이 1)
        const RGPassId read = graph.AddPass("post",
            { { color, RHIResourceState::ShaderResource } }, nullptr, true);

        if (!graph.Compile(error))
        {
            outLog += "[3/7] Compile 실패: " + error + "\n";
            return false;
        }

        const uint32_t drawBarriers = graph.GetPassBarrierCount(draw);
        const uint32_t keepBarriers = graph.GetPassBarrierCount(drawMore);
        const uint32_t readBarriers = graph.GetPassBarrierCount(read);

        const bool correct = (1 == drawBarriers) && (0 == keepBarriers) && (1 == readBarriers);
        if (!correct) { passed = false; }

        outLog += "[3/7] 텍스처 배리어 유도 " + std::string(correct ? "통과" : "실패")
            + " (전이 " + std::to_string(drawBarriers)
            + " · 유지 " + std::to_string(keepBarriers)
            + " · 전이 " + std::to_string(readBarriers) + ")\n";
    }

    // ── [4/7] 중립 buffer transition/UAV 배리어 유도 ──
    {
        // DX12 구체 생성자가 아닌 중립 서비스 생성자로도 ImportBuffer와 계획이
        // 같아야 한다. native handle을 풀지 않는 compile-only 계약 검사다.
        IRenderDeviceServices& neutralServices = resources;
        EnhancedRenderGraph graph(neutralServices);
        RHIResourceState finalState = RHIResourceState::Common;
        const RHIBufferHandle buffer{ RHIHandleBits::Encode(7, 1) };
        const RGHandle tracked = graph.ImportBuffer(buffer, RHIResourceState::Common,
            "neutral.buffer", &finalState);

        const RGPassId firstWrite = graph.AddPass("buffer.write",
            { { tracked, RHIResourceState::UnorderedAccess } }, nullptr);
        const RGPassId secondWrite = graph.AddPass("buffer.write.again",
            { { tracked, RHIResourceState::UnorderedAccess } }, nullptr);
        const RGPassId read = graph.AddPass("buffer.read",
            { { tracked, RHIResourceState::ShaderResource } }, nullptr, true);

        std::string bufferError;
        if (!graph.Compile(bufferError))
        {
            outLog += "[4/7] buffer Compile 실패: " + bufferError + "\n";
            return false;
        }

        const uint32_t firstBarriers = graph.GetPassBarrierCount(firstWrite);
        const uint32_t uavBarriers = graph.GetPassBarrierCount(secondWrite);
        const uint32_t readBarriers = graph.GetPassBarrierCount(read);
        const bool correct = tracked.IsValid() && 1 == firstBarriers &&
            1 == uavBarriers && 1 == readBarriers &&
            RHIResourceState::Common == finalState;
        if (!correct) passed = false;

        outLog += "[4/7] 중립 buffer transition/UAV 유도 "
            + std::string(correct ? "통과" : "실패")
            + " (전이 " + std::to_string(firstBarriers)
            + " · UAV " + std::to_string(uavBarriers)
            + " · 전이 " + std::to_string(readBarriers) + ")\n";
    }

    // ── [5/7] 미사용 패스 컬링 ──
    //
    // 결과에 기여하지 않는 패스는 걷어낸다. 그 패스만 쓰던 transient도 만들지
    // 않아야 한다 — 만들면 프레임마다 낭비가 반복된다.
    {
        EnhancedRenderGraph graph(resources);

        RGTextureDesc desc{};
        desc.width = 64; desc.height = 64; desc.allowRenderTarget = true;
        desc.name = "used";   const RGHandle used = graph.CreateTexture(desc);
        desc.name = "orphan"; const RGHandle orphan = graph.CreateTexture(desc);

        const RGPassId keep = graph.AddPass("keep",
            { { used, RHIResourceState::RenderTarget } }, nullptr, true);
        const RGPassId dead = graph.AddPass("dead",
            { { orphan, RHIResourceState::RenderTarget } }, nullptr);

        if (!graph.Compile(error))
        {
            outLog += "[5/7] Compile 실패: " + error + "\n";
            return false;
        }

        const auto stats = graph.GetStats();
        const bool correct = !graph.IsPassCulled(keep) && graph.IsPassCulled(dead)
            && (1 == stats.passesCulled) && (1 == stats.transientCreated);
        if (!correct) { passed = false; }

        outLog += "[5/7] 미사용 패스 컬링 " + std::string(correct ? "통과" : "실패")
            + " (선언 " + std::to_string(stats.passesDeclared)
            + " · 컬링 " + std::to_string(stats.passesCulled)
            + " · transient 생성 " + std::to_string(stats.transientCreated) + "/2)\n";
    }

    // RG1: declare the copy before its producer, then verify real GPU pixels.
    for(const int phase:{1,2,3})
    {
        const bool versioned=phase!=1;
        const auto mode=versioned ? RGSchedulingMode::ExplicitVersioned : RGSchedulingMode::ExplicitSingleWriter;
        const std::string tag="RG"+std::to_string(phase);
        EnhancedRenderGraph graph(resources,mode);
        RGTextureDesc d{}; d.width=resources.GetWidth(); d.height=resources.GetHeight();
        d.allowRenderTarget=true; d.name="rg1.gpu.target";
        std::copy_n(DX12DeviceResources::kClearColor,4,d.clearColor);
        const auto initial=graph.CreateTexture(d);
        const auto target=versioned ? graph.Write(initial) : initial;
        RGHandle replacement;
        RGHandle discarded;
        bool discardedExecuted=false;
        if(versioned)
        {
            if(phase==3) discarded=graph.Write(target);
            const auto next=phase==3 ? graph.Write(discarded) : graph.Modify(target);
            auto black=d; black.name="rg2.replacement";
            std::fill_n(black.clearColor,3,0.f); black.clearColor[3]=1.f;
            replacement=graph.Write(graph.CreateTexture(black));
            graph.AddPass("rg2.overwrite",{{next,RHIResourceState::CopyDest,phase==3 ? RGAccessMode::Write : RGAccessMode::ReadWrite},
                {replacement,RHIResourceState::CopySource,RGAccessMode::Read}},
                [next,replacement](const EnhancedRenderGraph::ExecuteContext& c) {
                    c.encoder->CopyTexture(c.ResolveHandle(next),c.ResolveHandle(replacement));
                },true);
        }
        graph.AddPass("rg1.readback",{{target,RHIResourceState::CopySource,RGAccessMode::Read}},
            [&resources,target](const EnhancedRenderGraph::ExecuteContext& c) {
                c.encoder->CopyToReadback(resources.GetFrameReadback(),c.ResolveHandle(target));
            },true);
        graph.AddPass("rg1.clear",{{target,RHIResourceState::RenderTarget,RGAccessMode::Write}},
            [&resources,target](const EnhancedRenderGraph::ExecuteContext& c) {
                const RHITextureHandle colors[]={c.ResolveHandle(target)};
                c.encoder->ClearRenderTargets(resources.CreateRenderTargets(colors),DX12DeviceResources::kClearColor);
            });
        if(phase==3) graph.AddPass("rg3.discarded",{{discarded,RHIResourceState::RenderTarget,RGAccessMode::Write}},
            [&discardedExecuted](const EnhancedRenderGraph::ExecuteContext&) { discardedExecuted=true; });
        if(versioned) graph.AddPass("rg2.black",{{replacement,RHIResourceState::RenderTarget,RGAccessMode::Write}},
            [&resources,replacement](const EnhancedRenderGraph::ExecuteContext& c) {
                const RHITextureHandle colors[]={c.ResolveHandle(replacement)};
                const float black[]={0.f,0.f,0.f,1.f};
                c.encoder->ClearRenderTargets(resources.CreateRenderTargets(colors),black);
            });
        if(!graph.Compile(error) || graph.GetExecuteOrder()!=(phase==3 ? std::vector<uint16_t>{2,1,4,0} : (versioned ? std::vector<uint16_t>{2,1,3,0} : std::vector<uint16_t>{1,0}))
            || !resources.BeginFrame(error) || !graph.Execute(error) || !resources.EndFrame(error))
        { outLog += tag+"_GPU_FAILED " + error + "\n"; return false; }
        resources.WaitForGpu();
        if(phase==3 && (discardedExecuted || graph.GetStats().passesCulled!=1))
        { outLog += "RG3_GPU_FAILED dead writer executed\n"; return false; }
        RHIReadbackImage image{};
        if(!resources.MapReadback(resources.GetFrameReadback(),image,error)) return false;
        for(uint32_t y=0;y<d.height;++y) for(uint32_t x=0;x<d.width;++x) for(uint32_t c=0;c<3;++c)
            if(std::fabs(image.At(x,y,c)-DX12DeviceResources::kClearColor[c])>1.5f/255.f)
            { outLog += tag+"_GPU_FAILED pixel mismatch\n"; return false; }
        outLog += tag+(versioned ? "_GPU_OK reversed-RAW-WAR-WAW old-version-pixels=0\n" : "_GPU_OK reversed-RAW pixels=0\n");
    }

    // RG4: the same compiled order through immediate, one-target, two/four-target
    // recording and cost fallback. Producer slices must finish before consumers.
    {
        DX12CommandListPool pool;
        if(!pool.Initialize(resources,4,DX12DeviceResources::kFrameCount,error)) return false;
        std::vector<uint8_t> expected;
        for(const int variant:{0,1,2,4,-4,8,16,32})
        {
            if(!resources.BeginFrame(error)) return false;
            pool.BeginFrame(0);
            EnhancedRenderGraph g(resources,RGSchedulingMode::ExplicitVersioned);
            g.SetParallelRecordCostThreshold(variant<0 ? 100 : 0);
            const bool independentGpu = variant==8 || variant==16;
            const bool balancedFixture = variant>=16;
            g.SetRecordingWorkloadBalance(balancedFixture);
            RGTextureDesc d{}; d.width=resources.GetWidth(); d.height=resources.GetHeight(); d.allowRenderTarget=true; d.name="RG4.A";
            std::copy_n(DX12DeviceResources::kClearColor,4,d.clearColor);
            const auto a=g.Write(g.CreateTexture(d)); d.name="RG4.B";
            const auto b=g.Write(g.CreateTexture(d));
            std::atomic<uint32_t> slices{0}, independent{0}, padding{0}; std::atomic<bool> copied{false};
            const uint32_t wantedSlices=(independentGpu || balancedFixture) ? 1u : variant>1 ? static_cast<uint32_t>(variant) : 1u;
            const auto readbackPass=g.AddPass("readback",{{b,RHIResourceState::CopySource,RGAccessMode::Read}},
                [&](const EnhancedRenderGraph::ExecuteContext& c){
                    if(!independentGpu && !copied.load()) throw std::runtime_error("consumer recorded before copy");
                    c.encoder->CopyToReadback(resources.GetFrameReadback(),c.ResolveHandle(b));
                },true);
            const auto copyPass=g.AddPass("copy",{{a,RHIResourceState::CopySource,RGAccessMode::Read},{b,RHIResourceState::CopyDest,RGAccessMode::Write}},
                [&](const EnhancedRenderGraph::ExecuteContext& c){
                    if(!independentGpu && slices.load()!=wantedSlices) throw std::runtime_error("consumer recorded before producer slices joined");
                    c.encoder->CopyTexture(c.ResolveHandle(b),c.ResolveHandle(a)); copied.store(true);
                });
            const auto clearPass=g.AddSplitPass("clear",{{a,RHIResourceState::RenderTarget,RGAccessMode::Write}},
                [&](const EnhancedRenderGraph::ExecuteContext& c,uint32_t slice,uint32_t count){
                    if(count!=wantedSlices) throw std::runtime_error("split/fallback count mismatch");
                    if(slice==0) { const RHITextureHandle colors[]={c.ResolveHandle(a)};
                        c.encoder->ClearRenderTargets(resources.CreateRenderTargets(colors),DX12DeviceResources::kClearColor); }
                    ++slices;
                },(independentGpu || balancedFixture) ? 1u : 4u,false,balancedFixture ? 64u : 1u);
            g.AddPass("independent",{},[&](const EnhancedRenderGraph::ExecuteContext&){++independent;},true);
            if(independentGpu)
            {
                // CPU observations above are read only after the whole join;
                // only GPU resources flow from clear -> copy -> readback.
                g.DeclareRecordingIndependent(copyPass);
                g.DeclareRecordingIndependent(readbackPass);
                bool rejectedSideEffect = false;
                try { g.DeclareRecordingSideEffect(copyPass); }
                catch(const std::logic_error&) { rejectedSideEffect=true; }
                // Split callbacks can opt into independence. Their shared
                // counters are read only after the final recording join.
                g.DeclareRecordingIndependent(clearPass);
                if (!rejectedSideEffect)
                {
                    outLog += "RG4_GPU_FAILED independent callback accepted a CPU side-effect declaration\n";
                    return false;
                }
            }
            if(balancedFixture)
            {
                // Deterministic hints exercise a different contiguous cut.
                // Unannotated CPU consumers must reject the added joins.
                for(int i=0;i<4;++i)
                    g.AddPass("balance.padding",{},[&](const auto&){++padding;},true);
            }
            const auto order = balancedFixture ? std::vector<uint16_t>{2,1,0,3,4,5,6,7} : std::vector<uint16_t>{2,1,0,3};
            if(!g.Compile(error) || g.GetExecuteOrder()!=order) return false;
            if(independentGpu)
            {
                bool rejectedCompiled=false;
                try { g.DeclareRecordingIndependent(readbackPass); }
                catch(const std::logic_error&) { rejectedCompiled=true; }
                if(!rejectedCompiled) return false;
            }
            RHISubmissionTicket ticket;
            if(variant==0) { if(!g.Execute(error)) return false; }
            else {
                RHIRecordedBatchDesc desc{}; desc.frameId=1;
                desc.backendGeneration=GetRHISubmissionThread().GetOwnerGeneration(&resources);
                RHIRecordedBatch batch;
                if(!g.RecordParallel(pool,static_cast<uint32_t>(std::abs(variant)),desc,batch,error) ||
                    !GetRHISubmissionThread().EnqueueRecordedBatch(&resources,resources,std::move(batch),ticket,error)) return false;
                if(!resources.EndFrame(error)) return false;
                resources.WaitForGpu();
                if(!GetRHISubmissionThread().Wait(ticket,error)) return false;
                const auto stats=g.GetStats();
                // Same-target dependencies can reduce the number of waves.
                // Callback observations and pixel equality below verify order;
                // the historical partition count is not a correctness contract.
                if (stats.recordUnits != wantedSlices + 3 + (balancedFixture ? 4u : 0u) ||
                    stats.recordingWaveCount == 0 || stats.recordingWaveCount > stats.recordUnits ||
                    stats.parallelDeclined != (variant < 0))
                {
                    outLog += "RG4_GPU_FAILED variant=" + std::to_string(variant) +
                        " units=" + std::to_string(stats.recordUnits) +
                        " waves=" + std::to_string(stats.recordingWaveCount) +
                        " declined=" + std::to_string(stats.parallelDeclined) + "\n";
                    return false;
                }
                if(independentGpu && (stats.recordingJobDispatchCount!=1 || stats.recordingWorkerJobCount!=4))
                {
                    outLog += "RG4_GPU_FAILED variant=" + std::to_string(variant) +
                        " dispatches=" + std::to_string(stats.recordingJobDispatchCount) +
                        " workerJobs=" + std::to_string(stats.recordingWorkerJobCount) + "\n";
                    return false;
                }
                // A balanced partition can now retain CPU predecessor joins.
                // Variant 32 must preserve the observations in its callbacks,
                // rather than decline a valid weighted partition unconditionally.
                if (balancedFixture && ((variant == 16 && !stats.recordingBalanced) || padding.load() != 4))
                {
                    outLog += "RG4_GPU_FAILED variant=" + std::to_string(variant) +
                        " balanced=" + std::to_string(stats.recordingBalanced) +
                        " padding=" + std::to_string(padding.load()) + "\n";
                    return false;
                }
            }
            if(variant==0)
            {
                if(!resources.EndFrame(error)) return false;
                resources.WaitForGpu();
            }
            if(independent.load()!=1 || slices.load()!=wantedSlices) return false;
            RHIReadbackImage image{};
            if(!resources.MapReadback(resources.GetFrameReadback(),image,error)) return false;
            if(variant==0) expected=image.data;
            else if(expected!=image.data) { outLog+="RG4_GPU_FAILED image mismatch\n"; return false; }
            std::string drop; if(pool.DrainEncoderDrops(drop)!=0) return false;
        }
        outLog += "RG4_GPU_BALANCED_OK weighted nonempty contiguous targets pixels=0 CPU joins preserved\n";
        outLog+="RG4_GPU_OK immediate workers=1/2/4 fallback split join compiled-order pixels=0 drops=0\n";
        outLog+="RG4_GPU_INDEPENDENT_OK four-target GPU RAW ordered pixels=0 CPU joins retained for unannotated callbacks\n";
    }

    // ── [6/7] 실제 실행 + 픽셀 확인 ──
    //
    // 앞의 넷은 계획이 맞는지만 본다. 그 계획대로 GPU가 돌아 결과가 나오는지는
    // 실행해서 되읽어야 안다 — 배리어가 하나라도 틀리면 검증 레이어가 잡거나
    // 픽셀이 어긋난다.
    {
        EnhancedRenderGraph graph(resources);

        const RGHandle backbuffer = graph.ImportTexture(backbufferRegistration.Handle(),
            RHIResourceState::RenderTarget, "backbuffer");

        // 클리어만 하는 패스. 임포트 리소스에 쓰므로 뿌리로 잡혀 살아남는다.
        graph.AddPass("clear", { { backbuffer, RHIResourceState::RenderTarget } },
            [&resources, backbuffer](const EnhancedRenderGraph::ExecuteContext& context)
            {
                const RHITextureHandle colors[] = { context.ResolveHandle(backbuffer) };
                const auto targets = resources.CreateRenderTargets(colors);
                if (!targets.IsValid()) return;
                context.encoder->ClearRenderTargets(targets,
                    DX12DeviceResources::kClearColor);
            });

        // 리드백을 위해 COPY_SOURCE로 전이시키는 패스 — 배리어는 그래프가 만든다.
        graph.AddPass("readback", { { backbuffer, RHIResourceState::CopySource } },
            [&resources, backbuffer](const EnhancedRenderGraph::ExecuteContext& context)
            {
                context.encoder->CopyToReadback(resources.GetFrameReadback(),
                    context.ResolveHandle(backbuffer));
            }, true);

        // 다음 프레임을 위해 RENDER_TARGET으로 되돌린다(임포트 리소스의 상태 계약).
        graph.AddPass("restore", { { backbuffer, RHIResourceState::RenderTarget } },
            nullptr, true);

        if (!graph.Compile(error))
        {
            outLog += "[6/7] Compile 실패: " + error + "\n";
            return false;
        }

        if (!resources.BeginFrame(error)) { outLog += "[6/7] Begin 실패\n"; return false; }
        if (!graph.Execute(error))
        {
            outLog += "[6/7] Execute 실패: " + error + "\n";
            return false;
        }
        if (!resources.EndFrame(error)) { outLog += "[6/7] End 실패\n"; return false; }
        resources.WaitForGpu();

        RHIReadbackImage captured{};
        {
            std::string readbackError;
            if (!resources.MapReadback(resources.GetFrameReadback(), captured, readbackError))
            {
                outLog += "[6/7] 리드백 Map 실패: " + readbackError + "\n";
                return false;
            }
        }

        // 클리어 색이 그대로 나와야 한다. UNORM이라 캡처가 0~1로 준다.
        const float expectedR = DX12DeviceResources::kClearColor[0];
        const float expectedG = DX12DeviceResources::kClearColor[1];
        const float expectedB = DX12DeviceResources::kClearColor[2];

        // sRGB 변환 없이 그대로 저장되므로 ±1/255 오차만 허용한다.
        constexpr float kTolerance = 1.5f / 255.f;

        uint32_t mismatches = 0;
        for (uint32_t y = 0; y < resources.GetHeight(); ++y)
            for (uint32_t x = 0; x < resources.GetWidth(); ++x)
            {
                if (std::fabs(captured.At(x, y, 0) - expectedR) > kTolerance ||
                    std::fabs(captured.At(x, y, 1) - expectedG) > kTolerance ||
                    std::fabs(captured.At(x, y, 2) - expectedB) > kTolerance)
                {
                    ++mismatches;
                }
            }

        const auto stats = graph.GetStats();
        if (0 != mismatches) { passed = false; }

        outLog += "[6/7] 실행·픽셀 확인 " + std::string(0 == mismatches ? "통과" : "실패")
            + " (불일치 " + std::to_string(mismatches)
            + " · 배리어 " + std::to_string(stats.barriersEmitted)
            + "건을 " + std::to_string(stats.barrierBatches) + "번에 삽입)\n";
    }

    // -- [7/7] transient 풀 재사용 --
    //
    // ★ 이 검사가 없어서 회귀 하나를 놓쳤다(2026-08-10). V2-c2가 풀에서
    //   '빌려 온' transient 를 반납하지 않게 만들어, 한 프레임 걸러 전
    //   transient 를 CreateCommittedResource 로 다시 만들었다. 픽셀은
    //   똑같아서 나머지 20종이 전부 통과했고, 드러난 곳은 에디터 카메라가
    //   뚝뚝 끊기는 것뿐이었다.
    //
    //   그래서 픽셀이 아니라 '몇 개를 만들었는가'를 잰다. 같은 풀로 그래프를
    //   두 번 세우면 두 번째는 0개를 만들어야 한다 -- 그것이 풀이 있는 이유다.
    {
        RGTransientPool pool;
        uint32_t created[2]{};

        for (uint32_t round = 0; round < 2; ++round)
        {
            EnhancedRenderGraph poolGraph(resources);
            poolGraph.SetTransientPool(&pool);

            RGTextureDesc poolDesc{};
            poolDesc.width = 64;
            poolDesc.height = 64;
            poolDesc.format = RHIFormat::RGBA8Unorm;
            poolDesc.allowRenderTarget = true;
            poolDesc.name = "pool.target";
            const RGHandle poolTarget = poolGraph.CreateTexture(poolDesc);

            poolGraph.AddPass("pool.write", { { poolTarget, RHIResourceState::RenderTarget } },
                [](const EnhancedRenderGraph::ExecuteContext&) {}, true);

            std::string poolError;
            if (!poolGraph.Compile(poolError))
            {
                passed = false;
                outLog += "[7/7] 풀 재사용 - 컴파일 실패: " + poolError + "\n";
                break;
            }
            created[round] = poolGraph.GetStats().transientCreated;
        }

        // 첫 회는 만들고(1), 두 번째는 풀에서 빌려 와야 한다(0).
        const bool poolWorks = (1 == created[0]) && (0 == created[1]);
        if (!poolWorks) passed = false;
        outLog += "[7/7] transient 풀 재사용 " + std::string(poolWorks ? "통과" : "실패")
            + " (1회차 생성 " + std::to_string(created[0])
            + " · 2회차 생성 " + std::to_string(created[1]) + " · 기대 1/0)\n";
    }

    std::string messages;
    const uint32_t problems = resources.DrainDebugMessages(messages);
    if (0 != problems)
    {
        passed = false;
        outLog += "검증 레이어 문제 " + std::to_string(problems) + "건\n" + messages;
    }

    backbufferRegistration.Reset();
    resources.Shutdown();

    outLog += passed ? "렌더 그래프 검증 통과\n" : "렌더 그래프 검증 실패\n";
    return passed;
}

bool DX12Test::RunScreenResizeTest(std::string& outLog)
{
    bool passed = true;

    // ★ [1/3] "DX11 텍스처가 정책대로 따라가는가"를 은퇴시켰다 (T6, 2026-08-08).
    //
    //   그 검사는 Texture::CreateScreenSized가 만든 DX11 텍스처가 ScreenResizeBus
    //   통지에 맞춰 크기를 바꾸는지를 쟀다. T6이 Texture의 DX11 표면을 걷으면서
    //   재는 대상 자체가 사라졌다.
    //
    //   ★ 버스(ScreenResizeBus)는 그대로 살아 있다 - 백엔드 중립이고 DX12가
    //     구독한다. 은퇴한 것은 DX11 구독자를 재던 검사 하나뿐이고, 아래
    //     [1/2]가 DX12 쪽 같은 계약을 잰다.

    // ── [1/2] DX12 디바이스가 리사이즈를 받는가 ──
    DX12DeviceResources resources;
    std::string error;
    constexpr uint32_t kInitialWidth = 256;
    constexpr uint32_t kInitialHeight = 256;
    if (!resources.Initialize(kInitialWidth, kInitialHeight, error))
    {
        outLog += "[1/2] 디바이스 초기화 실패: " + error + "\n";
        return false;
    }

    constexpr uint32_t kResizedWidth = 384;
    constexpr uint32_t kResizedHeight = 192;
    if (!resources.Resize(kResizedWidth, kResizedHeight, error))
    {
        outLog += "[1/2] 리사이즈 실패: " + error + "\n";
        resources.Shutdown();
        return false;
    }

    DX12TestTextureRegistration renderTargetRegistration(
        resources, resources.GetRenderTarget());
    if (!renderTargetRegistration.IsValid())
    {
        outLog += "[1/2] 리사이즈 렌더 타깃 핸들 등록 실패\n";
        resources.Shutdown();
        return false;
    }

    {
        char line[192]{};
        std::snprintf(line, sizeof(line),
            "[1/2] DX12 %ux%u -> %ux%u · 보고 %ux%u · rowPitch %u\n",
            kInitialWidth, kInitialHeight, kResizedWidth, kResizedHeight,
            resources.GetWidth(), resources.GetHeight(), resources.GetRowPitch());
        outLog += line;
    }

    if (resources.GetWidth() != kResizedWidth || resources.GetHeight() != kResizedHeight)
    {
        passed = false;
        outLog += "      실패 — 디바이스가 새 크기를 반영하지 않았다\n";
    }

    // 리소스가 실제로 다시 만들어졌는지는 설명(desc)으로 본다. 보고 값만 보면
    // 멤버 변수만 바뀌고 타깃은 그대로인 상태와 구분되지 않는다.
    if (nullptr != resources.GetRenderTarget())
    {
        const auto desc = resources.GetRenderTarget()->GetDesc();
        if (desc.Width != kResizedWidth || desc.Height != kResizedHeight)
        {
            passed = false;
            outLog += "      실패 — 렌더 타깃 리소스가 옛 크기 그대로다\n";
        }
    }
    else
    {
        passed = false;
        outLog += "      실패 — 리사이즈 뒤 렌더 타깃이 비었다\n";
    }

    // ── [2/2] 리사이즈한 타깃에 실제로 그리고 되읽을 수 있는가 ──
    //
    // 크기만 맞고 뷰가 옛 리소스를 가리키면 여기서 드러난다.
    if (!resources.BeginFrame(error))
    {
        outLog += "[2/2] BeginFrame 실패: " + error + "\n";
        resources.Shutdown();
        return false;
    }

    auto* commandList = resources.GetCommandList();
    const D3D12_CPU_DESCRIPTOR_HANDLE rtv = resources.GetRtvHandle();
    commandList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    commandList->ClearRenderTargetView(rtv, DX12DeviceResources::kClearColor, 0, nullptr);

    {
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = resources.GetRenderTarget();
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        commandList->ResourceBarrier(1, &barrier);

        resources.GetImmediateEncoder().CopyToReadback(
            resources.GetFrameReadback(), renderTargetRegistration.Handle());

        std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
        commandList->ResourceBarrier(1, &barrier);
    }

    if (!resources.EndFrame(error))
    {
        outLog += "[2/2] EndFrame 실패: " + error + "\n";
        resources.Shutdown();
        return false;
    }
    resources.WaitForGpu();

    uint32_t clearedPixels = 0;
    {
        RHIReadbackImage captured{};
        std::string readbackError;
        if (resources.MapReadback(resources.GetFrameReadback(), captured, readbackError))
        {
            constexpr float kTolerance = 2.5f / 255.f;
            const float expected = DX12DeviceResources::kClearColor[0];
            for (uint32_t y = 0; y < kResizedHeight; ++y)
                for (uint32_t x = 0; x < kResizedWidth; ++x)
                {
                    if (std::fabs(captured.At(x, y, 0) - expected) <= kTolerance) ++clearedPixels;
                }
        }
    }

    const uint32_t totalPixels = kResizedWidth * kResizedHeight;
    {
        char line[160]{};
        std::snprintf(line, sizeof(line), "[2/2] 리사이즈 후 렌더 — 클리어 픽셀 %u/%u\n",
            clearedPixels, totalPixels);
        outLog += line;
    }

    if (clearedPixels != totalPixels)
    {
        passed = false;
        outLog += "      실패 — 리사이즈한 타깃에 그린 결과가 새 크기 전체를 덮지 않는다"
            "(뷰가 옛 리소스를 가리킬 때 나오는 모양이다)\n";
    }

    std::string messages;
    const uint32_t problems = resources.DrainDebugMessages(messages);
    if (0 != problems)
    {
        passed = false;
        outLog += "검증 레이어 문제 " + std::to_string(problems) + "건\n" + messages;
    }

    renderTargetRegistration.Reset();
    resources.Shutdown();

    outLog += passed ? "크기 추종 검증 통과\n" : "크기 추종 검증 실패\n";
    return passed;
}

bool DX12Test::RunParallelRecordTest(std::string& outLog)
{
    using Microsoft::WRL::ComPtr;

    bool passed = true;

    constexpr uint32_t kWidth = 256;
    constexpr uint32_t kHeight = 256;

    DX12DeviceResources resources;
    std::string error;
    if (!resources.Initialize(kWidth, kHeight, error))
    {
        outLog += "[1/4] 디바이스 초기화 실패: " + error + "\n";
        return false;
    }

    // allocator 둘은 이제 frame index가 아니라 recording/completion 계약이다.
    // 직접 동시 할당을 재더라도 실제 recording을 열어 producer 전제를 맞춘다.
    if (!resources.BeginFrame(error))
    {
        outLog += "[1/4] allocator recording 시작 실패: " + error + "\n";
        resources.Shutdown();
        return false;
    }

    // ── [1/4] upload segment를 여러 스레드가 동시에 잘라도 겹치지 않는가 ──
    //
    // 경합은 매번 나지 않는다. 그래서 결과 픽셀만 보면 우연히 통과할 수 있고,
    // 그 우연은 나중에 '가끔 상수가 다른 드로우 것으로 보인다'로 돌아온다.
    // 할당 구간을 직접 모아 겹침을 본다.
    {
        constexpr uint32_t kThreads = 8;
        constexpr uint32_t kPerThread = 256;

        std::vector<std::vector<std::pair<uint64_t, uint64_t>>> ranges(kThreads);
        std::vector<std::thread> threads;

        for (uint32_t t = 0; t < kThreads; ++t)
        {
            threads.emplace_back([&, t]()
            {
                ranges[t].reserve(kPerThread);
                for (uint32_t i = 0; i < kPerThread; ++i)
                {
                    // 크기를 섞는다. 같은 크기만 쓰면 정렬 계산이 항상 같아
                    // 경합 창이 좁아진다.
                    const uint64_t size = 16ull + (i % 7) * 48ull;
                    const auto allocation = resources.AllocateUpload(
                        RHIUploadRequest{ size, RHIUploadUsage::ConstantBuffer, 1 });
                    if (allocation.IsValid())
                    {
                        ranges[t].emplace_back(allocation.offset, allocation.offset + size);
                    }
                }
            });
        }
        for (auto& thread : threads) thread.join();

        std::vector<std::pair<uint64_t, uint64_t>> all;
        for (auto& list : ranges) all.insert(all.end(), list.begin(), list.end());
        std::sort(all.begin(), all.end());

        uint32_t overlaps = 0;
        for (size_t i = 1; i < all.size(); ++i)
        {
            if (all[i].first < all[i - 1].second) ++overlaps;
        }

        char line[192]{};
        std::snprintf(line, sizeof(line),
            "[1/4] upload segment — 스레드 %u · 할당 %zu건 · 겹침 %u건\n",
            kThreads, all.size(), overlaps);
        outLog += line;

        if (0 != overlaps)
        {
            passed = false;
            outLog += "      실패 — 두 스레드가 같은 구간을 받았다\n";
        }
        if (all.size() < kThreads)
        {
            passed = false;
            outLog += "      실패 — 할당이 거의 이뤄지지 않았다(구간이 너무 작다)\n";
        }
    }

    // ── [2/4] descriptor page도 같은가 ──
    {
        constexpr uint32_t kThreads = 8;
        constexpr uint32_t kPerThread = 128;

        std::vector<std::vector<std::pair<uint64_t, uint64_t>>> ranges(kThreads);
        std::vector<std::thread> threads;

        for (uint32_t t = 0; t < kThreads; ++t)
        {
            threads.emplace_back([&, t]()
            {
                ranges[t].reserve(kPerThread);
                for (uint32_t i = 0; i < kPerThread; ++i)
                {
                    const uint32_t count = 1u + (i % 4);
                    const auto allocation = resources.GetDescriptorRecycler().Allocate(count);
                    if (allocation.IsValid())
                    {
                        ranges[t].emplace_back(allocation.gpu.ptr,
                            allocation.gpu.ptr + static_cast<uint64_t>(count)
                                * allocation.incrementSize);
                    }
                }
            });
        }
        for (auto& thread : threads) thread.join();

        std::vector<std::pair<uint64_t, uint64_t>> all;
        for (auto& list : ranges) all.insert(all.end(), list.begin(), list.end());
        std::sort(all.begin(), all.end());

        uint32_t overlaps = 0;
        for (size_t i = 1; i < all.size(); ++i)
        {
            if (all[i].first < all[i - 1].second) ++overlaps;
        }

        char line[192]{};
        std::snprintf(line, sizeof(line),
            "[2/4] descriptor page — 스레드 %u · 할당 %zu건 · 겹침 %u건\n",
            kThreads, all.size(), overlaps);
        outLog += line;

        if (0 != overlaps)
        {
            passed = false;
            outLog += "      실패 — 두 스레드가 같은 디스크립터 구간을 받았다\n";
        }
    }

    if (!resources.EndFrame(error))
    {
        outLog += "[2/4] allocator recording 제출 실패: " + error + "\n";
        resources.Shutdown();
        return false;
    }

    // ── [3/4] 순차와 병렬의 결과가 같은가 ──
    //
    // 같은 타깃에 색을 순서대로 덮는 패스를 여러 개 둔다. 순서가 지켜지면
    // 마지막 패스의 색만 남는다. 순서가 깨지거나 리스트가 두 번 실행되면
    // 다른 색이 나오고, 그 차이는 픽셀 대조로 잡힌다.
    constexpr uint32_t kPassCount = 6;

    RHIReadback readback{};
    {
        std::string readbackError;
        if (!resources.CreateReadback(kWidth, kHeight, FromDXGI(DXGI_FORMAT_R8G8B8A8_UNORM), 1,
            readback, readbackError))
        {
            outLog += "[3/4] 리드백 버퍼 생성 실패\n";
            resources.Shutdown();
            return false;
        }
    }

    ComPtr<ID3D12DescriptorHeap> rtvHeap;
    {
        D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
        heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        heapDesc.NumDescriptors = 1;
        if (FAILED(resources.GetDevice()->CreateDescriptorHeap(&heapDesc,
            IID_PPV_ARGS(&rtvHeap))))
        {
            outLog += "[3/4] RTV 힙 생성 실패\n";
            resources.Shutdown();
            return false;
        }
    }

    thread_pool recordingWorkers;
    job_scheduler recordingScheduler(recordingWorkers);
    DX12CommandListPool pool(recordingScheduler);
    if (!pool.Initialize(resources, 4, DX12DeviceResources::kFrameCount, error))
    {
        outLog += "[3/4] 커맨드 리스트 풀 초기화 실패: " + error + "\n";
        resources.Shutdown();
        return false;
    }

    if (!VerifyParallelRecordingJobs(pool, resources, recordingScheduler, outLog))
    {
        pool.Shutdown();
        resources.Shutdown();
        return false;
    }
    uint32_t recordingFrame = 0;
    std::atomic<uint32_t> callerRecordings{ 0 };

    // 순차/병렬을 같은 코드로 돌린다. 그래야 차이가 '병렬이라서'로 좁혀진다.
    const auto renderOnce = [&](uint32_t workers, std::vector<uint8_t>& outPixels,
        EnhancedRenderGraph::Stats& outStats, std::string& outStepError) -> bool
    {
        if (!resources.BeginFrame(outStepError)) return false;
        const uint32_t frameSlot = recordingFrame++ % DX12DeviceResources::kFrameCount;
        pool.BeginFrame(frameSlot);

        EnhancedRenderGraph graph(resources);

        RGTextureDesc targetDesc{};
        targetDesc.width = kWidth;
        targetDesc.height = kHeight;
        targetDesc.format = RHIFormat::RGBA8Unorm;
        targetDesc.allowRenderTarget = true;
        targetDesc.name = "Parallel.Target";

        // 클리어 값을 리소스에 미리 알린다. 다른 값으로 클리어하면 검증 레이어가
        // "느려진다"고 경고하고, 그 경고를 검증에서 무시하기 시작하면 다른 곳의
        // 진짜 실수도 같이 묻힌다.
        targetDesc.clearColor[0] = 0.25f;
        targetDesc.clearColor[1] = 0.5f;
        targetDesc.clearColor[2] = 0.75f;
        targetDesc.clearColor[3] = 1.f;
        const RGHandle target = graph.CreateTexture(targetDesc);

        // 패스마다 자기 가로 띠만 지운다.
        //
        // 색으로 순서를 확인하려면 패스마다 다른 색을 써야 하는데 그러면 위
        // 경고가 난다. 대신 띠를 나눠 '전 구간이 빠짐없이 덮이는가'를 본다 —
        // 리스트 하나가 통째로 빠지면 그 띠가 미정의 값으로 남는다.
        //
        // 순서 자체는 순차와 병렬의 픽셀이 완전히 같은지로 확인한다([3/4]).
        for (uint32_t i = 0; i < kPassCount; ++i)
        {
            graph.AddPass("clear" + std::to_string(i),
                { { target, RHIResourceState::RenderTarget } },
                [&, i](const EnhancedRenderGraph::ExecuteContext& executeContext)
                {
                    if (!thread_pool::is_worker_thread()) ++callerRecordings;
                    // 손으로 만든 RTV 힙이 사라졌다(V2-d). 띠 클리어가 원시
                    // 커맨드 리스트를 쓰던 유일한 이유였고, 이제 인코더가
                    // rect 클리어를 안다.
                    const RHITextureHandle colors[] = { executeContext.ResolveHandle(target) };
                    const auto targets = resources.CreateRenderTargets(colors);
                    if (!targets.IsValid()) return;

                    const LONG bandTop = static_cast<LONG>(kHeight * i / kPassCount);
                    const LONG bandBottom = static_cast<LONG>(kHeight * (i + 1) / kPassCount);
                    const RHIRect band{ 0, bandTop, static_cast<int32_t>(kWidth), bandBottom };

                    const float color[4] = { 0.25f, 0.5f, 0.75f, 1.f };
                    RHIEncoder& encoder = *executeContext.encoder;
                    encoder.BindRenderTargets(targets);
                    encoder.ClearRenderTargetRect(targets, color, band);
                }, true);
        }

        graph.AddPass("readback", { { target, RHIResourceState::CopySource } },
            [&](const EnhancedRenderGraph::ExecuteContext& executeContext)
            {
                executeContext.encoder->CopyToReadback( readback,
                    executeContext.ResolveHandle(target));
            }, true);

        if (!graph.Compile(outStepError)) return false;

        RHIRecordedBatchDesc batchDesc{};
        batchDesc.frameId = workers;
        batchDesc.backendGeneration =
            GetRHISubmissionThread().GetOwnerGeneration(&resources);
        RHIRecordedBatch batch;
        if (!graph.RecordParallel(pool, workers, batchDesc, batch, outStepError))
        {
            return false;
        }
        // batch가 mutable current slot을 다시 읽지 않는지 확인한다. 기록은 slot 0,
        // 제출 직전 pool current는 slot 1로 바꾼다. 잘못 구현하면 빈 slot 1이 제출된다.
        pool.BeginFrame((frameSlot + 1) % DX12DeviceResources::kFrameCount);
        if (frameSlot != batch.GetFrameSlot()) return false;
        RHISubmissionTicket batchTicket;
        if (!GetRHISubmissionThread().EnqueueRecordedBatch(&resources,
            resources, std::move(batch), batchTicket, outStepError)) return false;
        outStats = graph.GetStats();

        if (!resources.EndFrame(outStepError)) return false;
        resources.WaitForGpu();
        if (!GetRHISubmissionThread().Wait(batchTicket, outStepError)) return false;

        // 픽셀을 바이트 그대로 견준다 — 순차와 병렬이 같은 그림인가만 본다.
        RHIReadbackImage captured{};
        if (!resources.MapReadback(readback, captured, outStepError)) return false;

        outPixels = std::move(captured.data);
        return true;
    };

    std::vector<uint8_t> sequential;
    std::vector<uint8_t> parallel;
    EnhancedRenderGraph::Stats sequentialStats{};
    EnhancedRenderGraph::Stats parallelStats{};

    if (!renderOnce(1, sequential, sequentialStats, error))
    {
        outLog += "[3/4] 순차 실행 실패: " + error + "\n";
        pool.Shutdown();
        resources.Shutdown();
        return false;
    }
    if (!renderOnce(4, parallel, parallelStats, error))
    {
        outLog += "[3/4] 병렬 실행 실패: " + error + "\n";
        pool.Shutdown();
        resources.Shutdown();
        return false;
    }

    recordingScheduler.shutdown();
    recordingScheduler.start(4);
    bool repeatedPixels = true;
    for (uint32_t iteration = 0; iteration < DX12DeviceResources::kFrameCount * 2; ++iteration)
    {
        std::vector<uint8_t> repeated;
        EnhancedRenderGraph::Stats repeatedStats{};
        if (!renderOnce(4, repeated, repeatedStats, error) || repeated != sequential ||
            4 != repeatedStats.recordedLists)
        {
            repeatedPixels = false;
            outLog += "[jobs] repeated frame failed: " + error + "\n";
            break;
        }
    }
    passed = passed && repeatedPixels && 0 == callerRecordings;
    outLog += "[jobs] physical workers=1/4, logical lanes=4, frame slot reuse pixel parity: "
        + std::string(repeatedPixels && 0 == callerRecordings ? "passed\n" : "failed\n");

    size_t differing = sequential.size() == parallel.size() ? 0 : 1;
    for (size_t i = 0; i < sequential.size() && i < parallel.size(); ++i)
    {
        if (sequential[i] != parallel[i]) ++differing;
    }

    {
        char line[224]{};
        std::snprintf(line, sizeof(line),
            "[3/4] 순차(워커 %u·리스트 %u) vs 병렬(워커 %u·리스트 %u) — 다른 바이트 %zu\n",
            sequentialStats.recordWorkers, sequentialStats.recordedLists,
            parallelStats.recordWorkers, parallelStats.recordedLists, differing);
        outLog += line;
    }

    if (0 != differing)
    {
        passed = false;
        outLog += "      실패 — 병렬 결과가 순차와 다르다\n";
    }
    if (parallelStats.recordWorkers <= 1)
    {
        passed = false;
        outLog += "      실패 — 병렬로 요청했는데 워커가 하나다(비교가 성립하지 않는다)\n";
    }

    // ── [4/4] 모든 패스의 띠가 빠짐없이 덮였는가 ──
    //
    // 리스트 하나가 통째로 빠지면 그 띠가 미정의 값으로 남는다.
    //
    // 제출 순서 자체는 [3/4]가 본다 — 순서가 달라지면 순차와 픽셀이 갈린다.
    // 리스트 중복 기록은 구조로 막았다(연속 블록 배분이라 워커당 한 번이고,
    // batch 기록 리스트 수 == 워커 수로 확인한다).
    {
        const auto expected = static_cast<int>(0.25f * 255.f + 0.5f);

        uint32_t wrongPixels = 0;
        for (uint32_t y = 0; y < kHeight; ++y)
        {
            const uint8_t* row = parallel.data()
                + static_cast<size_t>(y) * readback.rowPitch;
            for (uint32_t x = 0; x < kWidth; ++x)
            {
                if (std::abs(static_cast<int>(row[static_cast<size_t>(x) * 4]) - expected) > 1)
                {
                    ++wrongPixels;
                }
            }
        }

        char line[192]{};
        std::snprintf(line, sizeof(line),
            "[4/4] 띠 덮임 — 기대와 다른 픽셀 %u/%u · 기록 리스트 %u(워커 %u)\n",
            wrongPixels, kWidth * kHeight,
            parallelStats.recordedLists, parallelStats.recordWorkers);
        outLog += line;

        if (0 != wrongPixels)
        {
            passed = false;
            outLog += "      실패 — 덮이지 않은 구간이 있다(리스트가 빠졌다)\n";
        }
        if (parallelStats.recordedLists != parallelStats.recordWorkers)
        {
            passed = false;
            outLog += "      실패 — batch 기록 리스트 수가 워커 수와 다르다\n";
        }
    }

    std::string messages;
    const uint32_t problems = resources.DrainDebugMessages(messages);
    if (0 != problems)
    {
        passed = false;
        outLog += "검증 레이어 문제 " + std::to_string(problems) + "건\n" + messages;
    }

    pool.Shutdown();
    bool schedulerAlive = false;
    recordingScheduler.submit([&] { schedulerAlive = true; }).wait();
    passed = passed && schedulerAlive;
    resources.Shutdown();

    outLog += passed ? "병렬 기록 검증 통과\n" : "병렬 기록 검증 실패\n";
    return passed;
}
