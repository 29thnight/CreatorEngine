#define NUM_HISTOGRAM_BINS 256
#define HISTOGRAM_AVERAGE_THREADS_PER_DIMENSION 16

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

[numthreads(HISTOGRAM_AVERAGE_THREADS_PER_DIMENSION, HISTOGRAM_AVERAGE_THREADS_PER_DIMENSION, 1)]
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
        }
        GroupMemoryBarrierWithGroupSync();
    }

    // 최종 평균 밝기 계산
    if (groupIndex == 0)
    {
        // 밝기 평균: weighted log 평균 → log scale → linear scale
        float weightedLogAverage = (HistogramShared[0] / max((float) pixelCount - countForThisBin, 1.0)) - 1.0;

        float weightedAverageLuminance = exp2(((weightedLogAverage / 254.0f) * logLuminanceRange) + minLogLuminance);

        // 시간 적응 (노출 변화 완화)
        float luminanceLastFrame = LuminanceOutput[uint2(0, 0)];
        float adaptedLuminance = luminanceLastFrame + (weightedAverageLuminance - luminanceLastFrame) * (1.0f - exp(-timeDelta * tau));

        LuminanceOutput[uint2(0, 0)] = adaptedLuminance;
    }
}