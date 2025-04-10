#define Epsilon 0.0001
#define NUM_HISTOGRAM_BINS 256

cbuffer LuminanceHistogramData : register(b0)
{
    uint inputWidth;
    uint inputHeight;
    float minLogLuminance;
    float oneOverLogLuminanceRange;
};

Texture2D HDRTexture : register(t0); // HDR 입력 텍스처
RWStructuredBuffer<uint> LuminanceHistogram : register(u0);

groupshared uint HistogramShared[NUM_HISTOGRAM_BINS];

// 밝기를 로그 스케일로 변환하여 히스토그램 bin에 매핑
uint HDRToHistogramBin(float3 hdrColor)
{
    float luminance = dot(hdrColor, float3(0.2126f, 0.7152f, 0.0722f)); // 표준 루미넌스 계산

    if (luminance < Epsilon)
        return 0;

    float logLuminance = saturate((log2(luminance) - minLogLuminance) * oneOverLogLuminanceRange);
    return (uint) (logLuminance * 254.0f + 1.0f); // bin 1~255 사용, 0은 거의 0에 가까운 값
}

[numthreads(16, 16, 1)]
void main(uint groupIndex : SV_GroupIndex, uint3 groupId : SV_GroupID, uint3 threadId : SV_DispatchThreadID)
{
    // 그룹 내 공유 히스토그램 초기화
    HistogramShared[groupIndex] = 0;
    GroupMemoryBarrierWithGroupSync();

    // 화면 범위 안쪽 픽셀만 처리
    if (threadId.x < inputWidth && threadId.y < inputHeight)
    {
        float3 hdrColor = HDRTexture.Load(int3(threadId.xy, 0)).rgb;
        uint binIndex = HDRToHistogramBin(hdrColor);
        InterlockedAdd(HistogramShared[binIndex], 1);
    }

    GroupMemoryBarrierWithGroupSync();

    // 그룹 내 각 스레드가 자신의 인덱스에 대응하는 bin 결과를 global buffer에 커밋
    if (groupIndex < NUM_HISTOGRAM_BINS)
    {
        InterlockedAdd(LuminanceHistogram[groupIndex], HistogramShared[groupIndex]);
    }
}
