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
