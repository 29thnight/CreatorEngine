#include "SsaoCommon.hlsli"

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
