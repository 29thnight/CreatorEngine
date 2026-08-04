#ifndef DYNAMICCPP_EXPORTS
#include "EnhancedForwardPass.h"
#include "DX12DeviceResources.h"
#include "DX12PSOManager.h"
#include "DX12RootSignatureCache.h"
#include "EnhancedRenderGraph.h"
#include "EnhancedSceneRenderer.h"

#include <d3dcompiler.h>
#include <algorithm>
#include <sstream>
#include <vector>

#pragma comment(lib, "d3dcompiler.lib")

// 남은 단계(순서대로 채운다):
//   [v] 1. 광원 컬링 컴퓨트 — 타일 프러스텀 vs 광원 구, 타일 목록 쓰기
//   [v] 2. 컬링 자가 검증 — 타일 카운트 리드백, 알려진 배치로 단정
//   [ ] 3. 포워드 셰이딩 — forwardQueue 드로우 + 타일 목록 조회
//   [ ] 4. 참조 경로(전 광원 루프)와 픽셀 대조
//   [ ] 5. 광원 수 스케일링 실측 — Forward+가 이기는 경계 찾기
//
// 3이 없으므로 아직 씬 경로에 붙이면 안 된다. 지금 Declare는 컬링만 선언한다.

namespace
{
    // 유니티 빌드에서 익명 네임스페이스가 파일 간 합쳐지므로 이름을 고유하게 둔다.
    std::string FwdHrToString(HRESULT hr)
    {
        std::ostringstream oss;
        oss << "HRESULT 0x" << std::hex << static_cast<unsigned long>(hr);
        return oss.str();
    }

