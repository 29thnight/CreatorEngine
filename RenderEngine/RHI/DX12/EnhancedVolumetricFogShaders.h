#pragma once
#ifndef DYNAMICCPP_EXPORTS

// 볼류메트릭 포그 셰이더 셋 (PHASE 3-6, 미구현 패스 이식 4차).
//
// DX11 VolumetricFog.cs.hlsl · VolumetricFogAccumulation.cs.hlsl ·
// VolumetricFogComposite.ps.hlsl · VolumetricFog.hlsli의 이식이다.
// 수식과 상수는 그대로 옮겼다 — 그림의 기준선이다.
//
// 바꾼 것은 셋뿐이다:
//
//   · 행렬 규약을 DX12 쪽으로 통일했다. 원본은 한 셰이더 안에서도
//     mul(v, M)(월드 복원)과 mul(M, v)(그림자·구름)를 섞어 쓰고 C++이
//     행렬마다 전치 여부를 달리 맞춰 줬다. 여기서는 전부 CPU가 전치하고
//     셰이더가 mul(v, M)로 통일한다 — 수학적으로 같다.
//   · 정점을 SV_VertexID 풀스크린 삼각형으로(합성).
//   · 누적 셰이더에서 읽지 않는 t0·t1 선언을 뺀다.
//
// ★ 옮기지 않은 것은 없다. 태양 항의 주석 처리, 구름의 isOn 무시,
//   누적의 경계 검사 부재는 원본의 결함이지만 그대로 둔다.

// 프록셀 격자와 월드 좌표를 오가는 헬퍼(VolumetricFog.hlsli).
//
// z는 지수 분포다 — near 근처를 촘촘하게 쪼개려는 것이고, 그래서
// 슬라이스 번호와 뷰 깊이 사이가 로그 관계다.
constexpr const char* kFogCommonHlsl = R"(
float LinearToExponentialDepth(float z, float NearPlaneZ, float FarPlaneZ)
{
    float z_buffer_params_y = FarPlaneZ / NearPlaneZ;
    float z_buffer_params_x = 1.0f - z_buffer_params_y;

    return (1.0f / z - z_buffer_params_y) / z_buffer_params_x;
}

float ExponentialToLinearDepth(float z, float n, float f)
{
    float z_buffer_params_y = f / n;
    float z_buffer_params_x = 1.0f - z_buffer_params_y;

    return 1.0f / (z_buffer_params_x * z + z_buffer_params_y);
}

float3 GetWorldPosFromVoxelID(uint3 texCoord, float jitter, float near, float far,
    float4x4 invViewProj, float3 volumeSize)
{
    float viewZ = near * pow(far / near,
        min((float(texCoord.z) + 0.5f + jitter) / volumeSize.z, 1.0f));
    float3 uv = float3((float(texCoord.x) + 0.5f) / volumeSize.x,
                       (float(texCoord.y) + 0.5f) / volumeSize.y,
                       viewZ / far);

    float3 ndc;
    ndc.x = 2.0f * uv.x - 1.0f;
    ndc.y = 1.0f - 2.0f * uv.y;   // DX라 위아래를 뒤집는다
    ndc.z = 2.0f * LinearToExponentialDepth(uv.z, near, far) - 1.0f;

    float4 worldPos = mul(float4(ndc, 1.0f), invViewProj);
    worldPos = worldPos / worldPos.w;
    return worldPos.rgb;
}

float3 GetUVFromVolumetricFogVoxelWorldPos(float3 worldPos, float n, float f,
    float4x4 viewProj, float3 volumeSize)
{
    float4 ndc = mul(float4(worldPos, 1.0f), viewProj);
    if (ndc.w > 0.0f)
        ndc /= ndc.w;

    float3 uv;
    uv.x = ndc.x * 0.5f + 0.5f;
    uv.y = 0.5f - ndc.y * 0.5f;   // DX라 위아래를 뒤집는다
    uv.z = ExponentialToLinearDepth(ndc.z * 0.5f + 0.5f, n, f);

    float2 params = float2(volumeSize.z / log2(f / n),
                          -(volumeSize.z * log2(n) / log2(f / n)));
    float view_z = uv.z * f;
    uv.z = (max(log2(view_z) * params.x + params.y, 0.0f)) / volumeSize.z;
    return uv;
}
)";

// ── ① 산란 ──
//
// 프록셀마다 광원 목록을 돌며 위상 함수를 먹이고, 그림자맵과 구름으로
// 가려짐을 곱한 뒤 지난 프레임 격자와 섞는다.
constexpr const char* kFogScatterHlsl = R"(
#define PI 3.14159265358
#define EPSILON 0.000001
#define LIGHT_DISABLED 0
#define DIRECTIONAL_LIGHT 0
#define POINT_LIGHT 1
#define SPOT_LIGHT 2
#define MAX_LIGHTS 20

