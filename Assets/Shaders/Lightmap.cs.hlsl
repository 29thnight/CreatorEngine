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
RWTexture2D<float4> TargetTexture : register(u0); // 타겟 텍스처 (UAV)

Texture2DArray<float> shadowMapTextures : register(t0); // 소스 텍스처 (SRV)
Texture2D<float4> positionMapTexture : register(t1); // 소스 텍스처 (SRV)
StructuredBuffer<Light> g_Lights : register(t2);
Texture2D<float4> normalMapTexture : register(t3); // 소스 텍스처 (SRV)
//StructuredBuffer<LightViewProj> g_LightViewProj : register(t3);
//StructuredBuffer<Vertex> g_InputVertices : register(t4);

cbuffer lightMapSetting : register(b0)
{
    float bias;
    int lightSize;
}

cbuffer CB : register(b1)
{
    int2 Offset; // 타겟 텍스처에서 그릴 위치
    int2 Size; // 복사할 영역 크기
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
    // 타겟 텍스처 좌표
    float2 targetPos = float2(DTid.xy); // 0 ~ lightmapSize

    // 범위 내 픽셀인지 체크 
    if (targetPos.x < Offset.x || targetPos.x > (Offset.x + Size.x) ||
        targetPos.y < Offset.y || targetPos.y > (Offset.y + Size.y))
        return;

    // 타겟 텍스처 좌표를 0~1로 정규화
    float2 localUV = (targetPos - Offset) / Size;

    // UV 좌표를 소스 텍스처 범위에 매핑
    //float2 sourceUV = localUV;
    //float2 sourceUV = UVOffset + (localUV * UVSize);


    
    
    float4 localCoord = positionMapTexture.SampleLevel(litSample, localUV, 0);
    float4 localNormal = normalMapTexture.SampleLevel(litSample, localUV, 0);
    
    float4 worldCoord = mul(worldMat, localCoord);
    float4 worldNormal = mul(worldMat, localNormal);
    
    float4 color = float4(0, 0, 0, 1); // 초기화
    
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

        // 섀도우맵에서 깊이 값 샘플링
        float shadowDepth = shadowMapTextures.SampleLevel(litSample, float3(shadowUV, i), 0.0).r;
        
        // 그림자 여부 결정 (bias 추가)
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
        
        
        
        // 라이트의 영향을 계산 (Directional Light)
        float3 lightDir = normalize(light.direction.xyz);
        float NdotL = max(dot(worldNormal.xyz, lightDir), 0.0);

        // 라이트 색상과 강도 적용
        float3 lightContribution = light.color.rgb * NdotL * shadow;//(inShadow ? 0.0 : 1.0);
        color.rgb += lightContribution;
    }

    // 타겟 텍스처에 기록
    TargetTexture[DTid.xy] = color;
    TargetTexture[DTid.xy + float2(0, 300)] = worldNormal;
    TargetTexture[DTid.xy + float2(0, 600)] = worldCoord;
    
#if Test
    TargetTexture[DTid.xy + float2(0, 300)] = worldNormal;
    TargetTexture[DTid.xy + float2(0, 600)] = worldCoord;
#endif
    
    //// 라이트맵 계산
    //float4 color = float4(0, 0, 0, 1); // 초기화

    //for (int i = 0; i < g_InputVertices.length; ++i)
    //{
    //    AppData vertex = g_InputVertices[i];

    //    // 월드 좌표에서 라이트 뷰-프로젝션 좌표로 변환
    //    float4 worldPos = float4(vertex.position, 1.0);
    //    float4 lightSpacePos = mul(lightViewProj, worldPos);

    //    // 섀도우맵에서 깊이 값 샘플링
    //    float shadowDepth = ShadowMap.Sample(Sampler0, lightSpacePos.xy / lightSpacePos.w).r;

    //    // 그림자 여부 결정
    //    bool inShadow = (shadowDepth < lightSpacePos.z / lightSpacePos.w);

    //    // 라이트의 영향을 계산
    //    float3 lightDir = normalize(lightPosition.xyz - vertex.position);
    //    float NdotL = max(dot(vertex.normal, lightDir), 0.0);

    //    // 라이트 색상과 강도 적용
    //    float3 lightContribution = lightColor.rgb * lightIntensity * NdotL * (inShadow ? 0.0 : 1.0);
    //    color.rgb += lightContribution;
    //}

    //// 타겟 텍스처에 기록
    //TargetTexture[DTid.xy] = color;
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    //// 라이트맵 계산
    //float4 color = float4(0, 0, 0, 1); // 초기화

    //for (int i = 0; i < lightSize; ++i)
    //{
    //    Light light = g_Lights[i];

    //    // 라이트의 영향을 계산
    //    float3 lightDir = normalize(light.position.xyz - g_InputVertices[DTid.x].position);
    //    float NdotL = max(dot(g_InputVertices[DTid.x].normal, lightDir), 0.0);

    //    // 라이트 색상과 강도 적용
    //    color.rgb += light.color.rgb * NdotL;
    //}

    //// 타겟 텍스처에 기록
    //TargetTexture[DTid.xy] = color;
    
    
    
    
    
    
    
    
    
    
    
    
    
    // 소스 텍스처 샘플링
    //float4 color = float4(temp, 1); //SourceTexture.SampleLevel(Sampler0, sourceUV, 0);
    
    // 타겟 텍스처에 기록
    //TargetTexture[DTid.xy] = temp;//color;
}

/*
1. 라이트맵 uv에 해당하는 픽셀을 뽑고
이 픽셀의 uv값으로 positionMap에서 localPosition을 가져옴.
2. 이 localPosition을 lightViewProj로 변환하여 섀도우맵에서 깊이값을 샘플링.
3. 섀도우맵에서 샘플링한 깊이값과 lightSpacePos.z / lightSpacePos.w를 비교하여 그림자 여부를 결정.
4. 그림자 여부에 따라 라이트의 영향을 계산하여 color에 더함.
5. color를 타겟 텍스처에 기록.

*/