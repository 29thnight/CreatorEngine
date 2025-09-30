#define USE_DIFFUSE 1 << 0
#define USE_NORMAL  1 << 1
#define USE_ORM     1 << 2

cbuffer PS_CONSTANT_BUFFER : register(b0)
{
    matrix g_inverseViewMatrix; // ī�޶� View-Projection�� �����
    matrix g_inverseProjectionMatrix; // ī�޶� View-Projection�� �����
    float2 g_screenDimensions; // ȭ�� �ػ� (�ʺ�, ����)
};

cbuffer PS_DECAL_BUFFER : register(b1)
{
    matrix g_inverseDecalWorldMatrix; // ��Į ��� ���� World�� �����
    uint g_useFlags;
    uint sliceX;
    uint sliceY;
    int sliceNum;
};

struct Output
{
    float4 outDiffuse : SV_TARGET0;
    float4 outNormal : SV_TARGET1;
    float4 outORM : SV_TARGET2;
};

// G-Buffer �ؽ�ó��
Texture2D g_depthTexture : register(t0);
Texture2D g_albedoTexture : register(t1);
Texture2D g_normalTexture : register(t2);
Texture2D g_ormTexture : register(t3);

// ��Į �ؽ�ó
Texture2D g_decalTexture : register(t4);
Texture2D g_decalNormalTexture : register(t5);
Texture2D g_decalORMTexture : register(t6);

// ���÷�
SamplerState g_linearSampler : register(s0); // ��Į �ؽ�ó ���ø���
SamplerState g_pointSampler : register(s1); // G-Buffer ���ø���

// ���� ���̴����� �Ѿ���� �Է�
struct PS_INPUT
{
    float4 position : SV_Position;
};

// ���� ��� ����
Output main(PS_INPUT input) : SV_Target
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
    float3 worldNormal = g_normalTexture.Sample(g_pointSampler, screenUV).xyz * 2 - 1;
    float4 baseAlbedo = g_albedoTexture.Sample(g_pointSampler, screenUV);
    float4 baseORM = g_ormTexture.Sample(g_pointSampler, screenUV);

    // 7. (���� ����) ����� �̿��� ���
    // ��Į�� �� ���� (���� Z��)�� ǥ���� ����� ��
    // ��Į�� ǥ���� �޸鿡 �����Ǵ� ���� ����
    float3 decalForward = normalize(mul((float3x3) g_inverseDecalWorldMatrix, float3(0, 1, 0)));
    //if (dot(worldNormal, decalForward) < 0.1f) // 0 ��� ���� �Ӱ谪�� �־� �����ڸ� ��Ƽ��Ʈ ����
    //{
    //    discard;
    //}
    
    float3 up = abs(worldNormal.y) < 0.999 ? float3(0, 1, 0) : float3(1, 0, 0);
    float3 tangent = normalize(cross(up, worldNormal));
    float3 bitangent = normalize(cross(worldNormal, tangent));
    float3x3 TBN = float3x3(tangent, bitangent, worldNormal);
    
    // 8. ��Į �ؽ�ó ���ø�
    // ��Į ���� ��ǥ(-0.5 ~ 0.5)�� �ؽ�ó UV(0 ~ 1)�� ��ȯ
    float2 decalUV = decalLocalPos.xz + 0.5;
    decalUV.y = 1-decalUV.y;
    //return float4(decalUV, 0, 1);
    float2 sliceUV = 1.0 / float2(sliceX, sliceY);
    float2 sliceIndex = float2(sliceNum % sliceX, sliceNum / sliceX);
    decalUV = (decalUV + sliceIndex) * sliceUV;
    
    float4 decalSample = g_decalTexture.Sample(g_linearSampler, decalUV);
    
    decalSample.rgb = pow(decalSample.rgb, 2.2);
    
    float3 nTex = g_decalNormalTexture.Sample(g_linearSampler, decalUV).xyz * 2 - 1;
    float3 nWS = normalize(mul(nTex, TBN));
    
    float3 decalorm = g_decalORMTexture.Sample(g_linearSampler, decalUV).xyz;
    float3 orm = float3(decalorm.b, decalorm.g, decalorm.r);
    
    // 9. ���� ȥ�� (Blending)
    // ƾƮ ����
    //decalSample.rgb *= g_decalColorTint;
    
    // ��Į �ؽ�ó�� ���� ���� �̿��� �⺻ �˺��� ����� ȥ��
    float3 finalColor = lerp(baseAlbedo.rgb, decalSample.rgb, decalSample.a);
    Output output;
    output.outDiffuse = (g_useFlags & USE_DIFFUSE) != 0 ? float4(finalColor, decalSample.a) : float4(0, 0, 0, 0);
    output.outNormal = (g_useFlags & USE_NORMAL) != 0 ? float4(nWS * 0.5 + 0.5, 0) : float4(0, 1, 0, 0);
    output.outORM = (g_useFlags & USE_ORM) != 0 ? float4(orm, baseORM.a) : float4(0, 0, 1, 0);
    
    return output;
    

    // ���� ���� ���
    //return float4(finalColor, 1.0f);
}