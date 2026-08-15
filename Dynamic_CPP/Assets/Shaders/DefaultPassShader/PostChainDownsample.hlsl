#include "PostChainCommon.hlsli"

Texture2D<float4> gSource : register(t0);
#ifdef __spirv__
[[vk::image_format("rgba16f")]]
#endif
RWTexture2D<float4> gOutput : register(u0);

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= gDstSize)) return;

    const float2 uv = DstToSrcUV(id.xy);
    const float2 t = 1.0f / float2(gSrcSize);

    const float3 a = LoadSrc(gSource, uv + float2(-2, -2) * t).rgb;
    const float3 b = LoadSrc(gSource, uv + float2( 0, -2) * t).rgb;
    const float3 c = LoadSrc(gSource, uv + float2( 2, -2) * t).rgb;
    const float3 d = LoadSrc(gSource, uv + float2(-1, -1) * t).rgb;
    const float3 e = LoadSrc(gSource, uv + float2( 1, -1) * t).rgb;
    const float3 f = LoadSrc(gSource, uv + float2(-2,  0) * t).rgb;
    const float3 g = LoadSrc(gSource, uv                    ).rgb;
    const float3 h = LoadSrc(gSource, uv + float2( 2,  0) * t).rgb;
    const float3 i = LoadSrc(gSource, uv + float2(-1,  1) * t).rgb;
    const float3 j = LoadSrc(gSource, uv + float2( 1,  1) * t).rgb;
    const float3 k = LoadSrc(gSource, uv + float2(-2,  2) * t).rgb;
    const float3 l = LoadSrc(gSource, uv + float2( 0,  2) * t).rgb;
    const float3 m = LoadSrc(gSource, uv + float2( 2,  2) * t).rgb;

    float3 result = (d + e + i + j) * 0.125f;
    result += (a + b + g + f) * 0.03125f;
    result += (b + c + h + g) * 0.03125f;
    result += (f + g + l + k) * 0.03125f;
    result += (g + h + m + l) * 0.03125f;

    gOutput[id.xy] = float4(result, 1.0f);
}
