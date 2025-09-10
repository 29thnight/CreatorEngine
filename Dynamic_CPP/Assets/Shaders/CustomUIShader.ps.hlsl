// PieSprite_PS.hlsl
cbuffer UIBuffer : register(b1)
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

    // �ݰ� Ŀ������ (���� + ���)
    float rFeather = max(radiusUV * 0.01, 1e-5);
    float outerEdge = 1.0 - smoothstep(radiusUV - rFeather, radiusUV, r); // ����=1, �ٱ�=0
    float innerEdge = (innerRadius <= 0.0)
    ? 1.0 // �� �� ��
    : smoothstep(innerRadius - rFeather, innerRadius, r); // r<inner=0, r>=inner=1
    float radialMask = saturate(innerEdge * outerEdge);

// ���� ���
    float ang = atan2(d.y, d.x); // (-��, ��]
    ang = normAngle(ang); // [0, 2��)

// 12�� ���� ���⺰ "�������θ�" �����ϴ� ��밢
    float relCW = normAngle(startAngle - ang); // �ð�������� ����
    float relCCW = normAngle(ang - startAngle); // �ݽð�������� ����
    float rel = (clockwise != 0) ? relCW : relCCW;

// �߶� ��ǥ ����
    float cut = saturate(percent) * TWO_PI;

// ���� ���(�ε巯�� ���)
    float fa = min(max(featherAngle, 0.0), cut); // ���� ��� ����
    float angleMask = (fa <= 0.0)
    ? step(rel, cut) // rel <= cut �� 1
    : (1.0 - smoothstep(cut - fa, cut, rel)); // cut-fa ~ cut������ �ε巴�� 1��0

    float coverage = radialMask * angleMask;
    
    // �������� ������Ƽ�ö��̵� ����
    float a = coverage * base.a;
    float3 rgb = base.rgb * coverage + bgColor.rgb * (1.0 - coverage);

    return float4(rgb,a);
}
