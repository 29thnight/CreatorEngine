#ifndef DYNAMICCPP_EXPORTS
#include "EnhancedSSGIPass.h"
#include "DX12DeviceResources.h"
#include "DX12PSOManager.h"
#include "DX12RootSignatureCache.h"
#include "EnhancedRenderGraph.h"
#include "EnhancedSceneRenderer.h"
#include "EnhancedSSGIShaders.h"

#include <d3dcompiler.h>
#include <algorithm>
#include <sstream>

#pragma comment(lib, "d3dcompiler.lib")

// 남은 단계(순서대로 채운다):
//   [v] 1. Hi-Z 피라미드 빌드 — 깊이 밉을 min으로 줄여 간다
//   [v] 2. 행진(트레이스) — Hi-Z를 타고 1/2 해상도로
//   [v] 3. 리졸브 — 지난 프레임을 재투영해 누적
//   [v] 4. 필터 — bilateral 한 번
//   [v] 5. 합성 — 업샘플 + 라이팅에 더하기
//
// 다섯 단계가 다 돈다(dx12.ssgi: 선언 14 · 실행 14 · 배리어 26).
//
// 아직 남은 것:
//   - 실제 씬에 붙이지 않았다. 검증은 합성 깊이로 도는 것만 확인했고,
//     GBuffer·라이팅을 물려 그림이 맞는지는 보지 않았다.
//   - 기존 DX11 SSGI와 시간을 대조하지 않았다. '아낀다'는 아직 추정이다.
//   - 상수 셋(누적 허용 깊이차 0.01, 필터 시그마 0.01·노멀 지수 16,
//     추적 거리 8·두께 0.5)이 전부 눈대중이다. 실측으로 조여야 한다.

namespace
{
    // 유니티 빌드에서 익명 네임스페이스가 파일 간 합쳐지므로 이름을 고유하게 둔다.
    std::string SsgiHrToString(HRESULT hr)
    {
        std::ostringstream oss;
        oss << "HRESULT 0x" << std::hex << static_cast<unsigned long>(hr);
        return oss.str();
    }

    // ── Hi-Z 빌드 ──
    //
    // 깊이 한 밉에서 다음 밉을 만든다. 2x2를 min으로 줄인다 — 화면 공간
    // 행진에서 '이 사각형 안에 가장 가까운 표면이 어디인가'를 물으므로
    // 최솟값이라야 건너뛰어도 안전하다. 평균이나 최댓값으로 줄이면 실제로는
    // 막혀 있는 구간을 비어 있다고 판단해 광선이 물체를 통과한다.
    //
    // 홀수 크기에서 한 텍셀이 빠지는 것을 막으려고 오른쪽·아래를 한 번 더
    // 본다. 빠뜨리면 그 줄만 낡은 값이 남고, 증상이 '가끔 광선이 샌다'라서
    // 찾기 어렵다.
    constexpr const char* kHiZBuildShader = R"(
Texture2D<float>   gSource : register(t0);
RWTexture2D<float> gTarget : register(u0);

cbuffer HiZParams : register(b0)
{
    uint2 gTargetSize;
    uint2 gSourceSize;
};

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= gTargetSize.x || id.y >= gTargetSize.y) return;

    const uint2 base = id.xy * 2;

    float depth = gSource.Load(int3(min(base, gSourceSize - 1), 0));
    depth = min(depth, gSource.Load(int3(min(base + uint2(1, 0), gSourceSize - 1), 0)));
    depth = min(depth, gSource.Load(int3(min(base + uint2(0, 1), gSourceSize - 1), 0)));
    depth = min(depth, gSource.Load(int3(min(base + uint2(1, 1), gSourceSize - 1), 0)));

    // 소스가 홀수면 마지막 열·행이 2x2에 안 들어온다. 한 텍셀 더 본다.
    if (base.x + 2 == gSourceSize.x - 1)
    {
        depth = min(depth, gSource.Load(int3(base.x + 2, min(base.y, gSourceSize.y - 1), 0)));
    }
    if (base.y + 2 == gSourceSize.y - 1)
    {
        depth = min(depth, gSource.Load(int3(min(base.x, gSourceSize.x - 1), base.y + 2, 0)));
    }

    gTarget[id.xy] = depth;
}
)";

    // ── 트레이스 ──
    //
    // 화면 공간 행진을 Hi-Z 위에서 한다. 기존 방식과 다른 점이 여기다:
    // 균등 스텝은 빈 공간도 물체가 빽빽한 곳과 같은 값을 치르는데, 화면
    // 공간에서 그 빈 구간이 대부분이다. Hi-Z는 거친 밉에서 '이 큰 사각형이
    // 통째로 비었나'를 한 번에 물어 건너뛴다.
    //
    // 알고리즘:
    //   거친 밉에서 시작 → 셀 경계까지 전진 → 그 셀의 최소 깊이보다
    //   광선이 앞이면 비었다는 뜻이니 밉을 올려 더 크게 건넌다.
    //   광선이 뒤로 가면 무언가 있다는 뜻이니 밉을 내려 정밀하게 본다.
    //   밉 0에서 교차하면 그것이 히트다.
    //
    // 프레임당 슬라이스는 적게(kSlicesPerFrame) 쓰고 프레임마다 방향을
    // 돌린다. 나머지는 시간축이 맡는다 — 그것이 이 설계의 전제이고,
    // 리졸브(3단계)가 붙기 전까지는 노이즈가 그대로 보인다.
    constexpr const char* kTraceShader = R"(
Texture2D<float>  gHiZ0 : register(t0);
Texture2D<float>  gHiZ1 : register(t1);
Texture2D<float>  gHiZ2 : register(t2);
Texture2D<float>  gHiZ3 : register(t3);
Texture2D<float>  gHiZ4 : register(t4);
Texture2D<float>  gHiZ5 : register(t5);
Texture2D<float>  gHiZ6 : register(t6);
Texture2D<float>  gHiZ7 : register(t7);
Texture2D<float4> gNormal   : register(t8);
Texture2D<float4> gLighting : register(t9);

RWTexture2D<float4> gOutput : register(u0);

cbuffer TraceParams : register(b0)
{
    float4x4 gInverseProjection;
    float4x4 gProjection;
    uint2    gOutputSize;
    uint2    gDepthSize;
    uint     gMipCount;
    uint     gFrameIndex;
    float    gMaxDistance;      // 뷰 공간 최대 추적 거리
    float    gThickness;        // 표면 두께 가정(뷰 공간)
};

static const float kPI = 3.14159265f;

float LoadHiZ(uint mip, int2 coord)
{
    // 밉마다 다른 텍스처다. 그래프가 밉 체인을 한 리소스로 다루지 않으므로
    // 밉당 텍스처를 만들었고, 그래서 여기서 분기한다.
    // (분기가 싫으면 Texture2DArray로 묶을 수 있지만 밉마다 크기가 달라
    //  배열이 성립하지 않는다.)
    switch (mip)
    {
    case 0:  return gHiZ0.Load(int3(coord, 0));
    case 1:  return gHiZ1.Load(int3(coord, 0));
    case 2:  return gHiZ2.Load(int3(coord, 0));
    case 3:  return gHiZ3.Load(int3(coord, 0));
    case 4:  return gHiZ4.Load(int3(coord, 0));
    case 5:  return gHiZ5.Load(int3(coord, 0));
    case 6:  return gHiZ6.Load(int3(coord, 0));
    default: return gHiZ7.Load(int3(coord, 0));
    }
}

uint2 MipSize(uint mip)
{
    return max(uint2(1, 1), gDepthSize >> mip);
}

