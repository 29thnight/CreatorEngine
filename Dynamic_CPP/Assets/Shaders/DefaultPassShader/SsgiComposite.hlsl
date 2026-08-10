Texture2D<float4> gGI       : register(t0);
Texture2D<float>  gGIDepth  : register(t1);
Texture2D<float4> gLighting : register(t2);
Texture2D<float>  gDepth    : register(t3);
Texture2D<float4> gDiffuse  : register(t4);
Texture2D<float2> gAO       : register(t5);   // x = AO · y = 뷰 깊이

RWTexture2D<float4> gOutput : register(u0);

cbuffer CompositeParams : register(b0)
{
    uint2  gOutputSize;
    uint2  gGISize;
    float  gIntensity;
    float  gDepthSigma;
    uint2  gAOSize;      // 0이면 AO가 없다는 뜻
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

    // ★ AO는 간접광에만 곱한다.
    //
    // 직접광에 곱하면 안 된다 — 광원이 실제로 보이는 곳까지 어두워져서
    // 물리적으로 틀리고, 화면에서는 '그림자가 두 번 진 것'처럼 보인다.
    // AO가 모형화하는 것은 '주변에서 오는 빛이 얼마나 막히는가'이고,
    // 그 주변광이 바로 여기의 gi다.
    //
    // 크기가 0이면 AO 패스가 안 돌았다는 뜻이라 1로 둔다. 조건에 따라
    // SRV 슬롯 수를 바꾸지 않는 이유는 SSGI에서 그것으로 레지스터가
    // 밀려 누적이 조용히 죽은 적이 있기 때문이다 — 슬롯은 늘 같은 수를
    // 잡고, 없을 때는 값으로 구분한다.
    float ao = 1.0f;
    if (gAOSize.x > 0 && gAOSize.y > 0)
    {
        const int2 aoCoord = clamp(int2(uv * float2(gAOSize)),
            int2(0, 0), int2(gAOSize) - 1);
        ao = gAO.Load(int3(aoCoord, 0)).x;
    }

    gOutput[id.xy] = float4(direct.rgb + gi * albedo * gIntensity * ao, direct.a);
}
