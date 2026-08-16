struct SpriteInstance
{
    float4x4 world;
    float4 uv;
    float4 color;
};

StructuredBuffer<SpriteInstance> gSprites : register(t0);
Texture2D                        gTexture : register(t1);
SamplerState                     gSampler : register(s0);

cbuffer SpriteCamera : register(b0)
{
    float4x4 gViewProjection;
};

struct VSOut
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};

VSOut VSMain(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID)
{
    const SpriteInstance sprite = gSprites[instanceId];
    const float2 corner = float2(float(vertexId & 1u),
        float((vertexId >> 1u) & 1u));
    const float3 local = float3(corner - 0.5f, 0.0f);
    const float4 world = mul(float4(local, 1.0f), sprite.world);

    VSOut output;
    output.position = mul(world, gViewProjection);
    output.uv = lerp(sprite.uv.xy, sprite.uv.zw, corner);
    output.color = sprite.color;
    return output;
}

float4 PSMain(VSOut input) : SV_TARGET
{
    return gTexture.Sample(gSampler, input.uv) * input.color;
}
