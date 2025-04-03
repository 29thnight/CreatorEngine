// AutoExposureShared.hlsli
#ifndef AUTO_EXPOSURE_SHARED_INCLUDED
#define AUTO_EXPOSURE_SHARED_INCLUDED

#define NUM_BINS 128

cbuffer ExposureCB : register(b0)
{
    float2 InputResolution;
    float LogMinLuminance;
    float LogMaxLuminance;
    float HistogramScale;
    float ExposureCompensation;
    float MinEV100;
    float MaxEV100;
    float LowPercentile;
    float HighPercentile;
    uint TotalPixelCount;
};

#endif // AUTO_EXPOSURE_SHARED_INCLUDED