#include "ColorGrading.hlsli"
RWTexture2D<float4> resultTexture : register(u0);

cbuffer ColorGradingParams : register(b0)
{
    // Tonal & Color Tinting
    float4 shadows; // .rgb: Color, .a: Brightness Offset
    float4 midtones; // .rgb: Color, .a: Brightness Power
    float4 highlights; // .rgb: Color, .a: Brightness Scale
    
    // Basic
    float exposure;
    float contrast;
    float saturation;

    // White Balance
    float temperature;
    float tint;
};
// Gid = Dispatch(8,8,1)
// GTid = numThreads(8,8,16)

// --- 톤 및 컬러 틴팅 함수 (Lift, Gamma, Gain) ---

float3 ApplyTonalGrading(float3 color)
{
    // 1. 현재 픽셀의 밝기(Luminance)를 계산
    float lum = dot(color, float3(0.299, 0.587, 0.114));

    // 2. 밝기 기반 마스크 생성
    float shadowsMask = pow(1.0 - saturate(lum), 2.0);
    float highlightsMask = pow(saturate(lum), 2.0);
    // 중간 영역은 양쪽 끝을 제외한 부분
    float midtonesMask = 1.0 - shadowsMask - highlightsMask;

    // 3. 각 영역별 밝기 및 색상 조절
    // Shadows (Lift): 더하기 연산으로 어두운 부분을 들어올림
    color += shadowsMask * (shadows.rgb - 1.0f + shadows.a);

    // Highlights (Gain): 곱하기 연산으로 밝은 부분을 증폭
    color *= highlightsMask * (highlights.rgb * highlights.a) + (1.0 - highlightsMask);

    // Midtones (Gamma): pow 연산으로 중간 톤을 조절
    // midtones.a가 1.0이면 변화 없음, 1.0보다 크면 밝아지고, 작으면 어두워짐
    color = pow(abs(color), 1.0 / midtones.a) * sign(color);
    color *= midtonesMask * midtones.rgb + (1.0 - midtonesMask);

    return color;
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID, uint3 Gid : SV_GroupID, uint3 GTid : SV_GroupThreadID)
{
    uint3 gid = Gid;
    uint3 gtid = GTid;
    
    uint2 xy = gid.xy * 8 + gtid.xy; //0~63
    float1 z = (xy.y / 16) * 4 + (xy.x / 16);
    
    float3 Color = float3(xy % 16 / 15.f, z / 15.f);
    
    Color = ApplyExposure(Color, exposure);
    Color = ApplyContrast(Color, contrast);
    Color = ApplyWhiteBalance(Color, temperature, tint);
    Color = ApplyTonalGrading(Color); // Lift, Gamma, Gain
    Color = ApplySaturation(Color, saturation);
    Color = saturate(Color); // 0~1 범위 밖의 값을 잘라냄
    
    resultTexture[xy] = float4(Color, 1);
}