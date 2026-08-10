#ifndef DYNAMICCPP_EXPORTS
#include "EnhancedSSAOPass.h"
#include "DX12DeviceResources.h"
#include "DX12PSOManager.h"
#include "DX12RootSignatureCache.h"
#include "EnhancedRenderGraph.h"
#include "RHIEncoder.h"

#include <d3dcompiler.h>
#include <sstream>
#include <vector>

#pragma comment(lib, "d3dcompiler.lib")

// 단계(순서대로 채운다):
//   [v] 1. 반해상도 AO 컴퓨트 + 자가 검증
//   [v] 2. 디노이즈·업샘플
//   [v] 3. 실제 씬 연결 + 기존 SSAO와 시간 비교
//
// 자가 검증(dx12.ssao):
//   평평한 곳 0.969 · 계단 안쪽 0.373 · 필터 이웃 차이 51.7% 감소
// 시간 비교(dx12.ssaoscale, 1920x1080):
//   신규 0.196 ms · 참조(커널 64) 0.810 ms — 4.1배
// 실제 씬(dx12.scene, 256x256):
//   SSAO.Compute 0.0133 ms · SSAO.Filter 0.0020 ms
//
// AO는 SSGI 합성이 간접광에 곱해 쓴다. 직접광에 곱하면 광원이 실제로
// 보이는 곳까지 어두워져 그림자가 두 번 진 것처럼 보인다.

namespace
{
    // 유니티 빌드에서 익명 네임스페이스가 합쳐지므로 이름을 고유하게 둔다.
    std::string SsaoHrToString(HRESULT hr)
    {
        std::ostringstream oss;
        oss << "HRESULT 0x" << std::hex << static_cast<unsigned long>(hr);
        return oss.str();
    }

    // ── 공통 조각 ──
    //
    // 두 셰이더가 같은 깊이 → 뷰 위치 변환을 쓴다. 한 곳에 두는 이유는
    // 이 변환이 어긋나면 AO와 필터가 서로 다른 공간을 보게 되는데, 그 증상이
    // '그림이 조금 이상하다'로만 나타나기 때문이다.
    constexpr const char* kCommonHlsl = R"(
cbuffer SSAOParams : register(b0)
{
    float4x4 gInverseProjection;
    float4x4 gProjection;    // 참조 경로가 표본을 클립으로 투영할 때 쓴다
    uint2    gSize;          // 반해상도 크기
    uint2    gFullSize;      // 원본 깊이 크기
    float    gRadius;
    float    gThickness;
    float    gIntensity;
    float    gDepthSigma;
    uint     gFrameIndex;
    uint3    gPad;
};

static const float kPi = 3.14159265f;

// 깊이(NDC)에서 뷰 공간 위치. w로 나누는 것을 잊으면 원근이 사라져
// 먼 곳의 AO가 통째로 틀리는데, 화면에서는 '멀리가 좀 이상하다'로만 보인다.
float3 ViewFromDepth(float2 uv, float depth)
{
    const float4 clip = float4(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f, depth, 1.0f);
    const float4 view = mul(clip, gInverseProjection);
    return view.xyz / view.w;
}

// 픽셀·프레임마다 도는 해시. 이웃끼리 다른 각을 보게 해서 잡음을 만들고,
// 그 잡음을 디노이즈가 걷어낸다 — 잡음을 안 만들려고 표본을 늘리는 것보다
// 이쪽이 싸다. 프레임 번호를 빼면 잡음이 화면에 박혀 지워지지 않는다.
float Hash21(uint2 pixel, uint frame)
{
    const float2 p = float2(pixel) + float2(frame * 0.7548f, frame * 0.5698f);
    return frac(sin(dot(p, float2(12.9898f, 78.233f))) * 43758.5453f);
}
)";

    // ── AO 컴퓨트 ──
    //
    // 방향마다 화면 공간으로 걸으며 32비트 가시성 마스크를 세운다.
    //
    // 왜 비트마스크인가: 가중치를 누적하는 방식은 같은 각도를 두 번 세기
    // 쉽다. 앞뒤로 겹친 가림막 둘이 같은 방향을 막고 있으면 기여가 두 번
    // 들어가 AO가 실제보다 어두워지는데, 그 오차는 '좀 어둡다'로만 보여
    // 원인을 특정할 수 없다. 마스크는 이미 세워진 비트를 다시 세우지
    // 않으므로 그 실수가 구조적으로 불가능하다.
    constexpr const char* kAOShader = R"(