float3 ViewFromDepth(float2 uv, float depth)
{
    const float4 clip = float4(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f, depth, 1.0f);
    const float4 view = mul(clip, gInverseProjection);
    return view.xyz / view.w;
}

// 해시 기반 노이즈. 프레임 인덱스를 섞어 프레임마다 다른 방향을 본다.
float Hash(uint2 p, uint frame)
{
    uint n = p.x * 73856093u ^ p.y * 19349663u ^ frame * 83492791u;
    n = (n ^ 61u) ^ (n >> 16);
    n *= 9u;
    n = n ^ (n >> 4);
    n *= 0x27d4eb2du;
    n = n ^ (n >> 15);
    return float(n & 0x00ffffffu) / float(0x01000000u);
}

// Hi-Z 행진. 히트하면 true와 히트 UV를 준다.
bool TraceHiZ(float3 origin, float3 direction, float2 startUV, out float2 hitUV)
{
    hitUV = startUV;

    // 뷰 공간 끝점을 화면으로 옮겨 화면 공간 방향을 얻는다.
    const float3 endView = origin + direction * gMaxDistance;
    float4 endClip = mul(float4(endView, 1.0f), gProjection);
    if (endClip.w <= 0.0001f) return false;
    endClip /= endClip.w;

    const float2 endUV = float2(endClip.x * 0.5f + 0.5f, 0.5f - endClip.y * 0.5f);
    float2 delta = endUV - startUV;
    if (dot(delta, delta) < 1e-12f) return false;

    // 시작 깊이(광선 원점의 화면 깊이)와 끝 깊이 사이를 선형 보간한다.
    const float startDepth = LoadHiZ(0, int2(startUV * gDepthSize));
    const float endDepth = endClip.z;

    uint mip = min(2u, gMipCount - 1u);   // 중간 밉에서 시작한다
    float t = 0.0f;
    const uint kMaxSteps = 64u;

    for (uint step = 0; step < kMaxSteps; ++step)
    {
        if (t >= 1.0f) return false;

        const float2 uv = startUV + delta * t;
        if (any(uv < 0.0f) || any(uv > 1.0f)) return false;

        const uint2 size = MipSize(mip);
        const int2 cell = int2(uv * size);
        const float cellDepth = LoadHiZ(mip, cell);
        const float rayDepth = lerp(startDepth, endDepth, t);

        if (rayDepth < cellDepth)
        {
            // 이 셀은 광선보다 뒤에 있다 = 비었다. 셀 경계까지 건너뛴다.
            const float2 texel = 1.0f / float2(size);
            const float2 next = (floor(uv / texel) + 1.0f) * texel;
            const float2 toEdge = (next - uv) / max(abs(delta), 1e-6f);
            t += max(min(toEdge.x, toEdge.y), 1e-4f) * sign(1.0f);

            if (mip + 1 < gMipCount) ++mip;   // 비었으니 더 크게 건넌다
        }
        else
        {
            if (0 == mip)
            {
                // 두께를 넘어 뚫고 지나간 것이면 히트로 보지 않는다.
                // 이것이 없으면 얇은 물체 뒤가 전부 막힌 것으로 잡힌다.
                if (rayDepth - cellDepth > gThickness) return false;

                hitUV = uv;
                return true;
            }
            --mip;   // 무언가 있다 — 정밀하게 본다
        }
    }

    return false;
}

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= gOutputSize.x || id.y >= gOutputSize.y) return;

    const float2 uv = (float2(id.xy) + 0.5f) / float2(gOutputSize);
    const int2 depthCoord = int2(uv * gDepthSize);

    const float depth = LoadHiZ(0, depthCoord);
    if (depth >= 1.0f)
    {
        // 하늘이다. 간접광을 모을 표면이 없다.
        gOutput[id.xy] = float4(0, 0, 0, 0);
        return;
    }

    const float3 position = ViewFromDepth(uv, depth);
    const float3 normal = normalize(gNormal.Load(int3(depthCoord, 0)).xyz * 2.0f - 1.0f);

    float3 gathered = 0.0f;
    uint   hits = 0;

    const float jitter = Hash(id.xy, gFrameIndex);

    [loop]
    for (uint slice = 0; slice < SLICES_PER_FRAME; ++slice)
    {
        // 반구에서 코사인 가중으로 방향을 뽑는다. 프레임마다 jitter가
        // 달라지므로 여러 프레임에 걸쳐 반구가 고르게 덮인다.
        const float u1 = frac(jitter + float(slice) / float(SLICES_PER_FRAME));
        const float u2 = frac(jitter * 1.61803f + float(slice) * 0.7548f);

        const float r = sqrt(u1);
        const float phi = 2.0f * kPI * u2;

        float3 tangent = abs(normal.z) < 0.999f ? float3(0, 0, 1) : float3(1, 0, 0);
        tangent = normalize(cross(tangent, normal));
        const float3 bitangent = cross(normal, tangent);

        const float3 direction = normalize(
            tangent * (r * cos(phi)) + bitangent * (r * sin(phi))
            + normal * sqrt(max(0.0f, 1.0f - u1)));

        float2 hitUV;
        if (TraceHiZ(position + normal * 0.01f, direction, uv, hitUV))
        {
            // 맞은 표면의 직접광이 이 픽셀의 간접광이 된다.
            // Load로 읽는다. 히트 지점의 색을 그대로 쓰므로 보간이 필요
            // 없고, 샘플러를 안 만들면 힙도 바인딩도 하나 준다.
            gathered += gLighting.Load(int3(hitUV * gDepthSize, 0)).rgb;
            ++hits;
        }
    }

    // 슬라이스 수로 나눈다. 맞지 않은 방향은 0(하늘/먼 곳)으로 친다 —
    // 하늘빛은 합성 단계에서 따로 더한다.
    const float3 gi = gathered / float(SLICES_PER_FRAME);

    // a에 히트 비율을 담는다. 리졸브가 이 값으로 신뢰도를 가늠한다.
    gOutput[id.xy] = float4(gi, float(hits) / float(SLICES_PER_FRAME));
}
)";

    struct ResolveParams
    {
        Mathf::Matrix inverseProjection{};
        Mathf::Matrix inverseView{};
        Mathf::Matrix previousViewProjection{};
        uint32_t      width{ 0 };
        uint32_t      height{ 0 };
        uint32_t      hasHistory{ 0 };
        uint32_t      maxAccum{ 0 };
        float         depthTolerance{ 0.f };
        float         pad0{ 0.f };
        float         pad1[2]{};
    };

    struct FilterParams
    {
        uint32_t width{ 0 };
        uint32_t height{ 0 };
        float    depthSigma{ 0.f };
        float    normalPower{ 0.f };
    };

    struct CompositeParams
    {
        uint32_t outputWidth{ 0 };
        uint32_t outputHeight{ 0 };
        uint32_t giWidth{ 0 };
        uint32_t giHeight{ 0 };
        float    intensity{ 0.f };
        float    depthSigma{ 0.f };
        float    pad[2]{};
    };

    struct HiZParams
    {
        uint32_t targetWidth{ 0 };
        uint32_t targetHeight{ 0 };
        uint32_t sourceWidth{ 0 };
        uint32_t sourceHeight{ 0 };
    };

    struct TraceParams
    {
        Mathf::Matrix inverseProjection{};
        Mathf::Matrix projection{};
        uint32_t      outputWidth{ 0 };
        uint32_t      outputHeight{ 0 };
        uint32_t      depthWidth{ 0 };
        uint32_t      depthHeight{ 0 };
        uint32_t      mipCount{ 0 };
        uint32_t      frameIndex{ 0 };
        float         maxDistance{ 0.f };
        float         thickness{ 0.f };
    };

    bool CompileSsgiShader(const char* source, const D3D_SHADER_MACRO* defines,
        Microsoft::WRL::ComPtr<ID3DBlob>& outBlob, std::string& outError)
    {
        Microsoft::WRL::ComPtr<ID3DBlob> errors;
        const HRESULT hr = D3DCompile(source, strlen(source), nullptr, defines, nullptr,
            "CSMain", "cs_5_0", 0, 0, &outBlob, &errors);
        if (FAILED(hr))
        {
            outError = "SSGI 셰이더 컴파일 실패: ";
            if (errors) outError += static_cast<const char*>(errors->GetBufferPointer());
            else        outError += SsgiHrToString(hr);
            return false;
        }
        return true;
    }
}

