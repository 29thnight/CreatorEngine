#ifndef COMMON
#define COMMON


static const float PI = 3.14159265f;
static const float EPSILON = 1e-6f;

float4 ToneMap(float4 colorin) 
{
    float4 mapped = colorin / (colorin + float4(1.0f, 1.0f, 1.0f, 1.0f));
    mapped.w = 1.0f;
    return mapped;
}

float ToneMap1D(float colorin) 
{
	float mapped = colorin / (colorin + float(1.0f));
	return mapped;
}

float GetPointLightAttenuation(float distance, float Kl, float Kq) 
{
    return 1.0f / (1 + Kl * distance + Kq * pow(distance,2));
}
float GetDirectionalLightAttenuation() 
{
    return 5.0f; // Overestimate energy from the sun as it is dominant source
}

float3 GammaDecode(float3 color)
{
    return color * (color * (color * 0.305306011f + 0.682171111f) + 0.012522878f);
    
    //return pow(color, 2.2f);
}

float3 GammaEncode(float3 color)
{
    color = saturate(color);
    return max(1.055f * pow(color, 0.416666667f) - 0.055f, 0.0f);
    
    //return pow(color, 1.0f / 2.2f);
}

#endif // COMMON