Texture2D<float>  gDepth  : register(t0);
Texture2D<float4> gNormal : register(t1);

RWTexture2D<float2> gOutput : register(u0);

// 고도 구간 [minEl, maxEl]을 덮는 비트를 만든다.
//
// ★ 고도는 접평면 기준 0~pi/2다. 법선 기준 0~pi가 아니다.
//
//   처음에 0~pi로 나눴다가 평평한 벽의 AO가 0.59로 나왔다. 같은 평면 위의
//   이웃 표본은 법선과 정확히 pi/2를 이루므로, 0~pi 척도에서는 그 위쪽
//   절반(16비트)이 통째로 켜져 가림 0.5가 된다. 실측 1-0.590 = 0.41이
//   그 계산과 맞았다.
//
//   pi/2 너머는 표면 아래라 애초에 반구에 없다. 고도로 재면 같은 평면 위의
//   표본은 고도 0이고, 뒷면은 음수라 구간이 [0,0]으로 접혀 비트가 0이 된다 —
//   평면이 자기 자신을 가리지 않는다는 것이 식에서 저절로 나온다.
uint AngleBits(float minEl, float maxEl)
{
    const float kHalfPi = kPi * 0.5f;
    const int lo = int(floor(saturate(minEl / kHalfPi) * float(BITMASK_BITS)));
    const int hi = int(ceil(saturate(maxEl / kHalfPi) * float(BITMASK_BITS)));
    if (hi <= lo) return 0u;

    // (1<<32)은 정의되지 않으므로 32비트 전체는 따로 만든다.
    const uint hiMask = (hi >= int(BITMASK_BITS)) ? 0xFFFFFFFFu : ((1u << uint(hi)) - 1u);
    const uint loMask = (1u << uint(lo)) - 1u;
    return hiMask ^ loMask;
}

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= gSize)) return;

    const float2 uv = (float2(id.xy) + 0.5f) / float2(gSize);
    const int2 fullPixel = int2(uv * float2(gFullSize));

    const float depth = gDepth.Load(int3(fullPixel, 0));
    if (depth >= 1.0f)
    {
        // 하늘. 가릴 것이 없으므로 AO는 1이고, 깊이는 필터가 '경계'로
        // 읽도록 큰 값을 넣는다.
        gOutput[id.xy] = float2(1.0f, 1e4f);
        return;
    }

    const float3 viewPos = ViewFromDepth(uv, depth);
    const float3 normal = normalize(gNormal.Load(int3(fullPixel, 0)).xyz * 2.0f - 1.0f);

    // 반경을 화면 공간 픽셀 수로 옮긴다. 뷰 공간 반경이 화면에서 몇 픽셀인지는
    // 깊이에 반비례한다 — 이 환산을 빼먹으면 먼 물체에 과하게 넓은 AO가 걸린다.
    //
    // gInverseProjection[0][0]이 곧 1/(투영 x 스케일)이므로, 그 역수가
    // '뷰 공간 1단위가 NDC에서 차지하는 폭'이다.
    const float projScaleX = 1.0f / max(abs(gInverseProjection[0][0]), 1e-6f);
    const float radiusPixels = clamp(
        gRadius * projScaleX / max(abs(viewPos.z), 1e-4f) * 0.5f * float(gSize.x),
        2.0f, 128.0f);

    const float noise = Hash21(id.xy, gFrameIndex);
    float occlusion = 0.0f;

    [loop]
    for (uint d = 0; d < DIRECTIONS; ++d)
    {
        // 방향은 픽셀 해시로 돌린다. 등간격으로 나눈 뒤 통째로 회전시키므로
        // 이웃 픽셀끼리는 다른 각을, 한 픽셀 안에서는 고르게 퍼진 각을 본다.
        const float angle = (float(d) + noise) * kPi / float(DIRECTIONS);
        const float2 dir = float2(cos(angle), sin(angle));

        uint mask = 0u;

        [loop]
        for (uint s = 0; s < STEPS; ++s)
        {
            // 등비로 늘린다. 가까운 곳을 촘촘히 보는 쪽이 접촉 그림자에
            // 유리하고, 그것이 AO에서 눈에 띄는 부분이다.
            const float t = (float(s) + 1.0f + noise) / float(STEPS);
            const float stepPixels = radiusPixels * t * t;

            // 양쪽을 다 본다. 한쪽만 보면 방향 수를 두 배로 늘려야 같은
            // 각도 범위를 덮는데, 표본은 어차피 반대쪽에도 있으므로 그건 낭비다.
            [unroll]
            for (int side = 0; side < 2; ++side)
            {
                const float2 offset = dir * stepPixels * ((0 == side) ? 1.0f : -1.0f);
                const float2 sampleUV = uv + offset / float2(gSize);
                if (any(sampleUV < 0.0f) || any(sampleUV > 1.0f)) continue;

                const int2 samplePixel = int2(sampleUV * float2(gFullSize));
                const float sampleDepth = gDepth.Load(int3(samplePixel, 0));
                if (sampleDepth >= 1.0f) continue;

                const float3 samplePos = ViewFromDepth(sampleUV, sampleDepth);
                const float3 delta = samplePos - viewPos;
                const float dist = length(delta);
                if (dist < 1e-5f || dist > gRadius) continue;

                // 표면 반구 안에서의 각도. 법선과 이루는 각이 0이면 정면,
                // pi/2면 지평선이다.
                const float3 dirToSample = delta / dist;
                const float cosTheta = dot(normal, dirToSample);

                // ★ 두께로 뒤쪽 구간을 자른다.
                //
                // 화면 공간에서는 물체의 뒷면을 볼 수 없다. 자르지 않으면
                // 멀리 있는 표본이 '무한히 두꺼운 가림막'이 되어 배경 전체가
                // 어두워지는데, 그 증상은 '전반적으로 어둡다'로만 보여
                // 원인을 짚기 어렵다.
                const float3 backPos = samplePos + normalize(samplePos) * gThickness;
                const float3 backDelta = backPos - viewPos;
                const float backDist = max(length(backDelta), 1e-5f);
                const float cosThetaBack = dot(normal, backDelta / backDist);

                // 접평면 기준 고도. asin이라 표면 아래(음수)가 그대로 음수로
                // 나오고, AngleBits의 saturate가 그것을 0으로 접는다.
                const float elevationNear = asin(clamp(cosTheta, -1.0f, 1.0f));
                const float elevationFar = asin(clamp(cosThetaBack, -1.0f, 1.0f));

                mask |= AngleBits(min(elevationNear, elevationFar),
                    max(elevationNear, elevationFar));
            }
        }

        occlusion += float(countbits(mask)) / float(BITMASK_BITS);
    }

    occlusion /= float(DIRECTIONS);

    const float ao = saturate(1.0f - occlusion * gIntensity);
    gOutput[id.xy] = float2(ao, viewPos.z);
}
)";


    // ── 참조 경로: 기존 반구 커널 ──
    //
    // 성능 비교의 기준선. 기존 DX11 SSAO가 하던 것을 그대로 옮겼다 —
    // 커널 64개, 표본마다 클립 투영 한 번과 깊이 역투영 한 번.
    //
    // 실제 DX11 패스와 직접 재지 않는 이유는 그러면 API 차이가 수에 섞여
    // '무엇 때문에 빠른가'를 알 수 없기 때문이다. 같은 디바이스·같은 입력
    // 위에서 알고리즘만 갈아 끼운다.
    //
    // 커널은 셰이더 안에서 해시로 만든다. 상수 버퍼로 64개를 넘기던 원본과
    // 표본 분포는 다르지만, 재는 것은 '표본 하나당 비용'이라 분포는 결과에
    // 영향을 주지 않는다.
    constexpr const char* kReferenceShader = R"(
