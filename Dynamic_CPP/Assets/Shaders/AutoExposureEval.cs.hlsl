// 입력 HDR 텍스처 (예: 이전 렌더링 패스의 결과 또는 더 큰 밉 레벨)
Texture2D<float4> g_InputTexture : register(t0);

// 다운샘플링된 결과를 저장할 출력 텍스처 (예: 다음으로 작은 밉 레벨)
RWTexture2D<float4> g_OutputTexture : register(u0);

// 휘도 계산을 위한 상수 (표준 Rec. 709)
static const float3 LuminanceFactors = float3(0.2126, 0.7152, 0.0722);

// 아주 작은 값으로 0 방지
static const float EPSILON = 1e-4f;

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint2 inputDims;
    g_InputTexture.GetDimensions(inputDims.x, inputDims.y);

    uint2 outputDims;
    g_OutputTexture.GetDimensions(outputDims.x, outputDims.y);

    uint2 inputCoordStart = DTid.xy * 2;

    float sumLogLum = 0.0;
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
                float3 rgb = g_InputTexture[sampleCoord].rgb;
                float lum = dot(rgb, LuminanceFactors);

                // 로그 스페이스로 변환
                sumLogLum += log2(lum + EPSILON);
                numSamples++;
            }
        }
    }

    float avgLogLum = sumLogLum / max(1, numSamples);

    // 로그 스페이스 평균을 다시 exp2로 되돌림
    float logAverageLum = exp2(avgLogLum);

    g_OutputTexture[DTid.xy] = float4(logAverageLum, logAverageLum, logAverageLum, 1.0);
}
