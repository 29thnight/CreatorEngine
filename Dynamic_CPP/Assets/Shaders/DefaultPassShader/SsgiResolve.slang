// ★ 레지스터 순서가 C++ 쪽 바인딩 순서와 맞아야 한다.
// 헬퍼가 그래프 핸들을 먼저, 외부 리소스(히스토리)를 뒤에 꽂으므로
// 그 순서를 여기서 받는다. 어긋나면 엉뚱한 텍스처를 읽는데, 증상이
// '결과가 이상하다'라서 원인을 찾기 어렵다.
Texture2D<float4> gTrace         : register(t0);
Texture2D<float>  gDepth         : register(t1);
Texture2D<float4> gNormal        : register(t2);
Texture2D<float4> gHistory       : register(t3);
Texture2D<float>  gHistoryDepth  : register(t4);

#ifdef __spirv__
[[vk::image_format("rgba16f")]]
#endif
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
