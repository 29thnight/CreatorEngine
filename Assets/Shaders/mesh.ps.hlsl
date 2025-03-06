
#include "common.hlsli"
#include "lighting.hlsli"
#include "pbr.hlsli"

cbuffer CameraBuffer                     : register(b0)   
{
    matrix VP;
    float3 CameraPosition;
    float Exposure;
    float3 ViewDir;
    float _placeholder4;
    float3 Up;
    float _placeholder5;
    float3 Right;
    float _placeholder6;
};

cbuffer SceneBuffer                      : register(b1)          
{
	float3	            AmbientColor;
	float	            IBLIntensity;
    float3	            SunPos;
	uint	            PreciseShadows;
	float3	            SunColor;
	float	            _placeholder3;
	float3	            IBLColor;
    uint				NumPointLights;
    PointLight			PointLights[MAXLIGHT];
};

cbuffer MaterialProperties               : register(b2)
{
    float3	            Diffuse;
    float				Roughness;
    float3              Emissive;
    float               Metalness;
    uint				Bitmask;
};

Texture2D<float4>      DiffuseTexture					 : register(t0);
Texture2D<float4>      OcclusionMetallicRoughnessTexture : register(t1);
Texture2D<float3>      EmissiveTexture					 : register(t2);
Texture2D<float3>      NormalTexture					 : register(t3);

TextureCube       	   SkyboxTexture   					 : register(t4);
Texture2D<float>       ShadowTexture   					 : register(t5);
TextureCube			   SkyboxPrefilter					 : register(t6);
Texture2D			   brdfLUT							 : register(t7);

SamplerState		   AnisotropicSampler				 : register(s0);
SamplerState		   LinearSampler  					 : register(s1);
SamplerState		   ClampSampler  					 : register(s2);
SamplerState		   CubemapSampler  					 : register(s3);

struct In
{
	float3x3 vTBN        			: TANGENT0;
    float2	 vTexcoord				: TEXCOORD0;
    float3	 vPixelWorldPos			: POSITION0;
	float4	 vPixelLightSpacePos	: POSITION1;
    float4	 vPosition				: SV_POSITION;
};

struct PsOut
{
    float4 Color  :SV_Target0;
    float  Shadow :SV_Target1;
};

void GetTextureSamples(
	in float2 texcoord,
	out float4 diffuse,
	out float3 emissive,
	out float metalness,
	out float roughness,
	out float occlusion)
{
	[branch]	// Diffuse
	if(Bitmask & (1 << 0))
	{
        diffuse = DiffuseTexture.Sample(AnisotropicSampler, texcoord) * float4(Diffuse, 1.0f);
    }
	else
	{
		diffuse = float4(Diffuse, 1.0f);
    }

	[branch]	// Metalness
	if(Bitmask & (1 << 1))
	{
        float4 orm = OcclusionMetallicRoughnessTexture.Sample(AnisotropicSampler, texcoord);
        occlusion = orm.r;
        roughness = orm.g;
        metalness = orm.b;
		
    }
	else
	{
        occlusion = 0.0f;
        roughness = Roughness;
        metalness = step(Metalness, 0.5f);
    }

	[branch]	// Emissive
	if(Bitmask & (1 << 2))
	{
        emissive = EmissiveTexture.Sample(AnisotropicSampler, texcoord);
    }
	else
	{
	}
}

float GetShadow(float4 fragPosLightSpace, float3 N, float3 L)
{
	// const float bias = 0.002f;
    float2 coords = float2(0.0f,0.0f);
	float currentDepth = (fragPosLightSpace.z / fragPosLightSpace.w);

	if(currentDepth > 1.0f)
	return 0.0f;
	// Tansition to [-1,1] -> [0,1]
	// And flip y due to DirectX notation
	coords.x = (fragPosLightSpace.x / fragPosLightSpace.w) * 0.5f + 0.5f;
	coords.y = (-fragPosLightSpace.y / fragPosLightSpace.w) * 0.5f + 0.5f;

	float bias = max(0.008f * (1.0f - dot(N, L)), 0.0001f);  
    const float dim = 8192.0f; // Constant shadow map

	const float texelSize = 1.0f / dim;
	float shadow = 0.0f;

	[branch]
	if(PreciseShadows)
	{
		[unroll]
		for(int x = 0; x < 5; ++x)
		{
			float xoffs = 0.5f * x - 1.0f;
			[unroll]
			for(int y = 0; y < 5; ++y)
			{
				float yoffs = 0.5f * y - 1.0f;
				float pcfDepth = ShadowTexture.Sample(ClampSampler, coords + float2(xoffs, yoffs) * texelSize.xx);
				shadow += currentDepth - bias > pcfDepth ? 1.0 : 0.0; 
			}
		}
		shadow /= 25.0f;
		return (1 - shadow);
	}
	else
	{
		[unroll]
		for(int x = -1; x <= 1; ++x)
		{
			[unroll]
			for(int y = -1; y <= 1; ++y)
			{
				float pcfDepth = ShadowTexture.Sample(ClampSampler, coords + float2(x,y) * texelSize.xx);
				shadow += currentDepth - bias > pcfDepth ? 1.0 : 0.0; 
			}
		}
		shadow /= 9.0f;
		
		return (1 - shadow);
	}

}

