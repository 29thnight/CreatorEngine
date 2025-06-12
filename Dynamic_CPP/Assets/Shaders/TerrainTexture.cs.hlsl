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
    
    // ��� �˻�
    //if (xy.x >= 512 || xy.y >= 512 || slice >= 4)
    //    return;

    // (������ �ٸ���) Sample() ��� Load()�� �ᵵ �ǰ�,
    // ���⼭�� ���ǻ� SampleLevel ���
    float2 uv = (float2(xy) + 0.5f) / 512.0f;
    float4 v = inputTexture.SampleLevel(LinearSampler, float2(uv), 0);
    
    // ���ϴ� ���� ���� �߰� ����
    // e.g. v.rgb = SRGBtoLINEAR(v.rgb);

    outputTexture[uint3(xy, slice)] = v;
}