bool EnhancedSSGIPass::Initialize(const EnhancedFrameContext& context, std::string& outError)
{
    if (nullptr == context.resources || nullptr == context.psoManager ||
        nullptr == context.rootSignatures)
    {
        outError = "SSGI 패스 컨텍스트가 불완전하다";
        return false;
    }

    return CreatePipelines(context, outError);
}

bool EnhancedSSGIPass::CreatePipelines(const EnhancedFrameContext& context, std::string& outError)
{
    // ── 루트 시그니처 ──
    //
    // 두 패스가 같은 것을 쓴다. Hi-Z 빌드는 SRV 1·UAV 1만 쓰지만, 트레이스가
    // 요구하는 넓은 테이블에 얹어도 비용이 없다(안 쓰는 슬롯은 바인딩만 안
    // 하면 된다). 시그니처를 둘로 나누면 캐시에 둘이 남고, 패스 사이에서
    // 루트 시그니처를 바꾸는 비용이 더 든다.
    D3D12_DESCRIPTOR_RANGE srvRange{};
    srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors = kMaxHiZMips + 2;   // Hi-Z 밉들 + 노멀 + 라이팅
    srvRange.BaseShaderRegister = 0;

    D3D12_DESCRIPTOR_RANGE uavRange{};
    uavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    uavRange.NumDescriptors = 1;
    uavRange.BaseShaderRegister = 0;

    // 샘플러는 두지 않는다. 두 셰이더 모두 Load로 읽으므로 필요가 없고,
    // 안 쓰는 루트 파라미터는 바인딩을 잊었을 때 조용히 통과하는 자리가 된다.
    D3D12_ROOT_PARAMETER params[3]{};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[0].Descriptor.ShaderRegister = 0;

    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].DescriptorTable.NumDescriptorRanges = 1;
    params[1].DescriptorTable.pDescriptorRanges = &srvRange;

    params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[2].DescriptorTable.NumDescriptorRanges = 1;
    params[2].DescriptorTable.pDescriptorRanges = &uavRange;

    D3D12_ROOT_SIGNATURE_DESC rootDesc{};
    rootDesc.NumParameters = _countof(params);
    rootDesc.pParameters = params;

    const auto root = context.rootSignatures->GetOrCreate(rootDesc, outError);
    if (!root.IsValid()) return false;
    m_rootSignature = root.signature;

    // ── Hi-Z 빌드 PSO ──
    ComPtr<ID3DBlob> hiZBlob;
    if (!CompileSsgiShader(kHiZBuildShader, nullptr, hiZBlob, outError)) return false;

    DX12ComputePipelineDesc hiZDesc{};
    hiZDesc.csBytecode = hiZBlob->GetBufferPointer();
    hiZDesc.csSize = hiZBlob->GetBufferSize();
    hiZDesc.rootSignature = root.signature;
    hiZDesc.rootSignatureId = root.id;

    m_hiZBuildPSO = context.psoManager->GetOrCreateCompute(hiZDesc, outError);
    if (nullptr == m_hiZBuildPSO) return false;

    // ── 트레이스 PSO ──
    //
    // 슬라이스 수를 매크로로 넘긴다. 상수 버퍼로 넘기면 루프가 동적이 되고,
    // 그러면 컴파일러가 펼치지 못해 레지스터 압박이 늘어난다.
    const std::string slices = std::to_string(kSlicesPerFrame);
    const D3D_SHADER_MACRO traceDefines[] = {
        { "SLICES_PER_FRAME", slices.c_str() },
        { nullptr, nullptr }
    };

    ComPtr<ID3DBlob> traceBlob;
    if (!CompileSsgiShader(kTraceShader, traceDefines, traceBlob, outError)) return false;

    DX12ComputePipelineDesc traceDesc{};
    traceDesc.csBytecode = traceBlob->GetBufferPointer();
    traceDesc.csSize = traceBlob->GetBufferSize();
    traceDesc.rootSignature = root.signature;
    traceDesc.rootSignatureId = root.id;

    m_tracePSO = context.psoManager->GetOrCreateCompute(traceDesc, outError);
    if (nullptr == m_tracePSO) return false;

    // ── 리졸브·필터·합성 PSO ──
    //
    // 셋 다 같은 루트 시그니처를 쓴다. 요구하는 SRV 수가 다르지만 넓은
    // 테이블에 얹어도 비용이 없고, 시그니처를 나누면 패스 사이에서 바꾸는
    // 비용이 더 든다.
    struct StagePSO
    {
        const char*           source;
        ID3D12PipelineState** target;
    };

    const StagePSO stages[] = {
        { SsgiShaders::kResolve,   &m_resolvePSO },
        { SsgiShaders::kFilter,    &m_filterPSO },
        { SsgiShaders::kComposite, &m_compositePSO },
    };

    for (const StagePSO& stage : stages)
    {
        ComPtr<ID3DBlob> blob;
        if (!CompileSsgiShader(stage.source, nullptr, blob, outError)) return false;

        DX12ComputePipelineDesc desc{};
        desc.csBytecode = blob->GetBufferPointer();
        desc.csSize = blob->GetBufferSize();
        desc.rootSignature = root.signature;
        desc.rootSignatureId = root.id;

        *stage.target = context.psoManager->GetOrCreateCompute(desc, outError);
        if (nullptr == *stage.target) return false;
    }

    return true;
}

bool EnhancedSSGIPass::EnsureHistory(const EnhancedFrameContext& context,
    std::string& outError)
{
    // 크기가 그대로면 다시 만들지 않는다. 매 프레임 만들면 그것만으로
    // 프레임 예산을 먹고, 히스토리가 매번 비어 누적이 성립하지 않는다.
    if (nullptr != m_history[0])
    {
        D3D12_RESOURCE_DESC existing = m_history[0]->GetDesc();
        if (existing.Width == m_giWidth && existing.Height == m_giHeight) return true;
    }

    // 크기가 바뀌었다 — 히스토리를 버린다. 낡은 크기의 값을 새 크기에
    // 섞으면 화면이 어긋난 채로 번진다.
    m_historyValid = false;

    auto* device = context.resources->GetDevice();

    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;

    for (uint32_t i = 0; i < kHistoryCount; ++i)
    {
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = m_giWidth;
        desc.Height = m_giHeight;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.SampleDesc.Count = 1;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        desc.Format = kGIFormat;
        HRESULT hr = device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, nullptr,
            IID_PPV_ARGS(&m_history[i]));
        if (FAILED(hr))
        {
            outError = "SSGI 히스토리 생성 실패 " + SsgiHrToString(hr);
            return false;
        }

        desc.Format = kHiZFormat;
        hr = device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, nullptr,
            IID_PPV_ARGS(&m_historyDepth[i]));
        if (FAILED(hr))
        {
            outError = "SSGI 히스토리 깊이 생성 실패 " + SsgiHrToString(hr);
            return false;
        }
    }

    return true;
}