Texture2D<float>  gDepth  : register(t0);
Texture2D<float4> gNormal : register(t1);

RWTexture2D<float2> gOutput : register(u0);

float3 KernelSample(uint i, uint2 pixel)
{
    const float a = frac(sin(float(i) * 12.9898f + 1.0f) * 43758.5453f);
    const float b = frac(sin(float(i) * 78.2330f + 2.0f) * 43758.5453f);
    const float c = frac(sin(float(i) * 37.7190f + 3.0f) * 43758.5453f);

    // 반구 안쪽으로 몰아 담는다(원본과 같은 방식).
    float3 v = float3(a * 2.0f - 1.0f, b * 2.0f - 1.0f, c);
    v = normalize(v) * lerp(0.1f, 1.0f, (float(i) / 64.0f) * (float(i) / 64.0f));
    return v;
}

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= gSize)) return;

    const float2 uv = (float2(id.xy) + 0.5f) / float2(gSize);
    const int2 fullPixel = int2(uv * float2(gFullSize));

    const float depth = gDepth.Load(int3(fullPixel, 0));
    if (depth >= 1.0f)
    {
        gOutput[id.xy] = float2(1.0f, 1e4f);
        return;
    }

    const float3 viewPos = ViewFromDepth(uv, depth);
    const float3 normal = normalize(gNormal.Load(int3(fullPixel, 0)).xyz * 2.0f - 1.0f);

    // TBN
    const float3 up = (abs(normal.z) < 0.999f) ? float3(0, 0, 1) : float3(0, 1, 0);
    const float3 tangent = normalize(cross(up, normal));
    const float3 bitangent = cross(normal, tangent);
    const float3x3 tbn = float3x3(tangent, bitangent, normal);

    float occlusion = 0.0f;

    [loop]
    for (uint i = 0; i < 64; ++i)
    {
        const float3 samplePos = viewPos + mul(KernelSample(i, id.xy), tbn) * gRadius;

        // ★ 표본마다 클립으로 투영한다. 이것이 옛 방식의 비용이다.
        const float4 clip = mul(float4(samplePos, 1.0f), gProjection);
        if (clip.w <= 0.0f) continue;

        const float2 sampleUV = float2(clip.x, -clip.y) / clip.w * 0.5f + 0.5f;
        if (any(sampleUV < 0.0f) || any(sampleUV > 1.0f)) continue;

        const int2 samplePixel = int2(sampleUV * float2(gFullSize));
        const float sceneDepth = gDepth.Load(int3(samplePixel, 0));

        // ★ 그리고 다시 뷰로 되돌린다. 표본당 행렬곱 둘이 여기서 나온다.
        const float3 scenePos = ViewFromDepth(sampleUV, sceneDepth);

        const float rangeCheck = smoothstep(0.0f, 1.0f,
            saturate(gRadius / max(abs(scenePos.z - viewPos.z), 1e-3f)));
        occlusion += ((scenePos.z < samplePos.z - gThickness) ? 1.0f : 0.0f) * rangeCheck;
    }

    occlusion /= 64.0f;

    const float ao = saturate(1.0f - occlusion * gIntensity);
    gOutput[id.xy] = float2(ao, viewPos.z);
}
)";

    // ── 디노이즈 ──
    //
    // 교차 양방향 필터. 깊이가 비슷한 이웃만 섞어 경계를 지킨다.
    //
    // 노멀을 안 쓰는 이유: AO는 이미 노멀을 반영한 값이고, 여기서 다시
    // 노멀로 가중치를 주면 같은 정보를 두 번 쓰는 셈이다. 깊이만으로
    // 부족하다는 실측이 나오면 그때 넣는다.
    constexpr const char* kFilterShader = R"(