    // ── 광원 컬링 ──
    //
    // 스레드 그룹 하나가 타일 하나다(16x16 = 256스레드). 세 단계:
    //   ① 각 스레드가 자기 픽셀의 깊이를 읽어 그룹 공유 min/max에 모은다.
    //      깊이는 float이지만 InterlockedMin/Max는 uint만 받는다 — 양수
    //      float은 비트 패턴의 대소가 값의 대소와 같으므로 asuint로 겪는다.
    //   ② 타일 프러스텀을 만든다. 옆면 4개는 뷰 공간에서 원점을 지나는
    //      평면이라 타일 코너의 뷰 방향 둘의 외적이 곧 법선이다.
    //   ③ 광원을 256개씩 나눠 병렬로 검사한다. 통과하면 공유 카운터로
    //      슬롯을 받아 목록에 쓴다.
    //
    // 하늘만 있는 타일은 광원을 받지 않는다 — min/max가 비어 있으면 어떤
    // 표면도 없다는 뜻이고, 그 타일의 픽셀 셰이더는 어차피 그릴 것이 없다.
    constexpr const char* kCullShader = R"(
Texture2D<float>             gDepth  : register(t0);
StructuredBuffer<float4>     gLights : register(t1);   // EnhancedLight = float4 x4

RWStructuredBuffer<uint> gTileCount : register(u0);
RWStructuredBuffer<uint> gTileList  : register(u1);

cbuffer CullParams : register(b0)
{
    float4x4 gView;
    float4x4 gInverseProjection;
    uint2    gScreenSize;
    uint2    gTileGrid;
    uint     gLightCount;
    uint3    gPad;
};

groupshared uint sMinDepth;
groupshared uint sMaxDepth;
groupshared uint sCount;

float ViewZFromClip(float depth)
{
    const float4 view = mul(float4(0.0f, 0.0f, depth, 1.0f), gInverseProjection);
    return view.z / view.w;
}

// 타일 코너 uv의 뷰 공간 방향. 옆면 평면의 재료다.
float3 CornerRay(float2 uv)
{
    const float4 clip = float4(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f, 1.0f, 1.0f);
    const float4 view = mul(clip, gInverseProjection);
    return view.xyz / view.w;
}

[numthreads(TILE_SIZE, TILE_SIZE, 1)]
void CSMain(uint3 groupId : SV_GroupID, uint3 threadId : SV_GroupThreadID,
    uint groupIndex : SV_GroupIndex)
{
    if (0 == groupIndex)
    {
        sMinDepth = 0x7F7FFFFFu;   // +FLT_MAX의 비트
        sMaxDepth = 0u;
        sCount = 0u;
    }
    GroupMemoryBarrierWithGroupSync();

    // ① 타일 깊이 범위
    const uint2 pixel = groupId.xy * TILE_SIZE + threadId.xy;
    if (all(pixel < gScreenSize))
    {
        const float depth = gDepth.Load(int3(pixel, 0));
        if (depth < 1.0f)
        {
            const uint bits = asuint(depth);
            InterlockedMin(sMinDepth, bits);
            InterlockedMax(sMaxDepth, bits);
        }
    }
    GroupMemoryBarrierWithGroupSync();

    // 하늘만 있는 타일 — 아무 광원도 안 받는다.
    const bool emptyTile = (0u == sMaxDepth);

    const float minViewZ = emptyTile ? 0.0f : ViewZFromClip(asfloat(sMinDepth));
    const float maxViewZ = emptyTile ? 0.0f : ViewZFromClip(asfloat(sMaxDepth));

    // ② 타일 옆면 4개. 화면 경계로 자른다 — 가장자리 타일이 화면 밖까지
    //    걸치면 이웃에 없는 광원까지 줍는다.
    const float2 tileMin = float2(groupId.xy * TILE_SIZE) / float2(gScreenSize);
    const float2 tileMax = min(float2((groupId.xy + 1) * TILE_SIZE), float2(gScreenSize))
        / float2(gScreenSize);

    const float3 rayTL = CornerRay(float2(tileMin.x, tileMin.y));
    const float3 rayTR = CornerRay(float2(tileMax.x, tileMin.y));
    const float3 rayBL = CornerRay(float2(tileMin.x, tileMax.y));
    const float3 rayBR = CornerRay(float2(tileMax.x, tileMax.y));

    // 안쪽을 향하는 법선. dot(n, p) >= -r 이면 광원 구가 평면 안쪽에 걸친다.
    //
    // ★ 외적 순서를 한 번 틀렸었다. cross(BL, TL)로 만든 왼쪽 법선을 손으로
    //   계산하면 (-X, -Z) — 바깥쪽이고, 그러면 중앙 광원조차 dot이 음수가
    //   되어 모든 타일이 0이 된다(실제로 그랬다: 켜진 타일 0/64).
    //   LH 뷰 공간에서 안쪽 법선이 나오는 순서로 고정한다.
    const float3 planeLeft   = normalize(cross(rayTL, rayBL));
    const float3 planeRight  = normalize(cross(rayBR, rayTR));
    const float3 planeTop    = normalize(cross(rayTR, rayTL));
    const float3 planeBottom = normalize(cross(rayBL, rayBR));

    // ③ 광원 병렬 검사
    const uint tileIndex = groupId.y * gTileGrid.x + groupId.x;

    for (uint lightIndex = groupIndex; lightIndex < gLightCount;
        lightIndex += TILE_SIZE * TILE_SIZE)
    {
        const float4 position = gLights[lightIndex * 4 + 0];
        const float4 attenuation = gLights[lightIndex * 4 + 3];

        bool inside;
        if (position.w < 0.5f)
        {
            // 방향광은 모든 타일에 닿는다.
            inside = !emptyTile;
        }
        else
        {
            const float3 viewPos = mul(float4(position.xyz, 1.0f), gView).xyz;
            const float radius = attenuation.w;

            inside = !emptyTile
                && (viewPos.z + radius >= minViewZ)
                && (viewPos.z - radius <= maxViewZ)
                && (dot(planeLeft, viewPos) >= -radius)
                && (dot(planeRight, viewPos) >= -radius)
                && (dot(planeTop, viewPos) >= -radius)
                && (dot(planeBottom, viewPos) >= -radius);
        }

        if (inside)
        {
            uint slot;
            InterlockedAdd(sCount, 1u, slot);
            if (slot < MAX_LIGHTS_PER_TILE)
            {
                gTileList[tileIndex * MAX_LIGHTS_PER_TILE + slot] = lightIndex;
            }
        }
    }
    GroupMemoryBarrierWithGroupSync();

    if (0 == groupIndex)
    {
        // 넘친 타일은 상한으로 자른다. 잘렸다는 사실은 카운트가 상한과
        // 같은 것으로 읽는 쪽이 알 수 있다(정확한 초과량은 담지 않는다 —
        // 그것까지 담으면 버퍼가 하나 더 필요한데, '잘렸는가'만 알면 된다).
        gTileCount[tileIndex] = min(sCount, MAX_LIGHTS_PER_TILE);
    }
}
)";

    struct CullParams
    {
        Mathf::Matrix view{};
        Mathf::Matrix inverseProjection{};
        uint32_t      screenWidth{ 0 };
        uint32_t      screenHeight{ 0 };
        uint32_t      tileGridX{ 0 };
        uint32_t      tileGridY{ 0 };
        uint32_t      lightCount{ 0 };
        uint32_t      pad[3]{};
    };

    bool CompileFwdShader(const char* source, const D3D_SHADER_MACRO* defines,
        Microsoft::WRL::ComPtr<ID3DBlob>& outBlob, std::string& outError)
    {
        Microsoft::WRL::ComPtr<ID3DBlob> errors;
        const HRESULT hr = D3DCompile(source, strlen(source), nullptr, defines, nullptr,
            "CSMain", "cs_5_0", 0, 0, &outBlob, &errors);
        if (FAILED(hr))
        {
            outError = "Forward+ 셰이더 컴파일 실패: ";
            if (errors) outError += static_cast<const char*>(errors->GetBufferPointer());
            else        outError += FwdHrToString(hr);
            return false;
        }
        return true;
    }
}

