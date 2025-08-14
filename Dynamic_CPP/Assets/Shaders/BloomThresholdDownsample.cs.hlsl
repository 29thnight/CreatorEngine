Texture2D<float4> inputTexture : register(t0);
RWTexture2D<float4> outputTexture : register(u0);

cbuffer ThresholdParams : register(b0)
{
    float threshold; // ��: 1.0 (HDR �� ����)
    float knee; // ��: 0.5 ~ 1.0
}

[numthreads(8, 8, 1)]
void main(uint3 groupID : SV_GroupID,
          uint3 groupThreadID : SV_GroupThreadID,
          uint groupIndex : SV_GroupIndex,
          uint3 dispatchID : SV_DispatchThreadID)
{
    uint2 pixel = dispatchID.xy; // half-res ��� �ȼ�

    // �Է� �ؽ�ó ũ�� ���
    uint inW, inH;
    inputTexture.GetDimensions(inW, inH);

    // ��� ���� üũ (����)
    uint outW, outH;
    outputTexture.GetDimensions(outW, outH);
    if (pixel.x >= outW || pixel.y >= outH)
        return;

    // �Է¿��� 2x �ٿ������ �ҽ� ��ǥ
    int2 inPixel = int2(pixel * 2);

    // ��� Ŭ���� (¦��/Ȧ�� �ػ� ��� ����)
    int2 p00 = clamp(inPixel + int2(0, 0), int2(0, 0), int2(int(inW) - 1, int(inH) - 1));
    int2 p10 = clamp(inPixel + int2(1, 0), int2(0, 0), int2(int(inW) - 1, int(inH) - 1));
    int2 p01 = clamp(inPixel + int2(0, 1), int2(0, 0), int2(int(inW) - 1, int(inH) - 1));
    int2 p11 = clamp(inPixel + int2(1, 1), int2(0, 0), int2(int(inW) - 1, int(inH) - 1));

    // ��Ȯ�� 2x2 ���(�ڽ� ����)
    float4 c00 = inputTexture.Load(int3(p00, 0));
    float4 c10 = inputTexture.Load(int3(p10, 0));
    float4 c01 = inputTexture.Load(int3(p01, 0));
    float4 c11 = inputTexture.Load(int3(p11, 0));
    float3 color = 0.25 * (c00.rgb + c10.rgb + c01.rgb + c11.rgb);

    // ���(Rec.709, Linear ����)
    float brightness = dot(color, float3(0.2126, 0.7152, 0.0722));

    // �ε巯�� ����Ʈ ������� (knee == 0 ��ȣ)
    float t0 = threshold;
    float t1 = threshold + max(knee, 1e-6);
    float soft = smoothstep(t0, t1, brightness);

    // �ణ �� �ε巴�� �Ϸ��� �� �� �� ���� (����)
    // soft *= soft;

    outputTexture[pixel] = float4(color * soft, 0.0); // ���Ĵ� 0 ����
}
