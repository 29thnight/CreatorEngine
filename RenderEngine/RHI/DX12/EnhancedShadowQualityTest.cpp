#ifndef DYNAMICCPP_EXPORTS
#include "EnhancedShadowPass.h"
#include "EnhancedGBufferPass.h"
#include "EnhancedDeferredPass.h"
#include "DX12DeviceResources.h"
#include "DX12PSOManager.h"
#include "DX12RootSignatureCache.h"
#include "DX12MeshCache.h"
#include "DX12TextureCache.h"
#include "EnhancedRenderGraph.h"
#include "EnhancedSceneRenderer.h"
#include "../../Mesh.h"
#include "../../DeviceState.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <vector>

// 그림자 품질 검증 (PHASE 3-6 — 경사 비례 편향 · 캐스케이드 경계 블렌딩).
//
// ── 둘 다 A/B로 잰다 ──
//
//   ① 경사 편향 — 빛이 스치는 바닥(가림막 없음)은 전부 밝아야 한다.
//      상수 편향을 일부러 작게 두면 경사면에서 자기 그림자(여드름)가
//      생기는데, 경사 항을 켜면 사라져야 한다. '켠 쪽이 항상 통과'가
//      아니라 '끈 쪽이 실제로 실패'까지 확인한다 — 끈 쪽도 통과하면
//      조건이 약해 검증이 아무것도 재지 않은 것이다.
//
//   ② 경계 블렌딩 — 그림자 줄무늬가 분할 경계를 가로지르게 두고
//      블렌딩 켬/끔 두 장을 비교한다. 차이가 나는 픽셀이 '있어야'
//      하고(경계에서 두 캐스케이드가 실제로 다르다), '일부'여야 한다
//      (블렌딩은 경계 구간만 바꾼다 — 화면 전체가 바뀌면 그건 버그다).
//
// 표본 자리를 못 박는 대신 수를 세는 이유: 여드름과 경계 어긋남은
// 텍셀 격자에 매인 패턴이라 한 점이 아니라 분포로 드러난다.
namespace
{
    constexpr uint32_t kShadowQualityWidth = 256;
    constexpr uint32_t kShadowQualityHeight = 256;

    constexpr uint64_t kShadowQualityRowPitch =
        ((static_cast<uint64_t>(kShadowQualityWidth) * 8ull)
            + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1ull)
        & ~static_cast<uint64_t>(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1ull);

    float ShadowQualityHalfToFloat(uint16_t bits)
    {
        const uint32_t sign = static_cast<uint32_t>(bits & 0x8000u) << 16;
        uint32_t exponent = (bits >> 10) & 0x1Fu;
        uint32_t mantissa = bits & 0x3FFu;

        if (0 == exponent)
        {
            if (0 == mantissa)
            {
                float out; memcpy(&out, &sign, 4); return out;
            }
            exponent = 1;
            while (0 == (mantissa & 0x400u)) { mantissa <<= 1; --exponent; }
            mantissa &= 0x3FFu;
            const uint32_t bitsOut = sign | ((exponent + 112u) << 23) | (mantissa << 13);
            float out; memcpy(&out, &bitsOut, 4); return out;
        }
        if (31 == exponent)
        {
            const uint32_t bitsOut = sign | 0x7F800000u | (mantissa << 13);
            float out; memcpy(&out, &bitsOut, 4); return out;
        }

        const uint32_t bitsOut = sign | ((exponent + 112u) << 23) | (mantissa << 13);
        float out; memcpy(&out, &bitsOut, 4); return out;
    }

    struct ShadowQualityCapture
    {
        std::vector<uint8_t> data;

        float At(uint32_t x, uint32_t y, uint32_t channel) const
        {
            const auto* row = reinterpret_cast<const uint16_t*>(
                data.data() + static_cast<size_t>(y) * kShadowQualityRowPitch);
            return ShadowQualityHalfToFloat(row[x * 4 + channel]);
        }

        // 중앙 영역에서 임계 미만(어두운) 픽셀 수. 가장자리는 지평선·배경이
        // 섞일 수 있어 뺀다.
        uint32_t CountDarkCenter(float threshold) const
        {
            uint32_t dark = 0;
            for (uint32_t y = kShadowQualityHeight / 4; y < kShadowQualityHeight * 3 / 4; ++y)
                for (uint32_t x = kShadowQualityWidth / 4; x < kShadowQualityWidth * 3 / 4; ++x)
                    if (At(x, y, 1) < threshold) ++dark;
            return dark;
        }

