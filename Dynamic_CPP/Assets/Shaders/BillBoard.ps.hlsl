// Sparkle.hlsl - ��¦�̴� ����Ʈ�� ���� �ȼ� ���̴�

// �ؽ�ó �� ���÷� ����
Texture2D sparkleTexture : register(t0);
SamplerState linearSampler : register(s0);

cbuffer TimeBuffer : register(b3)
{
    float gTime;
    float3 gPadding;
};

// �ȼ� ���̴� �Է� ����ü
struct VSOutput
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
    uint TexIndex : TEXCOORD1;
    float4 Color : COLOR0;
    float Age : TEXCOORD2;
};

float4 main(VSOutput input) : SV_TARGET
{
    // �⺻ �ؽ�ó ���� ��������
    float4 texColor = sparkleTexture.Sample(linearSampler, input.TexCoord);
    
    // �ؽ�ó�� ���İ��� �ʹ� ������ �ȼ� ��� (���� ���� ����)
    if (texColor.a < 0.01f)
        discard;
    
    // �ܰ��� �β� �� ���� ����
    //float outlineThickness = 0.04f;
    //float outlineIntensity = 2.0f;
    //float3 outlineColor = float3(1.0f, 1.0f, 1.0f);
    
    // Quad�� �����ڸ� ����
    //float2 border;
    //border.x = min(input.TexCoord.x, 1.0f - input.TexCoord.x);
    //border.y = min(input.TexCoord.y, 1.0f - input.TexCoord.y);
    //float borderDist = min(border.x, border.y);
    
    // �ܰ��� ȿ�� ���
    //float outlineEffect = 1.0f - smoothstep(0.0f, outlineThickness, borderDist);
    
    // ���� �ؽ�ó ������ �⺻���� ���
    float3 baseColor = texColor.rgb;
    
    // �Է� ������ additive ������� ���ϱ� (��� ����)
    // �Ǵ� multiply ������� ���� ����
    //float3 coloredTexture = baseColor * input.Color.rgb; // multiply ���
    float3 coloredTexture = baseColor + (input.Color.rgb * 0.5f); // additive ���
    
    // �ܰ����� �⺻ ���� ȥ��
    float3 finalColor = coloredTexture; //, outlineColor * outlineIntensity, outlineEffect);
    
    // ���� ���İ� ��� (���� �ؽ�ó ���� �� �Է� ����)
    float finalAlpha = texColor.a * input.Color.a;
    
    return float4(finalColor, finalAlpha);
}