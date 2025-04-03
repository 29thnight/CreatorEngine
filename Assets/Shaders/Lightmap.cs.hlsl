#define MAX_LIGHTS 4
#define DIRECTIONAL_LIGHT 0
#define POINT_LIGHT 1
#define SPOT_LIGHT 2

#define LIGHT_DISABLED 0
#define LIGHT_ENABLED 1
#define LIGHT_ENABLED_W_SHADOWMAP 2

//#define Test true

struct Light
{
    float4x4 litView;
    float4x4 litProj;
    
    float4 position;
    float4 direction;
    float4 color;

    float constantAtt;
    float linearAtt;
    float quadAtt;
    float spotAngle;

    int lightType;
    int status;
    int2 pad;
};

struct Vertex
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 texCoord : TEXCOORD;
    float3 tangent : TANGENT;
    float3 binormal : BINORMAL;
    float4 boneIds : BLENDINDICES;
    float4 boneWeight : BLENDWEIGHT;
};

// s, u, t, b
SamplerState litSample : register(s0);
RWTexture2D<float4> TargetTexture : register(u0); // Ÿ�� �ؽ�ó (UAV)

Texture2DArray<float> shadowMapTextures : register(t0); // �ҽ� �ؽ�ó (SRV)
Texture2D<float4> positionMapTexture : register(t1); // �ҽ� �ؽ�ó (SRV)
StructuredBuffer<Light> g_Lights : register(t2);
Texture2D<float4> normalMapTexture : register(t3); // �ҽ� �ؽ�ó (SRV)
//StructuredBuffer<LightViewProj> g_LightViewProj : register(t3);
//StructuredBuffer<Vertex> g_InputVertices : register(t4);

cbuffer lightMapSetting : register(b0)
{
    float bias;
    int lightSize;
}

cbuffer CB : register(b1)
{
    int2 Offset; // Ÿ�� �ؽ�ó���� �׸� ��ġ
    int2 Size; // ������ ���� ũ��
};

cbuffer transform : register(b2)
{
    matrix worldMat;
};

//struct VertexShaderOutput
//{
//    float4 position : SV_POSITION;
//    float4 pos : POSITION0;
//    float4 wPosition : POSITION1;
//    float3 normal : NORMAL;
//    float3 tangent : TANGENT;
//    float3 binormal : BINORMAL;
//    float2 texCoord : TEXCOORD0;
//};

//StructuredBuffer<AppData> g_InputVertices : register(t0);
//RWStructuredBuffer<AppData> g_OutputVertices : register(u1);