SamplerState            SamplerLinearClamp          : register(s0);
SamplerState            SamplerLinearWrap           : register(s1);
SamplerComparisonState  CascadedPcfShadowMapSampler : register(s2);

RWTexture3D<float4> VoxelWriteTexture : register(u0);

Texture2DArray<float> ShadowTexture     : register(t0);
Texture2D<float4>     BlueNoiseTexture  : register(t1);
Texture3D<float4>     VoxelReadTexture  : register(t2);
Texture2D<float4>     CloudShadowMap    : register(t3);

struct Light
{
    float4 position;
    float4 direction;
    float4 color;

    float constantAtt;
    float linearAtt;
    float quadAtt;
    float spotAngle;

    int   lightType;
    int   status;
    float range;
    float intencity;
};

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

cbuffer CloudShadowMapConstants : register(b1)
{
    float4x4 viewProjection;
    float2   cloudMapSize;
    float2   size;
    float2   direction;
    uint     frameIndex;
    float    moveSpeed;
    float    alpha;
    int      isOn;
}

cbuffer LightProperties : register(b2)
{
    float4 eyePosition;
    float4 globalAmbient;
    Light  Lights[MAX_LIGHTS];
}

float HenyeyGreensteinPhaseFunction(float3 viewDir, float3 lightDir, float g)
{
    float cos_theta = dot(viewDir, lightDir);
    float denom = 1.0f + g * g + 2.0f * g * cos_theta;
    return (1.0f / (4.0f * PI)) * (1.0f - g * g) / max(pow(denom, 1.5f), EPSILON);
}

float GetBlueNoiseSample(uint3 texCoord)
{
    uint width, height;
    BlueNoiseTexture.GetDimensions(width, height);
    uint2 noiseCoord = (texCoord.xy + uint2(0, 1) * texCoord.z * width) % width;
    return BlueNoiseTexture.Load(uint3(noiseCoord, 0)).r;
}

float GetVisibility(float3 voxelWorldPoint, float4x4 svp)
{
    float4 lightSpacePos = mul(float4(voxelWorldPoint, 1.0f), svp);
    float4 ShadowCoord = lightSpacePos / lightSpacePos.w;
    ShadowCoord.rg = ShadowCoord.rg * float2(0.5f, -0.5f) + float2(0.5f, 0.5f);

    // 슬라이스 2는 마지막 캐스케이드다 — C++이 넘기는 행렬과 짝이다.
    return ShadowTexture.SampleCmpLevelZero(CascadedPcfShadowMapSampler,
        float3(ShadowCoord.xy, 2), ShadowCoord.z).r;
}

float GetCloudVisibility(float4 worldPosition)
{
    float2 texelSize = float2(1, 1) / cloudMapSize;

    float4 lightSpacePosition = mul(worldPosition, viewProjection);
    float3 projCoords = lightSpacePosition.xyz / lightSpacePosition.w;
    float currentDepth = projCoords.z;
    projCoords.y = -projCoords.y;
    projCoords.xy = (projCoords.xy * 0.5) + 0.5f;

    float2 uv = float2(projCoords.xy * size + frameIndex * moveSpeed * direction);
    float closestDepth = CloudShadowMap.SampleLevel(SamplerLinearWrap, uv, 0).r;
    float shadow = 0;
    shadow = closestDepth;

    // ★ 원본 그대로: isOn을 보지 않는다.
    return shadow * alpha;
}

