#include "Sampler.hlsli"
#include "ColorGrading.hlsli"

static const float LutSize = 64;
static const float Size = 16;
static const float SizeRoot = 4;

struct PixelShaderInput // see Fullscreen.vs.hlsl
{
    float4 position : SV_POSITION;
    float2 texCoord : TEXCOORD0;
};

cbuffer ColorGradingCBuffer : register(b0)
{
    float lerpValue;
    float time; // 0~1
    bool useLUTEdit;
}

cbuffer ColorGradingParams : register(b1)
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

Texture2D<float4> ColorTexture : register(t0);

Texture2D<float4> LUT : register(t1);
//Texture2D<float4> NewLUT : register(t2);


float4 main(PixelShaderInput IN) : SV_TARGET
{
    float4 baseColor = ColorTexture.Sample(LinearSampler, IN.texCoord);
    baseColor = saturate(baseColor);

    float red = baseColor.r * (Size - 1);
    float green = baseColor.g * (Size - 1);
    float blue = baseColor.b * (Size - 1);

    float redFrac = frac(red);
    float greenFrac = frac(green);
    float blueFrac = frac(blue);

    int red0 = (int) floor(red);
    int red1 = min(red0 + 1, Size - 1);

    int green0 = (int) floor(green);
    int green1 = min(green0 + 1, Size - 1);

    int blue0 = (int) floor(blue);
    int blue1 = min(blue0 + 1, Size - 1);

    int blueCol0 = blue0 % SizeRoot;
    int blueRow0 = blue0 / SizeRoot;

    int blueCol1 = blue1 % SizeRoot;
    int blueRow1 = blue1 / SizeRoot;

    int2 basePos00 = int2(blueCol0 * Size, blueRow0 * Size);
    int2 basePos01 = int2(blueCol1 * Size, blueRow1 * Size);

    if (useLUTEdit == false)
    {
    
// Read 8 LUT values for trilinear interpolation
        float4 c000 = LUT.Load(int3(basePos00 + int2(red0, green0), 0));
        float4 c100 = LUT.Load(int3(basePos00 + int2(red1, green0), 0));
        float4 c010 = LUT.Load(int3(basePos00 + int2(red0, green1), 0));
        float4 c110 = LUT.Load(int3(basePos00 + int2(red1, green1), 0));
        float4 c001 = LUT.Load(int3(basePos01 + int2(red0, green0), 0));
        float4 c101 = LUT.Load(int3(basePos01 + int2(red1, green0), 0));
        float4 c011 = LUT.Load(int3(basePos01 + int2(red0, green1), 0));
        float4 c111 = LUT.Load(int3(basePos01 + int2(red1, green1), 0));

// Trilinear interpolation
        float4 c00 = lerp(c000, c100, redFrac);
        float4 c01 = lerp(c010, c110, redFrac);
        float4 c0 = lerp(c00, c01, greenFrac);

        float4 c10 = lerp(c001, c101, redFrac);
        float4 c11 = lerp(c011, c111, redFrac);
        float4 c1 = lerp(c10, c11, greenFrac);

        float4 lutColor = lerp(c0, c1, blueFrac);

// Blend LUT result with base color
    //float4 finalColor = lerp(baseColor, lutColor, lerpValue);
        float4 finalColor = lerp(baseColor, lutColor, min(time, 1.0f));
        finalColor = lerp(baseColor, finalColor, lerpValue);
        finalColor.a = 1.0f;
        return finalColor;
    }
    else
    {
// // Read 8 Edit LUT values for trilinear interpolation
//        float4 c000 = LUT.Load(int3(basePos00 + int2(red0, green0), 0));
//        float4 c100 = LUT.Load(int3(basePos00 + int2(red1, green0), 0));
//        float4 c010 = LUT.Load(int3(basePos00 + int2(red0, green1), 0));
//        float4 c110 = LUT.Load(int3(basePos00 + int2(red1, green1), 0));

//        float4 c001 = LUT.Load(int3(basePos01 + int2(red0, green0), 0));
//        float4 c101 = LUT.Load(int3(basePos01 + int2(red1, green0), 0));
//        float4 c011 = LUT.Load(int3(basePos01 + int2(red0, green1), 0));
//        float4 c111 = LUT.Load(int3(basePos01 + int2(red1, green1), 0));

//// Trilinear interpolation
//        float4 c00 = lerp(c000, c100, redFrac);
//        float4 c01 = lerp(c010, c110, redFrac);
//        float4 c0 = lerp(c00, c01, greenFrac);

//        float4 c10 = lerp(c001, c101, redFrac);
//        float4 c11 = lerp(c011, c111, redFrac);
//        float4 c1 = lerp(c10, c11, greenFrac);

//        float4 lutColor = lerp(c0, c1, blueFrac);

//// Blend LUT result with base color
//    //float4 finalColor = lerp(baseColor, lutColor, lerpValue);
//        float4 finalColor = lerp(baseColor, lutColor, min(time, 1.0f));
//        finalColor = lerp(baseColor, finalColor, lerpValue);
//        finalColor.a = 1.0f;
//        return finalColor;
        
        float3 sceneColor = ColorTexture.Sample(LinearSampler, IN.texCoord).rgb;
        // 2. 컬러 그레이딩 적용 (순서가 중요!)
        sceneColor = ApplyExposure(sceneColor, exposure);
        sceneColor = ApplyContrast(sceneColor, contrast);
        sceneColor = ApplyWhiteBalance(sceneColor, temperature, tint);
        sceneColor = ApplyTonalGrading(sceneColor); // Lift, Gamma, Gain
        sceneColor = ApplySaturation(sceneColor, saturation);
   
        // 3. 최종 클리핑 및 출력
        sceneColor = saturate(sceneColor); // 0~1 범위 밖의 값을 잘라냄
        
        return float4(sceneColor, 1);
    }
}