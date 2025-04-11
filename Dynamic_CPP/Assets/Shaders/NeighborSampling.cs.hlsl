RWTexture2D<float4> TargetTexture : register(u0); // 타겟 텍스처 (UAV)
Texture2D<float4> InputTexture : register(t0);
// sobel filter algorithm

// Sobel 필터 (엣지 검출)
float SobelEdgeDetection(Texture2D<float4> tex, int2 uv)
{
    float4 Gx =
        -tex.Load(int3(uv + int2(-1, -1), 0)) - 2 * tex.Load(int3(uv + int2(-1, 0), 0)) - tex.Load(int3(uv + int2(-1, 1), 0)) +
         tex.Load(int3(uv + int2(1, -1), 0)) + 2 * tex.Load(int3(uv + int2(1, 0), 0)) + tex.Load(int3(uv + int2(1, 1), 0));

    float4 Gy =
        -tex.Load(int3(uv + int2(-1, -1), 0)) - 2 * tex.Load(int3(uv + int2(0, -1), 0)) - tex.Load(int3(uv + int2(1, -1), 0)) +
         tex.Load(int3(uv + int2(-1, 1) , 0)) + 2 * tex.Load(int3(uv + int2(0, 1) , 0)) + tex.Load(int3(uv + int2(1, 1) , 0));

    return length(Gx.rgb + Gy.rgb);
}

[numthreads(32, 32, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    int2 uv = DTid.xy;
    
    float edge = SobelEdgeDetection(InputTexture, uv);
    
    float4 color = InputTexture[uv];

    // 엣지 픽셀이라면 주변 픽셀 가져오기 (팽창 효과)
    if (edge > 0.1)
    {
        for (int y = -1; y <= 1; y++)
        {
            for (int x = -1; x <= 1; x++)
            {
                float4 neighborColor = InputTexture[uv + int2(x, y)];
                if (neighborColor.a > 0.8)
                {
                    color = neighborColor;
                    break;
                }
            }
        }
        //color /= 9.0;
    }
    TargetTexture[uv] = color;
}