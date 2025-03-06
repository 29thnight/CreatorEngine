
// Blends 2 textures together
// One must be grayscale

Texture2D<float4> SourceA : register(t0);
Texture2D<float> SourceB : register(t1);

RWTexture2D<float4> Output : register(u0);

[numthreads(16, 16, 1)]
void main(int3 DispatchID : SV_DispatchThreadID)
{
	float4 color = SourceA[DispatchID.xy] * SourceB[DispatchID.xy];

    Output[DispatchID.xy] = pow(color, 1.0f / 2.2f);
}