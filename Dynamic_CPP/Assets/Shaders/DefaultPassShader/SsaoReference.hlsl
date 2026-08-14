#include "SsaoCommon.hlsli"

Texture2D<float>  gDepth  : register(t0);
Texture2D<float4> gNormal : register(t1);

#ifdef __spirv__
[[vk::image_format("rg16f")]]
#endif
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