Texture2D<float2> gInput : register(t0);

RWTexture2D<float2> gOutput : register(u0);

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= gSize)) return;

    const float2 center = gInput.Load(int3(id.xy, 0));
    const float centerDepth = center.y;

    float sum = 0.0f;
    float weightSum = 0.0f;

    [unroll]
    for (int dy = -FILTER_RADIUS; dy <= FILTER_RADIUS; ++dy)
    {
        [unroll]
        for (int dx = -FILTER_RADIUS; dx <= FILTER_RADIUS; ++dx)
        {
            const int2 pixel = int2(id.xy) + int2(dx, dy);
            if (any(pixel < 0) || any(pixel >= int2(gSize))) continue;

            const float2 neighbour = gInput.Load(int3(pixel, 0));

            // 깊이가 멀면 가중치를 0으로 떨어뜨린다. 이것이 없으면 물체
            // 경계에서 배경의 AO가 물체 위로 새어 나온다.
            const float depthDiff = abs(neighbour.y - centerDepth);
            const float weight = exp(-depthDiff * depthDiff
                / max(gDepthSigma * gDepthSigma, 1e-8f));

            sum += neighbour.x * weight;
            weightSum += weight;
        }
    }

    const float filtered = (weightSum > 1e-6f) ? (sum / weightSum) : center.x;
    gOutput[id.xy] = float2(filtered, centerDepth);
}
)";

    struct SSAOParams
    {
        Mathf::Matrix inverseProjection{};
        Mathf::Matrix projection{};
        uint32_t      sizeX{ 0 };
        uint32_t      sizeY{ 0 };
        uint32_t      fullSizeX{ 0 };
        uint32_t      fullSizeY{ 0 };
        float         radius{ 0.f };
        float         thickness{ 0.f };
        float         intensity{ 0.f };
        float         depthSigma{ 0.f };
        uint32_t      frameIndex{ 0 };
        uint32_t      pad[3]{};
    };

    bool CompileSsaoShader(const char* body, const D3D_SHADER_MACRO* defines,
        Microsoft::WRL::ComPtr<ID3DBlob>& outBlob, std::string& outError)
    {
        // 공통 조각을 앞에 붙여 한 덩어리로 컴파일한다. #include로 하려면
        // 인클루드 핸들러가 필요한데, 조각 하나 때문에 그것을 두는 것은
        // 얻는 것보다 늘어나는 것이 많다.
        const std::string source = std::string(kCommonHlsl) + body;

        Microsoft::WRL::ComPtr<ID3DBlob> errors;
        const HRESULT hr = D3DCompile(source.c_str(), source.size(), nullptr, defines,
            nullptr, "CSMain", "cs_5_0", 0, 0, &outBlob, &errors);
        if (FAILED(hr))
        {
            outError = "SSAO 셰이더 컴파일 실패: ";
            if (errors) outError += static_cast<const char*>(errors->GetBufferPointer());
            else        outError += SsaoHrToString(hr);
            return false;
        }
        return true;
    }
}