bool EnhancedForwardPass::Initialize(const EnhancedFrameContext& context, std::string& outError)
{
    if (nullptr == context.resources || nullptr == context.psoManager ||
        nullptr == context.rootSignatures)
    {
        outError = "Forward+ 패스 컨텍스트가 불완전하다";
        return false;
    }

    return CreatePipelines(context, outError);
}

bool EnhancedForwardPass::CreatePipelines(const EnhancedFrameContext& context, std::string& outError)
{
    // 컬링 루트 시그니처: b0 상수 · t0 깊이(테이블) · t1 광원(루트 SRV)
    // · u0/u1 타일 버퍼(테이블).
    D3D12_DESCRIPTOR_RANGE srvRange{};
    srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors = 1;
    srvRange.BaseShaderRegister = 0;

    D3D12_DESCRIPTOR_RANGE uavRange{};
    uavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    uavRange.NumDescriptors = 2;
    uavRange.BaseShaderRegister = 0;

    D3D12_ROOT_PARAMETER params[4]{};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[0].Descriptor.ShaderRegister = 0;

    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].DescriptorTable.NumDescriptorRanges = 1;
    params[1].DescriptorTable.pDescriptorRanges = &srvRange;

    // 광원 배열은 루트 SRV — 업로드 링 조각의 GPU 주소를 그대로 꽂는다.
    params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    params[2].Descriptor.ShaderRegister = 1;

    params[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[3].DescriptorTable.NumDescriptorRanges = 1;
    params[3].DescriptorTable.pDescriptorRanges = &uavRange;

    D3D12_ROOT_SIGNATURE_DESC rootDesc{};
    rootDesc.NumParameters = _countof(params);
    rootDesc.pParameters = params;

    const auto root = context.rootSignatures->GetOrCreate(rootDesc, outError);
    if (!root.IsValid()) return false;
    m_cullRootSignature = root.signature;

    const std::string tileSize = std::to_string(kTileSize);
    const std::string maxLights = std::to_string(kMaxLightsPerTile);
    const D3D_SHADER_MACRO defines[] = {
        { "TILE_SIZE", tileSize.c_str() },
        { "MAX_LIGHTS_PER_TILE", maxLights.c_str() },
        { nullptr, nullptr }
    };

    ComPtr<ID3DBlob> blob;
    if (!CompileFwdShader(kCullShader, defines, blob, outError)) return false;

    DX12ComputePipelineDesc desc{};
    desc.csBytecode = blob->GetBufferPointer();
    desc.csSize = blob->GetBufferSize();
    desc.rootSignature = root.signature;
    desc.rootSignatureId = root.id;

    m_cullPSO = context.psoManager->GetOrCreateCompute(desc, outError);
    return nullptr != m_cullPSO;
}

