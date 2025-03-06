#define MAXLIGHT 25

struct PointLight
{
    float3 Position;
    float Range; // 최대 거리
    float3 Color;
    float Intensity; // 밝기
};

float3 ComputeColorSun(float3 diffuse, float3 specular, float reflectivity, float3 N, float3 V, float3 P, float3 L, float3 suncolor)
{
	float3 reflectiondir = reflect(-L,N);
	float specularity = max(reflectivity * 64.0f, 2.0f);
	float dotL = max(mul(L, N), 0.0f);
	float dotV = max(mul(V, reflectiondir), 0.0f);
	
	float3 diffuseColor = diffuse * dotL * suncolor;
	float3 specularColor = specular * pow(dotV, specularity) * suncolor * reflectivity;
	return diffuseColor + specularColor;
}