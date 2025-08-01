// AutoExposureDownsample.cs.hlsl

Texture2D<float4> g_InputTexture : register(t0);
RWTexture2D<float> g_OutputTexture : register(u0);

static const float3 LuminanceFactors = float3(0.2126, 0.7152, 0.0722);

[numthreads(32, 32, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint2 inputDims;
    g_InputTexture.GetDimensions(inputDims.x, inputDims.y);

    uint2 outputDims;
    g_OutputTexture.GetDimensions(outputDims.x, outputDims.y);

    if (DTid.x >= outputDims.x || DTid.y >= outputDims.y)
        return;

    uint2 inputCoordStart = DTid.xy * 2;

    float sumLuminance = 0.0;
    int numSamples = 0;

    [unroll]
    for (int y = 0; y < 2; ++y)
    {
        [unroll]
        for (int x = 0; x < 2; ++x)
        {
            uint2 sampleCoord = inputCoordStart + uint2(x, y);
            if (sampleCoord.x < inputDims.x && sampleCoord.y < inputDims.y)
            {
                float luminance;
                float4 sampledColor = g_InputTexture[sampleCoord];
                
                if (sampledColor.g != 0 || sampledColor.b != 0)
                {
                    luminance = dot(sampledColor.rgb, LuminanceFactors);
                }
                else
                {
                    luminance = sampledColor.r;
                }
                sumLuminance += luminance;
                numSamples++;
            }
        }
    }

    float averageLuminance = sumLuminance / (float) numSamples;
    g_OutputTexture[DTid.xy] = averageLuminance;
}