RWStructuredBuffer<uint64_t> result : register(u0);
cbuffer Params : register(b0) { uint producer; uint padding0; uint padding1; uint padding2; };
[numthreads(64,1,1)]
void ResetCS(uint3 id : SV_DispatchThreadID)
{
    if (id.x < 17) result[id.x] = ~uint64_t(0);
}
[numthreads(64,1,1)]
void ReduceCS(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= 1024) return;
    uint pixel = id.x % 16;
    uint hi = 0x7ffffffdu + (id.x % 7);
    uint lo = (producer == (pixel & 1) ? 2048 : 4096) - id.x;
    uint64_t candidate = (uint64_t(hi) << 32) | uint64_t(lo);
    uint64_t ignored;
    InterlockedMin(result[pixel], candidate, ignored);
}
