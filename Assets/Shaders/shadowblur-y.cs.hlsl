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

[numthreads(1, N, 1)]
void main(int3 ThreadID : SV_GroupThreadID, int3 DispatchID : SV_DispatchThreadID)
{
	if (ThreadID.y < BLUR_RADIUS)
	{
		// Out of bounds Clamp
		int y = max(DispatchID.y - BLUR_RADIUS, 0);
		Cache[ThreadID.y] = ShadowMap[int2(DispatchID.x, y)];
	}
	if (ThreadID.y >= N - BLUR_RADIUS)
	{
		// Out of bounds Clamp
		int y = min(DispatchID.y + BLUR_RADIUS, TextureSize.y - 1);
		Cache[ThreadID.y + 2*BLUR_RADIUS] = ShadowMap[int2(DispatchID.x, y)];
	}

	// Out of bounds Clamp
	Cache[ThreadID.y + BLUR_RADIUS] = ShadowMap[min(DispatchID.xy, TextureSize.xy - 1)];

	// Wait for all threads in a thread group
	GroupMemoryBarrierWithGroupSync();

	float color = float(0.0f);

	[unroll]
	for(int i = -BLUR_RADIUS; i <= BLUR_RADIUS; ++i)
	{
		int k = ThreadID.y + BLUR_RADIUS + i;

		color += Weights[i + BLUR_RADIUS] * Cache[k];
	}

	BluredMap[DispatchID.xy] = color;
}