[numthreads(8, 8, 1)]
void main(uint3 Gid : SV_GroupID, uint3 GTid : SV_GroupThreadID, uint3 DTid : SV_DispatchThreadID)
{
    int3 texCoord = DTid.xyz;

    if (texCoord.x < VolumeSize.x && texCoord.y < VolumeSize.y && texCoord.z < VolumeSize.z)
    {
        float jitter = frac((GetBlueNoiseSample(texCoord) - 0.5f) * (1.0f - EPSILON)
            * CameraNearFar_FrameIndex_PreviousFrameBlend.z);
        float3 voxelWorldPos = GetWorldPosFromVoxelID(texCoord, jitter,
            CameraNearFar_FrameIndex_PreviousFrameBlend.x,
            CameraNearFar_FrameIndex_PreviousFrameBlend.y, InvViewProj, VolumeSize.xyz);
        float3 voxelWorldPosNoJitter = GetWorldPosFromVoxelID(texCoord, 0.0f,
            CameraNearFar_FrameIndex_PreviousFrameBlend.x,
            CameraNearFar_FrameIndex_PreviousFrameBlend.y, InvViewProj, VolumeSize.xyz);
        float3 viewDir = normalize(CameraPosition.xyz - voxelWorldPos);

        float3 lighting = float3(0.0, 0.0, 0.0);
        float visibility = GetVisibility(voxelWorldPos, ShadowMatrix)
            * GetCloudVisibility(float4(voxelWorldPos, 1.0));

        // ★ 원본 그대로: 태양 항이 주석이다. SunDirection·SunColor는 죽어 있다.
        //if (visibility > EPSILON)
        //    lighting += visibility * SunColor.xyz
        //        * HenyeyGreensteinPhaseFunction(viewDir, -SunDirection.xyz, Anisotropy);

        for (int i = 0; i < MAX_LIGHTS; i++)
        {
            Light light = Lights[i];
            if (light.status == LIGHT_DISABLED)
                continue;

            float3 lightDir;
            float attenuation = 1.0f;
            switch (abs(light.lightType))
            {
                case DIRECTIONAL_LIGHT:
                {
                    lightDir = normalize(light.direction.xyz);
                    attenuation = 1.0f;
                    break;
                }
                case POINT_LIGHT:
                {
                    float3 lightVec = light.position.xyz - voxelWorldPos;
                    float distanceToLight = length(lightVec);
                    lightDir = normalize(lightVec);
                    attenuation = 1.0 / (light.constantAtt
                        + (light.linearAtt * distanceToLight)
                        + light.quadAtt * (distanceToLight * distanceToLight));
                    attenuation *= pow(saturate(1 - pow((pow(distanceToLight, 2)
                        / pow(light.range, 2)), 2)), 2);
                    break;
                }
                case SPOT_LIGHT:
                {
                    float3 lightVec = light.position.xyz - voxelWorldPos;
                    float distanceToLight = length(lightVec);
                    lightDir = normalize(lightVec);
                    attenuation = 1.0 / (light.constantAtt
                        + (light.linearAtt * distanceToLight)
                        + light.quadAtt * (distanceToLight * distanceToLight));
                    attenuation *= pow(saturate(1 - pow((pow(distanceToLight, 2)
                        / pow(light.range, 2)), 2)), 2);
                    float minCos = cos(light.spotAngle);
                    float maxCos = (minCos + 1.0f) / 2.0f;
                    float cosAngle = dot(light.direction.xyz, -lightDir);
                    float intensity = smoothstep(minCos, maxCos, cosAngle);
                    attenuation = intensity * attenuation;
                    break;
                }
                default:
                    break;
            }
            if (attenuation > EPSILON)
            {
                float phase = HenyeyGreensteinPhaseFunction(viewDir, -lightDir, Anisotropy);
                lighting += light.color.rgb * phase * attenuation;
            }
        }

        float4 result = float4(lighting * Strength * Density, visibility * Density);

        // 지난 프레임과 섞는다 — 지터로 흔들리는 표본을 시간축에서 재운다.
        {
            float3 prevUV = GetUVFromVolumetricFogVoxelWorldPos(voxelWorldPosNoJitter,
                CameraNearFar_FrameIndex_PreviousFrameBlend.x,
                CameraNearFar_FrameIndex_PreviousFrameBlend.y, PrevViewProj, VolumeSize.xyz);

            if (prevUV.x >= 0.0f && prevUV.y >= 0.0f && prevUV.z >= 0.0f &&
                prevUV.x <= 1.0f && prevUV.y <= 1.0f && prevUV.z <= 1.0f)
            {
                float4 prevResult = VoxelReadTexture.SampleLevel(SamplerLinearClamp, prevUV, 0.0f);
                result = lerp(result, prevResult,
                    CameraNearFar_FrameIndex_PreviousFrameBlend.w);
            }
        }

        VoxelWriteTexture[texCoord] = result;
    }
}
)";

// ── ② 누적 ──
//
// z를 따라 앞에서 뒤로 훑으며 투과율을 곱해 나간다. 슬라이스마다
// 상수 투과율을 쓰지 않고 깊이에 대한 적분을 쓰는 것이 Frostbite의 개선이다.
//
// ★ 원본 그대로 경계 검사가 없다. 디스패치가 96행이라 y가 90~95인 스레드가
//   격자 밖에 쓰는데, D3D가 범위 밖 UAV 쓰기를 버려 사고는 나지 않는다.
constexpr const char* kFogAccumulateHlsl = R"(
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
)";

// ── ③ 합성 ──
//
// 화면 픽셀의 월드 좌표로 격자를 샘플해 씬 색에 얹는다.
// inputColor * 투과율 + 쌓인 산란광이 곧 이 픽셀이 보는 것이다.
constexpr const char* kFogCompositeHlsl = R"(
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
)";

#endif
