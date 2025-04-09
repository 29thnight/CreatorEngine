#define NUM_HISTOGRAM_BINS 256

cbuffer LuminanceAverageData : register(b0)
{
    uint pixelCount;
    float minLogLuminance;
    float logLuminanceRange;
    float timeDelta;
    float tau;
};

RWStructuredBuffer<uint> LuminanceHistogram : register(u0); // �Է� ������׷�
RWTexture2D<float> LuminanceOutput : register(u1); // ��� ��̳ͽ� �ؽ�ó

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

    // ���� ��� ��� ���
    if (groupIndex == 0)
    {
        float totalWeight = HistogramShared[0];

        // pixelCount - countForThisBin == 0 �̸� division by zero ����
        float denom = max((float) pixelCount - countForThisBin, 1.0f);

        // �α� ��� ��� (�����ϰ�)
        float weightedLogAverage = (totalWeight / denom) - 1.0f;

        // �α� ����� NaN�̸� �⺻�� ���
        if (!isfinite(weightedLogAverage))
            weightedLogAverage = 0.0f;

        // log �� linear ��ȯ
        float weightedAverageLuminance = exp2(((weightedLogAverage / 254.0f) * logLuminanceRange) + minLogLuminance);

        // ���� �������� ��
        float luminanceLastFrame = LuminanceOutput[uint2(0, 0)];

        // ���� ��� (���� ����)
        float adaptedLuminance = luminanceLastFrame + (weightedAverageLuminance - luminanceLastFrame) * (1.0f - exp(-timeDelta * tau));

        // ����� �������� ������ �⺻�� ���
        if (!isfinite(adaptedLuminance))
            adaptedLuminance = 1.0f;

        // ����
        LuminanceOutput[uint2(0, 0)] = adaptedLuminance;
    }
}