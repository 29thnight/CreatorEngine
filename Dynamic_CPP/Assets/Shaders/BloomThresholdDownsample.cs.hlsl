
Texture2D<float4> inputTexture : register(t0);
RWTexture2D<float4> outputTexture : register(u0);
SamplerState linearClampToBorder : register(s3);

cbuffer ThresholdParams : register(b0)
{
    float threshold; // 예: 1.0
    float knee; // 예: 0.5 ~ 1.0
}

[numthreads(8, 8, 1)]
void main(uint3 groupID : SV_GroupID,
          uint3 groupThreadID : SV_GroupThreadID,
          uint groupIndex : SV_GroupIndex,
          uint3 dispatchID : SV_DispatchThreadID)
{
    uint2 pixel = dispatchID.xy;

    uint outW, outH;
    outputTexture.GetDimensions(outW, outH);
    if (pixel.x >= outW || pixel.y >= outH)
        return;

    uint inW, inH;
    inputTexture.GetDimensions(inW, inH);

    // 이 출력 픽셀에 대응하는 입력 2x2 블록의 "시작" 좌표
    int2 base = int2(pixel * 2);

    // 4x4 텐트 커널의 1D 가중치 (분리 가능)
    const int wx[4] = { 1, 3, 3, 1 }; // 합 8
    const int wy[4] = { 1, 3, 3, 1 }; // 합 8
    const float norm = 1.0 / 64.0; // 8 * 8

    float3 sum = 0;

    // base-1 .. base+2 범위를 커버 (총 4x4 탭)
    // 중심은 base + (0.5, 0.5) 부근에 해당 → bilinear 텐트에 근접
    [unroll]
    for (int j = 0; j < 4; ++j)
    {
        int sy = base.y + (j - 1);
        sy = clamp(sy, 0, int(inH) - 1);

        [unroll]
        for (int i = 0; i < 4; ++i)
        {
            int sx = base.x + (i - 1);
            sx = clamp(sx, 0, int(inW) - 1);

            float3 c = inputTexture.Load(int3(sx, sy, 0)).rgb;
            sum += c * float(wx[i] * wy[j]);
        }
    }

    float3 cTent = sum * norm;

    // (선택) anti-firefly: threshold 기반 핫픽셀 억제
    float luma = dot(cTent, float3(0.2126, 0.7152, 0.0722));
    cTent *= min(1.0, threshold / max(luma, 1e-6));

    // 소프트 스레숄드
    float t0 = threshold;
    float t1 = threshold + max(knee, 1e-6);
    float soft = smoothstep(t0, t1, luma);

    outputTexture[pixel] = float4(cTent * soft, 0.0);
}
