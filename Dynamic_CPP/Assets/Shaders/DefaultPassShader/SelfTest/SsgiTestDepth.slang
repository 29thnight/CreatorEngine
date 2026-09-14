RWTexture2D<float>  gDepth  : register(u0);
RWTexture2D<float4> gNormal : register(u1);

cbuffer TestDepthParams : register(b0)
{
    uint2 gSize;
    float gNear;
    float gFar;
};

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= gSize.x || id.y >= gSize.y) return;

    const float2 uv = (float2(id.xy) + 0.5f) / float2(gSize);

    // 화면 아래 절반은 바닥. 위로 갈수록 멀어진다.
    // 위 절반은 하늘(깊이 1).
    float depth = 1.0f;

    if (uv.y > 0.5f)
    {
        // 바닥: y가 0.5(지평선)에서 1(발밑)로 갈수록 가까워진다.
        const float t = (uv.y - 0.5f) * 2.0f;
        depth = lerp(0.999f, 0.5f, t);
    }

    // 가운데 구. 화면 중앙에 원으로 놓는다.
    const float2 center = float2(0.5f, 0.45f);
    const float2 d = (uv - center) * float2(1.0f, float(gSize.y) / float(gSize.x));
    const float r = length(d);

    if (r < 0.15f)
    {
        // 구 표면: 가장자리에서 멀고 중앙에서 가깝다.
        const float bulge = sqrt(max(0.0f, 0.15f * 0.15f - r * r)) / 0.15f;
        depth = min(depth, 0.7f - bulge * 0.1f);
    }

    // ★ 떠 있는 얇은 가로 판.
    //
    // 두께 검사를 검증하려고 넣었다. 바닥과 구만으로는 '광선이 얇은 물체
    // 뒤를 지나가는' 기하가 없어서, 두께를 뷰 공간으로 고쳐도 히트 비율이
    // 꿈쩍하지 않았다(0.05 → 0.5507 · 5.0 → 0.5506) — 거부 경로가 발동할
    // 자리 자체가 없었던 것이다.
    //
    // 판은 바닥보다 훨씬 앞에 떠 있으므로, 바닥에서 위로 가는 광선이 판의
    // 뒤편 깊이 간극을 지나게 된다. 두께가 작으면 '뚫고 지나감'으로 거부되고
    // 크면 히트로 잡힌다 — 두께 값이 처음으로 결과를 가른다.
    const bool inBar = (uv.x > 0.25f) && (uv.x < 0.75f)
        && (uv.y > 0.62f) && (uv.y < 0.66f);
    if (inBar)
    {
        depth = min(depth, 0.55f);
    }

    gDepth[id.xy] = depth;

    // ★ 노멀도 함께 만든다.
    //
    // 필터가 노멀 가중을 쓰므로 노멀이 없으면 그 항이 무의미해진다.
    // 대체물(깊이 텍스처)을 꽂으면 깊이값을 노멀로 해석하게 되고,
    // 그러면 filterNormalPower를 스윕해도 무엇을 재는지 알 수 없다.
    //
    // GBuffer 규약대로 [0,1]로 인코딩한다(셰이더가 *2-1로 편다).
    float3 normal = float3(0.0f, 1.0f, 0.0f);   // 바닥은 위를 본다

    if (r < 0.15f)
    {
        // 구 표면 노멀: 중심에서 바깥으로.
        const float z = sqrt(max(0.0f, 1.0f - saturate(r / 0.15f) * saturate(r / 0.15f)));
        normal = normalize(float3(d.x, -d.y, z));
    }
    else if (uv.y <= 0.5f)
    {
        // 하늘. 값은 안 쓰이지만 0으로 두면 normalize가 NaN을 낸다.
        normal = float3(0.0f, 0.0f, 1.0f);
    }

    if (inBar)
    {
        // 판은 카메라를 본다.
        normal = float3(0.0f, 0.0f, -1.0f);
    }

    gNormal[id.xy] = float4(normal * 0.5f + 0.5f, 1.0f);
}
