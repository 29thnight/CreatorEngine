Texture2D<float4> inputTexture : register(t0);
RWTexture2D<float4> outputTexture : register(u0);

cbuffer ThresholdParams : register(b0)
{
    float threshold; // 예: 1.0 (HDR 씬 기준)
    float knee; // 예: 0.5 ~ 1.0
}

[numthreads(8, 8, 1)]
void main(uint3 groupID : SV_GroupID,
          uint3 groupThreadID : SV_GroupThreadID,
          uint groupIndex : SV_GroupIndex,
          uint3 dispatchID : SV_DispatchThreadID)
{
    uint2 pixel = dispatchID.xy; // half-res 출력 픽셀

    // 입력 텍스처 크기 얻기
    uint inW, inH;
    inputTexture.GetDimensions(inW, inH);

    // 출력 범위 체크 (안전)
    uint outW, outH;
    outputTexture.GetDimensions(outW, outH);
    if (pixel.x >= outW || pixel.y >= outH)
        return;

    // 입력에서 2x 다운샘플할 소스 좌표
    int2 inPixel = int2(pixel * 2);

    // 경계 클램프 (짝수/홀수 해상도 모두 안전)
    int2 p00 = clamp(inPixel + int2(0, 0), int2(0, 0), int2(int(inW) - 1, int(inH) - 1));
    int2 p10 = clamp(inPixel + int2(1, 0), int2(0, 0), int2(int(inW) - 1, int(inH) - 1));
    int2 p01 = clamp(inPixel + int2(0, 1), int2(0, 0), int2(int(inW) - 1, int(inH) - 1));
    int2 p11 = clamp(inPixel + int2(1, 1), int2(0, 0), int2(int(inW) - 1, int(inH) - 1));

    // 정확한 2x2 평균(박스 필터)
    float4 c00 = inputTexture.Load(int3(p00, 0));
    float4 c10 = inputTexture.Load(int3(p10, 0));
    float4 c01 = inputTexture.Load(int3(p01, 0));
    float4 c11 = inputTexture.Load(int3(p11, 0));
    float3 color = 0.25 * (c00.rgb + c10.rgb + c01.rgb + c11.rgb);

    // 밝기(Rec.709, Linear 가정)
    float brightness = dot(color, float3(0.2126, 0.7152, 0.0722));

    // 부드러운 소프트 스레숄드 (knee == 0 보호)
    float t0 = threshold;
    float t1 = threshold + max(knee, 1e-6);
    float soft = smoothstep(t0, t1, brightness);

    // 약간 더 부드럽게 하려면 한 번 더 제곱 (선택)
    // soft *= soft;

    outputTexture[pixel] = float4(color * soft, 0.0); // 알파는 0 권장
}