uint GetMipMapLevel(TextureCube tex)
{
    uint mipLevel = 0;
    uint width, height;
    tex.GetDimensions(0, width, height, mipLevel);
    return mipLevel;
}

float3 GetImageBasedLighting(PBRParameters params, float3 diffuse, float metallic, float roughness, float3 V, float3 N)
{
	
    float3 reflection = reflect(-V, N);
    float3 diff = IBLColor * IBLIntensity * diffuse * SunColor * roughness;
    float3 spec = SkyboxTexture.Sample(CubemapSampler, reflection).xyz * IBLIntensity * IBLColor * SunColor;

    float3 weakspec = spec * pow(max(dot(V, -reflection), 0.0f), 100.0f) * (1 - roughness);

    spec = lerp(spec, weakspec, clamp(exp(roughness * 3.0f - 1.0f), 0.0f, 1.0f));

    return diff + spec;
}

float3 ComputePointLight(
    PointLight light,
    float3 P, // 현재 픽셀 위치 (월드 좌표)
    float3 N, // 픽셀의 법선 벡터
    PBRParameters params,
    float3 V // 뷰 방향
)
{
    float3 L = light.Position - P; // 라이트 방향
    float distance = length(L); // 라이트와의 거리
    L = normalize(L);

    // 거리 감쇠 (Attenuation)
    float attenuation = saturate(1.0 - pow(distance / light.Range, 2.0));

    // N.L 계산 (라이트와 표면 간의 각도)
    float NdotL = max(dot(N, L), 0.0f);

    // PBR 기반 조명 계산
    float3 radiance = light.Color * light.Intensity * attenuation;
    float3 pointLightColor = ComputePBRColor(
        params,
        V,
        N,
        L,
        radiance
    );

    return pointLightColor * NdotL; // 최종 조명 값
}

bool IsTextureNormalEnabled()
{
	return Bitmask & (1 << 3); // Normal
}

float3 TwoChannelNormalX2(float2 normal)
{
    float2 xy = 2.0f * normal - 1.0f;
    float z = sqrt(1 - dot(xy, xy));
    return float3(xy.x, xy.y, z);
}

float3 GetNormal(
	in float2 texcoord,
	in float3x3 tbn)
{

	float3 sampledNormal = (2.0f * NormalTexture.Sample(LinearSampler, texcoord)) - 1.0f;
	sampledNormal = normalize(mul(sampledNormal, tbn));

	return sampledNormal;
}

PsOut main(in In input)
{
	float4 diffuse;
	float3 emissive;
	float metalness;
	float roughness;
	float occlusion;

	GetTextureSamples(
		input.vTexcoord,
		diffuse,
		emissive,
		metalness,
		roughness,
		occlusion
	);

	float3 N = float3(0.0f, 0.0f, 0.0f);

	[branch]			// Normal
	if (IsTextureNormalEnabled())
	{
		N = GetNormal(input.vTexcoord, input.vTBN);
	}
	else
	{
		N = normalize(input.vTBN[2]);
	}

	float3 V = normalize(CameraPosition - input.vPixelWorldPos);	// Camera dir
	float3 P = input.vPixelWorldPos;								// Pixel pos
    float3 L = normalize(SunPos); // Light dir

	PBRParameters params = ComputePBRParameters(
		diffuse.xyz,
		metalness,
		roughness
	);

	float3 suncolor = SunColor * GetDirectionalLightAttenuation();

    float3 color = ComputePBRColor(
		params,
		V,
		N,
		L,
		suncolor
	);

    float3 iblcolor = GetImageBasedLighting(params, diffuse.xyz, metalness, roughness, V, N);
    color *= occlusion;
    color += iblcolor;
	
	[unroll]
    for (uint i = 0; i < NumPointLights; ++i)
    {
        color += ComputePointLight(PointLights[i], P, N, params, V);
    }

    PsOut output;
    output.Color = ToneMap(float4(color, 1.0f));
	//output.Color.rgb = GammaEncode(output.Color.rgb);
	
	float shadow = GetShadow(input.vPixelLightSpacePos, N, L);
	
	// Lift shadows
    output.Shadow = shadow + IBLIntensity * 2.0f;
    clamp(output.Shadow, 0.0f, 1.0f);

    return output;
}