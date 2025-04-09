#define NUM_HISTOGRAM_BINS 256

cbuffer LuminanceAverageData : register(b0)
{
    uint pixelCount;
    float minLogLuminance;
    float logLuminanceRange;
    float timeDelta;
    float tau;
};

RWStructuredBuffer<uint> LuminanceHistogram : register(u0); // 입력 히스토그램
RWTexture2D<float> LuminanceOutput : register(u1); // 출력 루미넌스 텍스처

groupshared float HistogramShared[NUM_HISTOGRAM_BINS];

[numthreads(16, 16, 1)]
void main(uint groupIndex : SV_GroupIndex)
{
    float countForThisBin = (float) LuminanceHistogram[groupIndex];
    HistogramShared[groupIndex] = countForThisBin * (float) groupIndex;

    GroupMemoryBarrierWithGroupSync();

    // Parallel reduction: sum weighted bin values
    [unroll]
    for (uint histogramSampleIndex = (NUM_HISTOGRAM_BINS >> 1); histogramSampleIndex > 0; histogramSampleIndex >>= 1)
    {
        if (groupIndex < histogramSampleIndex)
        {
            HistogramShared[groupIndex] += HistogramShared[groupIndex + histogramSampleIndex];
            
            if (!isfinite(HistogramShared[groupIndex]))
                HistogramShared[groupIndex] = 0.0f;
        }
        GroupMemoryBarrierWithGroupSync();
    }

    // 최종 평균 밝기 계산
    if (groupIndex == 0)
    {
        float totalWeight = HistogramShared[0];

        // pixelCount - countForThisBin == 0 이면 division by zero 방지
        float denom = max((float) pixelCount - countForThisBin, 1.0f);

        // 로그 평균 계산 (안전하게)
        float weightedLogAverage = (totalWeight / denom) - 1.0f;

        // 로그 평균이 NaN이면 기본값 사용
        if (!isfinite(weightedLogAverage))
            weightedLogAverage = 0.0f;

        // log → linear 변환
        float weightedAverageLuminance = exp2(((weightedLogAverage / 254.0f) * logLuminanceRange) + minLogLuminance);

        // 이전 프레임의 값
        float luminanceLastFrame = LuminanceOutput[uint2(0, 0)];

        // 적응 계산 (지수 감쇠)
        float adaptedLuminance = luminanceLastFrame + (weightedAverageLuminance - luminanceLastFrame) * (1.0f - exp(-timeDelta * tau));

        // 결과가 유한하지 않으면 기본값 사용
        if (!isfinite(adaptedLuminance))
            adaptedLuminance = 1.0f;

        // 저장
        LuminanceOutput[uint2(0, 0)] = adaptedLuminance;
    }
}