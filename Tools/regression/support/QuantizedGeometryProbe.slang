#include "../../../Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/QuantizedGeometry.slang"
cbuffer Constants : register(b0) { uint4 request; };
ByteAddressBuffer packed : register(t0);
RWStructuredBuffer<uint> decoded : register(u0);
[shader("compute")]
[numthreads(64,1,1)]
void QuantizedGeometryCS(uint3 id : SV_DispatchThreadID)
{
    if (id.x < request.x)
    {
        decoded[id.x] = LXGeometryWord(packed, 0, id.x / request.y, (id.x % request.y) * 4);
    }
}
