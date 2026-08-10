#include "PostChainCommon.hlsli"

Texture2D<float4> gSource : register(t0);
RWTexture2D<float4> gOutput : register(u0);

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= gDstSize)) return;

    // 2x2를 모아 내려간다. 한 점만 집으면 밝은 점 하나가 깜빡일 때
    // 블룸 전체가 같이 깜빡인다.
    const float2 uv = DstToSrcUV(id.xy);
    const float2 texel = 1.0f / float2(gSrcSize);

    float3 color = 0.0f;
    [unroll]
    for (int dy = 0; dy <= 1; ++dy)
    {
        [unroll]
        for (int dx = 0; dx <= 1; ++dx)
        {
            color += LoadSrc(gSource,
                uv + float2(dx - 0.5f, dy - 0.5f) * texel).rgb;
        }
    }
    color *= 0.25f;

    const float brightness = max(color.r, max(color.g, color.b));

    // soft knee. threshold-knee 아래는 0, threshold 위는 그대로,
    // 사이는 이차식으로 잇는다.
    const float knee = max(gBloomKnee, 1e-4f);
    float soft = brightness - gBloomThreshold + knee;
    soft = clamp(soft, 0.0f, 2.0f * knee);
    soft = soft * soft / (4.0f * knee);

    const float contribution =
        max(soft, brightness - gBloomThreshold) / max(brightness, 1e-4f);

    gOutput[id.xy] = float4(color * contribution, 1.0f);
}