bool EnhancedSSGIPass::PrepareFrame(const EnhancedFrameContext& context, std::string& outError)
{
    (void)outError;

    // GI 해상도는 화면의 1/2. 0이 되지 않게 하한을 둔다 — 창을 아주 작게
    // 줄이면 Dispatch가 0이 되고, 그러면 조용히 아무것도 안 그린다.
    m_giWidth = (std::max)(1u, context.width / kResolutionDivisor);
    m_giHeight = (std::max)(1u, context.height / kResolutionDivisor);

    // Hi-Z 밉 수. 가장 작은 변이 1이 될 때까지 반으로 줄인다.
    m_hiZMipCount = 1;
    uint32_t w = m_giWidth;
    uint32_t h = m_giHeight;
    while (w > 1 && h > 1 && m_hiZMipCount < kMaxHiZMips)
    {
        w = (std::max)(1u, w / 2);
        h = (std::max)(1u, h / 2);
        ++m_hiZMipCount;
    }

    if (!EnsureHistory(context, outError)) return false;

    // 프레임마다 노이즈를 돌리기 위한 인덱스. 시간축이 샘플 수를 대신하려면
    // 프레임마다 다른 방향을 봐야 한다.
    ++m_frameIndex;

    // 히스토리 슬롯을 번갈아 쓴다. 이번 프레임이 쓰는 것과 지난 프레임이
    // 쓴 것이 달라야 한다 — 같으면 읽으면서 쓰게 된다.
    m_historyIndex = (m_historyIndex + 1) % kHistoryCount;

    // ★ 이전 프레임 행렬은 여기서 갱신하지 않는다.
    //
    // 처음에는 PrepareFrame에서 현재 행렬을 m_previousViewProjection에
    // 넣었다. 그러면 리졸브가 '지난 프레임'이라며 이번 프레임 행렬을 쓰게
    // 되고, 재투영이 늘 제자리를 가리켜 카메라가 움직여도 히스토리를
    // 버리지 않는다 — 움직이면 번지는데 원인이 안 보인다.
    //
    // 갱신은 Declare가 끝난 뒤(이번 프레임 값을 다 쓴 뒤)에 한다.

    return true;
}

