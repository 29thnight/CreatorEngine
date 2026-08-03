#pragma once
#ifndef DYNAMICCPP_EXPORTS

// SSGI 컴퓨트 셰이더 원본.
//
// 패스 구현(.cpp)에서 떼어 낸다. 셰이더 다섯 개가 한 파일에 같이 있으면
// C++ 로직을 찾을 때마다 HLSL을 스크롤해서 지나가야 하고, 파일이 1300줄을
// 넘어간다.
//
// 문자열 상수로 두는 이유는 런타임 컴파일이기 때문이다. 별도 .hlsl 파일로
// 빼면 배포 경로에 그 파일이 있어야 하고, 없을 때 조용히 죽는다 —
// 이 프로젝트에서 이미 겪은 부류다(nethost.dll이 저장소에 없이 손으로
// 놓여 있었다). 소스에 박아 두면 빌드된 실행 파일이 자기 셰이더를 든다.

namespace SsgiShaders
{
    // ── 리졸브: 시간적 재투영 + 누적 ──
    //
    // 이 설계에서 가장 크게 아끼는 자리다. 프레임당 슬라이스를 적게 쓰는
    // 대신 여러 프레임에 걸쳐 표본을 쌓는다.
    //
    // 재투영은 '지금 이 픽셀이 지난 프레임 어디에 있었나'를 묻는다:
    //   현재 깊이 → 뷰 위치 → 월드 위치 → 지난 프레임 클립 → 지난 UV
    //
    // 그 자리가 정말 같은 표면인지 확인해야 한다. 확인하지 않으면 카메라가
    // 움직일 때 다른 물체의 값을 섞고, 증상이 '움직이면 번진다'라서 노이즈로
    // 오해하기 쉽다. 지난 깊이와 재투영된 깊이를 비교해 거른다.
    //
    // 거부하면 누적을 처음부터 시작한다.
    constexpr const char* kResolve = R"(
// ★ 레지스터 순서가 C++ 쪽 바인딩 순서와 맞아야 한다.
// 헬퍼가 그래프 핸들을 먼저, 외부 리소스(히스토리)를 뒤에 꽂으므로
// 그 순서를 여기서 받는다. 어긋나면 엉뚱한 텍스처를 읽는데, 증상이
// '결과가 이상하다'라서 원인을 찾기 어렵다.
Texture2D<float4> gTrace         : register(t0);
Texture2D<float>  gDepth         : register(t1);
Texture2D<float4> gNormal        : register(t2);
Texture2D<float4> gHistory       : register(t3);
Texture2D<float>  gHistoryDepth  : register(t4);

RWTexture2D<float4> gResolved : register(u0);

cbuffer ResolveParams : register(b0)
{
    float4x4 gInverseProjection;
    float4x4 gInverseView;
    float4x4 gPreviousViewProjection;
    uint2    gSize;
    uint     gHasHistory;
    uint     gMaxAccum;
    float    gDepthTolerance;
    float    gPad0;
    float2   gPad1;
};

float3 ViewFromDepth(float2 uv, float depth)
{
    const float4 clip = float4(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f, depth, 1.0f);
    const float4 view = mul(clip, gInverseProjection);
    return view.xyz / view.w;
}

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= gSize.x || id.y >= gSize.y) return;

    const float4 current = gTrace.Load(int3(id.xy, 0));
    const float  depth = gDepth.Load(int3(id.xy, 0));

    if (depth >= 1.0f)
    {
        gResolved[id.xy] = float4(0, 0, 0, 0);
        return;
    }

    float3 accumulated = current.rgb;
    float  frames = 1.0f;

    if (0 != gHasHistory)
    {
        const float2 uv = (float2(id.xy) + 0.5f) / float2(gSize);
        const float3 viewPos = ViewFromDepth(uv, depth);
        const float4 worldPos = mul(float4(viewPos, 1.0f), gInverseView);

        float4 prevClip = mul(worldPos, gPreviousViewProjection);
        if (prevClip.w > 0.0001f)
        {
            prevClip /= prevClip.w;
            const float2 prevUV = float2(prevClip.x * 0.5f + 0.5f, 0.5f - prevClip.y * 0.5f);

            if (all(prevUV >= 0.0f) && all(prevUV <= 1.0f))
            {
                const int2 prevCoord = int2(prevUV * gSize);
                const float prevDepth = gHistoryDepth.Load(int3(prevCoord, 0));

                // 같은 표면인가. 클립 깊이 차이로 근사한다 — 허용치는
                // 실측으로 조일 자리다.
                if (abs(prevDepth - prevClip.z) < gDepthTolerance)
                {
                    const float4 history = gHistory.Load(int3(prevCoord, 0));
                    const float historyFrames = max(1.0f, history.a);

                    frames = min(historyFrames + 1.0f, float(gMaxAccum));

                    // 지수 이동 평균. frames가 커질수록 새 표본의 무게가
                    // 줄어 노이즈가 가라앉는다.
                    const float alpha = 1.0f / frames;
                    accumulated = lerp(history.rgb, current.rgb, alpha);
                }
            }
        }
    }

    gResolved[id.xy] = float4(accumulated, frames);
}
)";

    // ── 필터: bilateral 한 번 ──
    //
    // 기존은 dual filtering 다단계로 노이즈를 지웠다. 시간적 누적이 앞에서
    // 대부분을 지우므로 여기서는 한 번이면 된다 — 이것이 '노이즈가 줄어드는
    // 만큼 공간 필터도 가벼워진다'는 부분이다.
    //
    // 깊이와 노멀을 보고 가중한다. 그냥 흐리면 물체 경계에서 배경의 GI가
    // 새어 들어오고, 증상은 '윤곽에 후광이 생긴다'로 나타난다.
    constexpr const char* kFilter = R"(