bool EnhancedForwardPass::EnsureTileBuffers(const EnhancedFrameContext& context,
    std::string& outError)
{
    const uint32_t tileTotal = m_tileCountX * m_tileCountY;
    if (0 == tileTotal) return true;

    // 크기가 그대로면 다시 만들지 않는다(SSGI 히스토리와 같은 계약).
    if (nullptr != m_tileCountBuffer)
    {
        if (m_allocatedTiles >= tileTotal) return true;
    }

    auto* device = context.resources->GetDevice();

    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    // 버퍼는 COMMON으로 만들어지고 첫 사용에서 승격된다 — UAV를 초기
    // 상태로 주면 검증 레이어가 '무시한다'고 경고만 남긴다.
    desc.Width = static_cast<uint64_t>(tileTotal) * sizeof(uint32_t);
    HRESULT hr = device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&m_tileCountBuffer));
    if (FAILED(hr))
    {
        outError = "타일 카운트 버퍼 생성 실패 " + FwdHrToString(hr);
        return false;
    }

    desc.Width = static_cast<uint64_t>(tileTotal) * kMaxLightsPerTile * sizeof(uint32_t);
    hr = device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&m_tileListBuffer));
    if (FAILED(hr))
    {
        outError = "타일 목록 버퍼 생성 실패 " + FwdHrToString(hr);
        return false;
    }

    m_allocatedTiles = tileTotal;
    return true;
}

bool EnhancedForwardPass::PrepareFrame(const EnhancedFrameContext& context, std::string& outError)
{
    // 타일 수. 화면이 타일 크기로 나누어떨어지지 않으면 가장자리 타일이
    // 화면 밖까지 걸치는데, 컬링 셰이더가 화면 경계로 자른다.
    m_tileCountX = (context.width + kTileSize - 1) / kTileSize;
    m_tileCountY = (context.height + kTileSize - 1) / kTileSize;

    return EnsureTileBuffers(context, outError);
}

