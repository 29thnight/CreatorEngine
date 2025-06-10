//TerrainTexture.hlsl
sampler LinearSampler : register(s0);

Texture2D inputTexture : register(t0);
RWTexture2DArray<float4> outputTexture : register(u0);

cbuffer cBuf : register(b0)
{
    uint slice;
}

[numthreads(16,16,1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint2 xy = DTid.xy;
    
    //uint slice = DTid.z;
    
    // 경계 검사
    //if (xy.x >= 512 || xy.y >= 512 || slice >= 4)
    //    return;

    // (포맷이 다르면) Sample() 대신 Load()를 써도 되고,
    // 여기서는 편의상 SampleLevel 사용
    float2 uv = (float2(xy) + 0.5f) / 512.0f;
    float4 v = inputTexture.SampleLevel(LinearSampler, float2(uv), 0);
    
    // 원하는 보정 로직 추가 가능
    // e.g. v.rgb = SRGBtoLINEAR(v.rgb);

    outputTexture[uint3(xy, slice)] = v;
}