bool EnhancedSSAOPass::Initialize(const EnhancedFrameContext& context, std::string& outError)
{
    if (nullptr == context.resources || nullptr == context.psoManager ||
        nullptr == context.rootSignatures)
    {
        outError = "SSAO 패스 컨텍스트가 불완전하다";
        return false;
    }

    return CreatePipelines(context, outError);
}

bool EnhancedSSAOPass::CreatePipelines(const EnhancedFrameContext& context, std::string& outError)
{
    // 두 셰이더가 같은 시그니처를 쓴다. AO는 SRV 둘, 필터는 하나지만
    // 넓은 쪽에 얹어도 비용이 없고, 시그니처를 나누면 패스 사이에서
    // 그것을 바꾸는 비용이 더 든다(SSGI와 같은 판단이다).
    D3D12_DESCRIPTOR_RANGE srvRange{};
    srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors = 2;
    srvRange.BaseShaderRegister = 0;

    D3D12_DESCRIPTOR_RANGE uavRange{};
    uavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    uavRange.NumDescriptors = 1;
    uavRange.BaseShaderRegister = 0;

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

    const std::string directions = std::to_string(kDirectionsPerPixel);
    const std::string steps = std::to_string(kStepsPerDirection);
    const std::string bits = std::to_string(kBitmaskBits);

    const D3D_SHADER_MACRO aoDefines[] = {
        { "DIRECTIONS", directions.c_str() },
        { "STEPS", steps.c_str() },
        { "BITMASK_BITS", bits.c_str() },
        { nullptr, nullptr },
    };

    ComPtr<ID3DBlob> aoBlob;
    if (!CompileSsaoShader(kAOShader, aoDefines, aoBlob, outError)) return false;

    DX12ComputePipelineDesc aoDesc{};
    aoDesc.csBytecode = aoBlob->GetBufferPointer();
    aoDesc.csSize = aoBlob->GetBufferSize();
    aoDesc.rootSignature = root.signature;
    aoDesc.rootSignatureId = root.id;

    m_aoPSO = context.psoManager->GetOrCreateCompute(aoDesc, outError);
    if (nullptr == m_aoPSO) return false;

    ComPtr<ID3DBlob> refBlob;
    if (!CompileSsaoShader(kReferenceShader, nullptr, refBlob, outError)) return false;

    DX12ComputePipelineDesc refDesc{};
    refDesc.csBytecode = refBlob->GetBufferPointer();
    refDesc.csSize = refBlob->GetBufferSize();
    refDesc.rootSignature = root.signature;
    refDesc.rootSignatureId = root.id;

    m_referencePSO = context.psoManager->GetOrCreateCompute(refDesc, outError);
    if (nullptr == m_referencePSO) return false;

    // 필터 반경 1(3x3). 반해상도에서 3x3이면 전 해상도 6x6에 해당하고,
    // 그보다 넓히면 접촉 그림자가 뭉개진다 — 실측으로 바꿀 근거가 생기면 바꾼다.
    const D3D_SHADER_MACRO filterDefines[] = {
        { "FILTER_RADIUS", "1" },
        { nullptr, nullptr },
    };

    ComPtr<ID3DBlob> filterBlob;
    if (!CompileSsaoShader(kFilterShader, filterDefines, filterBlob, outError)) return false;

    DX12ComputePipelineDesc filterDesc{};
    filterDesc.csBytecode = filterBlob->GetBufferPointer();
    filterDesc.csSize = filterBlob->GetBufferSize();
    filterDesc.rootSignature = root.signature;
    filterDesc.rootSignatureId = root.id;

    m_filterPSO = context.psoManager->GetOrCreateCompute(filterDesc, outError);
    return nullptr != m_filterPSO;
}

