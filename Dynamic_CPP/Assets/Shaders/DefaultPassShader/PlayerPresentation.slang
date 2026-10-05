// Game output is already display-encoded LDR. Both the sampled image and final
// swapchain view are UNORM: an additional sRGB conversion would encode it twice.
#ifdef __spirv__
[[vk::binding(0, 0)]]
#endif
Texture2D<float4> sourceTexture : register(t0);
#ifdef __spirv__
[[vk::binding(1, 0)]]
#endif
SamplerState sourceSampler : register(s0);

struct PlayerPresentationVertex
{
    float4 position : SV_POSITION;
    float2 texCoord : TEXCOORD0;
};

PlayerPresentationVertex VSMain(uint vertexId : SV_VertexID)
{
    PlayerPresentationVertex output;
    output.texCoord = float2((vertexId << 1) & 2, vertexId & 2);
    output.position = float4(output.texCoord * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    return output;
}

float4 PSMain(PlayerPresentationVertex input) : SV_TARGET
{
    return sourceTexture.Sample(sourceSampler, input.texCoord);
}
