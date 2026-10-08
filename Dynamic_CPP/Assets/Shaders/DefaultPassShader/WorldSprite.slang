struct SpriteInstance
{
    float4x4 world;
    float4 uv;
    float4 color;
    float4 sampling; // x = signed-distance flag; 112-byte CPU ABI
};

StructuredBuffer<SpriteInstance> gSprites : register(t0);
Texture2D                        gTexture : register(t1);
SamplerState                     gSampler : register(s0);

cbuffer SpriteCamera : register(b0)
{
    float4x4 gViewProjection;
    uint gInstanceBase;
    uint3 gCameraPadding;
};

struct VSOut
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
    nointerpolation float signedDistance : TEXCOORD1;
};

VSOut VSMain(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID)
{
    const SpriteInstance sprite = gSprites[gInstanceBase + instanceId];
    const float2 corner = float2(float(vertexId & 1u),
        float((vertexId >> 1u) & 1u));
    const float3 local = float3(corner - 0.5f, 0.0f);
    const float4 world = mul(float4(local, 1.0f), sprite.world);

    VSOut output;
    output.position = mul(world, gViewProjection);
    output.uv = lerp(sprite.uv.xy, sprite.uv.zw, corner);
    output.color = sprite.color;
    output.signedDistance = sprite.sampling.x;
    return output;
}

float4 PSMain(VSOut input) : SV_TARGET
{
    const float4 sampled = gTexture.Sample(gSampler, input.uv);
    const float smoothing = max(fwidth(sampled.r), 1.0f / 255.0f);
    if (input.signedDistance > 0.5f)
    {
        const float coverage = smoothstep(128.0f / 255.0f - smoothing * 0.5f,
            128.0f / 255.0f + smoothing * 0.5f, sampled.r);
        return float4(input.color.rgb, input.color.a * coverage);
    }
    return sampled * input.color;
}