        // 중앙 영역에서 상대가 나보다 delta 이상 밝은 픽셀 수 — 즉 '내 쪽만
        // 어두워진' 픽셀이다. 여드름은 절대 밝기로는 못 센다: 스치는 빛에서
        // PCF 평균이 부분 감광(~15%)에 그쳐 '검은 픽셀' 기준에 안 걸린다.
        // 같은 자리의 A/B 차이는 그 함정이 없다.
        uint32_t CountDarkerThanCenter(const ShadowQualityCapture& other, float delta) const
        {
            uint32_t darker = 0;
            for (uint32_t y = kShadowQualityHeight / 4; y < kShadowQualityHeight * 3 / 4; ++y)
                for (uint32_t x = kShadowQualityWidth / 4; x < kShadowQualityWidth * 3 / 4; ++x)
                    if (At(x, y, 1) < other.At(x, y, 1) - delta) ++darker;
            return darker;
        }

        // 중앙 영역 G 채널의 분포. 임계값이 틀렸는지 판단하는 진단용이다 —
        // PCF가 부분값(0.25·0.5)을 만들면 '완전히 검은' 픽셀은 드물다.
        void CenterStats(float& outMin, float& outMean, float& outMax) const
        {
            outMin = 1e9f; outMax = -1e9f;
            double sum = 0.0;
            uint32_t count = 0;
            for (uint32_t y = kShadowQualityHeight / 4; y < kShadowQualityHeight * 3 / 4; ++y)
                for (uint32_t x = kShadowQualityWidth / 4; x < kShadowQualityWidth * 3 / 4; ++x)
                {
                    const float value = At(x, y, 1);
                    outMin = (std::min)(outMin, value);
                    outMax = (std::max)(outMax, value);
                    sum += value;
                    ++count;
                }
            outMean = (count > 0) ? static_cast<float>(sum / count) : 0.f;
        }

        uint32_t CountDifferent(const ShadowQualityCapture& other, float threshold) const
        {
            uint32_t different = 0;
            for (uint32_t y = 0; y < kShadowQualityHeight; ++y)
                for (uint32_t x = 0; x < kShadowQualityWidth; ++x)
                    if (std::fabs(At(x, y, 1) - other.At(x, y, 1)) > threshold) ++different;
            return different;
        }
    };

    FrameCameraSnapshot ShadowQualityCamera(const Mathf::Vector3& eye,
        const Mathf::Vector3& at, float fovRadians, float nearZ, float farZ)
    {
        const Mathf::xVector eyeVec = XMVectorSet(eye.x, eye.y, eye.z, 1.f);
        const Mathf::xVector atVec = XMVectorSet(at.x, at.y, at.z, 1.f);
        const Mathf::xVector upVec = XMVectorSet(0.f, 1.f, 0.f, 0.f);

        FrameCameraSnapshot snapshot{};
        snapshot.view = XMMatrixLookAtLH(eyeVec, atVec, upVec);
        snapshot.projection = XMMatrixPerspectiveFovLH(fovRadians, 1.f, nearZ, farZ);
        snapshot.inverseView = XMMatrixInverse(nullptr, snapshot.view);
        snapshot.inverseProjection = XMMatrixInverse(nullptr, snapshot.projection);
        snapshot.eyePosition = eyeVec;
        snapshot.forward = XMVector3Normalize(XMVectorSubtract(atVec, eyeVec));
        snapshot.right = XMVector3Normalize(XMVector3Cross(upVec, snapshot.forward));
        snapshot.up = XMVector3Cross(snapshot.forward, snapshot.right);
        snapshot.fov = fovRadians;
        snapshot.nearPlane = nearZ;
        snapshot.farPlane = farZ;
        snapshot.isOrthographic = false;
        return snapshot;
    }

