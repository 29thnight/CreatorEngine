float3 ApplyExposure(float3 color, float exp)
{
    return color * exp;
}

float3 ApplyContrast(float3 color, float cont)
{
    // 0.5를 기준으로 대비를 조절
    return (color - 0.5f) * cont + 0.5f;
}

float3 ApplySaturation(float3 color, float sat)
{
    // NTSC 표준 휘도(Luminance) 계산
    float lum = dot(color, float3(0.299, 0.587, 0.114));
    return lerp(lum.xxx, color, sat);
}

// --- 화이트 밸런스 함수 ---

float3 ApplyWhiteBalance(float3 color, float temp, float tnt)
{
    // 색 온도는 R과 B 채널에 영향을 줌
    color.r += temp * 0.1f; // 0.1은 민감도 조절용
    color.b -= temp * 0.1f;

    // 색조는 G 채널에 영향을 줌
    color.g += tnt * 0.1f;

    return color;
}