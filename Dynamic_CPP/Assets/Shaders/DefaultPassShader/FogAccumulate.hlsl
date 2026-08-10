#define EPSILON 0.000001

RWTexture3D<float4> VoxelWriteTexture : register(u0);
Texture3D<float4>   VoxelReadTexture  : register(t2);

cbuffer VolumetricFogCBuffer : register(b0)
{
    float4x4 InvViewProj;
    float4x4 PrevViewProj;
    float4x4 ShadowMatrix;
    float4   SunDirection;
    float4   SunColor;
    float4   CameraPosition;
    float4   CameraNearFar_FrameIndex_PreviousFrameBlend;
    float4   VolumeSize;
    float    Anisotropy;
    float    Density;
    float    Strength;
    float    ThicknessFactor;
}

float4 Accumulate(int z, float4 result, float4 colorDensityPerSlice)
{
    colorDensityPerSlice.a = max(colorDensityPerSlice.a, 0.000001);

    // Seb Hillaire, Physically-based & Unified Volumetric Rendering in Frostbite.
    // 슬라이스 안에서 깊이에 대한 적분을 쓴다(빛은 슬라이스마다 상수로 둔다).
    float sliceTransmittance = exp(-colorDensityPerSlice.a / VolumeSize.z);

    float3 sliceScattering = colorDensityPerSlice.rgb
        * (1.0f - sliceTransmittance) / colorDensityPerSlice.a;

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