void EnhancedForwardPass::Declare(EnhancedRenderGraph& graph, const EnhancedFrameContext& context)
{
    m_output = RGHandle{};

    if (!m_inputs.depth.IsValid() || nullptr == m_cullPSO ||
        nullptr == m_tileCountBuffer || nullptr == context.lights)
    {
        return;
    }

    // ── 광원 컬링 ──
    //
    // 그래프는 텍스처만 다루므로 타일 버퍼는 패스가 소유하고(영속),
    // 상태 전이도 패스가 책임진다. 지금은 UAV로 고정 — 셰이딩(3단계)이
    // 붙으면 SRV 전이가 여기 들어온다.
    graph.AddPass("Forward+.Cull",
        { { m_inputs.depth, RGResourceState::ShaderResource } },
        [this, &context](const EnhancedRenderGraph::ExecuteContext& executeContext)
        {
            auto* commandList = executeContext.commandList;
            auto* device = context.resources->GetDevice();

            const uint32_t lightCount =
                static_cast<uint32_t>(context.lights->size());

            // 광원 배열 업로드. EnhancedLight가 float4 넷이므로 그대로 복사한다.
            // 0개여도 최소 한 칸은 할당한다 — 빈 할당은 링이 거절한다.
            const uint64_t lightBytes =
                static_cast<uint64_t>((0 == lightCount) ? 1 : lightCount)
                * sizeof(EnhancedLight);
            const auto lightUpload = context.resources->GetUploadRing().Allocate(
                lightBytes, 16);
            if (!lightUpload.IsValid()) return;
            if (0 != lightCount)
            {
                memcpy(lightUpload.cpuAddress, context.lights->data(),
                    static_cast<size_t>(lightCount) * sizeof(EnhancedLight));
            }

            CullParams params{};
            if (nullptr != context.camera)
            {
                params.view = XMMatrixTranspose(context.camera->view);
                params.inverseProjection = XMMatrixTranspose(
                    XMMatrixInverse(nullptr, context.camera->projection));
            }
            params.screenWidth = context.width;
            params.screenHeight = context.height;
            params.tileGridX = m_tileCountX;
            params.tileGridY = m_tileCountY;
            params.lightCount = lightCount;

            const auto cb = context.resources->GetUploadRing().Allocate(
                sizeof(CullParams), DX12UploadRing::kConstantBufferAlignment);
            if (!cb.IsValid()) return;
            memcpy(cb.cpuAddress, &params, sizeof(params));

            const auto srvTable = context.resources->GetDescriptorRing().Allocate(1);
            const auto uavTable = context.resources->GetDescriptorRing().Allocate(2);
            if (!srvTable.IsValid() || !uavTable.IsValid()) return;

            D3D12_SHADER_RESOURCE_VIEW_DESC depthSrv{};
            depthSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            depthSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            depthSrv.Texture2D.MipLevels = 1;
            depthSrv.Format = DXGI_FORMAT_R32_FLOAT;
            device->CreateShaderResourceView(
                executeContext.Resolve(m_inputs.depth), &depthSrv, srvTable.CpuAt(0));

            const uint32_t tileTotal = m_tileCountX * m_tileCountY;

            D3D12_UNORDERED_ACCESS_VIEW_DESC countUav{};
            countUav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
            countUav.Format = DXGI_FORMAT_UNKNOWN;
            countUav.Buffer.NumElements = tileTotal;
            countUav.Buffer.StructureByteStride = sizeof(uint32_t);
            device->CreateUnorderedAccessView(m_tileCountBuffer.Get(), nullptr,
                &countUav, uavTable.CpuAt(0));

            D3D12_UNORDERED_ACCESS_VIEW_DESC listUav = countUav;
            listUav.Buffer.NumElements = tileTotal * kMaxLightsPerTile;
            device->CreateUnorderedAccessView(m_tileListBuffer.Get(), nullptr,
                &listUav, uavTable.CpuAt(1));

            ID3D12DescriptorHeap* heaps[] = {
                context.resources->GetDescriptorRing().GetHeap() };
            commandList->SetDescriptorHeaps(1, heaps);

            commandList->SetComputeRootSignature(m_cullRootSignature);
            commandList->SetPipelineState(m_cullPSO);
            commandList->SetComputeRootConstantBufferView(0, cb.gpuAddress);
            commandList->SetComputeRootDescriptorTable(1, srvTable.gpu);
            commandList->SetComputeRootShaderResourceView(2, lightUpload.gpuAddress);
            commandList->SetComputeRootDescriptorTable(3, uavTable.gpu);

            commandList->Dispatch(m_tileCountX, m_tileCountY, 1);

            // 다음 소비자(셰이딩·리드백)가 결과를 보기 전에 쓰기가 끝나야 한다.
            D3D12_RESOURCE_BARRIER uavBarriers[2]{};
            uavBarriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
            uavBarriers[0].UAV.pResource = m_tileCountBuffer.Get();
            uavBarriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
            uavBarriers[1].UAV.pResource = m_tileListBuffer.Get();
            commandList->ResourceBarrier(2, uavBarriers);
        },
        // 결과(타일 버퍼)가 그래프 밖 리소스라 컬링이 이 패스를 못 본다 —
        // 뿌리로 표시해야 걷어내지지 않는다. 셰이딩이 붙으면 그쪽이 뿌리가
        // 되고 이 표시는 뗀다.
        true);
}

