// Sparkle.hlsl - ��¦�̴� ����Ʈ�� ���� �ȼ� ���̴�

// ��� ���� ����
cbuffer SparkleParametersBuffer : register(b3)
{
    float time; // ���� �ð�
    float intensity; // ��¦�� ����
    float speed; // ��¦�� �ӵ�
    float padding; // 16����Ʈ ������ ���� �е�
    float4 color; // �⺻ ����
    float2 size; // ũ��
    float2 range; // ȿ�� ����
}

// �ؽ�ó �� ���÷� ����
Texture2D sparkleTexture : register(t0);
SamplerState linearSampler : register(s0);

// �ȼ� ���̴� �Է� ����ü
struct GSOutput
{
    float4 position : SV_POSITION;
    float2 texCoord : TEXCOORD0;
    //float depth : TEXCOORD1;
    //float distToCamera : TEXCOORD2;
    float4 color : COLOR0;
};

// ���� �ȼ� ���̴� �Լ�
float4 main(GSOutput input): SV_TARGET
{
    
    // �⺻ �ؽ�ó ���� ��������
    float4 texColor = sparkleTexture.Sample(linearSampler, input.texCoord);

    // �ð��� ���� ��¦�� ȿ�� ���
    float sparkle = 0.7f + 0.3f * sin((time * speed) * 10.0f);
    
    // �߽ɿ��� �����ڸ��� ������ ���������� �Ÿ� ���
    float2 center = float2(0.5f, 0.5f);
    float dist = length(input.texCoord - center) * 2.0f; // 0~1 ������ ����ȭ
    float edgeFade = 1.0f - saturate(dist);
    
    // �� ���� �ٸ� �ð� �Լ��� ��¦�ӿ� ��ȭ �߰�
    float sparkle2 = 0.8f + 0.2f * sin((time * speed * 0.7f) * 12.0f);
    
    // ���� ���� ���
    float4 baseColor = texColor * color; // �ý��� ����� ��ƼŬ ���� ����
    
    // ��¦�� ȿ���� ���� �߰� ��� ���
    float baseValue = 1.0f;
    float sparkleEffect = (sparkle * sparkle2 - 0.7f) * intensity; // 0.7�� �뷫���� ��հ�
    float brightness = baseValue + max(0.0f, sparkleEffect);
    
    float3 finalColor = baseColor.rgb * brightness;
    
    if (texColor.a < 0.01f)
        discard;
    
    // ���� ���� �κп� ��� ��¦�� �߰�
    finalColor = lerp(finalColor, float3(1.0f, 1.0f, 1.0f), pow(sparkle * sparkle2 - 0.5f, 2) * 0.5f);

    return float4(finalColor, texColor.a);
    //return input.color;
}