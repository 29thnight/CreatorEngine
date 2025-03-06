Texture2D<float4> g_txColourBuffer : register(t0); // Input texture
RWTexture2D<float4> g_txOutputBuffer : register(u0); // Output texture

cbuffer lapacianFilterProperties : register(b0)
{
    uint width;
    uint height;
    uint _spacer1;
    uint _spacer2;
}

const float edgeThreshold = 0.3f;

[numthreads(16, 16, 1)] // Adjust thread group size as needed
void main(uint3 DTid : SV_DispatchThreadID)
{
    int2 texCoord = int2(DTid.xy);

    // Check bounds
    if (texCoord.x >= width || texCoord.y >= height)
    {
        return;
    }

    const int2 texAddrOffsets[8] =
    {
        int2(-1, -1),
        int2(0, -1),
        int2(1, -1),
        int2(-1, 0),
        int2(1, 0),
        int2(-1, 1),
        int2(0, 1),
        int2(1, 1),
    };

    float lum[8];
    float3 LuminanceConv = { 0.2125f, 0.7154f, 0.0721f };

    for (int i = 0; i < 8; i++)
    {
        int2 offsetCoord = texCoord + texAddrOffsets[i];
        if (offsetCoord.x < 0 || offsetCoord.y < 0 || offsetCoord.x >= width || offsetCoord.y >= height)
        {
            lum[i] = 0.0f; // Handle out-of-bounds
        }
        else
        {
            float3 colour = g_txColourBuffer.Load(int3(offsetCoord, 0)).rgb;
            lum[i] = dot(colour, LuminanceConv);
        }
    }
    
    float4 originalColor = g_txColourBuffer.Load(int3(texCoord, 0));

    float x = lum[0] + 2 * lum[3] + lum[5] - lum[2] - 2 * lum[4] - lum[7];
    float y = lum[0] + 2 * lum[1] + lum[2] - lum[5] - 2 * lum[6] - lum[7];
    float edge = sqrt(x * x + y * y);
    
    // Combine original color with edge result
    float4 edgeColor = edge > edgeThreshold ? float4((float3) 1.f - float3(edge, edge, edge), 1.f) : (float4) 0.f;
    float4 finalColor = lerp(originalColor, edgeColor, 1.f) * originalColor;

    g_txOutputBuffer[texCoord] = finalColor;
}
