#include "VolumetricFog.hlsli"
#define PI 3.14159265358
#define EPSILON 0.000001

SamplerState SamplerLinear : register(s0);
SamplerComparisonState CascadedPcfShadowMapSampler : register(s2);

RWTexture3D<float4> VoxelWriteTexture : register(u0);

Texture2D<float> ShadowTexture : register(t0);
Texture2D<float4> BlueNoiseTexture : register(t1);
Texture3D<float4> VoxelReadTexture : register(t2);

cbuffer VolumetricFogCBuffer : register(b0)
{
    float4x4 InvViewProj;
    float4x4 PrevViewProj;
    float4x4 ShadowMatrix;
    float4 SunDirection;
    float4 SunColor;
    float4 CameraPosition;
    float4 CameraNearFar_FrameIndex_PreviousFrameBlend;
    float4 VolumeSize;
    float Anisotropy;
    float Density;
    float Strength;
    float ThicknessFactor;
}

float GetSliceDistance(int z, float near, float far)
{
    return near * pow(far / near, (float(z) + 0.5f) / VolumeSize.z);
}
float GetSliceThickness(int z, float near, float far)
{
    return abs(GetSliceDistance(z + 1, near, far) - GetSliceDistance(z, near, far));
}

// https://github.com/Unity-Technologies/VolumetricLighting/blob/master/Assets/VolumetricFog/Shaders/Scatter.compute
float4 Accumulate(int z, float4 result /*color (rgb) & transmittance (alpha)*/, float4 colorDensityPerSlice /*color (rgb) & density (alpha)*/)
{
    colorDensityPerSlice.a = max(colorDensityPerSlice.a, 0.000001);
    //float thickness = GetSliceThickness(z, CameraNearFar_FrameIndex_PreviousFrameBlend.x, CameraNearFar_FrameIndex_PreviousFrameBlend.y);
    //float sliceTransmittance = exp(-colorDensityPerSlice.a * thickness * ThicknessFactor);

    // Seb Hillaire's improved transmission by calculating an integral over slice depth instead of
	// constant per slice value. Light still constant per slice, but that's acceptable. See slide 28 of
	// Physically-based & Unified Volumetric Rendering in Frostbite
	// http://www.frostbite.com/2015/08/physically-based-unified-volumetric-rendering-in-frostbite/
    float sliceTransmittance = exp(-colorDensityPerSlice.a / VolumeSize.z);

    float3 sliceScattering = colorDensityPerSlice.rgb * (1.0f - sliceTransmittance) / colorDensityPerSlice.a;

    result.rgb += sliceScattering * result.a;
    result.a *= sliceTransmittance;
    return result;
}

[numthreads(8, 8, 1)]
void main(uint3 Gid : SV_GroupID, uint3 GTid : SV_GroupThreadID, uint3 DTid : SV_DispatchThreadID)
{
    float4 result = float4(0.0f, 0.0f, 0.0f, 1.0f);

    for (int z = 0; z < VolumeSize.z; z++)
    {
        uint3 texCoord = uint3(DTid.xy, z);
        float4 colorDensityPerSlice = VoxelReadTexture.Load(uint4(texCoord, 0));

        result = Accumulate(z, result, colorDensityPerSlice);
        VoxelWriteTexture[texCoord] = result;
    }
}