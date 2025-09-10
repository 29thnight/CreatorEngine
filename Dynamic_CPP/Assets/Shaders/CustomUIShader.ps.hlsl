// PieSprite_PS.hlsl
cbuffer PieParams : register(b1)
{
    float2 centerUV; // �� �߽� (0~1, �ؽ�ó UV ����)
    float radiusUV; // �ܰ� ������ (UV ����, 0~1)
    float percent; // 0~1 (�׸� ����)
    float startAngle; // ���� ����. 0 = +X��, PI/2 = +Y�� ����
    int clockwise; // 1�̸� �ð����, 0�̸� �ݽð�
    float featherAngle; // ���� ���(����). 0.005~0.05 ����
    float innerRadius; // 0�̸� �� �� ��, (0~radiusUV)�� �� �β� ����
    float4 tint; // ����
    float4 bgColor; // ����(������Ƽ�ö��̵� ���� ����)
}

Texture2D Diffuse : register(t0);
SamplerState Samp : register(s0);


// ��ƿ: ���� [0,2��) ����ȭ
static const float TWO_PI = 6.283185307179586f;
float normAngle(float a)
{
    a = fmod(a, TWO_PI);
    return (a < 0) ? a + TWO_PI : a;
}

float4 main(float4 color : COLOR0, float2 texCoord : TEXCOORD0) : SV_TARGET
{
    // �⺻ �ؽ�ó ��(������Ƽ�ö��̵� ���� ����)
    float4 base = Diffuse.Sample(Samp, texCoord) * color * tint;

    // �߽�/������ ��� ��ǥ
    float2 d = texCoord - centerUV;
    float r = length(d);

    // �ݰ� Ŀ������ (���� ���� + ���)
    float outerEdge = smoothstep(radiusUV, max(radiusUV - radiusUV * 0.01, 1e-5), r); // ��¦ �ε巴��
    float innerEdge = (innerRadius <= 0.0)
        ? 1.0
        : 1.0 - smoothstep(innerRadius, max(innerRadius - radiusUV * 0.01, 1e-5), r);

    float radialMask = saturate(innerEdge * outerEdge);

    // ���� ���: atan2(y,x) �� (-��, ��] �� [0, 2��)��
    float ang = atan2(d.y, d.x);
    ang = normAngle(ang);

    // ���۰� �������� ȸ�� ����
    float rel = normAngle(ang - startAngle); // 0���� ����
    if (clockwise == 0)
    {
        // �ݽð����: �ð�� ����ϰ� ������ ��Ī ��ȯ
        rel = (rel > 0) ? (TWO_PI - rel) : 0; // rel �� (0, 2��] �� (2��-rel)
    }

    // �߶� ��ǥ ����
    float cut = saturate(percent) * TWO_PI;

    // ���� ���(�ε巯�� ����)
    float angleMask = (featherAngle <= 0.0f)
        ? step(rel, cut)
        : smoothstep(cut, max(cut - featherAngle, 0.0), rel); // rel <= cut�� �� 1, ���� �ε巴��

    float coverage = radialMask * angleMask;

    // �������� ������Ƽ�ö��̵� ����
    float a = coverage * base.a;
    float3 rgb = base.rgb * coverage + bgColor.rgb * (1.0 - coverage);

    return float4(1,0,0,1);
}
