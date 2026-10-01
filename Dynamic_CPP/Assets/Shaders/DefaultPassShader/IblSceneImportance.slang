#include "Includes/IblImportance.slang"
cbuffer IblDrawConstants : register(b0)
{
    float4 gForward; float4 gRight; float4 gUp; float4 gParams;
};
TextureCube<float4> gCube : register(t0);
SamplerState gSampler : register(s0);
RWTexture2D<float4> gRows : register(u0);

// One scan per row: O(6*N*N) reads, instead of recomputing every prefix
// for every texel. This runs once during environment generation, never per frame.
[shader("compute")]
[numthreads(64,1,1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    const uint size=uint(gParams.z), row=id.x;
    if(row>=6u*size) return;
    const float v=(float(row%size)+0.5)*(2.0/float(size))-1.0;
    float weight=0.0;
    for(uint x=0u;x<size;++x)
    {
        const float2 uv=float2((float(x)+0.5)*(2.0/float(size))-1.0,v);
        const float3 radiance=SanitizeEnvironmentRadiance(gCube.SampleLevel(gSampler,IblFaceDirection(row/size,uv),0).rgb);
        weight+=IblCellWeight(radiance,uv);
        gRows[uint2(x,row)]=float4(radiance,weight);
    }
}
