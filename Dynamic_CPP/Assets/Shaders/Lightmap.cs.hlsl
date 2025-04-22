#include "Sampler.hlsli"
#define MAX_LIGHTS 4
#define DIRECTIONAL_LIGHT 0
#define POINT_LIGHT 1
#define SPOT_LIGHT 2

#define LIGHT_DISABLED 0
#define LIGHT_ENABLED 1
#define LIGHT_ENABLED_W_SHADOWMAP 2

struct SurfaceInfo
{
    float4 posW;
    float3 N;
    float3 T;
    float3 B;
};

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
struct Triangle
{
    float4 v0, v1, v2;
    float4 n0, n1, n2;
    float2 uv0, uv1, uv2;
    float2 lightmapUV0, lightmapUV1, lightmapUV2;
    int lightmapIndex;
    int3 pad;
};

struct BVHNode
{
    float3 boundsMin;
    float3 boundsMax;
    int left; // child index
    int right; // child index
    int start; // index range for triangles
    int end;
    bool isLeaf;
    int pad;
};

struct Ray
{
    float3 origin;
    float3 dir;
};
// s, u, t, b
RWTexture2D<float4> TargetTexture : register(u0); // Ÿ�� �ؽ�ó (UAV)

Texture2DArray<float> shadowMapTextures : register(t0); // �ҽ� �ؽ�ó (SRV)
Texture2D<float4> positionMapTexture : register(t1); // �ҽ� �ؽ�ó (SRV)
StructuredBuffer<Light> g_Lights : register(t2);
Texture2D<float4> normalMapTexture : register(t3); // ��ָʱ��� ����� �ؽ��ķ� �ޱ�.

TextureCube EnvMap : register(t4);
Texture2D AoMap : register(t5);

StructuredBuffer<Triangle> triangles : register(t10);
StructuredBuffer<int> TriIndices : register(t11); // BVH���� �ﰢ�� �ε���
StructuredBuffer<BVHNode> BVHNodes : register(t12);


cbuffer lightMapSetting : register(b0)
{
    float bias;
    int lightSize;
    int2 shadowmapSize;
    
    float4 globalAmbient;
    
    int useEnvMap;

}

cbuffer CB : register(b1)
{
    int2 Offset; // Ÿ�� �ؽ�ó���� �׸� ��ġ
    int2 Size; // ������ ���� ũ��
    int useAO;
};

cbuffer transform : register(b2)
{
    matrix worldMat;
};

float4 Sampling(Texture2D tex, SamplerState state, float2 uv, float2 textureSize)
{
    float2 texel = float2(1, 1) / textureSize;
    float4 color = { 0, 0, 0, 1 };
    
    for (int x = -2; x < 3; ++x)
    {
        //[unroll]
        for (int y = -2; y < 3; ++y)
        {
            color += tex.SampleLevel(state, uv + (float2(x, y) * texel), 0);
        }
    }
    color /= 25.0;
    return color;
}
float3 LinearToGamma(float3 color)
{
    return pow(color, 1.0 / 2.2);
}

