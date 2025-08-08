cbuffer UniformBuffer : register(b0)
{
    float4 gridColor;
    float4 checkerColor;
    float fadeStart;
    float fadeEnd;
    float unitSize;
    int subdivisions;
    float3 centerOffset;
    float majorLineThickness;
    float minorLineThickness;
    float minorLineAlpha;
};
cbuffer cameraPos : register(b1)
{
    float4 cameraPos;
}
// ��� ����ü: Ŭ�� ��ǥ�� �Բ� ���� ��ǥ�� TEXCOORD0�� �����մϴ�.
struct VS_OUTPUT
{
    float4 pos : SV_POSITION;
    float3 worldPos : TEXCOORD0;
};

// fwidth ��� (�Ļ��� ���� ��)
static float2 fwidth2(float2 v)
{
    return abs(ddx(v)) + abs(ddy(v));
}

// ���� ��ǥ ��� �׸��� ���� ����ũ(0~1)
float grid_mask(float2 posAbs, float unit, float thickness)
{
    unit = max(unit, 1e-6); // 0 ������ ����
    float2 fw = fwidth2(posAbs);
    float2 threshold = fw * thickness * 0.5 / unit;

    float2 coord = posAbs / unit;
    float2 fracP = frac(coord);
    float2 fracN = frac(-coord);

    float2 hit;
    hit.x = ((fracP.x < threshold.x) ? 1.0 : 0.0) + ((fracN.x < threshold.x) ? 1.0 : 0.0);
    hit.y = ((fracP.y < threshold.y) ? 1.0 : 0.0) + ((fracN.y < threshold.y) ? 1.0 : 0.0);

    return saturate(max(hit.x, hit.y));
}

float4 main(VS_OUTPUT input) : SV_TARGET
{
// ---- ���� ��ǥ�� �׸��� ��� (���� ���� + centerOffset ����) ----
    float2 posAbs = input.worldPos.xz - centerOffset.xz;

    float fSubs = max((float) subdivisions, 1.0);
    float minorUnit = unitSize / fSubs;

    float major = grid_mask(posAbs, unitSize, majorLineThickness);
    float minor = grid_mask(posAbs, minorUnit, minorLineThickness) * minorLineAlpha;
    float lineMask = saturate(major + minor);

    // ---- ī�޶� �������� ���� ���� ���̵� (���� ��ǥ ������ ����) ----
    float distPlanar = length(input.worldPos.xz - cameraPos.xz); // �� ����/float ���, int ĳ���� ����
    float denom = max(fadeEnd - fadeStart, 1e-5);
    float fadeFactor = 1.0 - saturate((distPlanar - fadeStart) / denom);

    // �߰��� �ʹ� �ָ� ��¦ �� �����ϰ� �ʹٸ� (�ɼ�)
     fadeFactor *= (1.0 - saturate(distPlanar / 100.0));

    // ---- ��/���� �ռ� ----
    float alphaGrid = lineMask * gridColor.a;
    float alpha = saturate(alphaGrid) * fadeFactor;

    // ���(checkerColor) ���� ����(gridColor)�� ����
    float3 color = lerp(checkerColor.rgb, gridColor.rgb, lineMask);

    return float4(color, alpha);
}