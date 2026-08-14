RWTexture2D<float> gDepth : register(u0);

#ifdef __spirv__
[[vk::image_format("rgba16f")]]
#endif
RWTexture2D<float4> gNormal : register(u1);

#ifdef __spirv__
[[vk::image_format("rgba16f")]]
#endif
RWTexture2D<float4> gLighting : register(u2);

#ifdef __spirv__
[[vk::image_format("rgba16f")]]
#endif
RWTexture2D<float4> gDiffuse : register(u3);

#ifdef __spirv__
[[vk::image_format("rg16f")]]
#endif
RWTexture2D<float2> gAO : register(u4);

cbuffer SceneParams : register(b0)
{
    uint2 gSize;
    uint2 gAOSize;
};

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= gSize)) return;

    const float2 uv = (float2(id.xy) + 0.5f) / float2(gSize);
    float depth = 1.0f;
    float3 normal = float3(0.0f, 0.0f, 1.0f);

    // 아래쪽 경사면, 가운데 구, 그 앞의 얇은 판을 한 장의 결정적 입력으로
    // 만든다. 실제 SSGI의 Hi-Z 교차와 bilateral 경계를 모두 밟기 위한 씬이다.
    if (uv.y > 0.28f)
    {
        const float t = (uv.y - 0.28f) / 0.72f;
        depth = lerp(0.92f, 0.58f, saturate(t));
        normal = normalize(float3(0.0f, 0.75f, -0.66f));
    }

    const float2 sphereCenter = float2(0.56f, 0.47f);
    const float2 sphereDelta = (uv - sphereCenter) * float2(1.0f,
        float(gSize.y) / float(gSize.x));
    const float radius = length(sphereDelta);
    if (radius < 0.16f)
    {
        const float z = sqrt(max(0.0f, 1.0f - radius * radius / (0.16f * 0.16f)));
        depth = min(depth, 0.66f - z * 0.08f);
        normal = normalize(float3(sphereDelta.x / 0.16f,
            -sphereDelta.y / 0.16f, -z));
    }

    const bool bar = uv.x > 0.18f && uv.x < 0.46f &&
        uv.y > 0.57f && uv.y < 0.62f;
    if (bar)
    {
        depth = 0.54f;
        normal = float3(0.0f, 0.0f, -1.0f);
    }

    const bool sky = depth >= 1.0f;
    float3 direct = sky ? float3(0.015f, 0.025f, 0.05f)
        : float3(0.08f, 0.07f, 0.055f);
    float3 diffuse = float3(0.70f, 0.66f, 0.58f);

    // 왼쪽 판과 구를 서로 다른 색의 강한 간접광 광원으로 만든다.
    if (bar)
    {
        direct = float3(2.4f, 0.35f, 0.12f);
        diffuse = float3(0.90f, 0.30f, 0.12f);
    }
    else if (radius < 0.16f)
    {
        direct = float3(0.18f, 0.55f, 2.0f);
        diffuse = float3(0.25f, 0.48f, 0.92f);
    }
    else if (!sky && uv.x > 0.72f)
    {
        direct = float3(0.12f, 0.75f, 0.22f);
    }

    gDepth[id.xy] = depth;
    gNormal[id.xy] = float4(normal * 0.5f + 0.5f, 1.0f);
    gLighting[id.xy] = float4(direct, 1.0f);
    gDiffuse[id.xy] = float4(diffuse, 1.0f);

    if (all(id.xy < gAOSize))
    {
        const float2 aoUv = (float2(id.xy) + 0.5f) / float2(gAOSize);
        const float centerOcclusion = saturate(1.0f - length(aoUv - 0.5f) * 2.4f);
        gAO[id.xy] = float2(lerp(1.0f, 0.62f, centerOcclusion), 1.0f);
    }
}
