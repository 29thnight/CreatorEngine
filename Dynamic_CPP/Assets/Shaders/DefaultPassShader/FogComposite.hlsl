#include "FogCommon.hlsli"

Texture2D<float4> InputScreenColor              : register(t0);
Texture2D<float>  DepthTexture                  : register(t1);
Texture3D<float4> VolumetricFogVoxelGridTexture : register(t2);

SamplerState LinearSampler : register(s0);

cbuffer VolumetricFogCompositeCBuffer : register(b0)
{
    float4x4 ViewProj;
    float4x4 InvViewMatrix;
    float4x4 InvProjMatrix;
    float4   CameraNearFarPlanes;
    float4   VoxelSize;
    float    BlendingWithSceneColorFactor;
}

struct VSOut
{
    float4 position : SV_POSITION;
    float2 texCoord : TEXCOORD0;
};

VSOut VSMain(uint id : SV_VertexID)
{
    VSOut output;
    output.texCoord = float2((id << 1) & 2, id & 2);
    output.position = float4(output.texCoord * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f),
        0.0f, 1.0f);
    return output;
}

float3 GetVolumetricFog(float3 inputColor, float3 worldPos, float nearPlane,
    float farPlane, float4x4 viewProj)
{
    float3 uv = GetUVFromVolumetricFogVoxelWorldPos(worldPos, nearPlane, farPlane,
        viewProj, VoxelSize.xyz);
    float4 scatteredLight = VolumetricFogVoxelGridTexture.SampleLevel(LinearSampler, uv, 0.0f);
    return inputColor * scatteredLight.a + scatteredLight.rgb;
}

float4 PSMain(VSOut input) : SV_TARGET
{
    float4 inputColor = InputScreenColor.Sample(LinearSampler, input.texCoord);
    float  depth = DepthTexture.Sample(LinearSampler, input.texCoord).r;

    float2 clipXY = input.texCoord * 2.0 - 1.0;
    clipXY.y = -clipXY.y;

    float4 clipSpace = float4(clipXY, depth, 1.0);
    float4 viewSpace = mul(clipSpace, InvProjMatrix);

    viewSpace /= viewSpace.w;

    float4 worldSpace = mul(viewSpace, InvViewMatrix);

    float3 color = GetVolumetricFog(inputColor.rgb, worldSpace.rgb,
        CameraNearFarPlanes.x, CameraNearFarPlanes.y, ViewProj);
    return float4(lerp(inputColor.rgb, color, BlendingWithSceneColorFactor), 1.0f);
}