// �� �Լ��� ť��� ���ø��ϴ� ���� �ƴ�. ������ü ������ ���ø� �ϴ°��ε�?
static const float2 invAtan = float2(0.1591, 0.3183);
float2 SampleSphericalMap(float3 v)
{
    float2 uv = float2(atan2(v.z, v.x), -asin(v.y));
    uv *= invAtan;
    uv += 0.5;
    return uv;
}
inline bool RayTriangleIntersect(float3 orig, float3 dir, Triangle tri, out float t, out float3 bary)
{
    //float3 origin = orig + dir * 0.001; // �ڽ��� ������ ��� ����.
    
    float3 v0 = tri.v0, v1 = tri.v1, v2 = tri.v2;
    float3 edge1 = v1 - v0;
    float3 edge2 = v2 - v0;
    float3 pvec = cross(dir, edge2);
    float det = dot(edge1, pvec);
    if (det < 1e-4)
        return false;

    float invDet = 1.0 / det;
    float3 tvec = orig - v0;
    float u = dot(tvec, pvec) * invDet;
    if (u < 0 || u > 1)
        return false;

    float3 qvec = cross(tvec, edge1);
    float v = dot(dir, qvec) * invDet;
    if (v < 0 || u + v > 1)
        return false;

    t = dot(edge2, qvec) * invDet;
    if (t < 0.0001)
        return false;

    bary = float3(1 - u - v, u, v);
    return true;
}
bool IntersectAABB(Ray ray, float3 bmin, float3 bmax)
{
    float3 t1 = (bmin - ray.origin) / ray.dir;
    float3 t2 = (bmax - ray.origin) / ray.dir;
    float3 tmin = min(t1, t2);
    float3 tmax = max(t1, t2);
    float tEnter = max(max(tmin.x, tmin.y), tmin.z);
    float tExit = min(min(tmax.x, tmax.y), tmax.z);
    return tEnter <= tExit && tExit > 0;
}
bool TraceShadow(float3 origin, float3 dir, float maxDist)
{
    uint stack[64];
    int stackPtr = 0;
    stack[stackPtr++] = 0; // root node

    float hitT;
    float3 bary;

    while (stackPtr > 0)
    {
        uint nodeIndex = stack[--stackPtr];
        BVHNode node = BVHNodes[nodeIndex];

        Ray ray = { origin, dir };
        if (!IntersectAABB(ray, node.boundsMin, node.boundsMax))
            continue;

        if (node.isLeaf)
        {
            for (uint i = node.start; i < node.end; ++i)
            {
                Triangle tri = triangles[i];
                if (RayTriangleIntersect(origin, dir, tri, hitT, bary) && hitT < maxDist)
                    return true;
            }
        }
        else
        {
            stack[stackPtr++] = node.left;
            stack[stackPtr++] = node.right;
        }
    }

    return false;
}

