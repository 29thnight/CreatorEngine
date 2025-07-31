// TrailPixel.hlsl
Texture2D g_trailTexture : register(t0);
SamplerState g_linearSampler : register(s0);

struct PixelInput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
    float4 color : COLOR;
    float3 worldPos : TEXCOORD1;
    float3 normal : NORMAL;
    float3 viewDir : TEXCOORD2;
    float depth : TEXCOORD3;
};

float4 main(PixelInput input) : SV_TARGET
{
    // �ؽ�ó ���ø�
    float4 texColor = g_trailTexture.Sample(g_linearSampler, input.texcoord);
    
    // �⺻ ����
    float4 finalColor = input.color * texColor;
    
    // ����ȭ (VS���� �̹� ���� viewDir ���)
    float3 normal = normalize(input.normal);
    float3 viewDir = normalize(input.viewDir);
    
    // ������ ȿ�� (�ε巴��)
    float fresnel = 1.0f - abs(dot(normal, viewDir));
    fresnel = smoothstep(0.0f, 1.0f, fresnel);
    
    // �����ڸ� ����
    finalColor.rgb *= (1.0f + fresnel * 0.3f);
    
    // V ��ǥ ��� ���̵� (Ʈ���� �����ڸ�)
    float vFade = smoothstep(0.0f, 0.1f, input.texcoord.y) *
                  smoothstep(1.0f, 0.9f, input.texcoord.y);
    finalColor.a *= vFade;
    
    // �Ÿ� ��� ���̵� (viewDir ���� ���)
    float distance = length(input.viewDir);
    float distanceFade = 1.0f - saturate((distance - 50.0f) / 100.0f);
    finalColor.a *= distanceFade;
    
    // ���� �׽�Ʈ
    clip(finalColor.a - 0.01f);
    
    return finalColor;
}