void EnhancedForwardPass::Shutdown()
{
    m_tileCountBuffer.Reset();
    m_tileListBuffer.Reset();
    m_allocatedTiles = 0;
    m_tileCountX = 0;
    m_tileCountY = 0;
    m_lastCulledLights = 0;
    m_lastOverflowTiles = 0;

    m_cullPSO = nullptr;
    m_shadePSO = nullptr;
    m_cullRootSignature = nullptr;
    m_shadeRootSignature = nullptr;
}

// ── 자가 검증 ──
//
// 알려진 배치로 컬링을 단정한다. 광원 하나를 화면 중앙 표면 위에 두면
// 중앙 타일의 카운트는 1, 구석 타일은 0이어야 한다. 이것이 틀리는 방향이
// 둘 다 위험하다 — 중앙이 0이면 광원이 사라지고(어두워짐), 구석이 1이면
// 컬링이 안 도는 것이다(Forward+의 이득이 통째로 사라진다).
bool EnhancedSceneRenderer::RunForwardPlusTest(std::string& outLog)
{
    using Microsoft::WRL::ComPtr;

    constexpr uint32_t kWidth = 128;
    constexpr uint32_t kHeight = 128;

    outLog += "── Forward+ 검증 (PHASE 3-6) ──\n";

    std::string error;

    DX12DeviceResources resources;
    if (!resources.Initialize(kWidth, kHeight, error))
    {
        outLog += "[1/3] DX12 초기화 실패: " + error + "\n";
        return false;
    }

    DX12PSOManager psoManager;
    DX12RootSignatureCache rootSignatures;
    if (!psoManager.Initialize(resources.GetDevice(), L"dx12_fwd.cache", error) ||
        !rootSignatures.Initialize(resources.GetDevice(), error))
    {
        outLog += "[1/3] 캐시 초기화 실패: " + error + "\n";
        resources.Shutdown();
        return false;
    }

    // 카메라: 원점에서 +Z를 본다. 깊이 0.6의 표면이 화면 전체에 깔린다.
    FrameCameraSnapshot camera{};
    camera.view = XMMatrixIdentity();
    camera.projection = XMMatrixPerspectiveFovLH(XM_PIDIV2, 1.f, 0.1f, 100.f);
    camera.inverseView = XMMatrixIdentity();
    camera.inverseProjection = XMMatrixInverse(nullptr, camera.projection);

    // 광원: 깊이 0.6이 뷰 z ≈ 0.25이므로 그 근처 화면 중앙에 반경 0.05짜리
    // 점광 하나. 중앙 타일에는 닿고 구석에는 절대 닿지 않는 크기다.
    const float surfaceViewZ = 0.25f;
    std::vector<EnhancedLight> lights(1);
    lights[0].position = Mathf::Vector4(0.f, 0.f, surfaceViewZ, 1.f);   // 점광
    lights[0].attenuation = Mathf::Vector4(1.f, 0.f, 0.f, 0.05f);       // 반경

    EnhancedFrameContext frameContext{};
    frameContext.resources = &resources;
    frameContext.psoManager = &psoManager;
    frameContext.rootSignatures = &rootSignatures;
    frameContext.width = kWidth;
    frameContext.height = kHeight;
    frameContext.camera = &camera;
    frameContext.lights = &lights;

    EnhancedForwardPass forward;
    if (!forward.Initialize(frameContext, error))
    {
        outLog += "[1/3] Forward+ 초기화 실패: " + error + "\n";
        resources.Shutdown();
        return false;
    }
    outLog += "[1/3] 컬링 셰이더 컴파일·PSO 생성 통과\n";

    bool passed = true;
    {
        // 깊이 텍스처를 0.6으로 채운다. ClearUnorderedAccessView가 제일
        // 짧지만 디스크립터 두 벌(셰이더 가시 + 비가시)이 필요해 오히려
        // 길다 — 업로드 텍스처 복사로 채운다.
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_RESOURCE_DESC depthDesc{};
        depthDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        depthDesc.Width = kWidth;
        depthDesc.Height = kHeight;
        depthDesc.DepthOrArraySize = 1;
        depthDesc.MipLevels = 1;
        depthDesc.Format = DXGI_FORMAT_R32_FLOAT;
        depthDesc.SampleDesc.Count = 1;

        ComPtr<ID3D12Resource> depth;
        if (FAILED(resources.GetDevice()->CreateCommittedResource(&heap,
            D3D12_HEAP_FLAG_NONE, &depthDesc, D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr, IID_PPV_ARGS(&depth))))
        {
            outLog += "[2/3] 깊이 생성 실패\n";
            forward.Shutdown();
            resources.Shutdown();
            return false;
        }

        if (!resources.BeginFrame(error))
        {
            outLog += "[2/3] BeginFrame 실패: " + error + "\n";
            forward.Shutdown();
            resources.Shutdown();
            return false;
        }

        // 깊이 채우기: 업로드 링에서 행 정렬 규칙에 맞춰 복사한다.
        {
            const uint32_t rowPitch =
                ((kWidth * 4u) + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1u)
                & ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1u);
            const auto upload = resources.GetUploadRing().Allocate(
                static_cast<uint64_t>(rowPitch) * kHeight, 512);
            if (upload.IsValid())
            {
                for (uint32_t y = 0; y < kHeight; ++y)
                {
                    auto* row = reinterpret_cast<float*>(
                        static_cast<uint8_t*>(upload.cpuAddress)
                        + static_cast<size_t>(y) * rowPitch);
                    for (uint32_t x = 0; x < kWidth; ++x) row[x] = 0.6f;
                }

                D3D12_TEXTURE_COPY_LOCATION dst{};
                dst.pResource = depth.Get();
                dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;

                D3D12_TEXTURE_COPY_LOCATION src{};
                src.pResource = upload.resource;
                src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                src.PlacedFootprint.Offset = upload.offset;
                src.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R32_FLOAT;
                src.PlacedFootprint.Footprint.Width = kWidth;
                src.PlacedFootprint.Footprint.Height = kHeight;
                src.PlacedFootprint.Footprint.Depth = 1;
                src.PlacedFootprint.Footprint.RowPitch = rowPitch;

                resources.GetCommandList()->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

                D3D12_RESOURCE_BARRIER toSrv{};
                toSrv.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                toSrv.Transition.pResource = depth.Get();
                toSrv.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
                toSrv.Transition.StateAfter =
                    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
                toSrv.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                resources.GetCommandList()->ResourceBarrier(1, &toSrv);
            }
        }

        if (!forward.PrepareFrame(frameContext, error))
        {
            outLog += "[2/3] PrepareFrame 실패: " + error + "\n";
            passed = false;
        }

        EnhancedRenderGraph graph;

        EnhancedForwardPass::Inputs inputs{};
        inputs.depth = graph.ImportTexture(depth.Get(),
            RGResourceState::ShaderResource, "Fwd.TestDepth");
        forward.SetInputs(inputs);

        forward.Declare(graph, frameContext);

        // 타일 카운트 리드백.
        const uint32_t tileGrid = kWidth / EnhancedForwardPass::kTileSize;   // 8
        const uint32_t tileTotal = tileGrid * tileGrid;

        D3D12_HEAP_PROPERTIES readbackHeap{};
        readbackHeap.Type = D3D12_HEAP_TYPE_READBACK;

        D3D12_RESOURCE_DESC readbackDesc{};
        readbackDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        readbackDesc.Width = tileTotal * sizeof(uint32_t);
        readbackDesc.Height = 1;
        readbackDesc.DepthOrArraySize = 1;
        readbackDesc.MipLevels = 1;
        readbackDesc.SampleDesc.Count = 1;
        readbackDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        ComPtr<ID3D12Resource> readback;
        if (FAILED(resources.GetDevice()->CreateCommittedResource(&readbackHeap,
            D3D12_HEAP_FLAG_NONE, &readbackDesc, D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr, IID_PPV_ARGS(&readback))))
        {
            outLog += "[2/3] 리드백 생성 실패\n";
            passed = false;
        }

        if (passed)
        {
            graph.AddPass("Fwd.Readback",
                { { inputs.depth, RGResourceState::ShaderResource } },
                [&](const EnhancedRenderGraph::ExecuteContext& executeContext)
                {
                    (void)executeContext;
                    auto* commandList = executeContext.commandList;

                    // 타일 버퍼는 그래프 밖 리소스라 전이도 여기서 직접 한다.
                    D3D12_RESOURCE_BARRIER toCopy{};
                    toCopy.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                    toCopy.Transition.pResource = forward.GetTileCountBuffer();
                    toCopy.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
                    toCopy.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
                    toCopy.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                    commandList->ResourceBarrier(1, &toCopy);

                    commandList->CopyBufferRegion(readback.Get(), 0,
                        forward.GetTileCountBuffer(), 0,
                        tileTotal * sizeof(uint32_t));

                    D3D12_RESOURCE_BARRIER back = toCopy;
                    back.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
                    back.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
                    commandList->ResourceBarrier(1, &back);
                }, true);

            if (!graph.Compile(resources.GetDevice(), error))
            {
                outLog += "[2/3] Compile 실패: " + error + "\n";
                passed = false;
            }
            if (passed && !graph.Execute(resources.GetCommandList(), error))
            {
                outLog += "[2/3] Execute 실패: " + error + "\n";
                passed = false;
            }
        }

        if (!resources.EndFrame(error))
        {
            outLog += "[2/3] EndFrame 실패: " + error + "\n";
            passed = false;
        }
        else
        {
            resources.WaitForGpu();
        }

        // ── 단정 ──
        if (passed)
        {
            void* mapped = nullptr;
            D3D12_RANGE range{ 0, tileTotal * sizeof(uint32_t) };
            if (FAILED(readback->Map(0, &range, &mapped)))
            {
                outLog += "[3/3] 리드백 Map 실패\n";
                passed = false;
            }
            else
            {
                const auto* counts = static_cast<const uint32_t*>(mapped);

                const uint32_t centerTile = (tileGrid / 2) * tileGrid + (tileGrid / 2);
                const uint32_t cornerTile = 0;

                uint32_t litTiles = 0;
                for (uint32_t i = 0; i < tileTotal; ++i)
                {
                    if (0 != counts[i]) ++litTiles;
                }

                char line[224]{};
                std::snprintf(line, sizeof(line),
                    "[3/3] 타일 %ux%u — 중앙 %u · 구석 %u · 켜진 타일 %u/%u\n",
                    tileGrid, tileGrid, counts[centerTile], counts[cornerTile],
                    litTiles, tileTotal);
                outLog += line;

                // 중앙이 0이면 광원이 사라진다(어두워짐). 구석이 1이면 컬링이
                // 안 도는 것이다(전 타일에 다 들어가 Forward+의 이득이 없다).
                if (0 == counts[centerTile])
                {
                    outLog += "중앙 타일에 광원이 없다 — 컬링이 광원을 떨어뜨렸다\n";
                    passed = false;
                }
                if (0 != counts[cornerTile])
                {
                    outLog += "구석 타일에 광원이 있다 — 컬링이 자르지 않는다\n";
                    passed = false;
                }
                if (litTiles == tileTotal)
                {
                    outLog += "모든 타일이 켜졌다 — 반경 컬링이 죽었다\n";
                    passed = false;
                }

                readback->Unmap(0, nullptr);
            }
        }
    }

    std::string validation;
    const uint32_t problems = resources.DrainDebugMessages(validation);
    if (0 != problems)
    {
        passed = false;
        outLog += "검증 레이어 문제 " + std::to_string(problems) + "건\n" + validation;
    }

    forward.Shutdown();
    rootSignatures.Shutdown();
    psoManager.Shutdown();
    resources.Shutdown();

    outLog += passed ? "Forward+ 검증 통과\n" : "Forward+ 검증 실패\n";
    return passed;
}

#endif