void EnhancedSSGIPass::Declare(EnhancedRenderGraph& graph, const EnhancedFrameContext& context)
{
    if (!m_inputs.depth.IsValid() || nullptr == m_tracePSO)
    {
        // 입력이 없으면 선언하지 않는다. 빈 패스를 넣으면 배리어와 컬링이
        // 그것을 진짜로 취급한다.
        m_output = RGHandle{};
        return;
    }

    // ── Hi-Z 밉 체인 선언 ──
    //
    // 밉마다 별도 텍스처다. 그래프가 밉 체인을 한 리소스로 다루지 않기
    // 때문이고, 그래서 UAV도 밉마다 따로 만든다.
    for (uint32_t mip = 0; mip < m_hiZMipCount; ++mip)
    {
        RGTextureDesc desc{};
        desc.width = (std::max)(1u, m_giWidth >> mip);
        desc.height = (std::max)(1u, m_giHeight >> mip);
        desc.format = kHiZFormat;
        desc.allowUnorderedAccess = true;
        desc.name = "SSGI.HiZ." + std::to_string(mip);
        m_hiZMips[mip] = graph.CreateTexture(desc);
    }

    RGTextureDesc giDesc{};
    giDesc.width = m_giWidth;
    giDesc.height = m_giHeight;
    giDesc.format = kGIFormat;
    giDesc.allowUnorderedAccess = true;

    giDesc.name = "SSGI.Trace";
    m_traceResult = graph.CreateTexture(giDesc);

    giDesc.name = "SSGI.Resolved";
    m_resolved = graph.CreateTexture(giDesc);

    giDesc.name = "SSGI.Filtered";
    m_filtered = graph.CreateTexture(giDesc);

    // 합성은 전 해상도다. 라이팅에 간접광을 더한 결과를 뒤 패스가 읽는다.
    RGTextureDesc outDesc{};
    outDesc.width = context.width;
    outDesc.height = context.height;
    outDesc.format = kGIFormat;
    outDesc.allowUnorderedAccess = true;
    outDesc.name = "SSGI.Output";
    m_output = graph.CreateTexture(outDesc);

    // ── 1단계: Hi-Z 빌드 ──
    //
    // 밉마다 패스를 하나씩 선언한다. 한 패스로 묶으면 밉 사이의 배리어를
    // 손으로 넣어야 하는데, 그래프에 맡기면 선언만으로 해결된다 — 각 밉이
    // 앞 밉을 SRV로 읽고 자기를 UAV로 쓴다고 적으면 된다.
    for (uint32_t mip = 0; mip < m_hiZMipCount; ++mip)
    {
        std::vector<EnhancedRenderGraph::RGPassUsage> usages;
        if (0 == mip)
        {
            usages.push_back({ m_inputs.depth, RGResourceState::ShaderResource });
        }
        else
        {
            usages.push_back({ m_hiZMips[mip - 1], RGResourceState::ShaderResource });
        }
        usages.push_back({ m_hiZMips[mip], RGResourceState::UnorderedAccess });

        graph.AddPass("SSGI.HiZ." + std::to_string(mip), usages,
            [this, &context, mip](const EnhancedRenderGraph::ExecuteContext& executeContext)
            {
                auto* commandList = executeContext.commandList;
                auto* device = context.resources->GetDevice();

                const uint32_t targetWidth = (std::max)(1u, m_giWidth >> mip);
                const uint32_t targetHeight = (std::max)(1u, m_giHeight >> mip);
                const uint32_t sourceWidth = (0 == mip) ? context.width
                    : (std::max)(1u, m_giWidth >> (mip - 1));
                const uint32_t sourceHeight = (0 == mip) ? context.height
                    : (std::max)(1u, m_giHeight >> (mip - 1));

                HiZParams params{};
                params.targetWidth = targetWidth;
                params.targetHeight = targetHeight;
                params.sourceWidth = sourceWidth;
                params.sourceHeight = sourceHeight;

                const auto cb = context.resources->GetUploadRing().Allocate(
                    sizeof(HiZParams), DX12UploadRing::kConstantBufferAlignment);
                if (!cb.IsValid()) return;
                memcpy(cb.cpuAddress, &params, sizeof(params));

                // SRV 테이블은 시그니처 크기만큼 잡는다. 안 쓰는 슬롯도
                // 디스크립터가 있어야 검증 레이어가 조용하다.
                const auto srvTable = context.resources->GetDescriptorRing()
                    .Allocate(kMaxHiZMips + 2);
                const auto uavTable = context.resources->GetDescriptorRing().Allocate(1);
                if (!srvTable.IsValid() || !uavTable.IsValid()) return;

                auto* source = (0 == mip)
                    ? executeContext.Resolve(m_inputs.depth)
                    : executeContext.Resolve(m_hiZMips[mip - 1]);

                D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
                srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                srvDesc.Texture2D.MipLevels = 1;
                // 깊이는 D32_FLOAT라 SRV에서 R32_FLOAT로 본다.
                srvDesc.Format = (0 == mip) ? DXGI_FORMAT_R32_FLOAT : kHiZFormat;

                for (uint32_t i = 0; i < kMaxHiZMips + 2; ++i)
                {
                    device->CreateShaderResourceView(source, &srvDesc, srvTable.CpuAt(i));
                }

                D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
                uavDesc.Format = kHiZFormat;
                uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
                device->CreateUnorderedAccessView(
                    executeContext.Resolve(m_hiZMips[mip]), nullptr, &uavDesc,
                    uavTable.CpuAt(0));

                // ★ 디스크립터 힙을 먼저 바인딩한다.
                //
                // 빠뜨렸다가 SetComputeRootDescriptorTable에서 죽었다. 링에서
                // 받은 GPU 핸들은 그 힙이 바인딩돼 있을 때만 뜻이 있다 —
                // 핸들 자체는 유효해 보이지만 GPU가 다른 힙을 보고 있으면
                // 엉뚱한 곳을 가리킨다.
                ID3D12DescriptorHeap* heaps[] = {
                    context.resources->GetDescriptorRing().GetHeap() };
                commandList->SetDescriptorHeaps(1, heaps);

                commandList->SetComputeRootSignature(m_rootSignature);
                commandList->SetPipelineState(m_hiZBuildPSO);
                commandList->SetComputeRootConstantBufferView(0, cb.gpuAddress);
                commandList->SetComputeRootDescriptorTable(1, srvTable.gpu);
                commandList->SetComputeRootDescriptorTable(2, uavTable.gpu);

                commandList->Dispatch((targetWidth + 7) / 8, (targetHeight + 7) / 8, 1);
            });
    }

    // ── 2단계: 트레이스 ──
    //
    // 뿌리로 표시하지 않는다. 뒤 단계(리졸브·합성)가 읽으면 컬링이 살리고,
    // 아직 아무도 안 읽으면 걷어내는 것이 맞다 — 결과를 안 쓰는데 도는
    // 패스가 남아 있으면 프레임 시간만 먹는다.
    {
        std::vector<EnhancedRenderGraph::RGPassUsage> usages;
        for (uint32_t mip = 0; mip < m_hiZMipCount; ++mip)
        {
            usages.push_back({ m_hiZMips[mip], RGResourceState::ShaderResource });
        }
        if (m_inputs.normal.IsValid())
        {
            usages.push_back({ m_inputs.normal, RGResourceState::ShaderResource });
        }
        if (m_inputs.lighting.IsValid())
        {
            usages.push_back({ m_inputs.lighting, RGResourceState::ShaderResource });
        }
        usages.push_back({ m_traceResult, RGResourceState::UnorderedAccess });

        graph.AddPass("SSGI.Trace", usages,
            [this, &context](const EnhancedRenderGraph::ExecuteContext& executeContext)
            {
                auto* commandList = executeContext.commandList;
                auto* device = context.resources->GetDevice();

                TraceParams params{};
                if (nullptr != context.camera)
                {
                    const Mathf::xMatrix projection = context.camera->projection;
                    params.projection = XMMatrixTranspose(projection);
                    params.inverseProjection = XMMatrixTranspose(
                        XMMatrixInverse(nullptr, projection));
                }
                params.outputWidth = m_giWidth;
                params.outputHeight = m_giHeight;
                params.depthWidth = m_giWidth;
                params.depthHeight = m_giHeight;
                params.mipCount = m_hiZMipCount;
                params.frameIndex = m_frameIndex;
                // 실측으로 정할 값들이다. 지금은 눈으로 볼 수 있는 범위를
                // 잡아 두고, 리졸브가 붙은 뒤 씬에 맞춰 조인다.
                params.maxDistance = 8.f;
                params.thickness = 0.5f;

                const auto cb = context.resources->GetUploadRing().Allocate(
                    sizeof(TraceParams), DX12UploadRing::kConstantBufferAlignment);
                if (!cb.IsValid()) return;
                memcpy(cb.cpuAddress, &params, sizeof(params));

                const auto srvTable = context.resources->GetDescriptorRing()
                    .Allocate(kMaxHiZMips + 2);
                const auto uavTable = context.resources->GetDescriptorRing().Allocate(1);
                if (!srvTable.IsValid() || !uavTable.IsValid()) return;

                D3D12_SHADER_RESOURCE_VIEW_DESC hiZSrv{};
                hiZSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                hiZSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                hiZSrv.Texture2D.MipLevels = 1;
                hiZSrv.Format = kHiZFormat;

                for (uint32_t i = 0; i < kMaxHiZMips; ++i)
                {
                    // 밉이 모자라면 마지막 것으로 채운다. 셰이더가 gMipCount
                    // 안에서만 읽으므로 값은 안 쓰이지만, 디스크립터가 비어
                    // 있으면 검증 레이어가 잡는다.
                    const uint32_t index = (i < m_hiZMipCount) ? i : (m_hiZMipCount - 1);
                    device->CreateShaderResourceView(
                        executeContext.Resolve(m_hiZMips[index]), &hiZSrv,
                        srvTable.CpuAt(i));
                }

                D3D12_SHADER_RESOURCE_VIEW_DESC colorSrv = hiZSrv;
                colorSrv.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;

                auto* normal = m_inputs.normal.IsValid()
                    ? executeContext.Resolve(m_inputs.normal) : nullptr;
                auto* lighting = m_inputs.lighting.IsValid()
                    ? executeContext.Resolve(m_inputs.lighting) : nullptr;

                // ★ 없어도 디스크립터는 반드시 만든다.
                //
                // 처음에는 nullptr이면 건너뛰었다. 그러자 그 슬롯이 초기화되지
                // 않은 채 테이블에 남았고, GPU가 그것을 읽어 그 자리에서
                // 죽었다(그래프 Execute에서 크래시). 디스크립터 힙은 쓰레기
                // 메모리이지 '비어 있음'이 아니다.
                //
                // 입력이 없으면 Hi-Z 0을 꽂는다. 값은 뜻이 없지만 유효한
                // 디스크립터이고, 셰이더가 그 값을 쓰더라도 검은 결과가 나올
                // 뿐 죽지는 않는다.
                auto* fallback = executeContext.Resolve(m_hiZMips[0]);

                D3D12_SHADER_RESOURCE_VIEW_DESC fallbackSrv = hiZSrv;

                if (nullptr != normal)
                {
                    device->CreateShaderResourceView(normal, &colorSrv,
                        srvTable.CpuAt(kMaxHiZMips));
                }
                else
                {
                    device->CreateShaderResourceView(fallback, &fallbackSrv,
                        srvTable.CpuAt(kMaxHiZMips));
                }

                if (nullptr != lighting)
                {
                    device->CreateShaderResourceView(lighting, &colorSrv,
                        srvTable.CpuAt(kMaxHiZMips + 1));
                }
                else
                {
                    device->CreateShaderResourceView(fallback, &fallbackSrv,
                        srvTable.CpuAt(kMaxHiZMips + 1));
                }

                D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
                uavDesc.Format = kGIFormat;
                uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
                device->CreateUnorderedAccessView(
                    executeContext.Resolve(m_traceResult), nullptr, &uavDesc, uavTable.CpuAt(0));

                ID3D12DescriptorHeap* heaps[] = {
                    context.resources->GetDescriptorRing().GetHeap() };
                commandList->SetDescriptorHeaps(1, heaps);

                commandList->SetComputeRootSignature(m_rootSignature);
                commandList->SetPipelineState(m_tracePSO);
                commandList->SetComputeRootConstantBufferView(0, cb.gpuAddress);
                commandList->SetComputeRootDescriptorTable(1, srvTable.gpu);
                commandList->SetComputeRootDescriptorTable(2, uavTable.gpu);

                commandList->Dispatch((m_giWidth + 7) / 8, (m_giHeight + 7) / 8, 1);
            });
    }

    // ── 3~5단계 공통 ──
    //
    // 세 패스가 같은 모양이라 헬퍼로 묶는다: 상수 올리기, SRV 테이블 채우기,
    // UAV 하나, Dispatch. 손으로 세 번 쓰면 한 곳만 고치고 나머지를 잊는
    // 종류의 실수가 난다 — 이 작업에서 이미 두 번 겪었다(대상 선택 계산,
    // 경계 측정).
    const auto declareStage = [this, &graph, &context](
        const std::string& name,
        const std::vector<EnhancedRenderGraph::RGPassUsage>& usages,
        ID3D12PipelineState* pso,
        RGHandle target,
        const std::vector<RGHandle>& srvHandles,
        const std::vector<ID3D12Resource*>& externalSrvs,
        const void* constants, size_t constantBytes,
        uint32_t dispatchWidth, uint32_t dispatchHeight,
        bool hasSideEffect)
    {
        // 상수는 값으로 복사해 둔다. 람다가 프레임 뒤에 실행되므로 호출부의
        // 지역 변수를 가리키면 그때는 이미 사라져 있다.
        std::vector<uint8_t> constantCopy(constantBytes);
        memcpy(constantCopy.data(), constants, constantBytes);

        graph.AddPass(name, usages,
            [this, &context, pso, target, srvHandles, externalSrvs, constantCopy,
             dispatchWidth, dispatchHeight]
            (const EnhancedRenderGraph::ExecuteContext& executeContext)
            {
                auto* commandList = executeContext.commandList;
                auto* device = context.resources->GetDevice();

                const auto cb = context.resources->GetUploadRing().Allocate(
                    constantCopy.size(), DX12UploadRing::kConstantBufferAlignment);
                if (!cb.IsValid()) return;
                memcpy(cb.cpuAddress, constantCopy.data(), constantCopy.size());

                const auto srvTable = context.resources->GetDescriptorRing()
                    .Allocate(kMaxHiZMips + 2);
                const auto uavTable = context.resources->GetDescriptorRing().Allocate(1);
                if (!srvTable.IsValid() || !uavTable.IsValid()) return;

                // 그래프 리소스와 외부 리소스를 순서대로 꽂는다.
                std::vector<ID3D12Resource*> sources;
                for (const RGHandle handle : srvHandles)
                {
                    sources.push_back(executeContext.Resolve(handle));
                }
                for (ID3D12Resource* external : externalSrvs)
                {
                    sources.push_back(external);
                }

                // ★ 남는 슬롯도 반드시 채운다. 비워 두면 GPU가 초기화되지
                //   않은 디스크립터를 읽고 그 자리에서 죽는다 — 트레이스에서
                //   이미 겪었다.
                ID3D12Resource* fallback = sources.empty()
                    ? executeContext.Resolve(m_hiZMips[0]) : sources.front();

                for (uint32_t i = 0; i < kMaxHiZMips + 2; ++i)
                {
                    ID3D12Resource* resource = (i < sources.size() && nullptr != sources[i])
                        ? sources[i] : fallback;
                    if (nullptr == resource) continue;

                    const D3D12_RESOURCE_DESC desc = resource->GetDesc();

                    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
                    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                    srvDesc.Texture2D.MipLevels = 1;
                    // 깊이는 D32_FLOAT로 만들어져 SRV에서는 R32_FLOAT로 본다.
                    srvDesc.Format = (DXGI_FORMAT_D32_FLOAT == desc.Format)
                        ? DXGI_FORMAT_R32_FLOAT : desc.Format;

                    device->CreateShaderResourceView(resource, &srvDesc, srvTable.CpuAt(i));
                }

                auto* targetResource = executeContext.Resolve(target);
                const D3D12_RESOURCE_DESC targetDesc = targetResource->GetDesc();

                D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
                uavDesc.Format = targetDesc.Format;
                uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
                device->CreateUnorderedAccessView(targetResource, nullptr, &uavDesc,
                    uavTable.CpuAt(0));

                ID3D12DescriptorHeap* heaps[] = {
                    context.resources->GetDescriptorRing().GetHeap() };
                commandList->SetDescriptorHeaps(1, heaps);

                commandList->SetComputeRootSignature(m_rootSignature);
                commandList->SetPipelineState(pso);
                commandList->SetComputeRootConstantBufferView(0, cb.gpuAddress);
                commandList->SetComputeRootDescriptorTable(1, srvTable.gpu);
                commandList->SetComputeRootDescriptorTable(2, uavTable.gpu);

                commandList->Dispatch((dispatchWidth + 7) / 8, (dispatchHeight + 7) / 8, 1);
            }, hasSideEffect);
    };

    // ── 3단계: 리졸브(재투영 + 누적) ──
    {
        ResolveParams params{};
        if (nullptr != context.camera)
        {
            const Mathf::xMatrix projection = context.camera->projection;
            params.inverseProjection = XMMatrixTranspose(
                XMMatrixInverse(nullptr, projection));
            params.inverseView = XMMatrixTranspose(
                XMMatrixInverse(nullptr, context.camera->view));
        }
        params.previousViewProjection = XMMatrixTranspose(m_previousViewProjection);
        params.width = m_giWidth;
        params.height = m_giHeight;
        params.hasHistory = (m_historyValid && m_hasPreviousFrame) ? 1u : 0u;
        params.maxAccum = kMaxAccumFrames;
        // 실측으로 조일 값이다. 너무 크면 다른 표면을 같은 것으로 보고,
        // 너무 작으면 정지 상태에서도 히스토리를 버린다.
        params.depthTolerance = 0.01f;

        const uint32_t readIndex = (m_historyIndex + kHistoryCount - 1) % kHistoryCount;

        std::vector<EnhancedRenderGraph::RGPassUsage> usages;
        usages.push_back({ m_traceResult, RGResourceState::ShaderResource });
        usages.push_back({ m_hiZMips[0], RGResourceState::ShaderResource });
        if (m_inputs.normal.IsValid())
        {
            usages.push_back({ m_inputs.normal, RGResourceState::ShaderResource });
        }
        usages.push_back({ m_resolved, RGResourceState::UnorderedAccess });

        // ★ 슬롯 순서가 셰이더 선언과 맞아야 한다.
        //   t0 트레이스 · t1 히스토리 · t2 히스토리깊이 · t3 깊이 · t4 노멀
        //
        // 그런데 헬퍼는 그래프 핸들을 먼저 꽂고 외부 리소스를 뒤에 꽂는다.
        // 히스토리(외부)가 t1·t2에 와야 하므로 이 순서로는 맞출 수 없다 —
        // 리졸브만 SRV를 직접 만든다면 헬퍼를 쓰는 뜻이 없어지므로,
        // 셰이더 쪽 레지스터를 헬퍼 순서에 맞춘다.
        //
        // 실제 순서: t0 트레이스 · t1 깊이 · t2 노멀 · t3 히스토리 · t4 히스토리깊이
        std::vector<RGHandle> ordered{ m_traceResult, m_hiZMips[0] };
        if (m_inputs.normal.IsValid()) ordered.push_back(m_inputs.normal);

        std::vector<ID3D12Resource*> orderedExternal{
            m_history[readIndex].Get(), m_historyDepth[readIndex].Get() };

        declareStage("SSGI.Resolve", usages, m_resolvePSO, m_resolved,
            ordered, orderedExternal, &params, sizeof(params),
            m_giWidth, m_giHeight, false);
    }

    // ── 4단계: bilateral 필터 ──
    {
        FilterParams params{};
        params.width = m_giWidth;
        params.height = m_giHeight;
        params.depthSigma = 0.01f;
        params.normalPower = 16.f;

        std::vector<EnhancedRenderGraph::RGPassUsage> usages;
        usages.push_back({ m_resolved, RGResourceState::ShaderResource });
        usages.push_back({ m_hiZMips[0], RGResourceState::ShaderResource });
        if (m_inputs.normal.IsValid())
        {
            usages.push_back({ m_inputs.normal, RGResourceState::ShaderResource });
        }
        usages.push_back({ m_filtered, RGResourceState::UnorderedAccess });

        std::vector<RGHandle> srvHandles{ m_resolved, m_hiZMips[0] };
        if (m_inputs.normal.IsValid()) srvHandles.push_back(m_inputs.normal);

        declareStage("SSGI.Filter", usages, m_filterPSO, m_filtered,
            srvHandles, {}, &params, sizeof(params),
            m_giWidth, m_giHeight, false);
    }

    // ── 5단계: 업샘플 + 합성 ──
    {
        CompositeParams params{};
        params.outputWidth = context.width;
        params.outputHeight = context.height;
        params.giWidth = m_giWidth;
        params.giHeight = m_giHeight;
        params.intensity = 1.f;
        params.depthSigma = 0.01f;

        std::vector<EnhancedRenderGraph::RGPassUsage> usages;
        usages.push_back({ m_filtered, RGResourceState::ShaderResource });
        usages.push_back({ m_hiZMips[0], RGResourceState::ShaderResource });
        if (m_inputs.lighting.IsValid())
        {
            usages.push_back({ m_inputs.lighting, RGResourceState::ShaderResource });
        }
        usages.push_back({ m_inputs.depth, RGResourceState::ShaderResource });
        if (m_inputs.diffuse.IsValid())
        {
            usages.push_back({ m_inputs.diffuse, RGResourceState::ShaderResource });
        }
        usages.push_back({ m_output, RGResourceState::UnorderedAccess });

        // t0 GI · t1 GI깊이 · t2 라이팅 · t3 깊이 · t4 디퓨즈
        std::vector<RGHandle> srvHandles{ m_filtered, m_hiZMips[0] };
        if (m_inputs.lighting.IsValid()) srvHandles.push_back(m_inputs.lighting);
        srvHandles.push_back(m_inputs.depth);
        if (m_inputs.diffuse.IsValid()) srvHandles.push_back(m_inputs.diffuse);

        // 합성 결과는 그래프 밖으로 나간다 — 뿌리로 표시한다.
        declareStage("SSGI.Composite", usages, m_compositePSO, m_output,
            srvHandles, {}, &params, sizeof(params),
            context.width, context.height, true);
    }

    // ── 히스토리 갱신 ──
    //
    // 이번 프레임 결과를 다음 프레임이 읽도록 복사한다. 그래프의 transient는
    // 프레임이 끝나면 사라지므로 영속 리소스로 옮겨야 한다.
    {
        std::vector<EnhancedRenderGraph::RGPassUsage> usages;
        usages.push_back({ m_filtered, RGResourceState::CopySource });
        usages.push_back({ m_hiZMips[0], RGResourceState::CopySource });

        auto* historyTarget = m_history[m_historyIndex].Get();
        auto* historyDepthTarget = m_historyDepth[m_historyIndex].Get();

        graph.AddPass("SSGI.StoreHistory", usages,
            [this, historyTarget, historyDepthTarget]
            (const EnhancedRenderGraph::ExecuteContext& executeContext)
            {
                auto* commandList = executeContext.commandList;

                // 영속 리소스는 그래프가 상태를 모른다. 복사 전후로 손으로
                // 전이한다 — 그래프에 맡길 수 없는 것은 여기서 책임진다.
                D3D12_RESOURCE_BARRIER toCopy[2]{};
                for (uint32_t i = 0; i < 2; ++i)
                {
                    toCopy[i].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                    toCopy[i].Transition.pResource = (0 == i) ? historyTarget : historyDepthTarget;
                    toCopy[i].Transition.StateBefore =
                        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
                    toCopy[i].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
                    toCopy[i].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                }
                commandList->ResourceBarrier(2, toCopy);

                commandList->CopyResource(historyTarget,
                    executeContext.Resolve(m_filtered));
                commandList->CopyResource(historyDepthTarget,
                    executeContext.Resolve(m_hiZMips[0]));

                D3D12_RESOURCE_BARRIER back[2]{};
                for (uint32_t i = 0; i < 2; ++i)
                {
                    back[i] = toCopy[i];
                    back[i].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
                    back[i].Transition.StateAfter =
                        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
                }
                commandList->ResourceBarrier(2, back);
            }, true);
    }

    // 이번 프레임 행렬을 다음 프레임의 '이전'으로 남긴다. Declare가 끝난
    // 뒤라야 이번 프레임 리졸브가 진짜 지난 프레임 값을 쓴다.
    if (nullptr != context.camera)
    {
        m_previousViewProjection = XMMatrixMultiply(context.camera->view,
            context.camera->projection);
        m_hasPreviousFrame = true;
    }
    m_historyValid = true;
}