[numthreads(32, 32, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    // Ÿ�� �ؽ�ó ��ǥ
    float2 targetPos = float2(DTid.xy); // 0 ~ lightmapSize

    // ���� �� �ȼ����� üũ 
    if (targetPos.x < Offset.x || targetPos.x > (Offset.x + Size.x) ||
        targetPos.y < Offset.y || targetPos.y > (Offset.y + Size.y))
        return;

    // Ÿ�� �ؽ�ó ��ǥ�� 0~1�� ����ȭ
    float2 localUV = (targetPos - Offset) / Size;

    // UV ��ǥ�� �ҽ� �ؽ�ó ������ ����
    //float2 sourceUV = localUV;
    //float2 sourceUV = UVOffset + (localUV * UVSize);


    
    
    float4 localCoord = positionMapTexture.SampleLevel(litSample, localUV, 0);
    float4 localNormal = normalMapTexture.SampleLevel(litSample, localUV, 0);
    
    float4 worldCoord = mul(worldMat, localCoord);
    float4 worldNormal = mul(worldMat, localNormal);
    
    float4 color = float4(0, 0, 0, 1); // �ʱ�ȭ
    
#if Test
    float4 testColor = float4(0, 0, 0, 1);
#endif
    
    float2 shadowMaptexelSize = float2(1, 1) / float2(lightSize, lightSize);
    
    for (int i = 0; i < MAX_LIGHTS; ++i)
    {
        Light light = g_Lights[i];
       
        if (light.status == LIGHT_DISABLED || light.lightType != DIRECTIONAL_LIGHT)
            continue;
        
        float4 lightSpaceView = mul(light.litView, worldCoord);
        float4 lightSpaceProj = mul(light.litProj, lightSpaceView);
        
        float2 shadowUV = (lightSpaceProj.xy / lightSpaceProj.w) * 0.5 + 0.5;
        
        //TargetTexture[DTid.xy] = worldCoord;
        //TargetTexture[DTid.xy] = lightSpaceProj;
        //TargetTexture[DTid.xy] = light.litView._11_12_13_14;
        //TargetTexture[DTid.xy] = light.litView._21_22_23_24;
        //TargetTexture[DTid.xy] = light.litView._31_32_33_34;
        //TargetTexture[DTid.xy] = light.litView._41_42_43_44;      

        // ������ʿ��� ���� �� ���ø�
        float shadowDepth = shadowMapTextures.SampleLevel(litSample, float3(shadowUV, i), 0.0).r;
        
        // �׸��� ���� ���� (bias �߰�)
        bool inShadow = (shadowDepth < (lightSpaceProj.z / lightSpaceProj.w)/* - bias*/);

        
        
        float shadow = 0;
        float3 projCoords = lightSpaceProj.xyz / lightSpaceProj.w;
        float currentDepth = projCoords.z;
        
        //float epsilon = 0.01f;
        //[unroll]
        for (int x = -1; x < 2; ++x)
        {
        //[unroll]
            for (int y = -1; y < 2; ++y)
            {
                float closestDepth = shadowMapTextures.SampleLevel(litSample, float3(projCoords.xy + (float2(x, y) * shadowMaptexelSize), i), 0.0).r;
                shadow += (closestDepth < currentDepth);
            }
        }

        shadow /= 9;
        
        
        
        // ����Ʈ�� ������ ��� (Directional Light)
        float3 lightDir = normalize(light.direction.xyz);
        float NdotL = max(dot(worldNormal.xyz, lightDir), 0.0);

        // ����Ʈ ����� ���� ����
        float3 lightContribution = light.color.rgb * NdotL * shadow;//(inShadow ? 0.0 : 1.0);
        color.rgb += lightContribution;
    }

    // Ÿ�� �ؽ�ó�� ���
    TargetTexture[DTid.xy] = color;
    TargetTexture[DTid.xy + float2(0, 300)] = worldNormal;
    TargetTexture[DTid.xy + float2(0, 600)] = worldCoord;
    
#if Test
    TargetTexture[DTid.xy + float2(0, 300)] = worldNormal;
    TargetTexture[DTid.xy + float2(0, 600)] = worldCoord;
#endif
    
    //// ����Ʈ�� ���
    //float4 color = float4(0, 0, 0, 1); // �ʱ�ȭ

    //for (int i = 0; i < g_InputVertices.length; ++i)
    //{
    //    AppData vertex = g_InputVertices[i];

    //    // ���� ��ǥ���� ����Ʈ ��-�������� ��ǥ�� ��ȯ
    //    float4 worldPos = float4(vertex.position, 1.0);
    //    float4 lightSpacePos = mul(lightViewProj, worldPos);

    //    // ������ʿ��� ���� �� ���ø�
    //    float shadowDepth = ShadowMap.Sample(Sampler0, lightSpacePos.xy / lightSpacePos.w).r;

    //    // �׸��� ���� ����
    //    bool inShadow = (shadowDepth < lightSpacePos.z / lightSpacePos.w);

    //    // ����Ʈ�� ������ ���
    //    float3 lightDir = normalize(lightPosition.xyz - vertex.position);
    //    float NdotL = max(dot(vertex.normal, lightDir), 0.0);

    //    // ����Ʈ ����� ���� ����
    //    float3 lightContribution = lightColor.rgb * lightIntensity * NdotL * (inShadow ? 0.0 : 1.0);
    //    color.rgb += lightContribution;
    //}

    //// Ÿ�� �ؽ�ó�� ���
    //TargetTexture[DTid.xy] = color;
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    //// ����Ʈ�� ���
    //float4 color = float4(0, 0, 0, 1); // �ʱ�ȭ

    //for (int i = 0; i < lightSize; ++i)
    //{
    //    Light light = g_Lights[i];

    //    // ����Ʈ�� ������ ���
    //    float3 lightDir = normalize(light.position.xyz - g_InputVertices[DTid.x].position);
    //    float NdotL = max(dot(g_InputVertices[DTid.x].normal, lightDir), 0.0);

    //    // ����Ʈ ����� ���� ����
    //    color.rgb += light.color.rgb * NdotL;
    //}

    //// Ÿ�� �ؽ�ó�� ���
    //TargetTexture[DTid.xy] = color;
    
    
    
    
    
    
    
    
    
    
    
    
    
    // �ҽ� �ؽ�ó ���ø�
    //float4 color = float4(temp, 1); //SourceTexture.SampleLevel(Sampler0, sourceUV, 0);
    
    // Ÿ�� �ؽ�ó�� ���
    //TargetTexture[DTid.xy] = temp;//color;
}

/*
1. ����Ʈ�� uv�� �ش��ϴ� �ȼ��� �̰�
�� �ȼ��� uv������ positionMap���� localPosition�� ������.
2. �� localPosition�� lightViewProj�� ��ȯ�Ͽ� ������ʿ��� ���̰��� ���ø�.
3. ������ʿ��� ���ø��� ���̰��� lightSpacePos.z / lightSpacePos.w�� ���Ͽ� �׸��� ���θ� ����.
4. �׸��� ���ο� ���� ����Ʈ�� ������ ����Ͽ� color�� ����.
5. color�� Ÿ�� �ؽ�ó�� ���.

*/