[numthreads(32, 32, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    // Ÿ�� �ؽ�ó ��ǥ
    float2 targetPos = float2(DTid.xy); // 0 ~ lightmapSize

    // ���� �� �ȼ����� üũ 
    if (targetPos.x < Offset.x || targetPos.x > (Offset.x + Size.x) ||
        targetPos.y < Offset.y || targetPos.y > (Offset.y + Size.y))
        return;

    // Ÿ�� �ؽ�ó ��ǥ�� 0~1�� ����ȭ // targetpos 0~lightmapSize, offset
    float2 localUV = (targetPos - Offset) / Size; 
    
    float4 localpos = Sampling(positionMapTexture, LinearSampler, localUV, float2(2048, 2048));
    float4 localNormal = normalMapTexture.SampleLevel(LinearSampler, localUV, 0);
    
    
    //float4 localpos = positionMapTexture.SampleLevel(LinearSampler, localUV, 0);
    //float4 localNormal = normalMapTexture.SampleLevel(PointSampler, localUV, 0);
    
    
    
    
    
    //float4 worldpos = mul(worldMat, localpos);
    //float4 worldNormal = mul(worldMat, localNormal);
    
    //float4 color = float4(0, 0, 0, 1); // �ʱ�ȭ
    
    //float2 shadowMaptexelSize = float2(1, 1) / shadowmapSize;
    
    //for (int i = 0; i < MAX_LIGHTS; ++i)
    //{
    //    Light light = g_Lights[i];
       
    //    if (light.status == LIGHT_DISABLED || light.lightType != DIRECTIONAL_LIGHT)
    //        continue;
        
    //    float4 lightSpaceView = mul(light.litView, worldpos);
    //    float4 lightSpaceProj = mul(light.litProj, lightSpaceView);
        
    //    float shadow = 0;
    //    float3 projCoords = lightSpaceProj.xyz / lightSpaceProj.w;
    //    float currentDepth = projCoords.z;
    //    projCoords = projCoords * 0.5 + 0.5;
    //    projCoords.y = -projCoords.y;
        
    //    //float epsilon = 0.01f;
    //    //[unroll]
    //    for (int x = -4; x < 5; ++x)
    //    {
    //    //[unroll]
    //        for (int y = -4; y < 5; ++y)
    //        {
    //            float closestDepth = shadowMapTextures.SampleLevel(LinearSampler, float3(projCoords.xy + (float2(x, y) * shadowMaptexelSize), i), 0.0).r;
    //            shadow += (closestDepth < currentDepth - bias) ? 1.0 : 0.0;
    //        }
    //    }

    //    shadow /= 81; // float���� ������ sphere�� �׸��ڿ� �������. ������ float���� �ؾ� �ε巯�� �ε��� ���ø� ����
        
    //    // ����Ʈ�� ������ ��� (Directional Light)
    //    float3 lightDir = normalize(light.direction.xyz);
    //    float NdotL = max(dot(normalize(worldNormal.xyz), -lightDir), 0.0);

    //    // ����Ʈ ����� ���� ����
    //    float3 lightContribution = light.color.rgb * NdotL * (1 - shadow) * 3.0; //(inShadow ? 0.0 : 1.0);
    //    color.rgb += lightContribution;
    //}
    
    //SurfaceInfo surf;
    //surf.posW = worldpos;
    //surf.N = normalize(worldNormal.xyz);
    //float4 ambient = globalAmbient;
    //if (useEnvMap)
    //{
    //    float3 irradiance = EnvMap.SampleLevel(LinearSampler, surf.N, 0.0).rgb;
    //    float3 diffuse = irradiance;
    //    ambient.rgb = diffuse;
    //}
    //float ao = useAO ? AoMap.SampleLevel(PointSampler, localUV.xy, 0.0).r : 1.0;
    
    //ambient *= ao;// * occlusion;
    
    
    //float4 finalColor = color;
    //finalColor.rgb += ambient;
    ////finalColor.rgb = LinearToGamma(finalColor.rgb);
    //TargetTexture[DTid.xy] = finalColor;
    
    ////TargetTexture[DTid.xy + float2(0, 500)] = float4(surf.N, 1);
    ////TargetTexture[DTid.xy + float2(0, 1000)] = worldpos;
    
    
    
    
    float3 worldPos = (mul(worldMat, localpos)).xyz; // ����Ʈ�� PositionMap���� ������ ���� ��ǥ
    float3 normal = (normalize(mul(worldMat, localNormal))).xyz; // ����Ʈ�� NormalMap �Ǵ� ���� �������� ����
    float3 finalColor = float3(0, 0, 0);

    for (int i = 0; i < MAX_LIGHTS; ++i)
    {
        Light light = g_Lights[i];
        if (light.status == LIGHT_DISABLED)
            continue;

        float3 toLight;
        float distance = 1.0;
        float attenuation = 1.0;
        float NdotL = 0.0;

        if (light.lightType == DIRECTIONAL_LIGHT)
        {
            toLight = normalize(-light.direction.xyz);
            NdotL = max(dot(normal, toLight), 0.0);
        }
        else
        {
            toLight = light.position.xyz - worldPos;
            distance = length(toLight);
            toLight = normalize(toLight);
            NdotL = max(dot(normal, toLight), 0.0);

            // Distance attenuation (Point, Spot)
            float att = light.constantAtt + light.linearAtt * distance + light.quadAtt * (distance * distance);
            attenuation = 1.0 / max(att, 0.001);
        }

        // Spot light extra check
        if (light.lightType == SPOT_LIGHT)
        {
            float3 lightDir = normalize(-light.direction.xyz);
            float spotCos = dot(toLight, lightDir);
            if (spotCos < light.spotAngle)
                continue; // outside cone
            // optional: smoothstep for soft edge
        }

        // Shadow check (BVH traversal)
        bool blocked = TraceShadow(worldPos + normal * bias, toLight, distance);
        if (!blocked)
        {
            float3 lightColor = light.color.rgb;
            finalColor += NdotL * attenuation * lightColor;
        }
    }
    SurfaceInfo surf;
    surf.N = normalize(normal.xyz);
    float4 ambient = globalAmbient;
    if (useEnvMap)
    {
        float3 irradiance = EnvMap.SampleLevel(LinearSampler, surf.N, 0.0).rgb;
        float3 diffuse = irradiance;
        ambient.rgb = diffuse;
    }
    float ao = useAO ? AoMap.SampleLevel(PointSampler, localUV.xy, 0.0).r : 1.0;
    
    ambient *= ao; // * occlusion;
    finalColor.rgb += ambient;
    TargetTexture[DTid.xy] = float4(finalColor, 1.0);
}

/*
1. ����Ʈ�� uv�� �ش��ϴ� �ȼ��� �̰�
�� �ȼ��� uv������ positionMap���� localPosition�� ������.
2. �� localPosition�� lightViewProj�� ��ȯ�Ͽ� ������ʿ��� ���̰��� ���ø�.
3. ������ʿ��� ���ø��� ���̰��� lightSpacePos.z / lightSpacePos.w�� ���Ͽ� �׸��� ���θ� ����.
4. �׸��� ���ο� ���� ����Ʈ�� ������ ����Ͽ� color�� ����.
5. color�� Ÿ�� �ؽ�ó�� ���.

*/