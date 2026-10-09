struct SpriteInstance
{
    float4x4 world;
    float4x4 previousWorld;
    float4 uv;
    float4 color;
    // signed distance, previous transform valid, appearance valid, depth test.
    float4 sampling; // 176-byte CPU ABI
};

StructuredBuffer<SpriteInstance> gSprites : register(t0);
Texture2D                        gTexture : register(t1);
SamplerState                     gSampler : register(s0);

cbuffer SpriteCamera : register(b0)
{
    float4x4 gViewProjection;
    float4x4 gCurrentUnjittered;
    float4x4 gPreviousUnjittered;
    float2 gScreenDimensions;
    uint gReset;
    uint gInstanceBase;
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

// Both color and temporal rasterization evaluate precisely the same alpha/SDF
// coverage, including tint alpha and filtered atlas edges.
float4 SpriteColor(float2 uv, float4 color, float signedDistance)
{
    const float4 sampled = gTexture.Sample(gSampler, uv);
    const float smoothing = max(fwidth(sampled.r), 1.0f / 255.0f);
    if (signedDistance > 0.5f)
    {
        const float coverage = smoothstep(128.0f / 255.0f - smoothing * 0.5f,
            128.0f / 255.0f + smoothing * 0.5f, sampled.r);
        return float4(color.rgb, color.a * coverage);
    }
    return sampled * color;
}

float4 PSMain(VSOut input) : SV_TARGET
{
    return SpriteColor(input.uv, input.color, input.signedDistance);
}

Texture2D<float> gSceneDepth : register(t2);

struct TemporalVSOut
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
    nointerpolation float4 sampling : TEXCOORD1;
    float4 currentClip : TEXCOORD2;
    float4 previousClip : TEXCOORD3;
};

TemporalVSOut TemporalVS(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID)
{
    const SpriteInstance sprite = gSprites[gInstanceBase + instanceId];
    const float2 corner = float2(float(vertexId & 1u), float((vertexId >> 1u) & 1u));
    const float4 local = float4(corner - 0.5f, 0, 1);
    const float4 world = mul(local, sprite.world);
    TemporalVSOut output;
    // Raster positions use the jittered projection, while both motion endpoints
    // exclude jitter. The previous world is the actual previous submitted quad.
    output.position = mul(world, gViewProjection);
    output.currentClip = mul(world, gCurrentUnjittered);
    output.previousClip = mul(mul(local, sprite.previousWorld), gPreviousUnjittered);
    output.uv = lerp(sprite.uv.xy, sprite.uv.zw, corner);
    output.color = sprite.color;
    output.sampling = sprite.sampling;
    return output;
}

struct TemporalSpriteOutput
{
    float2 motion : SV_Target0;
    float4 reactive : SV_Target1;
    float4 transparency : SV_Target2;
    float4 responsive : SV_Target3;
};

TemporalSpriteOutput TemporalPS(TemporalVSOut input)
{
    const float alpha = saturate(SpriteColor(input.uv, input.color, input.sampling.x).a);
    if (alpha <= 0) discard;
    // Sprites never write the color scene's opaque depth. Repeating its test
    // rather than the modified temporal depth preserves overlay/painter order.
    if (input.sampling.w > 0.5 && input.position.z > gSceneDepth.Load(int3(uint2(input.position.xy), 0))) discard;
    const bool valid = gReset == 0 && input.sampling.y > 0.5 &&
        input.currentClip.w > 0.000001 && input.previousClip.w > 0.000001 &&
        all(isfinite(input.currentClip)) && all(isfinite(input.previousClip));
    const bool appearanceValid = valid && input.sampling.z > 0.5;
    const float responsive = appearanceValid ? 0 : alpha;
    // Partial-coverage pixels contain independently moving background. Changed
    // UVs, tint, texture or glyph coverage conservatively reject old history.
    const float reactive = appearanceValid ? alpha * (1 - alpha) : alpha;
    TemporalSpriteOutput output;
    output.motion = valid ? (input.previousClip.xy / input.previousClip.w -
        input.currentClip.xy / input.currentClip.w) * gScreenDimensions * float2(0.5, -0.5) : float2(0);
    // One/InvSrcAlpha unions each mask, preserving earlier translucent layers.
    output.reactive = float4(reactive, 0, 0, reactive);
    output.transparency = float4(alpha, 0, 0, alpha);
    output.responsive = float4(responsive, 0, 0, responsive);
    return output;
}
