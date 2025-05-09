#include "VolumetricFog.hlsli"
#include "Sampler.hlsli"

Texture2D<float4> InputScreenColor : register(t0);
Texture2D<float> DepthTexture : register(t1);
Texture3D<float4> VolumetricFogVoxelGridTexture : register(t2);
//Texture2D<float4> GBufferWorldPosTexture : register(t3);

cbuffer VolumetricFogCompositeCBuffer : register(b0)
{
	float4x4 ViewProj;
	float4x4 InvViewMatrix;
	float4x4 InvProjMatrix;
	float4 CameraNearFarPlanes;
	float4 VoxelSize;
	float BlendingWithSceneColorFactor;
}

float3 GetVolumetricFog(float3 inputColor, float3 worldPos, float nearPlane, float farPlane, float4x4 viewProj)
{
	float3 uv = GetUVFromVolumetricFogVoxelWorldPos(worldPos, nearPlane, farPlane, viewProj, VoxelSize.xyz);
	float4 scatteredLight = VolumetricFogVoxelGridTexture.SampleLevel(LinearSampler, uv, 0.0f);
	return inputColor * scatteredLight.a + scatteredLight.rgb;
}

struct PixelShaderInput // see Fullscreen.vs.hlsl
{
	float4 position : SV_POSITION;
	float2 texCoord : TEXCOORD0;
};

float4 main(PixelShaderInput input) : SV_TARGET
{
	float4 res = float4(0.0, 0.0, 0.0, 1.0);
	float4 inputColor = InputScreenColor.Sample(LinearSampler, input.texCoord);
	float depth = DepthTexture.Sample(LinearSampler, input.texCoord).r;
	
	float2 clipXY = input.texCoord * 2.0 - 1.0;
	clipXY.y = -clipXY.y;
	
	float4 clipSpace = float4(clipXY, depth, 1.0);
	float4 viewSpace = mul(InvProjMatrix, clipSpace);

	// perspective divide
	viewSpace /= viewSpace.w;

	float4 worldSpace = mul(InvViewMatrix, viewSpace);
	
	//float4 worldPos = GBufferWorldPosTexture.Sample(LinearSampler, input.texCoord);
	//if (worldSpace.w == 0.0f)
	//    return inputColor;
	
	float3 color = GetVolumetricFog(inputColor.rgb, worldSpace.rgb, CameraNearFarPlanes.x, CameraNearFarPlanes.y, ViewProj);
	return float4(lerp(inputColor.rgb, color, BlendingWithSceneColorFactor), 1.0f);
}