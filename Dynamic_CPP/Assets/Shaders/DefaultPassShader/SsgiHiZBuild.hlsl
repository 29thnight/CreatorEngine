Texture2D<float>   gSource : register(t0);
RWTexture2D<float> gTarget : register(u0);

cbuffer HiZParams : register(b0)
{
    uint2 gTargetSize;
    uint2 gSourceSize;
};

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= gTargetSize.x || id.y >= gTargetSize.y) return;

    const uint2 base = id.xy * 2;

    float depth = gSource.Load(int3(min(base, gSourceSize - 1), 0));
    depth = min(depth, gSource.Load(int3(min(base + uint2(1, 0), gSourceSize - 1), 0)));
    depth = min(depth, gSource.Load(int3(min(base + uint2(0, 1), gSourceSize - 1), 0)));
    depth = min(depth, gSource.Load(int3(min(base + uint2(1, 1), gSourceSize - 1), 0)));

    // 소스가 홀수면 마지막 열·행이 2x2에 안 들어온다. 한 텍셀 더 본다.
    if (base.x + 2 == gSourceSize.x - 1)
    {
        depth = min(depth, gSource.Load(int3(base.x + 2, min(base.y, gSourceSize.y - 1), 0)));
    }
    if (base.y + 2 == gSourceSize.y - 1)
    {
        depth = min(depth, gSource.Load(int3(min(base.x, gSourceSize.x - 1), base.y + 2, 0)));
    }

    gTarget[id.xy] = depth;
}
