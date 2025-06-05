#define MAX_LAYERS 4

Texture2D<float4> g_SplatMap : register(t0);
Texture2D<float4> g_DiffuseMaps[MAX_LAYERS] : register(t1);
SamplerState g_SamplerSM : register(s0);

float4 PS_Main(PS_Input IN) : SV_Target
{
	float4 splat = g_SplatMap.Sample(g_SamplerSM, IN.TexCoord);

	float weight[MAX_LAYERS];
	weight[0] = splat.r;
	weight[1] = splat.g;
	weight[2] = splat.b;
	weight[3] = splat.a;

	float2 layerUV[MAX_LAYERS];
	[unroll]
	for (int i = 0; i < MAX_LAYERS; ++i)
	{
		layerUV[i] = IN.worldPos.xz * g_LayerTiling[i].x;
	}

	float3 accumColor = float3(0, 0, 0);
	[unroll]
	for (int i = 0; i < MAX_LAYERS; ++i)
	{
		float3 layerColor = g_DiffuseMaps[i].Sample(g_SamplerSM, layerUV[i]).rgb;
		accumColor += layerColor * weight[i];
	}

	// lighting calculations can be added here if needed

	return float4(accumColor, 1.0f);

}