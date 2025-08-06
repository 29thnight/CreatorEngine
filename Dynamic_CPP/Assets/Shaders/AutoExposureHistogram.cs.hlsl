#define Epsilon 0.0001
#define NUM_HISTOGRAM_BINS 256

cbuffer LuminanceHistogramData : register(b0)
{
    uint inputWidth;
    uint inputHeight;
    float minLogLuminance;
    float oneOverLogLuminanceRange;
};

Texture2D HDRTexture : register(t0); // HDR �Է� �ؽ�ó
RWStructuredBuffer<uint> LuminanceHistogram : register(u0);

groupshared uint HistogramShared[NUM_HISTOGRAM_BINS];

// ��⸦ �α� �����Ϸ� ��ȯ�Ͽ� ������׷� bin�� ����
uint HDRToHistogramBin(float3 hdrColor)
{
    float luminance = dot(hdrColor, float3(0.2126f, 0.7152f, 0.0722f)); // ǥ�� ��̳ͽ� ���

    if (luminance < Epsilon)
        return 0;

    float logLuminance = saturate((log2(luminance) - minLogLuminance) * oneOverLogLuminanceRange);
    return (uint) (logLuminance * 254.0f + 1.0f); // bin 1~255 ���, 0�� ���� 0�� ����� ��
}

[numthreads(16, 16, 1)]
void main(uint groupIndex : SV_GroupIndex, uint3 groupId : SV_GroupID, uint3 threadId : SV_DispatchThreadID)
{
    // �׷� �� ���� ������׷� �ʱ�ȭ
    HistogramShared[groupIndex] = 0;
    GroupMemoryBarrierWithGroupSync();

    // ȭ�� ���� ���� �ȼ��� ó��
    if (threadId.x < inputWidth && threadId.y < inputHeight)
    {
        float3 hdrColor = HDRTexture.Load(int3(threadId.xy, 0)).rgb;
        uint binIndex = HDRToHistogramBin(hdrColor);
        InterlockedAdd(HistogramShared[binIndex], 1);
    }

    GroupMemoryBarrierWithGroupSync();

    // �׷� �� �� �����尡 �ڽ��� �ε����� �����ϴ� bin ����� global buffer�� Ŀ��
    if (groupIndex < NUM_HISTOGRAM_BINS)
    {
        InterlockedAdd(LuminanceHistogram[groupIndex], HistogramShared[groupIndex]);
    }
}
