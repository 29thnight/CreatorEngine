Texture2D<float3> Input : register(t0); // SRV
RWTexture2D<float3> Output : register(u0); // UAV

[numthreads(32, 32, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    int2 pixelCoord = DTid.xy;

    // 4°³ÀÇ »ùÇÃÀ» Æò±Õ³¿
    float3 colorSum = float3(0, 0, 0);
    
    for (int i = -2; i < 3; ++i)
    {
        for (int j = -2; j < 3; ++j)
        {
            int2 sampleCoord = pixelCoord + int2(i, j);
            colorSum += Input.Load(int3(sampleCoord, 0));
        }
    }
    
    float3 resolvedColor = colorSum / 25.0;

    Output[pixelCoord] = resolvedColor;
}