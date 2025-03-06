// Threads per group
#define N 256
#define BLUR_RADIUS 3

cbuffer BlurParameters		: register(b0)
{
	int2	TextureSize;
	float	_spacer1;
	float	_spacer2;
};

// cbuffer ConstBuffer			: register(b1)
// {
// 	static const int BlurRadius;
// }

Texture2D<float> 	ShadowMap 	: register(t0);
RWTexture2D<float> 	BluredMap 	: register(u0);
Buffer<float> 		Weights		: register(t1);
groupshared float 	Cache[N + 2 * BLUR_RADIUS];

[numthreads(N, 1, 1)]
void main(int3 ThreadID : SV_GroupThreadID, int3 DispatchID : SV_DispatchThreadID)
{
	if (ThreadID.x < BLUR_RADIUS)
	{
		// Out of bounds Clamp
		int x = max(DispatchID.x - BLUR_RADIUS, 0);
		Cache[ThreadID.x] = ShadowMap[int2(x, DispatchID.y)];
	}
	if (ThreadID.x >= N - BLUR_RADIUS)
	{
		// Out of bounds Clamp
		int x = min(DispatchID.x + BLUR_RADIUS, TextureSize.x - 1);
		Cache[ThreadID.x + 2*BLUR_RADIUS] = ShadowMap[int2(x, DispatchID.y)];
	}

	// Out of bounds Clamp
	Cache[ThreadID.x + BLUR_RADIUS] = ShadowMap[min(DispatchID.xy, TextureSize.xy - 1)];

	// Wait for all threads in a thread group
	GroupMemoryBarrierWithGroupSync();

	// float color = float(0.0f);
	float color = float(0.0f);

	[unroll]
	for(int i = -BLUR_RADIUS; i <= BLUR_RADIUS; ++i)
	{
		int k = ThreadID.x + BLUR_RADIUS + i;

		color += Weights[i + BLUR_RADIUS] * Cache[k];
	}


	BluredMap[DispatchID.xy] = color;
	// BluredMap[DispatchID.xy] = color;
}