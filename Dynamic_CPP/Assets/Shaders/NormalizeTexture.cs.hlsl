RWTexture2D<float4> TargetTexture : register(u0); // 타겟 텍스처 (UAV)
Texture2D<float4> InputTexture : register(t0);

[numthreads(16, 16, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    int2 uv = DTid.xy;
    
    float3 colorSum = float3(0, 0, 0);
    //int count = 0;
    
    for (int i = -1; i < 2; ++i)
    {
        for (int j = -1; j < 2; ++j)
        {
            int2 sampleCoord = uv + int2(i, j);
            //float3 color = InputTexture.Load(int3(sampleCoord, 0)).rgb;
            //if (color.r == 0 && color.g == 0 && color.b == 0)
            //{
            //    // Skip black pixels
            //    continue;
            //}
            colorSum += InputTexture.Load(int3(sampleCoord, 0)).rgb;
            //count++;
        }
    }
    
    float3 resolvedColor = colorSum / 9.0;
    resolvedColor = normalize(resolvedColor);
    
    // -1~1을 0~1로 변환
    resolvedColor = (resolvedColor + 1.0) * 0.5;
    
    TargetTexture[uv] = float4(resolvedColor, 1);
    //TargetTexture[uv] = float4(normalize(InputTexture.Load(int3(uv, 0)).rgb) + float3(1, 1, 1), 1);
    
    //float4 color = InputTexture.Load(int3(uv, 0));
    
    //TargetTexture[uv] = float4(normalize(color.xyz), 0);
}