// TrailPixel.hlsl - Ʈ���� �ȼ� ���̴�

// �ؽ�ó�� ���÷�
Texture2D g_trailTexture : register(t0);
SamplerState g_linearSampler : register(s0);

// �Է� ����ü (���� ���̴� ��°� ��ġ)
struct PixelInput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
    float4 color : COLOR;
    float3 worldPos : TEXCOORD1;
    float3 normal : NORMAL;
    float3 viewDir : TEXCOORD2;
};

// ��� ����ü
struct PixelOutput
{
    float4 color : SV_TARGET;
};

PixelOutput main(PixelInput input)
{
    PixelOutput output;
    
    // �ؽ�ó ���ø�
    float4 texColor = g_trailTexture.Sample(g_linearSampler, input.texcoord);
    
    // �⺻ ����� �ؽ�ó ����
    float4 baseColor = input.color * texColor;
    
    // ������ ������ ��� (�ɼ�)
    float3 normalizedNormal = normalize(input.normal);
    float3 normalizedViewDir = normalize(input.viewDir);
    
    // ������ ȿ�� (�����ڸ����� �� ���)
    float fresnel = 1.0f - saturate(dot(normalizedNormal, normalizedViewDir));
    fresnel = pow(fresnel, 2.0f);
    
    // �����ڸ� ����
    float edgeIntensity = 1.0f + fresnel * 0.5f;
    baseColor.rgb *= edgeIntensity;
    
    // �Ÿ��� ���� ���̵� �ƿ� (�ɼ�)
    // ���⼭�� ���������� �ʿ�� �߰� ����
    
    // ���� ���� ���
    output.color = baseColor;
    
    // ���İ� �ʹ� ������ �ȼ� ������
    if (output.color.a < 0.01f)
        discard;
    
    return output;
}