Texture2D<float4> gSource : register(t0);
Texture2D<float>  gDepth  : register(t1);
Texture2D<float4> gNormal : register(t2);

RWTexture2D<float4> gFiltered : register(u0);

cbuffer FilterParams : register(b0)
{
    uint2 gSize;
    float gDepthSigma;
    float gNormalPower;
};

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= gSize.x || id.y >= gSize.y) return;

    const float centerDepth = gDepth.Load(int3(id.xy, 0));
    if (centerDepth >= 1.0f)
    {
        gFiltered[id.xy] = gSource.Load(int3(id.xy, 0));
        return;
    }

    const float3 centerNormal = normalize(gNormal.Load(int3(id.xy, 0)).xyz * 2.0f - 1.0f);

    float4 sum = 0.0f;
    float  weightSum = 0.0f;

    // 십자 5탭. 반경을 키우면 더 매끄러워지지만 그만큼 비싸진다 —
    // 누적이 앞에서 일하므로 이 정도로 시작하고 실측으로 조인다.
    const int2 offsets[5] = {
        int2(0, 0), int2(-1, 0), int2(1, 0), int2(0, -1), int2(0, 1)
    };

    [unroll]
    for (uint i = 0; i < 5; ++i)
    {
        const int2 coord = clamp(int2(id.xy) + offsets[i], int2(0, 0), int2(gSize) - 1);

        const float sampleDepth = gDepth.Load(int3(coord, 0));
        if (sampleDepth >= 1.0f) continue;

        const float3 sampleNormal = normalize(gNormal.Load(int3(coord, 0)).xyz * 2.0f - 1.0f);

        const float depthDiff = abs(sampleDepth - centerDepth);
        const float depthWeight = exp(-depthDiff / max(gDepthSigma, 1e-6f));
        const float normalWeight = pow(max(0.0f, dot(centerNormal, sampleNormal)), gNormalPower);

        const float weight = depthWeight * normalWeight;

        sum += gSource.Load(int3(coord, 0)) * weight;
        weightSum += weight;
    }

    gFiltered[id.xy] = (weightSum > 1e-6f) ? (sum / weightSum) : gSource.Load(int3(id.xy, 0));
}
)";

    // ── 합성: 업샘플 + 더하기 ──
    //
    // 1/2 해상도 GI를 전 해상도로 올린다. 그냥 늘리면 경계가 뭉개지므로
    // 깊이를 보고 가중한다(joint bilateral upsample) — 1/2 해상도로 계산한
    // 값을 여기서 되살리는 것이 '잃는 것이 거의 없다'의 근거다.
    //
    // 간접광은 알베도를 곱해 더한다. GI가 옮기는 것은 빛이고, 그 빛이 이
    // 표면에서 얼마나 반사되는지는 알베도가 정한다.
    constexpr const char* kComposite = R"(
Texture2D<float4> gGI       : register(t0);
Texture2D<float>  gGIDepth  : register(t1);
Texture2D<float4> gLighting : register(t2);
Texture2D<float>  gDepth    : register(t3);
Texture2D<float4> gDiffuse  : register(t4);

RWTexture2D<float4> gOutput : register(u0);

cbuffer CompositeParams : register(b0)
{
    uint2  gOutputSize;
    uint2  gGISize;
    float  gIntensity;
    float  gDepthSigma;
    float2 gPad;
};

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= gOutputSize.x || id.y >= gOutputSize.y) return;

    const float4 direct = gLighting.Load(int3(id.xy, 0));
    const float centerDepth = gDepth.Load(int3(id.xy, 0));

    if (centerDepth >= 1.0f)
    {
        // 하늘은 그대로 통과시킨다.
        gOutput[id.xy] = direct;
        return;
    }

    // 1/2 해상도에서 2x2를 깊이 가중으로 모은다.
    const float2 uv = (float2(id.xy) + 0.5f) / float2(gOutputSize);
    const float2 giCoordF = uv * float2(gGISize) - 0.5f;
    const int2   giBase = int2(floor(giCoordF));

    float3 gi = 0.0f;
    float  weightSum = 0.0f;

    [unroll]
    for (int dy = 0; dy <= 1; ++dy)
    {
        [unroll]
        for (int dx = 0; dx <= 1; ++dx)
        {
            const int2 coord = clamp(giBase + int2(dx, dy), int2(0, 0), int2(gGISize) - 1);

            const float sampleDepth = gGIDepth.Load(int3(coord, 0));
            if (sampleDepth >= 1.0f) continue;

            const float depthDiff = abs(sampleDepth - centerDepth);
            const float weight = exp(-depthDiff / max(gDepthSigma, 1e-6f));

            gi += gGI.Load(int3(coord, 0)).rgb * weight;
            weightSum += weight;
        }
    }

    if (weightSum > 1e-6f) gi /= weightSum;
    else                   gi = 0.0f;

    const float3 albedo = gDiffuse.Load(int3(id.xy, 0)).rgb;

    gOutput[id.xy] = float4(direct.rgb + gi * albedo * gIntensity, direct.a);
}
)";
}

#endif
