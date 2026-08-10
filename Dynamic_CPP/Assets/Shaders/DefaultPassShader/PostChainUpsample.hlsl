#include "PostChainCommon.hlsli"

Texture2D<float4> gSource : register(t0);
RWTexture2D<float4> gOutput : register(u0);

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= gDstSize)) return;

    const float2 uv = DstToSrcUV(id.xy);
    const float2 t = 1.0f / float2(gSrcSize);

    // 3x3 텐트. 가중치 합이 1이라 밝기가 보존된다.
    float3 result = LoadSrc(gSource, uv + float2(-1, -1) * t).rgb * 1.0f;
    result += LoadSrc(gSource, uv + float2( 0, -1) * t).rgb * 2.0f;
    result += LoadSrc(gSource, uv + float2( 1, -1) * t).rgb * 1.0f;
    result += LoadSrc(gSource, uv + float2(-1,  0) * t).rgb * 2.0f;
    result += LoadSrc(gSource, uv                    ).rgb * 4.0f;
    result += LoadSrc(gSource, uv + float2( 1,  0) * t).rgb * 2.0f;
    result += LoadSrc(gSource, uv + float2(-1,  1) * t).rgb * 1.0f;
    result += LoadSrc(gSource, uv + float2( 0,  1) * t).rgb * 2.0f;
    result += LoadSrc(gSource, uv + float2( 1,  1) * t).rgb * 1.0f;
    result *= (1.0f / 16.0f);

    gOutput[id.xy] += float4(result, 0.0f);
}