void EnhancedSSGIPass::Shutdown()
{
    for (auto& texture : m_history) texture.Reset();
    for (auto& texture : m_historyDepth) texture.Reset();

    m_historyValid = false;
    m_hasPreviousFrame = false;
    m_hiZMipCount = 0;
    m_frameIndex = 0;

    m_hiZBuildPSO = nullptr;
    m_tracePSO = nullptr;
    m_resolvePSO = nullptr;
    m_filterPSO = nullptr;
    m_compositePSO = nullptr;
    m_rootSignature = nullptr;
}

// ── 자가 검증 ──
//
// 셰이더는 런타임 컴파일이라 C++ 빌드로는 HLSL 오류가 안 잡힌다. 실제로
// 선언하지 않은 샘플러를 쓰는 코드가 빌드를 통과했다 — 부르는 곳이 없으면
// 컴파일 자체가 안 돈다. 그래서 부르는 자리를 만든다.
bool EnhancedSceneRenderer::RunSSGITest(std::string& outLog)
{
    using Microsoft::WRL::ComPtr;

    constexpr uint32_t kWidth = 256;
    constexpr uint32_t kHeight = 256;

    outLog += "── SSGI 검증 (PHASE 3-6) ──\n";

    std::string error;

    DX12DeviceResources resources;
    if (!resources.Initialize(kWidth, kHeight, error))
    {
        outLog += "[1/3] DX12 초기화 실패: " + error + "\n";
        return false;
    }

    DX12PSOManager psoManager;
    DX12RootSignatureCache rootSignatures;
    if (!psoManager.Initialize(resources.GetDevice(), L"dx12_ssgi.cache", error) ||
        !rootSignatures.Initialize(resources.GetDevice(), error))
    {
        outLog += "[1/3] 캐시 초기화 실패: " + error + "\n";
        resources.Shutdown();
        return false;
    }

    EnhancedFrameContext frameContext{};
    frameContext.resources = &resources;
    frameContext.psoManager = &psoManager;
    frameContext.rootSignatures = &rootSignatures;
    frameContext.width = kWidth;
    frameContext.height = kHeight;

    // ── [1/3] 셰이더 컴파일과 PSO 생성 ──
    EnhancedSSGIPass ssgi;
    if (!ssgi.Initialize(frameContext, error))
    {
        outLog += "[1/3] SSGI 초기화 실패: " + error + "\n";
        resources.Shutdown();
        return false;
    }
    outLog += "[1/3] 셰이더 컴파일·PSO 생성 통과(Hi-Z·트레이스·리졸브·필터·합성)\n";

    // ── [2/3] 밉 수 산정 ──
    if (!ssgi.PrepareFrame(frameContext, error))
    {
        outLog += "[2/3] PrepareFrame 실패: " + error + "\n";
        ssgi.Shutdown();
        resources.Shutdown();
        return false;
    }

    // 256을 1/2로 줄이면 128이고, 1이 될 때까지 반이면 128→64→…→1로
    // 여덟 단계다. 상한(kMaxHiZMips)에 걸린다.
    const uint32_t expectedMips = EnhancedSSGIPass::kMaxHiZMips;
    const uint32_t giWidth = kWidth / EnhancedSSGIPass::kResolutionDivisor;

    {
        char line[192]{};
        std::snprintf(line, sizeof(line),
            "[2/3] GI 해상도 %ux%u · Hi-Z 밉 %u(기대 %u)\n",
            giWidth, kHeight / EnhancedSSGIPass::kResolutionDivisor,
            expectedMips, expectedMips);
        outLog += line;
    }

    // ── [3/3] 실제 렌더 ──
    //
    // 깊이를 그래프 밖에서 만들어 임포트한다. 여기서 확인하려는 것은
    // Hi-Z 체인과 트레이스가 도는가이지 GBuffer가 아니다.
    bool passed = true;
    {
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
        depthDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        ComPtr<ID3D12Resource> depth;
        if (FAILED(resources.GetDevice()->CreateCommittedResource(&heap,
            D3D12_HEAP_FLAG_NONE, &depthDesc,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, nullptr, IID_PPV_ARGS(&depth))))
        {
            outLog += "[3/3] 깊이 텍스처 생성 실패\n";
            ssgi.Shutdown();
            resources.Shutdown();
            return false;
        }

        if (!resources.BeginFrame(error))
        {
            outLog += "[3/3] BeginFrame 실패: " + error + "\n";
            ssgi.Shutdown();
            resources.Shutdown();
            return false;
        }

        EnhancedRenderGraph graph;

        EnhancedSSGIPass::Inputs inputs{};
        inputs.depth = graph.ImportTexture(depth.Get(),
            RGResourceState::ShaderResource, "SSGI.TestDepth");
        ssgi.SetInputs(inputs);

        ssgi.Declare(graph, frameContext);

        const auto output = ssgi.GetOutput();
        if (!output.IsValid())
        {
            outLog += "[3/3] 출력 핸들이 비었다 — Declare가 패스를 선언하지 않았다\n";
            passed = false;
        }

        // ★ 결과를 읽는 패스를 붙인다.
        //
        // 없으면 트레이스가 컬링돼 Dispatch가 한 번도 안 돈다. 실제로 첫
        // 실행이 '선언 9 · 실행 0'이었다 — 셰이더 컴파일만 확인하고 넘어갈
        // 뻔했다. 컬링이 도는 것은 옳지만, 도는지 보려면 읽는 쪽이 있어야 한다.
        // ★ 리드백 크기는 읽는 리소스에 맞춘다.
        //
        // 처음에는 GI 해상도(1/2)로 잡았다. 합성이 붙으면서 출력이 전 해상도가
        // 됐는데 리드백은 그대로였고, CopyTextureRegion이 "목적지 경계를
        // 넘는다"로 실패했다. 그 실패가 커맨드 리스트를 무효로 만들어
        // EndFrame의 Close가 E_INVALIDARG를 냈다 — 증상이 나온 자리와
        // 원인이 있는 자리가 달랐다.
        const uint32_t giW = kWidth;
        const uint32_t giH = kHeight;
        const uint32_t rowPitch = ((giW * 8u) + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1u)
            & ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1u);

        D3D12_HEAP_PROPERTIES readbackHeap{};
        readbackHeap.Type = D3D12_HEAP_TYPE_READBACK;

        D3D12_RESOURCE_DESC readbackDesc{};
        readbackDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        readbackDesc.Width = static_cast<uint64_t>(rowPitch) * giH;
        readbackDesc.Height = 1;
        readbackDesc.DepthOrArraySize = 1;
        readbackDesc.MipLevels = 1;
        readbackDesc.Format = DXGI_FORMAT_UNKNOWN;
        readbackDesc.SampleDesc.Count = 1;
        readbackDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        ComPtr<ID3D12Resource> readback;
        if (FAILED(resources.GetDevice()->CreateCommittedResource(&readbackHeap,
            D3D12_HEAP_FLAG_NONE, &readbackDesc, D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr, IID_PPV_ARGS(&readback))))
        {
            outLog += "[3/3] 리드백 버퍼 생성 실패\n";
            passed = false;
        }

        if (passed && output.IsValid())
        {
            graph.AddPass("SSGI.Readback", { { output, RGResourceState::CopySource } },
                [&](const EnhancedRenderGraph::ExecuteContext& executeContext)
                {
                    D3D12_TEXTURE_COPY_LOCATION dst{};
                    dst.pResource = readback.Get();
                    dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                    dst.PlacedFootprint.Footprint.Format = EnhancedSSGIPass::kGIFormat;
                    dst.PlacedFootprint.Footprint.Width = giW;
                    dst.PlacedFootprint.Footprint.Height = giH;
                    dst.PlacedFootprint.Footprint.Depth = 1;
                    dst.PlacedFootprint.Footprint.RowPitch = rowPitch;

                    D3D12_TEXTURE_COPY_LOCATION src{};
                    src.pResource = executeContext.Resolve(output);
                    src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;

                    executeContext.commandList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
                }, true);
        }

        if (passed && !graph.Compile(resources.GetDevice(), error))
        {
            outLog += "[3/3] 그래프 Compile 실패: " + error + "\n";
            passed = false;
        }

        if (passed && !graph.Execute(resources.GetCommandList(), error))
        {
            outLog += "[3/3] 그래프 Execute 실패: " + error + "\n";
            passed = false;
        }

        const auto stats = graph.GetStats();

        if (!resources.EndFrame(error))
        {
            outLog += "[3/3] EndFrame 실패: " + error + "\n";
            passed = false;
        }
        else
        {
            resources.WaitForGpu();
        }

        {
            char line[224]{};
            std::snprintf(line, sizeof(line),
                "[3/3] 그래프 — 선언 %u · 실행 %u · 컬링 %u · 배리어 %u(%u번에)\n",
                stats.passesDeclared, stats.passesExecuted, stats.passesCulled,
                stats.barriersEmitted, stats.barrierBatches);
            outLog += line;
        }

        // Hi-Z 밉마다 하나 + 트레이스 + 리졸브 + 필터 + 합성
        // + 히스토리 저장 + 리드백.
        const uint32_t expectedPasses = expectedMips + 6;
        if (stats.passesDeclared != expectedPasses)
        {
            outLog += "선언된 패스가 " + std::to_string(stats.passesDeclared)
                + "개인데 " + std::to_string(expectedPasses) + "개여야 한다\n";
            passed = false;
        }

        // ★ 리드백이 결과를 읽으므로 전부 살아나야 한다.
        //
        // 이 단정이 없던 첫 실행이 '선언 9 · 실행 0'이었다 — 아무도 결과를
        // 안 읽어 전부 컬링됐고, 셰이더 컴파일만 확인한 채 '통과'가 나왔다.
        // 컬링이 도는 것은 옳지만 그 상태로는 Hi-Z 체인과 행진이 실제로
        // 도는지 알 수 없다. 그래서 리드백을 붙이고 실행 수를 단정한다.
        if (stats.passesExecuted != expectedPasses)
        {
            outLog += "실행된 패스가 " + std::to_string(stats.passesExecuted)
                + "개다 — Hi-Z 체인의 의존이 끊겼다\n";
            passed = false;
        }

        // 밉이 앞 밉을 SRV로 읽으므로 UAV→SRV 전이가 밉 수만큼은 나와야 한다.
        // 0이면 배리어 유도가 죽은 것이고, 그러면 GPU가 덜 쓴 것을 읽는다.
        if (stats.barriersEmitted < expectedMips)
        {
            outLog += "배리어가 " + std::to_string(stats.barriersEmitted)
                + "건뿐이다 — UAV→SRV 전이가 빠졌다\n";
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

    ssgi.Shutdown();
    rootSignatures.Shutdown();
    psoManager.Shutdown();
    resources.Shutdown();

    outLog += passed ? "SSGI 검증 통과\n" : "SSGI 검증 실패\n";
    return passed;
}

#endif
