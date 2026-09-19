RWTexture2D<float> gDepth : register(u0);
#ifdef __spirv__
[[vk::image_format("rgba16f")]]
#endif
RWTexture2D<float4> gNormal : register(u1);

cbuffer SceneParams : register(b0)
{
    uint2 gSize;
    float gNearZ;
    float gFarZ;
    float gLeftViewZ;
    float gRightViewZ;
    uint gMode; // 0: step, 1: tilted plane, 2: narrow recess, 3: sky
    uint gPad;
    float2 gSlope;
    float2 gPad2;
    float4x4 gInverseProjection;
    float4x4 gInverseView;
};

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= gSize)) return;
    const float2 uv = (float2(id.xy) + 0.5f) / float2(gSize);
    const float4 rayH = mul(float4(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f, 0.5f, 1.0f), gInverseProjection);
    const float3 ray = rayH.xyz / rayH.z;
    // The exact optical-center texel stays on the far plane. At this point the
    // one-sided wall occupies at most half of each view-normal slice.
    float viewZ = (id.x > gSize.x / 2) ? gRightViewZ : gLeftViewZ;
    float3 viewNormal = float3(0.0f, 0.0f, -1.0f);
    if (gMode == 1)
    {
        // z = base + slope.x * x + slope.y * y, sampled at actual texel centers.
        viewZ = gLeftViewZ / (1.0f - dot(gSlope, ray.xy));
        viewNormal = normalize(float3(gSlope, -1.0f));
    }
    else if (gMode == 2)
    {
        viewZ = abs(int(id.x) - int(gSize.x / 2)) < int(gSize.x / 32)
            ? gLeftViewZ : gRightViewZ;
    }
    const float a = gFarZ / (gFarZ - gNearZ);
    gDepth[id.xy] = (gMode == 3) ? 1.0f : a * (1.0f - gNearZ / viewZ);
    // Match the production GBuffer contract even when the camera is rotated.
    const float3 worldNormal = normalize(mul(viewNormal, (float3x3)gInverseView));
    gNormal[id.xy] = float4(worldNormal * 0.5f + 0.5f, 1.0f);
}
