cbuffer GridConstants : register(b0)
{
    float4x4 gViewProjection;
    float4   gCameraPos;        // xyz = eye
    float4   gGridColor;
    float4   gCheckerColor;
    float4   gFadeUnitSub;      // x fadeStart · y fadeEnd · z unitSize · w subdivisions
    float4   gCenterOffset;     // xyz centerOffset · w majorLineThickness
    float4   gMinorParams;      // x minorLineThickness · y minorLineAlpha
};

struct VSOut
{
    float4 position : SV_POSITION;
    float3 worldPos : TEXCOORD0;
};

VSOut VSMain(uint vertexId : SV_VertexID)
{
    // 0,1,2,3 -> (0,0) (1,0) (0,1) (1,1). 삼각형 스트립 순서다.
    const float2 corner = float2(float(vertexId & 1u), float((vertexId >> 1u) & 1u));
    const float3 pos = float3(
        lerp(-10000.0f, 10000.0f, corner.x),
        0.0f,
        lerp(-10000.0f, 10000.0f, corner.y));

    // DX11 quirk 그대로: 카메라 위치를 정수로 잘라 쿼드를 재중심한다.
    int3 cPos = int3(gCameraPos.xyz);
    cPos.y = 0;

    const float3 worldPos = pos + float3(cPos);

    VSOut output;
    output.worldPos = worldPos;
    output.position = mul(float4(worldPos, 1.0f), gViewProjection);
    return output;
}

float2 fwidth2(float2 v)
{
    return abs(ddx(v)) + abs(ddy(v));
}

float grid_mask(float2 posAbs, float unit, float thickness)
{
    unit = max(unit, 1e-6);
    const float2 fw = fwidth2(posAbs);
    const float2 threshold = fw * thickness * 0.5f / unit;

    const float2 coord = posAbs / unit;
    const float2 fracP = frac(coord);
    const float2 fracN = frac(-coord);

    float2 hit;
    hit.x = ((fracP.x < threshold.x) ? 1.0f : 0.0f) + ((fracN.x < threshold.x) ? 1.0f : 0.0f);
    hit.y = ((fracP.y < threshold.y) ? 1.0f : 0.0f) + ((fracN.y < threshold.y) ? 1.0f : 0.0f);

    return saturate(max(hit.x, hit.y));
}

float4 PSMain(VSOut input) : SV_TARGET
{
    const float2 posAbs = input.worldPos.xz - gCenterOffset.xz;

    const float fSubs = max(gFadeUnitSub.w, 1.0f);
    const float minorUnit = gFadeUnitSub.z / fSubs;

    const float major = grid_mask(posAbs, gFadeUnitSub.z, gCenterOffset.w);
    const float minor = grid_mask(posAbs, minorUnit, gMinorParams.x) * gMinorParams.y;
    const float lineMask = saturate(major + minor);

    const float distPlanar = length(input.worldPos.xz - gCameraPos.xz);
    const float denom = max(gFadeUnitSub.y - gFadeUnitSub.x, 1e-5f);
    float fadeFactor = 1.0f - saturate((distPlanar - gFadeUnitSub.x) / denom);

    // DX11 원본의 두 번째 페이드. 100 이후를 통째로 끈다 — 그대로 둔다.
    fadeFactor *= (1.0f - saturate(distPlanar / 100.0f));

    const float alphaGrid = lineMask * gGridColor.a;
    const float alpha = saturate(alphaGrid) * fadeFactor;

    const float3 color = lerp(gCheckerColor.rgb, gGridColor.rgb, lineMask);

    // 완전히 투명한 셀 내부는 색뿐 아니라 깊이도 건드리면 안 된다. 알파 0을
    // 반환하는 것만으로는 early/late depth write가 남아 뒤의 기즈모를 가린다.
    clip(alpha - 1e-4f);
    return float4(color, alpha);
}
