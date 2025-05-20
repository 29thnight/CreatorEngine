// Sparkle.hlsl - ��¦�̴� ����Ʈ�� ���� �ȼ� ���̴�

// �ؽ�ó �� ���÷� ����
Texture2D sparkleTexture : register(t0);
SamplerState linearSampler : register(s0);

// �ȼ� ���̴� �Է� ����ü
struct VSOutput
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
    uint TexIndex : TEXCOORD1;
    float4 Color : COLOR0;
};

float4 main(VSOutput input) : SV_TARGET
{
    // �⺻ �ؽ�ó ���� ��������
    float4 texColor = sparkleTexture.Sample(linearSampler, input.TexCoord);
    float texAlpha = texColor.a;
    
    // �ܰ��� �β� �� ���� ����
    float outlineThickness = 0.04f; // �ܰ��� �β� ���� (���� �������� ���� ��)
    float outlineIntensity = 2.0f; // �ܰ��� ��� ����
    float3 outlineColor = float3(1.0f, 1.0f, 1.0f); // ��� �ܰ��� (���ϴ� �������� ���� ����)
    
    // Quad�� �����ڸ� ���� (�ؽ�ó ��ǥ�� 0 �Ǵ� 1�� ������� Ȯ��)
    float2 border;
    border.x = min(input.TexCoord.x, 1.0f - input.TexCoord.x);
    border.y = min(input.TexCoord.y, 1.0f - input.TexCoord.y);
    float borderDist = min(border.x, border.y);
    
    // �ܰ��� ȿ�� ��� (0�� �������� �����ڸ�)
    float outlineEffect = 1.0f - smoothstep(0.0f, outlineThickness, borderDist);
    
    // �⺻ �÷� (�ؽ�ó ���� �� �Է� ����)
    float4 baseColor = float4(input.Color.rgb, texAlpha * input.Color.a);
    
    // ���İ��� �ʹ� ������ �ȼ� ���
    if (baseColor.a < 0.01f)
        discard;
    
    // ���� ���� ��� (�⺻ ����� �ܰ��� ���� ȥ��)
    float3 finalColor = lerp(baseColor.rgb, outlineColor, outlineEffect);
    
    return float4(finalColor, baseColor.a);
    //return float4(1, 0, 0, 1);
}