bool EnhancedSSAOPass::PrepareFrame(const EnhancedFrameContext& context, std::string& outError)
{
    (void)outError;

    // 참조 경로는 전 해상도로 돈다. 반해상도는 새 방식이 가져가는 이득의
    // 일부라, 기준선에까지 적용하면 그 이득이 수에서 사라진다.
    const uint32_t divisor = m_useReferencePath ? 1u : kResolutionDivisor;
    m_width = (context.width + divisor - 1) / divisor;
    m_height = (context.height + divisor - 1) / divisor;
    return true;
}

void EnhancedSSAOPass::Declare(EnhancedRenderGraph& graph, const EnhancedFrameContext& context)
{
    m_output = RGHandle{};
    m_rawOutput = RGHandle{};

    if (!m_inputs.depth.IsValid() || !m_inputs.normal.IsValid() ||
        nullptr == m_aoPSO || nullptr == m_filterPSO ||
        0 == m_width || 0 == m_height)
    {
        return;
    }

    RGTextureDesc aoDesc{};
    aoDesc.width = m_width;
    aoDesc.height = m_height;
    aoDesc.format = ToDXGI(kAOFormat);
    aoDesc.allowUnorderedAccess = true;
    aoDesc.name = "SSAO.Raw";
    m_rawOutput = graph.CreateTexture(aoDesc);

    RGTextureDesc filteredDesc = aoDesc;
    filteredDesc.name = "SSAO.Filtered";
    m_output = graph.CreateTexture(filteredDesc);

    // 상수는 두 패스가 같은 것을 쓴다. 한 번 만들어 둘 다 가리키게 하면
    // 값이 갈릴 자리가 없어진다 — SSGI에서 크기 상수를 패스마다 따로
    // 채우다가 필터가 다른 해상도를 본 적이 있다.
    const auto fillParams = [this, &context]() -> SSAOParams
    {
        SSAOParams params{};
        if (nullptr != context.camera)
        {
            params.inverseProjection = XMMatrixTranspose(
                XMMatrixInverse(nullptr, context.camera->projection));
            params.projection = XMMatrixTranspose(context.camera->projection);
        }
        params.sizeX = m_width;
        params.sizeY = m_height;
        params.fullSizeX = context.width;
        params.fullSizeY = context.height;
        params.radius = m_tuning.radius;
        params.thickness = m_tuning.thickness;
        params.intensity = m_tuning.intensity;
        params.depthSigma = m_tuning.filterDepthSigma;
        params.frameIndex = m_frameIndex;
        return params;
    };

    // ── AO ──
    graph.AddPass("SSAO.Compute",
        { { m_inputs.depth, RGResourceState::ShaderResource },
          { m_inputs.normal, RGResourceState::ShaderResource },
          { m_rawOutput, RGResourceState::UnorderedAccess } },
        [this, &context, fillParams](const EnhancedRenderGraph::ExecuteContext& executeContext)
        {

            const SSAOParams params = fillParams();
            const auto cb = context.resources->GetUploadRing().Allocate(
                sizeof(SSAOParams), DX12UploadRing::kConstantBufferAlignment);
            if (!cb.IsValid()) return;
            memcpy(cb.cpuAddress, &params, sizeof(params));

            // 테이블 둘을 잘라 받는다(R2) — 루트 파라미터가 SRV·UAV로 나뉘어 있다.
            const RHIBindingDesc srvs[] = {
                RHIBindingDesc::SrvDepth(executeContext.ResolveHandle(m_inputs.depth)),
                RHIBindingDesc::Srv(executeContext.ResolveHandle(m_inputs.normal)),
            };
            const RHIBindingDesc uavs[] = {
                RHIBindingDesc::Uav2D(executeContext.ResolveHandle(m_rawOutput), ToDXGI(kAOFormat)),
            };
            const RHIBindingTable srvTable = context.resources->CreateBindings(srvs);
            const RHIBindingTable uavTable = context.resources->CreateBindings(uavs);
            if (!srvTable.IsValid() || !uavTable.IsValid()) return;

            RHIEncoder& encoder = *executeContext.encoder;
            encoder.SetPipeline(RHIBindPoint::Compute,
                m_useReferencePath ? m_referencePSO : m_aoPSO, m_rootSignature);
            encoder.SetConstantBuffer(RHIBindPoint::Compute, 0, cb.gpuAddress);
            encoder.SetBindings(RHIBindPoint::Compute, 1, srvTable);
            encoder.SetBindings(RHIBindPoint::Compute, 2, uavTable);

            encoder.Dispatch((m_width + 7) / 8, (m_height + 7) / 8, 1);
        });

    // ── 디노이즈 ──
    graph.AddPass("SSAO.Filter",
        { { m_rawOutput, RGResourceState::ShaderResource },
          { m_output, RGResourceState::UnorderedAccess } },
        [this, &context, fillParams](const EnhancedRenderGraph::ExecuteContext& executeContext)
        {

            const SSAOParams params = fillParams();
            const auto cb = context.resources->GetUploadRing().Allocate(
                sizeof(SSAOParams), DX12UploadRing::kConstantBufferAlignment);
            if (!cb.IsValid()) return;
            memcpy(cb.cpuAddress, &params, sizeof(params));

            // ★ SRV 슬롯 수는 고정이다.
            //
            // 필터는 t0 하나만 쓰지만 테이블은 둘을 자른다. 조건에 따라
            // 슬롯 수를 바꾸면 레지스터가 밀리는데, SSGI에서 그것으로
            // 누적이 조용히 죽은 적이 있다. 안 쓰는 슬롯도 같은 것으로
            // 채워 두면 디스크립터 힙에 쓰레기가 남지 않는다.
            const RHITextureHandle raw = executeContext.ResolveHandle(m_rawOutput);
            const RHIBindingDesc srvs[] = {
                RHIBindingDesc::Srv2D(raw, ToDXGI(kAOFormat)),
                RHIBindingDesc::Srv2D(raw, ToDXGI(kAOFormat)),
            };
            const RHIBindingDesc uavs[] = {
                RHIBindingDesc::Uav2D(executeContext.ResolveHandle(m_output), ToDXGI(kAOFormat)),
            };
            const RHIBindingTable srvTable = context.resources->CreateBindings(srvs);
            const RHIBindingTable uavTable = context.resources->CreateBindings(uavs);
            if (!srvTable.IsValid() || !uavTable.IsValid()) return;

            RHIEncoder& encoder = *executeContext.encoder;
            encoder.SetPipeline(RHIBindPoint::Compute, m_filterPSO, m_rootSignature);
            encoder.SetConstantBuffer(RHIBindPoint::Compute, 0, cb.gpuAddress);
            encoder.SetBindings(RHIBindPoint::Compute, 1, srvTable);
            encoder.SetBindings(RHIBindPoint::Compute, 2, uavTable);

            encoder.Dispatch((m_width + 7) / 8, (m_height + 7) / 8, 1);
        });
}

void EnhancedSSAOPass::Shutdown()
{
    m_width = 0;
    m_height = 0;
    m_frameIndex = 0;
    m_aoPSO = nullptr;
    m_referencePSO = nullptr;
    m_filterPSO = nullptr;
    m_rootSignature = nullptr;
}

#endif
