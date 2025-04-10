Texture2D<float4> inputTexture : register(t0);
RWTexture2D<float4> outputTexture : register(u0);

cbuffer ThresholdParams: register(b0)
{
    float threshold;
    float knee;
}

[numthreads(8, 8, 1)]
void main(uint3 groupID : SV_GroupID, uint3 groupThreadID : SV_GroupThreadID, uint groupIndex : SV_GroupIndex, uint3 dispatchID : SV_DispatchThreadID)
{
    // output pixel in half resolution
    uint2 pixel = uint2(dispatchID.x, dispatchID.y);

    // bilinear interpolation for downsampling
    uint2 inPixel = pixel * 2;
    float4 hIntensity0 = lerp(inputTexture[inPixel], inputTexture[inPixel + uint2(1, 0)], 0.5);
    float4 hIntensity1 = lerp(inputTexture[inPixel + uint2(0, 1)], inputTexture[inPixel + uint2(1, 1)], 0.5);
    float4 intensity = lerp(hIntensity0, hIntensity1, 0.5);

    //// thresholding on downsampled value
    //float intensityTest = (float)(length(intensity.rgb) > threshold);

    //outputTexture[pixel] = float4(intensityTest * intensity. rgb, 1.0);
    
    float4 h0 = lerp(inputTexture[inPixel], inputTexture[inPixel + uint2(1, 0)], 0.5);
    float4 h1 = lerp(inputTexture[inPixel + uint2(0, 1)], inputTexture[inPixel + uint2(1, 1)], 0.5);
    float4 color = lerp(h0, h1, 0.5);

    // 밝기 계산 (luminance)
    float luminance = dot(color.rgb, float3(0.2126, 0.7152, 0.0722)); // perceptual

    // soft-knee bloom 커브
    float softness = saturate((luminance - threshold) / knee);
    float curveWeight = softness * softness * (3.0 - 2.0 * softness); // smoothstep

    // 커브를 적용한 밝기 마스킹
    float3 bloomColor = color.rgb * curveWeight;

    outputTexture[pixel] = float4(bloomColor, 1.0);
}