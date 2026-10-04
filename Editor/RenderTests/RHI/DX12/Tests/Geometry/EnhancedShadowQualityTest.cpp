#include "Render/Passes/Geometry/EnhancedShadowPass.h"
#include "Render/Passes/Geometry/EnhancedGBufferPass.h"
#include "Render/Passes/Geometry/EnhancedDeferredPass.h"
#include "RHI/DX12/DX12DeviceResources.h"
#include "RHI/DX12/DX12PSOManager.h"
#include "RHI/DX12/DX12RootSignatureCache.h"
#include "RHI/DX12/DX12MeshCache.h"
#include "RHI/DX12/DX12TextureCache.h"
#include "Render/Graph/EnhancedRenderGraph.h"
#include "RHI/DX12/Tests/DX12SelfTest.h"
#include "Mesh.h"
// DeviceState.h include가 여기 있었다 (E, 2026-08-09).
// 이 파일에서 DirectX11:: 심볼을 쓰는 코드가 0이다.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <mathematics/transform.hpp>
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
//   ③ 가장자리 필터 — 같은 줄무늬를 필터 넷(하드웨어 2x2 · 텐트 3x3/5x5/7x7)
//      으로 그린다. 반그림자 픽셀은 필터가 넓을수록 '늘어야' 하고, 화면
//      평균은 '그대로'여야 한다(대칭 필터는 그림자를 옮기지 않는다).
//
//   ④ 넓은 필터의 여드름 — 스치는 빛의 맨바닥을 제품 기본 편향으로 그린다.
//      그림자를 끈 그림보다 어두운 픽셀은 전부 여드름이다. 넓은 필터는 먼
//      텍셀까지 읽으므로 법선 오프셋이 그 몫을 덮어야 0 이 된다.
//
// ①·② 는 하드웨어 2x2 로 고정한다 — 그 둘이 재는 경사 항과 블렌딩은 필터와
// 따로 있는 장치이고, 법선 오프셋이 끈 쪽의 여드름까지 덮으면 A/B 가 무너진다.
//
// 표본 자리를 못 박는 대신 수를 세는 이유: 여드름과 경계 어긋남은
// 텍셀 격자에 매인 패턴이라 한 점이 아니라 분포로 드러난다.
namespace
{
    constexpr uint32_t kShadowQualityWidth = 256;
    constexpr uint32_t kShadowQualityHeight = 256;

    // 픽셀 저장·디코드는 RHIReadbackImage가 한다. 여기 남는 것은 이 검사에만
    // 있는 통계뿐이다 — 여드름은 절대 밝기가 아니라 A/B 분포로만 드러난다.
    struct ShadowQualityCapture
    {
        RHIReadbackImage image;

