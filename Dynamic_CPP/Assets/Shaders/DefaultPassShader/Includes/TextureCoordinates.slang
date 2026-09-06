#pragma once
struct TextureCoordinates
{
    float4 u;
    float4 v;
};
float2 TransformTextureUv(TextureCoordinates transform, float2 uv0, float2 uv1)
{
    const float2 uv = transform.u.w == 1.0 ? uv1 : uv0;
    return float2(dot(transform.u.xyz, float3(uv, 1)),
        dot(transform.v.xyz, float3(uv, 1)));
}