    // 사각 평판 메시. 법선을 지정해 GBuffer 라이팅이 올바로 계산되게 한다.
    void ShadowQualityQuad(std::vector<Vertex>& outVertices,
        std::vector<uint32_t>& outIndices,
        const Mathf::Vector3& origin, const Mathf::Vector3& axisU,
        const Mathf::Vector3& axisV, const Mathf::Vector3& normal)
    {
        const uint32_t base = static_cast<uint32_t>(outVertices.size());

        const Mathf::Vector3 corners[4] = {
            origin,
            origin + axisU,
            origin + axisU + axisV,
            origin + axisV,
        };
        for (const auto& corner : corners)
        {
            Vertex vertex{};
            vertex.position = corner;
            vertex.normal = normal;
            outVertices.push_back(vertex);
        }

        outIndices.push_back(base + 0); outIndices.push_back(base + 1); outIndices.push_back(base + 2);
        outIndices.push_back(base + 0); outIndices.push_back(base + 2); outIndices.push_back(base + 3);
    }
}

bool EnhancedSceneRenderer::RunShadowQualityTest(std::string& outLog)
{
    using Microsoft::WRL::ComPtr;

    outLog += "── 그림자 품질 검증 (경사 편향 · 경계 블렌딩) ──\n";

    std::string error;

    DX12DeviceResources resources;
    if (!resources.Initialize(kShadowQualityWidth, kShadowQualityHeight, error))
    {
        outLog += "[1/4] DX12 초기화 실패: " + error + "\n";
        return false;
    }

    DX12PSOManager psoManager;
    DX12RootSignatureCache rootSignatures;
    DX12MeshCache meshCache;
    DX12TextureCache textureCache;
    if (!psoManager.Initialize(resources.GetDevice(), L"dx12_shadowquality.cache", error) ||
        !rootSignatures.Initialize(resources.GetDevice(), error) ||
        !meshCache.Initialize(&resources, error) ||
        !textureCache.Initialize(&resources, error))
    {
        outLog += "[1/4] 캐시 초기화 실패: " + error + "\n";
        resources.Shutdown();
        return false;
    }

    // ── 합성 메시 — 바닥과 가림 기둥 ──
    //
    // ★ 바운드를 다시 계산해야 한다. Mesh 생성자는 바운드를 만들지 않고
    //   ModelLoader가 임포트 때 채운다 — 합성 메시는 기본값(반지름 1)이
    //   남고, 그러면 400 단위 바닥이 그림자 캐스터 컬링에서 잘려 맵이
    //   빈다. 실제로 이 검증의 첫 실행이 '여드름 0'으로 그것을 잡았고,
    //   그래서 Mesh::RecalculateBounds가 생겼다.
    std::vector<Vertex> groundVertices;
    std::vector<uint32_t> groundIndices;
    ShadowQualityQuad(groundVertices, groundIndices,
        { -200.f, 0.f, -200.f }, { 400.f, 0.f, 0.f }, { 0.f, 0.f, 400.f }, { 0.f, 1.f, 0.f });
    Mesh groundMesh("dx12_shadow_ground", groundVertices, groundIndices);
    groundMesh.RecalculateBounds();

    // 화면 왼쪽 밖에 세운 기둥. 옆으로 눕는 빛이 그림자 줄무늬를 화면
    // 안으로 드리운다 — 기둥 자체는 보이지 않아 그림만 남는다.
    std::vector<Vertex> blockerVertices;
    std::vector<uint32_t> blockerIndices;
    ShadowQualityQuad(blockerVertices, blockerIndices,
        { 0.f, 0.f, -0.5f }, { 0.f, 0.f, 1.f }, { 0.f, 6.f, 0.f }, { 1.f, 0.f, 0.f });
    Mesh blockerMesh("dx12_shadow_blocker", blockerVertices, blockerIndices);
    blockerMesh.RecalculateBounds();

    EnhancedFrameContext frameContext{};
    frameContext.resources = &resources;
    frameContext.psoManager = &psoManager;
    frameContext.rootSignatures = &rootSignatures;
    frameContext.meshCache = &meshCache;
    frameContext.textureCache = &textureCache;
    frameContext.width = kShadowQualityWidth;
    frameContext.height = kShadowQualityHeight;

    EnhancedShadowPass shadow;
    EnhancedGBufferPass gbuffer;
    EnhancedDeferredPass deferred;
    if (!shadow.Initialize(frameContext, error) ||
        !gbuffer.Initialize(frameContext, error) ||
        !deferred.Initialize(frameContext, error))
    {
        outLog += "[1/4] 패스 초기화 실패: " + error + "\n";
        resources.Shutdown();
        return false;
    }
    gbuffer.SetKeepAlive(false);
    outLog += "[1/4] 패스 3종 초기화 통과\n";

    ComPtr<ID3D12Resource> readback;
    {
        D3D12_HEAP_PROPERTIES readbackHeap{};
        readbackHeap.Type = D3D12_HEAP_TYPE_READBACK;

        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = kShadowQualityRowPitch * kShadowQualityHeight;
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        if (FAILED(resources.GetDevice()->CreateCommittedResource(&readbackHeap,
            D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr, IID_PPV_ARGS(&readback))))
        {
            outLog += "[1/4] 리드백 생성 실패\n";
            resources.Shutdown();
            return false;
        }
    }

    bool passed = true;
    EnhancedRenderGraph::Stats lastStats{};

    // 한 프레임: 그림자 → GBuffer → Deferred → 리드백.
    // mutate가 그림자 상수(경사 계수·블렌딩 폭)를 A/B로 바꾼다.
    const auto renderOnce = [&](const FrameCameraSnapshot& snapshot,
        const std::vector<EnhancedDrawItem>& draws,
        const std::vector<EnhancedLight>& lights,
        const std::function<void(EnhancedShadowData&)>& mutate,
        ShadowQualityCapture& outCapture) -> bool
    {
        frameContext.camera = &snapshot;
        frameContext.draws = &draws;
        frameContext.lights = &lights;

        if (!resources.BeginFrame(error))
        {
            outLog += "BeginFrame 실패: " + error + "\n";
            return false;
        }

        if (!shadow.PrepareFrame(frameContext, error) ||
            !gbuffer.PrepareFrame(frameContext, error) ||
            !deferred.PrepareFrame(frameContext, error))
        {
            outLog += "PrepareFrame 실패: " + error + "\n";
            return false;
        }

        // ★ 그래프는 제출 이후까지 살아 있어야 한다(dx12.compare 크래시).
        EnhancedRenderGraph graph;

        shadow.Declare(graph, frameContext);
        gbuffer.Declare(graph, frameContext);

        deferred.SetInputs(gbuffer.GetOutputs());

        EnhancedShadowData shadowData = shadow.GetShadowData();
        if (mutate) mutate(shadowData);
        deferred.SetShadow(shadow.GetShadowMap(), shadowData);

        deferred.Declare(graph, frameContext);

        const RGHandle output = deferred.GetOutput();
        if (!output.IsValid())
        {
            outLog += "Deferred 출력이 없다\n";
            return false;
        }

        graph.AddPass("ShadowQuality.Readback",
            { { output, RGResourceState::CopySource } },
            [&](const EnhancedRenderGraph::ExecuteContext& executeContext)
            {
                D3D12_TEXTURE_COPY_LOCATION src{};
                src.pResource = executeContext.Resolve(output);
                src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;

                D3D12_TEXTURE_COPY_LOCATION dst{};
                dst.pResource = readback.Get();
                dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                dst.PlacedFootprint.Footprint.Format = EnhancedDeferredPass::kOutputFormat;
                dst.PlacedFootprint.Footprint.Width = kShadowQualityWidth;
                dst.PlacedFootprint.Footprint.Height = kShadowQualityHeight;
                dst.PlacedFootprint.Footprint.Depth = 1;
                dst.PlacedFootprint.Footprint.RowPitch =
                    static_cast<UINT>(kShadowQualityRowPitch);

                executeContext.commandList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
            }, true);

        if (!graph.Compile(resources.GetDevice(), error))
        {
            outLog += "Compile 실패: " + error + "\n";
            return false;
        }
        if (!graph.Execute(resources.GetCommandList(), error))
        {
            outLog += "Execute 실패: " + error + "\n";
            return false;
        }
        lastStats = graph.GetStats();

        if (!resources.EndFrame(error))
        {
            outLog += "EndFrame 실패: " + error + "\n";
            return false;
        }
        resources.WaitForGpu();

        void* mapped = nullptr;
        D3D12_RANGE range{ 0,
            static_cast<SIZE_T>(kShadowQualityRowPitch * kShadowQualityHeight) };
        if (FAILED(readback->Map(0, &range, &mapped)))
        {
            outLog += "리드백 Map 실패\n";
            return false;
        }
        outCapture.data.assign(static_cast<const uint8_t*>(mapped),
            static_cast<const uint8_t*>(mapped)
            + kShadowQualityRowPitch * kShadowQualityHeight);
        readback->Unmap(0, nullptr);
        return true;
    };

    // ── [2/4] 경사 비례 편향 — 스치는 빛의 맨바닥 A/B ──
    //
    // 바닥뿐이라 그림자를 드리울 것이 없다 — 어두운 픽셀은 전부 여드름이다.
    // 상수 편향을 일부러 작게 둬(0.00005) 끈 쪽이 실제로 실패하게 만든다.
    if (passed)
    {
        std::vector<EnhancedDrawItem> draws(1);
        draws[0].mesh = &groundMesh;
        draws[0].worldMatrix = XMMatrixIdentity();

        std::vector<EnhancedLight> lights(1);
        lights[0].position = Mathf::Vector4(0.f, 0.f, 0.f, 0.f);   // w=0 방향광
        lights[0].direction = Mathf::Vector4(
            Mathf::Vector3(XMVector3Normalize(XMVectorSet(1.f, -0.18f, 0.f, 0.f))));
        lights[0].color = Mathf::Color4(1.f, 1.f, 1.f, 5.f);

        const FrameCameraSnapshot camera = ShadowQualityCamera(
            { 0.f, 25.f, -12.f }, { 0.f, 0.f, 6.f },
            DirectX::XM_PI / 3.f, 0.1f, 200.f);

        shadow.SetBias(0.00005f);

        ShadowQualityCapture slopeOff{};
        ShadowQualityCapture slopeOn{};
        if (!renderOnce(camera, draws, lights,
                [](EnhancedShadowData& data) { data.bias.w = 0.f; }, slopeOff) ||
            !renderOnce(camera, draws, lights, nullptr, slopeOn))
        {
            passed = false;
        }
        else
        {
            // 같은 자리 A/B: 여드름 = 끈 쪽만 어두워진 픽셀. 반대 방향
            // (켠 쪽만 어두워진 픽셀)은 0이어야 한다 — 편향은 그림자를
            // 지우기만 해야지 새로 만들면 안 된다.
            const uint32_t acneOff = slopeOff.CountDarkerThanCenter(slopeOn, 0.05f);
            const uint32_t acneOn = slopeOn.CountDarkerThanCenter(slopeOff, 0.05f);

            float minOff = 0.f, meanOff = 0.f, maxOff = 0.f;
            float minOn = 0.f, meanOn = 0.f, maxOn = 0.f;
            slopeOff.CenterStats(minOff, meanOff, maxOff);
            slopeOn.CenterStats(minOn, meanOn, maxOn);

            char line[288]{};
            std::snprintf(line, sizeof(line),
                "[2/4] 경사 편향 — 끔만 어두움 %u · 켬만 어두움 %u (중앙 %u픽셀) · "
                "평균 끔 %.3f/켬 %.3f · 방향광 %d · 그림자 드로우 %u·컬링 %u\n",
                acneOff, acneOn,
                (kShadowQualityWidth / 2) * (kShadowQualityHeight / 2),
                meanOff, meanOn,
                shadow.HasDirectionalLight() ? 1 : 0,
                shadow.GetLastDrawCount(), shadow.GetLastCulledCount());
            outLog += line;

            if (acneOff < 500)
            {
                outLog += "끈 쪽에 여드름이 없다 — 조건이 약해 아무것도 재지 않았다\n";
                passed = false;
            }
            if (acneOn > acneOff / 20)
            {
                outLog += "경사 항이 그림자를 새로 만들었다 — 편향 방향이 뒤집혔다\n";
                passed = false;
            }
        }

        shadow.SetBias(0.0015f);   // 기본으로 되돌린다
    }

    // ── [3/4] 캐스케이드 경계 블렌딩 — 줄무늬 그림자 A/B ──
    //
    // 화면 왼쪽 밖 기둥들이 옆으로 눕는 빛에 줄무늬를 드리운다. 줄무늬가
    // 분할 경계를 가로지르므로 두 캐스케이드의 해상도 차이가 경계에서
    // 드러난다 — 블렌딩 켬/끔의 차이가 그 구간에 나타나야 한다.
    if (passed)
    {
        std::vector<EnhancedDrawItem> draws;
        {
            EnhancedDrawItem ground{};
            ground.mesh = &groundMesh;
            ground.worldMatrix = XMMatrixIdentity();
            draws.push_back(ground);

            for (int i = 0; i < 8; ++i)
            {
                EnhancedDrawItem blocker{};
                blocker.mesh = &blockerMesh;
                blocker.worldMatrix =
                    XMMatrixTranslation(-14.f, 0.f, 6.f + 3.f * static_cast<float>(i));
                draws.push_back(blocker);
            }
        }

        std::vector<EnhancedLight> lights(1);
        lights[0].position = Mathf::Vector4(0.f, 0.f, 0.f, 0.f);
        // direction은 빛이 나아가는 방향이다 — (+1,-0.45,0)이라야 왼쪽(-X)
        // 기둥의 그림자가 +X로 뻗어 화면을 가로지른다. 처음에 부호를 반대로
        // 뒀더니 줄무늬가 전부 화면 왼쪽 밖으로 나가 차이가 0이었다.
        lights[0].direction = Mathf::Vector4(
            Mathf::Vector3(XMVector3Normalize(XMVectorSet(1.f, -0.45f, 0.f, 0.f))));
        lights[0].color = Mathf::Color4(1.f, 1.f, 1.f, 5.f);

        const FrameCameraSnapshot camera = ShadowQualityCamera(
            { 0.f, 7.f, -3.f }, { 0.f, 0.f, 25.f },
            DirectX::XM_PI / 3.f, 0.1f, 200.f);

        ShadowQualityCapture blendOff{};
        ShadowQualityCapture blendOn{};
        if (!renderOnce(camera, draws, lights,
                [](EnhancedShadowData& data) { data.cascadeBlendBand = 0.f; }, blendOff) ||
            !renderOnce(camera, draws, lights, nullptr, blendOn))
        {
            passed = false;
        }
        else
        {
            // 줄무늬는 방향광이 통째로 꺼진 자리라 어둡다. 밝은 바닥이 1.1
            // 안팎이므로 0.3이면 넉넉히 가른다.
            const uint32_t stripes = blendOff.CountDarkCenter(0.3f);
            const uint32_t different = blendOff.CountDifferent(blendOn, 0.02f);
            const uint32_t total = kShadowQualityWidth * kShadowQualityHeight;

            char line[192]{};
            std::snprintf(line, sizeof(line),
                "[3/4] 경계 블렌딩 — 줄무늬(끔의 어두운 픽셀) %u · 켬/끔 차이 %u(%.1f%%)\n",
                stripes, different,
                100.f * static_cast<float>(different) / static_cast<float>(total));
            outLog += line;

            if (stripes < 100)
            {
                outLog += "줄무늬 그림자가 없다 — 경계 비교의 재료가 없다\n";
                passed = false;
            }
            if (0 == different)
            {
                outLog += "블렌딩 켬/끔이 같은 그림이다 — 블렌딩이 셰이더에 안 닿는다\n";
                passed = false;
            }
            if (different > total * 3 / 10)
            {
                outLog += "화면 3할 넘게 바뀌었다 — 블렌딩이 경계 구간을 벗어난다\n";
                passed = false;
            }
        }
    }

    // ── [4/4] 그래프·검증 레이어 ──
    if (passed)
    {
        char line[128]{};
        std::snprintf(line, sizeof(line),
            "[4/4] 그래프 — 선언 %u · 실행 %u · 컬링 %u\n",
            lastStats.passesDeclared, lastStats.passesExecuted, lastStats.passesCulled);
        outLog += line;

        if (lastStats.passesExecuted < 4 || 0 != lastStats.passesCulled)
        {
            outLog += "패스 수가 다르다 — 그림자·GBuffer·Deferred·리드백이 다 돌아야 한다\n";
            passed = false;
        }
    }

    std::string validation;
    const uint32_t problems = resources.DrainDebugMessages(validation);
    if (0 != problems)
    {
        passed = false;
        outLog += "검증 레이어 문제 " + std::to_string(problems) + "건\n" + validation;
    }

    deferred.Shutdown();
    gbuffer.Shutdown();
    shadow.Shutdown();
    textureCache.Shutdown();
    meshCache.Shutdown();
    rootSignatures.Shutdown();
    psoManager.Shutdown();
    resources.Shutdown();

    outLog += passed ? "그림자 품질 검증 통과\n" : "그림자 품질 검증 실패\n";
    return passed;
}

#endif