        float At(uint32_t x, uint32_t y, uint32_t channel) const
        {
            return image.At(x, y, channel);
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

        // 중앙 영역에서 (lower, upper) 사이 픽셀 수 — 반그림자 폭의 대리값이다.
        uint32_t CountBetweenCenter(float lower, float upper) const
        {
            uint32_t between = 0;
            for (uint32_t y = kShadowQualityHeight / 4; y < kShadowQualityHeight * 3 / 4; ++y)
                for (uint32_t x = kShadowQualityWidth / 4; x < kShadowQualityWidth * 3 / 4; ++x)
                {
                    const float value = At(x, y, 1);
                    if (value > lower && value < upper) ++between;
                }
            return between;
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

    FrameCameraSnapshot ShadowQualityCamera(const math::vector3& eye,
        const math::vector3& at, float fovRadians, float nearZ, float farZ)
    {
        const math::vector3 up = math::vector3::unit_y();

        FrameCameraSnapshot snapshot{};
        snapshot.view = math::look_at_lh(eye, at, up);
        snapshot.projection = math::perspective_fov_lh(fovRadians, 1.f, nearZ, farZ);
        snapshot.inverseView = math::inverse(snapshot.view);
        snapshot.inverseProjection = math::inverse(snapshot.projection);
        snapshot.eyePosition = eye;
        snapshot.forward = math::normalize(at - eye);
        snapshot.right = math::normalize(math::cross(up, snapshot.forward));
        snapshot.up = math::cross(snapshot.forward, snapshot.right);
        snapshot.fov = math::degrees(fovRadians);
        snapshot.nearPlane = nearZ;
        snapshot.farPlane = farZ;
        snapshot.isOrthographic = false;
        return snapshot;
    }

    // 사각 평판 메시. 법선을 지정해 GBuffer 라이팅이 올바로 계산되게 한다.
    void ShadowQualityQuad(std::vector<Vertex>& outVertices,
        std::vector<uint32_t>& outIndices,
        const math::vector3& origin, const math::vector3& axisU,
        const math::vector3& axisV, const math::vector3& normal)
    {
        const uint32_t base = static_cast<uint32_t>(outVertices.size());

        const math::vector3 corners[4] = {
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

bool DX12Test::RunShadowQualityTest(std::string& outLog)
{
    outLog += "── 그림자 품질 검증 (경사 편향 · 경계 블렌딩 · 가장자리 필터) ──\n";

    std::string error;

    DX12DeviceResources resources;
    if (!resources.Initialize(kShadowQualityWidth, kShadowQualityHeight, error))
    {
        outLog += "[1/6] DX12 초기화 실패: " + error + "\n";
        return false;
    }

    DX12PSOManager psoManager;
    DX12RootSignatureCache rootSignatures;
    DX12MeshCache meshCache;
    DX12TextureCache textureCache;
    if (!psoManager.Initialize(&resources, L"dx12_shadowquality.cache", error) ||
        !rootSignatures.Initialize(&resources, error) ||
        !meshCache.Initialize(&resources, error) ||
        !textureCache.Initialize(&resources, error))
    {
        outLog += "[1/6] 캐시 초기화 실패: " + error + "\n";
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
        outLog += "[1/6] 패스 초기화 실패: " + error + "\n";
        resources.Shutdown();
        return false;
    }
    gbuffer.SetKeepAlive(false);
    outLog += "[1/6] 패스 3종 초기화 통과\n";

    RHIReadback readback{};
    if (!resources.CreateReadback(kShadowQualityWidth, kShadowQualityHeight,
        EnhancedDeferredPass::kOutputFormat, 1, readback, error))
    {
        outLog += "[1/6] 리드백 생성 실패: " + error + "\n";
        resources.Shutdown();
        return false;
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
        EnhancedRenderGraph graph(resources);

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
            { { output, RHIResourceState::CopySource } },
            [&](const EnhancedRenderGraph::ExecuteContext& executeContext)
            {
                executeContext.encoder->CopyToReadback( readback,
                    executeContext.ResolveHandle(output));
            }, true);

        if (!graph.Compile(error))
        {
            outLog += "Compile 실패: " + error + "\n";
            return false;
        }
        if (!graph.Execute(error))
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

        if (!resources.MapReadback(readback, outCapture.image, error))
        {
            outLog += "리드백 Map 실패: " + error + "\n";
            return false;
        }
        return true;
    };

    // ── 장면 둘 ──
    //
    // 스치는 빛의 맨바닥(②·⑤)과 기둥 줄무늬(③·④). 단계끼리 같은 장면을
    // 써야 서로 다른 장치를 같은 조건에서 잰다.
    std::vector<EnhancedDrawItem> grazingDraws(1);
    grazingDraws[0].mesh = &groundMesh;
    grazingDraws[0].worldMatrix = math::matrix4x4::identity();

    std::vector<EnhancedLight> grazingLights(1);
    grazingLights[0].position = math::vector4(0.f, 0.f, 0.f, 0.f);   // w=0 방향광
    const math::vector3 firstDirection = math::normalize(
        math::vector3{ 1.f, -0.18f, 0.f });
    grazingLights[0].direction = math::vector4{
        firstDirection.x, firstDirection.y, firstDirection.z, 0.f };
    grazingLights[0].color = math::color(1.f, 1.f, 1.f, 5.f);

    const FrameCameraSnapshot grazingCamera = ShadowQualityCamera(
        { 0.f, 25.f, -12.f }, { 0.f, 0.f, 6.f },
        math::pi / 3.f, 0.1f, 200.f);

    // 화면 왼쪽 밖 기둥들이 옆으로 눕는 빛에 줄무늬를 드리운다.
    std::vector<EnhancedDrawItem> stripeDraws;
    {
        EnhancedDrawItem ground{};
        ground.mesh = &groundMesh;
        ground.worldMatrix = math::matrix4x4::identity();
        stripeDraws.push_back(ground);

        for (int i = 0; i < 8; ++i)
        {
            EnhancedDrawItem blocker{};
            blocker.mesh = &blockerMesh;
            blocker.worldMatrix = math::translation_matrix(math::vector3{
                -14.f, 0.f, 6.f + 3.f * static_cast<float>(i) });
            stripeDraws.push_back(blocker);
        }
    }

    std::vector<EnhancedLight> stripeLights(1);
    stripeLights[0].position = math::vector4(0.f, 0.f, 0.f, 0.f);
    // direction은 빛이 나아가는 방향이다 — (+1,-0.45,0)이라야 왼쪽(-X)
    // 기둥의 그림자가 +X로 뻗어 화면을 가로지른다. 처음에 부호를 반대로
    // 뒀더니 줄무늬가 전부 화면 왼쪽 밖으로 나가 차이가 0이었다.
    const math::vector3 secondDirection = math::normalize(
        math::vector3{ 1.f, -0.45f, 0.f });
    stripeLights[0].direction = math::vector4{
        secondDirection.x, secondDirection.y, secondDirection.z, 0.f };
    stripeLights[0].color = math::color(1.f, 1.f, 1.f, 5.f);

    const FrameCameraSnapshot stripeCamera = ShadowQualityCamera(
        { 0.f, 7.f, -3.f }, { 0.f, 0.f, 25.f },
        math::pi / 3.f, 0.1f, 200.f);

    // ①·② 는 하드웨어 2x2 로 잰다(머리 주석 참고). 패스 기본값은 Tent5x5 다.
    shadow.SetFilter(EnhancedShadowFilter::Hardware2x2);

    // ── [2/6] 경사 비례 편향 — 스치는 빛의 맨바닥 A/B ──
    //
    // 바닥뿐이라 그림자를 드리울 것이 없다 — 어두운 픽셀은 전부 여드름이다.
    // 상수 편향을 일부러 작게 둬(0.00005) 끈 쪽이 실제로 실패하게 만든다.
    if (passed)
    {
        shadow.SetBias(0.00005f);

        ShadowQualityCapture slopeOff{};
        ShadowQualityCapture slopeOn{};
        if (!renderOnce(grazingCamera, grazingDraws, grazingLights,
                [](EnhancedShadowData& data) { data.bias.w = 0.f; }, slopeOff) ||
            !renderOnce(grazingCamera, grazingDraws, grazingLights, nullptr, slopeOn))
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
                "[2/6] 경사 편향 — 끔만 어두움 %u · 켬만 어두움 %u (중앙 %u픽셀) · "
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

        // 제품 기본(0.5 텍셀)으로 되돌린다. 예전의 SetBias(0.0015f)는 텍셀 단위
        // 전환 전의 값이라 6.1 텍셀이 되어, 뒤 단계가 기본이 아닌 편향으로 돌았다.
        shadow.SetBiasTexels(0.5f);
    }

    // ── [3/6] 캐스케이드 경계 블렌딩 — 줄무늬 그림자 A/B ──
    //
    // 줄무늬가 분할 경계를 가로지르므로 두 캐스케이드의 해상도 차이가 경계에서
    // 드러난다 — 블렌딩 켬/끔의 차이가 그 구간에 나타나야 한다.
    if (passed)
    {
        ShadowQualityCapture blendOff{};
        ShadowQualityCapture blendOn{};
        if (!renderOnce(stripeCamera, stripeDraws, stripeLights,
                [](EnhancedShadowData& data) { data.cascadeBlendBand = 0.f; }, blendOff) ||
            !renderOnce(stripeCamera, stripeDraws, stripeLights, nullptr, blendOn))
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
                "[3/6] 경계 블렌딩 — 줄무늬(끔의 어두운 픽셀) %u · 켬/끔 차이 %u(%.1f%%)\n",
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


    constexpr EnhancedShadowFilter kFilters[]{ EnhancedShadowFilter::Hardware2x2,
        EnhancedShadowFilter::Tent3x3, EnhancedShadowFilter::Tent5x5, EnhancedShadowFilter::Tent7x7 };
    constexpr const char* kFilterNames[]{ "2x2", "3x3", "5x5", "7x7" };
    constexpr size_t kFilterCount = std::size(kFilters);

    // ── [4/6] 가장자리 필터 — 같은 줄무늬를 필터 넷으로 ──
    //
    // 밝기 문턱은 하드웨어 2x2 그림의 중앙 최소·최대에서 잡는다. 그 사이
    // 1할~9할에 드는 픽셀이 반그림자다. 텐트 너비가 3·5·7 텍셀로 늘면 그
    // 수도 늘어야 하고, 대칭 필터라 넓은 영역의 평균은 그대로여야 한다.
    //
    // ③·④ 는 앞 판정의 붉음과 따로 돈다. 법선 오프셋을 0 으로 만든 변이가
    // ③ 의 평균 판정에서 먼저 잡혀 ④ 가 한 번도 돌지 않았다 — 한 변이를
    // 어느 절이 잡았는지 보려면 앞 단계가 뒤 단계를 가리면 안 된다. 그리기
    // 자체가 실패했을 때만 멈춘다.
    bool renderable = true;
    {
        std::array<ShadowQualityCapture, kFilterCount> captures{};
        for (size_t i = 0; i < kFilterCount && renderable; ++i)
        {
            shadow.SetFilter(kFilters[i]);
            renderable = renderOnce(stripeCamera, stripeDraws, stripeLights, nullptr, captures[i]);
        }
        if (renderable)
        {
            float dark = 0.f, unusedMean = 0.f, lit = 0.f;
            captures[0].CenterStats(dark, unusedMean, lit);
            const float contrast = lit - dark;
            const float lower = dark + 0.1f * contrast;
            const float upper = lit - 0.1f * contrast;

            std::array<uint32_t, kFilterCount> penumbra{};
            std::array<float, kFilterCount> means{};
            std::string line = "[4/6] 가장자리 필터 — 반그림자";
            for (size_t i = 0; i < kFilterCount; ++i)
            {
                penumbra[i] = captures[i].CountBetweenCenter(lower, upper);
                float minimum = 0.f, maximum = 0.f;
                captures[i].CenterStats(minimum, means[i], maximum);
                char item[96]{};
                std::snprintf(item, sizeof(item), " %s %u(평균 %.4f)", kFilterNames[i], penumbra[i], means[i]);
                line += item;
            }
            char tail[96]{};
            std::snprintf(tail, sizeof(tail), " · 그늘 %.3f 빛 %.3f\n", dark, lit);
            line += tail;
            outLog += line;

            if (contrast < 0.5f)
            {
                outLog += "줄무늬의 명암 차가 없다 — 반그림자를 셀 재료가 없다\n";
                passed = false;
            }
            for (size_t i = 1; i < kFilterCount; ++i)
            {
                // 텐트는 지지 폭이 하드웨어 2x2(1 텍셀)의 두 배 이상이다. 단조 증가만
                // 보면 법선 오프셋이 줄무늬를 조금 옮기는 것만으로도 통과해서,
                // 필터를 통째로 빼는 변이에서 3x3·5x5 가 초록으로 남았다.
                if (penumbra[i] <= penumbra[i - 1] || penumbra[i] * 2 < penumbra[0] * 3)
                {
                    outLog += std::string("반그림자가 ") + kFilterNames[i]
                        + " 에서 넓어지지 않았다(앞 필터보다 넓고 2x2 의 1.5배 이상이어야 한다)"
                          " — 필터가 셰이더에 안 닿거나 너비가 틀렸다\n";
                    passed = false;
                }
                // 대칭 필터는 그림자를 옮기지 않는다. 표본 자리가 한쪽으로 쏠리면
                // 줄무늬 전체가 밀려 평균이 움직인다.
                if (std::fabs(means[i] - means[0]) > 0.03f * contrast)
                {
                    outLog += std::string("평균이 명암 차의 3% 넘게 바뀌었다(") + kFilterNames[i]
                        + ") — 표본 자리가 한쪽으로 쏠렸다\n";
                    passed = false;
                }
            }
        }
    }

    // ── [5/6] 넓은 필터의 여드름 — 스치는 빛의 맨바닥, 제품 기본 편향 ──
    //
    // ② 와 같은 바닥·빛이지만 편향은 제품 기본(0.5 텍셀 · 경사 2)이다. 가릴
    // 것이 없으니 그림자를 끈 그림보다 어두운 픽셀은 전부 여드름이다. 넓은
    // 필터는 먼 텍셀까지 읽으므로 법선 오프셋이 그 몫을 덮지 못하면 여기서
    // 줄무늬가 선다. 숫자 잡음만 봐주도록 중앙의 0.1% 까지 둔다.
    if (renderable)
    {
        shadow.SetBiasTexels(0.5f);
        shadow.SetSlopeScale(2.f);

        ShadowQualityCapture unshadowed{};
        renderable = renderOnce(grazingCamera, grazingDraws, grazingLights,
            [](EnhancedShadowData& data) { data.enabled = false; }, unshadowed);

        const uint32_t tolerance = (kShadowQualityWidth / 2) * (kShadowQualityHeight / 2) / 1000;
        std::string line = "[5/6] 넓은 필터 여드름 — 그림자 끔보다 어두운 픽셀";
        std::string failures;
        for (size_t i = 0; i < kFilterCount && renderable; ++i)
        {
            shadow.SetFilter(kFilters[i]);
            ShadowQualityCapture capture{};
            if (!renderOnce(grazingCamera, grazingDraws, grazingLights, nullptr, capture))
            {
                renderable = false;
                break;
            }
            const uint32_t acne = capture.CountDarkerThanCenter(unshadowed, 0.05f);
            char item[48]{};
            std::snprintf(item, sizeof(item), " %s %u", kFilterNames[i], acne);
            line += item;
            if (acne > tolerance)
            {
                failures += std::string(kFilterNames[i]) + " 에서 맨바닥이 스스로를 가린다 — "
                    "넓은 필터의 편향(법선 오프셋)이 모자라다\n";
            }
        }
        char tail[48]{};
        std::snprintf(tail, sizeof(tail), " (허용 %u)\n", tolerance);
        outLog += line + tail + failures;
        if (!failures.empty()) passed = false;
    }
    if (!renderable) passed = false;

    // ── [6/6] 그래프·검증 레이어 ──
    if (passed)
    {
        char line[128]{};
        std::snprintf(line, sizeof(line),
            "[6/6] 그래프 — 선언 %u · 실행 %u · 컬링 %u\n",
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
