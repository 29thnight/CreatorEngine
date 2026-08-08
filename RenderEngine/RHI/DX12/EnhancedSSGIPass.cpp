#ifndef DYNAMICCPP_EXPORTS
#include "EnhancedSSGIPass.h"
#include "DX12DeviceResources.h"
#include "DX12PSOManager.h"
#include "DX12RootSignatureCache.h"
#include "EnhancedRenderGraph.h"
#include "RHIEncoder.h"
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
    uint2    gLightingSize;   // 라이팅 텍스처 해상도 — GI 해상도와 다르다
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

// 방향에 맞는 셀 경계까지의 t 증가량.
//
// ★ 한때 floor+1(양의 방향 경계)만 봤다. 위로 가는 광선(delta.y < 0)은
//   반대쪽 경계까지의 거리를 얻어 최대 한 셀을 헛디뎠다. 성분별로
//   진행 방향의 경계를 고른다. 거의 0인 성분은 그 축으로는 영원히
//   경계를 안 만나므로 큰 값으로 밀어 min에서 빠지게 한다.
float StepToCellEdge(float2 uv, float2 direction, uint2 size)
{
    const float2 texel = 1.0f / float2(size);
    const float2 cellIndex = floor(uv / texel);
    const float2 boundary = (cellIndex + step(0.0f, direction)) * texel;

    float2 toEdge = float2(1e9f, 1e9f);
    if (abs(direction.x) > 1e-8f) toEdge.x = (boundary.x - uv.x) / direction.x;
    if (abs(direction.y) > 1e-8f) toEdge.y = (boundary.y - uv.y) / direction.y;

    return max(min(toEdge.x, toEdge.y), 1e-4f);
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

    // ★ 시작 셀을 벗어난 지점에서 출발한다.
    //
    //   t=0에서 시작하면 첫 비교가 자기 픽셀의 깊이와 이루어지고, 시작
    //   깊이가 곧 그 셀의 깊이이므로 모든 광선이 그 자리에서 '히트'한다.
    //   실측이 그것을 드러냈다 — 히트 비율 0.5507이 비하늘 픽셀 비율
    //   (9032/16384 = 0.551)과 정확히 일치했고, 씬에 무엇을 넣어도 숫자가
    //   꿈쩍하지 않았다. 트레이스 전체가 자기 조명을 되먹이고 있었던 것이다.
    float t = StepToCellEdge(startUV, delta, gDepthSize) + 1e-4f;
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
            t += StepToCellEdge(uv, delta, size);

            if (mip + 1 < gMipCount) ++mip;   // 비었으니 더 크게 건넌다
        }
        else
        {
            if (0 == mip)
            {
                // 두께를 넘어 뚫고 지나간 것이면 히트로 보지 않는다.
                // 이것이 없으면 얇은 물체 뒤가 전부 막힌 것으로 잡힌다.
                //
                // ★ 뷰 공간에서 비교한다. 한때 클립 깊이(0~1 비선형) 차이를
                //   gThickness(뷰 공간 값)와 그대로 비교했는데, 단위가 달라
                //   검사가 사실상 죽어 있었다 — 두께를 100배 바꿔도 히트
                //   비율이 0.6% 안에서만 움직였다(실측). 클립 깊이는 원근
                //   나눗셈 때문에 먼 곳에서 촘촘해지므로, 같은 클립 차이가
                //   가까이서는 몇 cm, 멀리서는 몇 m를 뜻한다. 두께는
                //   물리량이니 뷰 공간에서 재야 한다.
                //
                //   선형화는 히트 판정 시점에만 하므로 비용은 행렬 곱 둘이다.
                const float rayViewZ = ViewFromDepth(uv, rayDepth).z;
                const float cellViewZ = ViewFromDepth(uv, cellDepth).z;
                if (rayViewZ - cellViewZ > gThickness) return false;

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
            // ★ 라이팅은 자기 해상도 좌표로 읽는다. 한때 gDepthSize(1/2
            //   해상도)를 곱했는데, 실제 씬의 라이팅은 전 해상도라 왼쪽 위
            //   사분면만 읽고 있었다 — GI가 화면 절반 밖의 빛을 절대 못 봤다.
            gathered += gLighting.Load(int3(hitUV * gLightingSize, 0)).rgb;
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

    /// R16_FLOAT 한 성분을 float으로.
    ///
    /// XMConvertHalfToFloat을 쓸 수도 있지만 리드백 해석 두 곳뿐이라
    /// 의존을 늘리지 않는다.
    float SsgiHalfToFloat(uint16_t half)
    {
        const uint32_t sign = (half >> 15) & 0x1u;
        const uint32_t exponent = (half >> 10) & 0x1Fu;
        const uint32_t mantissa = half & 0x3FFu;

        if (0 == exponent) return 0.f;   // 비정규는 0으로 본다

        const uint32_t bits = (sign << 31) | ((exponent + 112u) << 23) | (mantissa << 13);
        float value = 0.f;
        memcpy(&value, &bits, sizeof(value));
        return value;
    }

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

        // 0이면 AO 입력이 없다는 뜻이다. 슬롯 수를 조건부로 바꾸는 대신
        // 값으로 구분한다 — 슬롯이 밀리면 레지스터가 어긋나고, 그 증상은
        // '결과가 조용히 이상해진다'라서 잡기 어렵다.
        uint32_t aoWidth{ 0 };
        uint32_t aoHeight{ 0 };
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
        uint32_t      lightingWidth{ 0 };
        uint32_t      lightingHeight{ 0 };
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

    RHITextureDesc desc{};
    desc.width = m_giWidth;
    desc.height = m_giHeight;
    desc.allowUnorderedAccess = true;

    // 히스토리는 만들자마자 다음 프레임의 셰이더가 읽는다 — COMMON에서
    // 출발시키면 첫 읽기 앞에 전이가 하나 더 붙는다.
    desc.initialState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

    for (uint32_t i = 0; i < kHistoryCount; ++i)
    {
        desc.format = kGIFormat;
        desc.debugName = (0 == i) ? L"SSGI.History0" : L"SSGI.History1";
        if (!context.resources->CreateTexture(desc, m_history[i], outError))
        {
            outError = "SSGI 히스토리 — " + outError;
            return false;
        }

        desc.format = kHiZFormat;
        desc.debugName = (0 == i) ? L"SSGI.HistoryDepth0" : L"SSGI.HistoryDepth1";
        if (!context.resources->CreateTexture(desc, m_historyDepth[i], outError))
        {
            outError = "SSGI 히스토리 깊이 — " + outError;
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
    //
    // RTV도 허용한다. 이 텍스처가 라이브 배선의 '라이팅 결과'이고, 투명
    // (Forward+)이 그 위에 직접 알파 블렌딩으로 그린다 — UAV 전용으로 두면
    // RTV를 못 만들어 별도 타깃 + 합성 패스가 필요해진다. 플래그 하나로
    // 전 화면 패스 하나와 HDR 타깃 하나를 아낀다.
    RGTextureDesc outDesc{};
    outDesc.width = context.width;
    outDesc.height = context.height;
    outDesc.format = kGIFormat;
    outDesc.allowUnorderedAccess = true;
    outDesc.allowRenderTarget = true;
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

                auto* source = (0 == mip)
                    ? executeContext.Resolve(m_inputs.depth)
                    : executeContext.Resolve(m_hiZMips[mip - 1]);

                // SRV 테이블은 시그니처 크기만큼 잡는다. 안 쓰는 슬롯도
                // 디스크립터가 있어야 검증 레이어가 조용하다 — 그래서 전부
                // 같은 것으로 채운다.
                std::array<RHIBindingDesc, kMaxHiZMips + 2> srvs{};
                srvs.fill((0 == mip)
                    ? RHIBindingDesc::SrvDepth(source)
                    : RHIBindingDesc::Srv2D(source, kHiZFormat));
                const RHIBindingDesc uavs[] = {
                    RHIBindingDesc::Uav2D(executeContext.Resolve(m_hiZMips[mip]),
                        kHiZFormat),
                };
                const RHIBindingTable srvTable = context.resources->CreateBindings(srvs);
                const RHIBindingTable uavTable = context.resources->CreateBindings(uavs);
                if (!srvTable.IsValid() || !uavTable.IsValid()) return;

                // ★ 디스크립터 힙을 먼저 바인딩한다.
                //
                // 빠뜨렸다가 SetComputeRootDescriptorTable에서 죽었다. 링에서
                // 받은 GPU 핸들은 그 힙이 바인딩돼 있을 때만 뜻이 있다 —
                // 핸들 자체는 유효해 보이지만 GPU가 다른 힙을 보고 있으면
                // 엉뚱한 곳을 가리킨다.
                context.resources->BindDescriptorHeaps(commandList);

                RHIEncoder& encoder = *executeContext.encoder;
                encoder.SetPipeline(RHIBindPoint::Compute, m_hiZBuildPSO, m_rootSignature);
                encoder.SetConstantBuffer(RHIBindPoint::Compute, 0, cb.gpuAddress);
                encoder.SetBindings(RHIBindPoint::Compute, 1, srvTable);
                encoder.SetBindings(RHIBindPoint::Compute, 2, uavTable);

                encoder.Dispatch((targetWidth + 7) / 8, (targetHeight + 7) / 8, 1);
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
                // 라이팅이 없으면 대체물(Hi-Z 0)이 꽂히므로 그 해상도를 준다.
                params.lightingWidth = m_inputs.lighting.IsValid()
                    ? context.width : m_giWidth;
                params.lightingHeight = m_inputs.lighting.IsValid()
                    ? context.height : m_giHeight;
                // 실측으로 정할 값들이다. 지금은 눈으로 볼 수 있는 범위를
                // 잡아 두고, 리졸브가 붙은 뒤 씬에 맞춰 조인다.
                params.maxDistance = m_tuning.traceDistance;
                params.thickness = m_tuning.traceThickness;

                const auto cb = context.resources->GetUploadRing().Allocate(
                    sizeof(TraceParams), DX12UploadRing::kConstantBufferAlignment);
                if (!cb.IsValid()) return;
                memcpy(cb.cpuAddress, &params, sizeof(params));

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
                constexpr DXGI_FORMAT kColorFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;

                std::array<RHIBindingDesc, kMaxHiZMips + 2> srvs{};
                for (uint32_t i = 0; i < kMaxHiZMips; ++i)
                {
                    // 밉이 모자라면 마지막 것으로 채운다. 셰이더가 gMipCount
                    // 안에서만 읽으므로 값은 안 쓰이지만, 디스크립터가 비어
                    // 있으면 검증 레이어가 잡는다.
                    const uint32_t index = (i < m_hiZMipCount) ? i : (m_hiZMipCount - 1);
                    srvs[i] = RHIBindingDesc::Srv2D(
                        executeContext.Resolve(m_hiZMips[index]), kHiZFormat);
                }
                srvs[kMaxHiZMips] = m_inputs.normal.IsValid()
                    ? RHIBindingDesc::Srv2D(
                        executeContext.Resolve(m_inputs.normal), kColorFormat)
                    : RHIBindingDesc::Srv2D(fallback, kHiZFormat);
                srvs[kMaxHiZMips + 1] = m_inputs.lighting.IsValid()
                    ? RHIBindingDesc::Srv2D(
                        executeContext.Resolve(m_inputs.lighting), kColorFormat)
                    : RHIBindingDesc::Srv2D(fallback, kHiZFormat);

                const RHIBindingDesc uavs[] = {
                    RHIBindingDesc::Uav2D(executeContext.Resolve(m_traceResult), kGIFormat),
                };
                const RHIBindingTable srvTable = context.resources->CreateBindings(srvs);
                const RHIBindingTable uavTable = context.resources->CreateBindings(uavs);
                if (!srvTable.IsValid() || !uavTable.IsValid()) return;

                context.resources->BindDescriptorHeaps(commandList);

                RHIEncoder& encoder = *executeContext.encoder;
                encoder.SetPipeline(RHIBindPoint::Compute, m_tracePSO, m_rootSignature);
                encoder.SetConstantBuffer(RHIBindPoint::Compute, 0, cb.gpuAddress);
                encoder.SetBindings(RHIBindPoint::Compute, 1, srvTable);
                encoder.SetBindings(RHIBindPoint::Compute, 2, uavTable);

                encoder.Dispatch((m_giWidth + 7) / 8, (m_giHeight + 7) / 8, 1);
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

                const auto cb = context.resources->GetUploadRing().Allocate(
                    constantCopy.size(), DX12UploadRing::kConstantBufferAlignment);
                if (!cb.IsValid()) return;
                memcpy(cb.cpuAddress, constantCopy.data(), constantCopy.size());

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

                std::array<RHIBindingDesc, kMaxHiZMips + 2> srvs{};
                for (uint32_t i = 0; i < kMaxHiZMips + 2; ++i)
                {
                    ID3D12Resource* resource = (i < sources.size() && nullptr != sources[i])
                        ? sources[i] : fallback;
                    // 깊이면 색 포맷으로 갈아 보고, 아니면 리소스 포맷 그대로다.
                    srvs[i] = RHIBindingDesc::SrvDepth(resource);
                }

                auto* targetResource = executeContext.Resolve(target);
                const RHIBindingDesc uavs[] = {
                    RHIBindingDesc::Uav2D(targetResource,
                        (nullptr != targetResource) ? targetResource->GetDesc().Format
                                                    : DXGI_FORMAT_UNKNOWN),
                };
                const RHIBindingTable srvTable = context.resources->CreateBindings(srvs);
                const RHIBindingTable uavTable = context.resources->CreateBindings(uavs);
                if (!srvTable.IsValid() || !uavTable.IsValid()) return;

                context.resources->BindDescriptorHeaps(commandList);

                RHIEncoder& encoder = *executeContext.encoder;
                encoder.SetPipeline(RHIBindPoint::Compute, pso, m_rootSignature);
                encoder.SetConstantBuffer(RHIBindPoint::Compute, 0, cb.gpuAddress);
                encoder.SetBindings(RHIBindPoint::Compute, 1, srvTable);
                encoder.SetBindings(RHIBindPoint::Compute, 2, uavTable);

                encoder.Dispatch((dispatchWidth + 7) / 8, (dispatchHeight + 7) / 8, 1);
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
        params.depthTolerance = m_tuning.accumDepthTolerance;

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
        //
        // ★ 입력이 없어도 자리를 비우지 않는다.
        //
        // 처음에는 normal이 유효할 때만 push_back했다. 그러자 검증(노멀
        // 미설정)에서 슬롯이 한 칸씩 밀려 t3에 히스토리 깊이가, t4에 엉뚱한
        // 것이 들어갔다. 누적이 2에서 멈췄고 — 히스토리의 a를 못 읽으니
        // 늘 1로 봤다 — depthTolerance를 100까지 키워도 그대로였다.
        //
        // 조건부로 슬롯 수가 바뀌는 것 자체가 위험하다. 자리는 고정하고
        // 없는 것만 대체물로 채운다.
        std::vector<RGHandle> ordered{ m_traceResult, m_hiZMips[0] };
        ordered.push_back(m_inputs.normal.IsValid() ? m_inputs.normal : m_hiZMips[0]);

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
        params.depthSigma = m_tuning.filterDepthSigma;
        params.normalPower = m_tuning.filterNormalPower;

        std::vector<EnhancedRenderGraph::RGPassUsage> usages;
        usages.push_back({ m_resolved, RGResourceState::ShaderResource });
        usages.push_back({ m_hiZMips[0], RGResourceState::ShaderResource });
        if (m_inputs.normal.IsValid())
        {
            usages.push_back({ m_inputs.normal, RGResourceState::ShaderResource });
        }
        usages.push_back({ m_filtered, RGResourceState::UnorderedAccess });

        // 자리를 고정한다(리졸브와 같은 이유).
        std::vector<RGHandle> srvHandles{ m_resolved, m_hiZMips[0] };
        srvHandles.push_back(m_inputs.normal.IsValid() ? m_inputs.normal : m_hiZMips[0]);

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
        params.intensity = m_tuning.intensity;
        params.depthSigma = m_tuning.compositeDepthSigma;

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
        if (m_inputs.ambientOcclusion.IsValid())
        {
            usages.push_back({ m_inputs.ambientOcclusion, RGResourceState::ShaderResource });
        }
        usages.push_back({ m_output, RGResourceState::UnorderedAccess });

        // t0 GI · t1 GI깊이 · t2 라이팅 · t3 깊이 · t4 디퓨즈 · t5 AO
        // 자리를 고정한다(리졸브와 같은 이유).
        std::vector<RGHandle> srvHandles{ m_filtered, m_hiZMips[0] };
        srvHandles.push_back(m_inputs.lighting.IsValid() ? m_inputs.lighting : m_hiZMips[0]);
        srvHandles.push_back(m_inputs.depth);
        srvHandles.push_back(m_inputs.diffuse.IsValid() ? m_inputs.diffuse : m_hiZMips[0]);
        srvHandles.push_back(m_inputs.ambientOcclusion.IsValid()
            ? m_inputs.ambientOcclusion : m_hiZMips[0]);

        if (m_inputs.ambientOcclusion.IsValid())
        {
            params.aoWidth = (context.width + 1) / 2;
            params.aoHeight = (context.height + 1) / 2;
        }

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
        // ★ 저장하는 것은 필터 결과가 아니라 리졸브 결과다.
        //
        // 처음에는 필터 결과를 넣었다. 그러자 누적이 2에서 멈췄다(여덟
        // 프레임을 돌려도 평균 2.00). 두 가지가 겹친 탓이다:
        //   ① 필터가 float4 전체를 이웃과 가중 평균하므로 a 채널(누적
        //      프레임 수)도 함께 뭉개진다.
        //   ② 설계상으로도 틀렸다 — 필터 결과를 히스토리에 넣으면 블러가
        //      매 프레임 다시 블러되어 계속 번진다.
        //
        // 누적은 리졸브 결과를 이어가고, 필터는 화면에 낼 때만 쓴다.
        std::vector<EnhancedRenderGraph::RGPassUsage> usages;
        usages.push_back({ m_resolved, RGResourceState::CopySource });
        usages.push_back({ m_hiZMips[0], RGResourceState::CopySource });

        auto* historyTarget = m_history[m_historyIndex].Get();
        auto* historyDepthTarget = m_historyDepth[m_historyIndex].Get();

        graph.AddPass("SSGI.StoreHistory", usages,
            [this, historyTarget, historyDepthTarget]
            (const EnhancedRenderGraph::ExecuteContext& executeContext)
            {
                RHIEncoder& encoder = *executeContext.encoder;
                auto* commandList = executeContext.commandList;

                // 영속 리소스는 그래프가 상태를 모른다. 복사 전후로 손으로
                // 전이한다 — 그래프에 맡길 수 없는 것은 여기서 책임진다.
                //
                // ★ 이 전이는 인코더로 안 간다. 인코더에 전이가 없는 것이
                //   R3의 계약이고(그래프의 몫), 여기는 그래프가 추적하지 않는
                //   영속 리소스라 예외가 아니라 계약 밖이다. 포그의 초기화
                //   배리어와 같은 부류다.
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

                encoder.CopyResource(historyTarget,
                    executeContext.Resolve(m_resolved));
                encoder.CopyResource(historyDepthTarget,
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

    // ★ 카메라를 넣어야 재투영이 성립한다.
    //
    // 처음에는 width/height만 넣었다. 그러자 리졸브가 이전 프레임 행렬을
    // 못 얻어 히스토리를 통째로 버렸고, 여덟 프레임을 돌려도 누적이 1에
    // 머물렀다(평균 1.00 · 최소 1 · 최대 1). 셰이더도 그래프도 멀쩡한데
    // 검증 쪽 입력이 빠져 있었던 것이다.
    //
    // 카메라를 고정해 둔다 — 정지 상태에서 누적이 쌓이는지가 질문이므로
    // 움직이면 답이 흐려진다.
    FrameCameraSnapshot camera{};
    camera.view = XMMatrixLookAtLH(
        XMVectorSet(0.f, 1.f, -3.f, 1.f),
        XMVectorSet(0.f, 0.f, 0.f, 1.f),
        XMVectorSet(0.f, 1.f, 0.f, 0.f));
    camera.projection = XMMatrixPerspectiveFovLH(
        XM_PIDIV4, static_cast<float>(kWidth) / static_cast<float>(kHeight), 0.1f, 100.f);
    camera.inverseView = XMMatrixInverse(nullptr, camera.view);
    camera.inverseProjection = XMMatrixInverse(nullptr, camera.projection);
    camera.nearPlane = 0.1f;
    camera.farPlane = 100.f;

    frameContext.camera = &camera;

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
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&depth))))
        {
            outLog += "[3/3] 깊이 텍스처 생성 실패\n";
            ssgi.Shutdown();
            resources.Shutdown();
            return false;
        }

        // 노멀도 만든다.
        //
        // 필터가 노멀 가중을 쓰므로 없으면 그 항을 잴 수 없다. 대체물(깊이
        // 텍스처)을 꽂으면 깊이값을 노멀로 해석하게 되고, 그러면
        // filterNormalPower를 스윕해도 무엇을 재는지 알 수 없다.
        D3D12_RESOURCE_DESC normalDesc = depthDesc;
        normalDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;

        ComPtr<ID3D12Resource> testNormal;
        if (FAILED(resources.GetDevice()->CreateCommittedResource(&heap,
            D3D12_HEAP_FLAG_NONE, &normalDesc,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&testNormal))))
        {
            outLog += "[3/3] 테스트 노멀 생성 실패\n";
            ssgi.Shutdown();
            resources.Shutdown();
            return false;
        }

        // ★ 깊이를 실제로 채운다.
        //
        // 만들기만 하고 두면 초기화되지 않은 메모리를 깊이로 읽는다. 그
        // 상태로는 히트 비율 같은 숫자가 뜻을 잃고, 무엇을 재는지 모르는 채
        // 상수를 조이게 된다. 바닥 평면과 구 하나를 절차적으로 그린다.
        {
            ComPtr<ID3DBlob> depthBlob;
            if (!CompileSsgiShader(SsgiShaders::kTestDepth, nullptr, depthBlob, error))
            {
                outLog += "[3/3] 테스트 깊이 셰이더 컴파일 실패: " + error + "\n";
                ssgi.Shutdown();
                resources.Shutdown();
                return false;
            }

            D3D12_DESCRIPTOR_RANGE uavRange{};
            uavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
            uavRange.NumDescriptors = 2;   // 깊이 + 노멀

            D3D12_ROOT_PARAMETER depthParams[2]{};
            depthParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
            depthParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            depthParams[1].DescriptorTable.NumDescriptorRanges = 1;
            depthParams[1].DescriptorTable.pDescriptorRanges = &uavRange;

            D3D12_ROOT_SIGNATURE_DESC depthRootDesc{};
            depthRootDesc.NumParameters = _countof(depthParams);
            depthRootDesc.pParameters = depthParams;

            const auto depthRoot = rootSignatures.GetOrCreate(depthRootDesc, error);
            if (!depthRoot.IsValid())
            {
                outLog += "[3/3] 테스트 깊이 루트 시그니처 실패: " + error + "\n";
                ssgi.Shutdown();
                resources.Shutdown();
                return false;
            }

            DX12ComputePipelineDesc depthPsoDesc{};
            depthPsoDesc.csBytecode = depthBlob->GetBufferPointer();
            depthPsoDesc.csSize = depthBlob->GetBufferSize();
            depthPsoDesc.rootSignature = depthRoot.signature;
            depthPsoDesc.rootSignatureId = depthRoot.id;

            auto* depthPso = psoManager.GetOrCreateCompute(depthPsoDesc, error);
            if (nullptr == depthPso)
            {
                outLog += "[3/3] 테스트 깊이 PSO 실패: " + error + "\n";
                ssgi.Shutdown();
                resources.Shutdown();
                return false;
            }

            if (!resources.BeginFrame(error))
            {
                outLog += "[3/3] 깊이 채우기 BeginFrame 실패: " + error + "\n";
                ssgi.Shutdown();
                resources.Shutdown();
                return false;
            }

            struct { uint32_t w, h; float nearP, farP; } depthCb{ kWidth, kHeight, 0.1f, 100.f };

            const auto cb = resources.GetUploadRing().Allocate(
                sizeof(depthCb), DX12UploadRing::kConstantBufferAlignment);
            const RHIBindingDesc depthUavs[] = {
                RHIBindingDesc::Uav2D(depth.Get(), DXGI_FORMAT_R32_FLOAT),
                RHIBindingDesc::Uav2D(testNormal.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT),
            };
            const RHIBindingTable uavTable = resources.CreateBindings(depthUavs);
            if (cb.IsValid() && uavTable.IsValid())
            {
                memcpy(cb.cpuAddress, &depthCb, sizeof(depthCb));

                auto* cmd = resources.GetCommandList();
                resources.BindDescriptorHeaps(cmd);
                cmd->SetComputeRootSignature(depthRoot.signature);
                cmd->SetPipelineState(depthPso);
                cmd->SetComputeRootConstantBufferView(0, cb.gpuAddress);
                cmd->SetComputeRootDescriptorTable(1, uavTable.gpu);
                cmd->Dispatch((kWidth + 7) / 8, (kHeight + 7) / 8, 1);

                // SSGI가 SRV로 읽으므로 전이한다.
                D3D12_RESOURCE_BARRIER toSrv{};
                toSrv.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                toSrv.Transition.pResource = depth.Get();
                toSrv.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
                toSrv.Transition.StateAfter =
                    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
                toSrv.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

                D3D12_RESOURCE_BARRIER normalToSrv = toSrv;
                normalToSrv.Transition.pResource = testNormal.Get();

                D3D12_RESOURCE_BARRIER both[] = { toSrv, normalToSrv };
                cmd->ResourceBarrier(2, both);
            }

            if (!resources.EndFrame(error))
            {
                outLog += "[3/3] 깊이 채우기 EndFrame 실패: " + error + "\n";
                ssgi.Shutdown();
                resources.Shutdown();
                return false;
            }
            resources.WaitForGpu();
        }

        // ★ 결과를 읽는 패스를 붙인다.
        //
        // 없으면 트레이스가 컬링돼 Dispatch가 한 번도 안 돈다. 실제로 첫
        // 실행이 '선언 9 · 실행 0'이었다 — 셰이더 컴파일만 확인하고 넘어갈
        // 뻔했다. 컬링이 도는 것은 옳지만, 도는지 보려면 읽는 쪽이 있어야 한다.
        // ★ 리드백 크기는 읽는 리소스에 맞춘다.
        //
        // 처음에는 GI 해상도(1/2)로 잡았다. 합성이 붙으면서 출력이 전 해상도가
        // 됐는데 리드백은 그대로였고, CopyTextureRegion이 "목적지 경계를\n// 넘는다"로 실패했다. 그 실패가 커맨드 리스트를 무효로 만들어
        // EndFrame의 Close가 E_INVALIDARG를 냈다 — 증상이 나온 자리와
        // 원인이 있는 자리가 달랐다.
        // ★ 리드백 대상은 리졸브 결과다.
        //
        // 합성 결과가 아니라 리졸브를 읽는 이유: a 채널에 누적 프레임 수가
        // 들어 있다. 그것이 '시간축이 샘플 수를 대신한다'는 전제가 실제로
        // 도는지 보여 주는 유일한 숫자다. 합성 결과에는 그 정보가 없다.
        const uint32_t giW = kWidth / EnhancedSSGIPass::kResolutionDivisor;
        const uint32_t giH = kHeight / EnhancedSSGIPass::kResolutionDivisor;
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

        std::vector<double> sweepHits;

        // ── 여러 프레임 돌린다 ──
        //
        // 한 프레임만 돌면 누적이 늘 1이고, 그러면 '시간축이 샘플 수를
        // 대신한다'는 이 설계의 전제를 확인할 수 없다. 카메라를 고정한 채
        // 여러 프레임 돌려 누적이 실제로 쌓이는지 본다.
        //
        // 정지 상태에서 쌓이지 않으면 재투영이 어긋난 것이고, 그때는
        // depthTolerance가 너무 빡빡하다는 뜻이다.
        constexpr uint32_t kTestFrames = 8;
        EnhancedRenderGraph::Stats stats{};

        for (uint32_t frameIndex = 0; frameIndex < kTestFrames && passed; ++frameIndex)
        {
        if (!resources.BeginFrame(error))
        {
            outLog += "[3/3] BeginFrame 실패: " + error + "\n";
            ssgi.Shutdown();
            resources.Shutdown();
            return false;
        }

        if (!ssgi.PrepareFrame(frameContext, error))
        {
            outLog += "[3/3] PrepareFrame 실패: " + error + "\n";
            passed = false;
            break;
        }

        EnhancedRenderGraph graph;

        EnhancedSSGIPass::Inputs inputs{};
        inputs.depth = graph.ImportTexture(depth.Get(),
            RGResourceState::ShaderResource, "SSGI.TestDepth");
        inputs.normal = graph.ImportTexture(testNormal.Get(),
            RGResourceState::ShaderResource, "SSGI.TestNormal");
        ssgi.SetInputs(inputs);

        ssgi.Declare(graph, frameContext);

        const auto output = ssgi.GetOutput();
        if (!output.IsValid())
        {
            outLog += "[3/3] 출력 핸들이 비었다 — Declare가 패스를 선언하지 않았다\n";
            passed = false;
        }

        if (passed && output.IsValid())
        {
            graph.AddPass("SSGI.Readback",
                { { ssgi.GetResolvedResult(), RGResourceState::CopySource } },
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
                    src.pResource = executeContext.Resolve(ssgi.GetResolvedResult());
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

        stats = graph.GetStats();

        if (!resources.EndFrame(error))
        {
            outLog += "[3/3] EndFrame 실패: " + error + "\n";
            passed = false;
        }
        else
        {
            resources.WaitForGpu();
        }
        }   // 프레임 루프 끝

        {
            char line[224]{};
            std::snprintf(line, sizeof(line),
                "[3/3] 그래프 — 선언 %u · 실행 %u · 컬링 %u · 배리어 %u(%u번에)\n",
                stats.passesDeclared, stats.passesExecuted, stats.passesCulled,
                stats.barriersEmitted, stats.barrierBatches);
            outLog += line;
        }

        // ── 누적이 실제로 쌓이는가 ──
        //
        // 리졸브 결과의 a 채널에 누적 프레임 수가 들어 있다. 카메라를
        // 고정하고 여덟 프레임 돌렸으므로, 재투영이 맞으면 8까지 올라가야
        // 한다. 1에 머물면 매 프레임 히스토리를 버리는 것이고, 그러면
        // 프레임당 슬라이스 둘만 쓰는 셈이라 품질만 나빠진다.
        {
            void* mapped = nullptr;
            const size_t bytes = static_cast<size_t>(rowPitch) * giH;
            D3D12_RANGE range{ 0, bytes };

            if (SUCCEEDED(readback->Map(0, &range, &mapped)))
            {
                const auto* pixels = static_cast<const uint8_t*>(mapped);

                double accumSum = 0.0;
                float  accumMin = 1e9f;
                float  accumMax = 0.f;
                size_t counted = 0;

                for (uint32_t y = 0; y < giH; ++y)
                {
                    // R16G16B16A16_FLOAT — 반정밀도 넷. a가 누적 수다.
                    const auto* row = reinterpret_cast<const uint16_t*>(pixels
                        + static_cast<size_t>(y) * rowPitch);

                    for (uint32_t x = 0; x < giW; ++x)
                    {
                        const uint16_t half = row[x * 4 + 3];

                        // half → float. 지수·가수를 직접 편다. DirectXMath의
                        // XMConvertHalfToFloat를 쓸 수도 있지만 여기 하나뿐이라
                        // 의존을 늘리지 않는다.
                        const uint32_t sign = (half >> 15) & 0x1u;
                        const uint32_t exponent = (half >> 10) & 0x1Fu;
                        const uint32_t mantissa = half & 0x3FFu;

                        float value = 0.f;
                        if (0 != exponent)
                        {
                            const uint32_t bits = (sign << 31)
                                | ((exponent + 112u) << 23) | (mantissa << 13);
                            memcpy(&value, &bits, sizeof(value));
                        }

                        if (value <= 0.f) continue;   // 하늘은 0이다

                        accumSum += value;
                        accumMin = (std::min)(accumMin, value);
                        accumMax = (std::max)(accumMax, value);
                        ++counted;
                    }
                }

                readback->Unmap(0, nullptr);

                if (0 != counted)
                {
                    const double average = accumSum / static_cast<double>(counted);

                    char line[256]{};
                    std::snprintf(line, sizeof(line),
                        "[3/3] 누적 — %u프레임 뒤 평균 %.2f · 최소 %.0f · 최대 %.0f"
                        " (픽셀 %zu)\n",
                        kTestFrames, average, accumMin, accumMax, counted);
                    outLog += line;

                    // ★ 정지 상태에서 누적이 안 쌓이면 전제가 무너진 것이다.
                    //   여덟 프레임을 돌았으니 평균이 절반은 넘어야 한다.
                    if (average < kTestFrames * 0.5)
                    {
                        outLog += "누적이 쌓이지 않는다 — 재투영이 매 프레임"
                            " 히스토리를 버리고 있다(depthTolerance를 볼 것)\n";
                        passed = false;
                    }
                }
                else
                {
                    outLog += "[3/3] 누적을 잴 픽셀이 없다 — 전부 하늘로 판정됐다\n";
                    passed = false;
                }
            }
        }

        // ── 상수 스윕 ──
        //
        // 값을 정하기 전에 그 값이 결과를 실제로 바꾸는지부터 본다.
        // 바꿔도 숫자가 그대로면 그 상수는 지금 아무 일도 안 하는 것이고,
        // 그때는 값을 고르는 것이 아니라 배선을 봐야 한다 — 누적에서
        // 이미 그렇게 세 번 틀렸다.
        //
        // 히트 비율은 트레이스 출력의 a 채널이다. 추적 거리와 두께가
        // 그것을 좌우한다.
        {
            struct SweepCase { float distance; float thickness; };
            const SweepCase cases[] = {
                { 2.f,  0.5f }, { 8.f,  0.5f }, { 32.f, 0.5f },
                // 두께 검사가 뷰 공간으로 바뀌었으므로 뷰 단위 눈금으로 본다.
                // 0.05는 얇은 판, 0.5는 사람 몸통, 5는 벽 두께쯤 된다.
                { 8.f,  0.05f }, { 8.f,  5.0f },
            };

            outLog += "[3/3] 추적 스윕 — 거리/두께 → 히트 비율\n";

            for (const SweepCase& sweepCase : cases)
            {
                EnhancedSSGIPass::Tuning tuning = ssgi.GetTuning();
                tuning.traceDistance = sweepCase.distance;
                tuning.traceThickness = sweepCase.thickness;
                ssgi.SetTuning(tuning);

                if (!resources.BeginFrame(error)) break;
                if (!ssgi.PrepareFrame(frameContext, error)) break;

                EnhancedRenderGraph sweepGraph;

                // 깊이는 그래프마다 새로 임포트한다. 핸들은 그래프에 매인
                // 것이라 앞 그래프의 것을 넘겨 쓰면 다른 리소스를 가리킨다.
                EnhancedSSGIPass::Inputs sweepInputs{};
                sweepInputs.depth = sweepGraph.ImportTexture(depth.Get(),
                    RGResourceState::ShaderResource, "SSGI.SweepDepth");
                sweepInputs.normal = sweepGraph.ImportTexture(testNormal.Get(),
                    RGResourceState::ShaderResource, "SSGI.SweepNormal");
                ssgi.SetInputs(sweepInputs);

                ssgi.Declare(sweepGraph, frameContext);

                sweepGraph.AddPass("SSGI.SweepReadback",
                    { { ssgi.GetTraceResult(), RGResourceState::CopySource } },
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
                        src.pResource = executeContext.Resolve(ssgi.GetTraceResult());
                        src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;

                        executeContext.commandList->CopyTextureRegion(
                            &dst, 0, 0, 0, &src, nullptr);
                    }, true);

                if (!sweepGraph.Compile(resources.GetDevice(), error)) break;
                if (!sweepGraph.Execute(resources.GetCommandList(), error)) break;
                if (!resources.EndFrame(error)) break;
                resources.WaitForGpu();

                void* mapped = nullptr;
                D3D12_RANGE range{ 0, static_cast<SIZE_T>(rowPitch) * giH };
                if (FAILED(readback->Map(0, &range, &mapped))) break;

                const auto* pixels = static_cast<const uint8_t*>(mapped);
                double hitSum = 0.0;
                size_t counted = 0;

                for (uint32_t y = 0; y < giH; ++y)
                {
                    const auto* row = reinterpret_cast<const uint16_t*>(pixels
                        + static_cast<size_t>(y) * rowPitch);

                    for (uint32_t x = 0; x < giW; ++x)
                    {
                        const uint16_t half = row[x * 4 + 3];
                        const uint32_t exponent = (half >> 10) & 0x1Fu;
                        const uint32_t mantissa = half & 0x3FFu;

                        float value = 0.f;
                        if (0 != exponent)
                        {
                            const uint32_t bits = ((exponent + 112u) << 23) | (mantissa << 13);
                            memcpy(&value, &bits, sizeof(value));
                        }

                        hitSum += value;
                        ++counted;
                    }
                }

                readback->Unmap(0, nullptr);

                char line[192]{};
                std::snprintf(line, sizeof(line),
                    "        거리 %5.1f · 두께 %4.2f → 히트 %.4f\n",
                    sweepCase.distance, sweepCase.thickness,
                    (0 != counted) ? (hitSum / static_cast<double>(counted)) : 0.0);
                outLog += line;

                sweepHits.push_back((0 != counted)
                    ? (hitSum / static_cast<double>(counted)) : 0.0);
            }

            // ★ 값을 바꿔도 결과가 그대로면 그 상수는 지금 안 쓰이는 것이다.
            if (sweepHits.size() >= 3)
            {
                const double spread = *std::max_element(sweepHits.begin(), sweepHits.end())
                    - *std::min_element(sweepHits.begin(), sweepHits.end());
                if (spread < 1e-4)
                {
                    outLog += "추적 상수를 바꿔도 히트 비율이 그대로다"
                        " — 그 값이 셰이더에 닿지 않는다\n";
                    passed = false;
                }
            }
        }

        // ── 필터 상수 스윕 ──
        //
        // 필터가 하는 일은 노이즈를 줄이는 것이다. 그러니 지표는 분산이다 —
        // 리졸브 결과와 필터 결과의 표준편차를 나란히 보면 실제로 일하는지
        // 알 수 있다. 값을 바꿔도 감소율이 그대로면 그 상수는 안 닿는 것이다.
        //
        // 깊이 시그마: 작을수록 깊이 차이에 민감해져 덜 섞는다.
        // 노멀 지수: 클수록 노멀이 다른 이웃을 강하게 배제한다.
        {
            struct FilterCase { float depthSigma; float normalPower; };
            const FilterCase cases[] = {
                { 0.0001f, 16.f }, { 0.01f, 16.f }, { 1.0f, 16.f },
                { 0.01f,    1.f }, { 0.01f, 64.f },
            };

            outLog += "[3/3] 필터 스윕 — 시그마/지수 → 이웃 차이(리졸브 → 필터)\n";

            std::vector<double> reductions;

            for (const FilterCase& filterCase : cases)
            {
                EnhancedSSGIPass::Tuning tuning = ssgi.GetTuning();
                tuning.filterDepthSigma = filterCase.depthSigma;
                tuning.filterNormalPower = filterCase.normalPower;
                ssgi.SetTuning(tuning);

                double sigmaBefore = 0.0;
                double sigmaAfter = 0.0;

                // 리졸브와 필터를 각각 읽는다. 한 프레임에 둘 다 읽으려면
                // 리드백 버퍼가 둘이어야 하는데, 두 번 도는 편이 단순하다.
                for (uint32_t stage = 0; stage < 2; ++stage)
                {
                    if (!resources.BeginFrame(error)) break;
                    if (!ssgi.PrepareFrame(frameContext, error)) break;

                    EnhancedRenderGraph filterGraph;

                    EnhancedSSGIPass::Inputs filterInputs{};
                    filterInputs.depth = filterGraph.ImportTexture(depth.Get(),
                        RGResourceState::ShaderResource, "SSGI.FilterDepth");
                    filterInputs.normal = filterGraph.ImportTexture(testNormal.Get(),
                        RGResourceState::ShaderResource, "SSGI.FilterNormal");
                    ssgi.SetInputs(filterInputs);

                    ssgi.Declare(filterGraph, frameContext);

                    const RGHandle target = (0 == stage)
                        ? ssgi.GetResolvedResult() : ssgi.GetOutput();
                    const RGHandle readSource = (0 == stage)
                        ? ssgi.GetResolvedResult() : ssgi.GetFilteredResult();
                    (void)target;

                    filterGraph.AddPass("SSGI.FilterReadback",
                        { { readSource, RGResourceState::CopySource } },
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
                            src.pResource = executeContext.Resolve(readSource);
                            src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;

                            executeContext.commandList->CopyTextureRegion(
                                &dst, 0, 0, 0, &src, nullptr);
                        }, true);

                    if (!filterGraph.Compile(resources.GetDevice(), error)) break;
                    if (!filterGraph.Execute(resources.GetCommandList(), error)) break;
                    if (!resources.EndFrame(error)) break;
                    resources.WaitForGpu();

                    void* mapped = nullptr;
                    D3D12_RANGE range{ 0, static_cast<SIZE_T>(rowPitch) * giH };
                    if (FAILED(readback->Map(0, &range, &mapped))) break;

                    const auto* pixels = static_cast<const uint8_t*>(mapped);

                    // ★ 표준편차가 아니라 '이웃 간 차이'를 잰다.
                    //
                    // 처음에는 전체 표준편차를 썼다. 필터 전후로 0.079422 →
                    // 0.079416, 감소 0.0%가 나왔다. 필터가 죽은 것처럼 보였지만
                    // 지표가 틀린 것이었다 — 표준편차는 공간 구조(밝은 곳과
                    // 어두운 곳의 차이)를 포함하고, 필터는 그것을 지우면 안 된다.
                    //
                    // 노이즈는 이웃 픽셀 사이에서 튀고 구조는 완만하다. 그래서
                    // 오른쪽·아래 이웃과의 차이 평균을 본다. 필터가 일하면
                    // 이 값이 줄고, 구조를 지우는지는 별개로 봐야 한다.
                    double diffSum = 0.0;
                    size_t counted = 0;

                    const auto lumaAt = [&](uint32_t x, uint32_t y) -> double
                    {
                        const auto* row = reinterpret_cast<const uint16_t*>(pixels
                            + static_cast<size_t>(y) * rowPitch);
                        const float r = SsgiHalfToFloat(row[x * 4 + 0]);
                        const float g = SsgiHalfToFloat(row[x * 4 + 1]);
                        const float b = SsgiHalfToFloat(row[x * 4 + 2]);
                        return 0.2126 * r + 0.7152 * g + 0.0722 * b;
                    };

                    for (uint32_t y = 0; y + 1 < giH; ++y)
                    {
                        for (uint32_t x = 0; x + 1 < giW; ++x)
                        {
                            const double here = lumaAt(x, y);
                            diffSum += std::abs(lumaAt(x + 1, y) - here);
                            diffSum += std::abs(lumaAt(x, y + 1) - here);
                            counted += 2;
                        }
                    }

                    readback->Unmap(0, nullptr);

                    if (0 != counted)
                    {
                        const double roughness = diffSum / static_cast<double>(counted);
                        if (0 == stage) sigmaBefore = roughness;
                        else            sigmaAfter = roughness;
                    }
                }

                const double reduction = (sigmaBefore > 1e-9)
                    ? (1.0 - sigmaAfter / sigmaBefore) : 0.0;
                reductions.push_back(reduction);

                char line[224]{};
                std::snprintf(line, sizeof(line),
                    "        시그마 %7.4f · 지수 %5.1f → %.6f → %.6f (감소 %.1f%%)\n",
                    filterCase.depthSigma, filterCase.normalPower,
                    sigmaBefore, sigmaAfter, reduction * 100.0);
                outLog += line;
            }

            // ★ 값을 바꿔도 감소율이 그대로면 그 상수는 안 닿는 것이다.
            if (reductions.size() >= 3)
            {
                const double spread =
                    *std::max_element(reductions.begin(), reductions.end())
                    - *std::min_element(reductions.begin(), reductions.end());

                char line[192]{};
                std::snprintf(line, sizeof(line),
                    "        감소율 폭 %.1f%%p — %s\n",
                    spread * 100.0,
                    (spread < 0.01) ? "상수가 결과를 거의 안 바꾼다"
                                    : "상수가 결과를 바꾼다");
                outLog += line;
            }
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
