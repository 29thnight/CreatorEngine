cbuffer PS_CONSTANT_BUFFER : register(b0)
{
    matrix g_inverseViewMatrix; // ī�޶� View-Projection�� �����
    matrix g_inverseProjectionMatrix; // ī�޶� View-Projection�� �����
    float2 g_screenDimensions; // ȭ�� �ػ� (�ʺ�, ����)
};

cbuffer PS_DECAL_BUFFER : register(b1)
{
    matrix g_inverseDecalWorldMatrix; // ��Į ��� ���� World�� �����
};

// G-Buffer �ؽ�ó��
Texture2D g_depthTexture : register(t0);
Texture2D g_albedoTexture : register(t1);
Texture2D g_normalTexture : register(t2);

// ��Į �ؽ�ó
Texture2D g_decalTexture : register(t3);

// ���÷�
SamplerState g_linearSampler : register(s0); // ��Į �ؽ�ó ���ø���
SamplerState g_pointSampler : register(s1); // G-Buffer ���ø���

// ���� ���̴����� �Ѿ���� �Է�
struct PS_INPUT
{
    float4 position : SV_Position;
};

// ���� ��� ����
float4 main(PS_INPUT input) : SV_Target
{
    // 1. ȭ�� UV ��ǥ ���
    float2 screenUV = input.position.xy / g_screenDimensions;
    
    // 2. G-Buffer���� ���� �� ���ø�
    float depth = g_depthTexture.Sample(g_pointSampler, screenUV).r;

    // ���� ���� 1.0�̸� �ϴ�(���)�̹Ƿ� ��Į�� �׸��� ����
    if (depth >= 1.0f)
    {
        discard;
    }
    
    // 3. ���� ��ǥ ����
    // ȭ�� UV�� ���� ������ Ŭ�� ���� ��ǥ�� ����
    float2 clipUV = screenUV * float2(2.0, -2.0) - float2(1.0, -1.0);
    float4 clipPos = float4(clipUV, depth, 1.0);
    
    // ī�޶��� View-Projection ������� ���� ���� ��ǥ�� ���
    float4 viewSpace = mul(g_inverseProjectionMatrix, clipPos);

    // perspective divide
    viewSpace /= viewSpace.w;

    float4 worldPos = mul(g_inverseViewMatrix, viewSpace);
    
    // 4. ���� ��ǥ�� ��Į�� ���� �������� ��ȯ
    float3 decalLocalPos = mul(g_inverseDecalWorldMatrix, float4(worldPos.xyz, 1)).xyz;
    //decalLocalPos /= decalLocalPos.w;
    
    // 5. ��Į ��� ����(Unit Cube: -0.5 ~ +0.5)�� ����� �ȼ��� ���
    if (abs(decalLocalPos.x) > 0.5 || abs(decalLocalPos.y) > 0.5 || abs(decalLocalPos.z) > 0.5)
    {
        discard;
    }
    
    // 6. G-Buffer���� ��ְ� �˺���(�⺻ ����) ���ø�
    // ����� ���� ������ ����Ǿ� �ִٰ� ����
    float3 worldNormal = g_normalTexture.Sample(g_pointSampler, screenUV).xyz;
    float3 baseAlbedo = g_albedoTexture.Sample(g_pointSampler, screenUV).rgb;

    // 7. (���� ����) ����� �̿��� ���
    // ��Į�� �� ���� (���� Z��)�� ǥ���� ����� ��
    // ��Į�� ǥ���� �޸鿡 �����Ǵ� ���� ����
    float3 decalForward = normalize(mul((float3x3) g_inverseDecalWorldMatrix, float3(0, 1, 0)));
    if (dot(worldNormal, decalForward) < 0.1f) // 0 ��� ���� �Ӱ谪�� �־� �����ڸ� ��Ƽ��Ʈ ����
    {
        discard;
    }
    
    // 8. ��Į �ؽ�ó ���ø�
    // ��Į ���� ��ǥ(-0.5 ~ 0.5)�� �ؽ�ó UV(0 ~ 1)�� ��ȯ
    float2 decalUV = decalLocalPos.xz + 0.5;
    decalUV.y = 1-decalUV.y;
    //return float4(decalUV, 0, 1);
    float4 decalSample = g_decalTexture.Sample(g_linearSampler, decalUV);
    decalSample.rgb = pow(decalSample.rgb, 2.2);

    // 9. ���� ȥ�� (Blending)
    // ƾƮ ����
    //decalSample.rgb *= g_decalColorTint;

    // ��Į �ؽ�ó�� ���� ���� �̿��� �⺻ �˺��� ����� ȥ��
    float3 finalColor = lerp(baseAlbedo, decalSample.rgb, decalSample.a);

    // ���� ���� ���
    return float4(finalColor, 1.0f);
}