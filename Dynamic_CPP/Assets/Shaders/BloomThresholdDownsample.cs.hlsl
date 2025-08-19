
Texture2D<float4> inputTexture : register(t0);
RWTexture2D<float4> outputTexture : register(u0);
SamplerState linearClampToBorder : register(s3);

cbuffer ThresholdParams : register(b0)
{
    float threshold; // ��: 1.0
    float knee; // ��: 0.5 ~ 1.0
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

    // �� ��� �ȼ��� �����ϴ� �Է� 2x2 ����� "����" ��ǥ
    int2 base = int2(pixel * 2);

    // 4x4 ��Ʈ Ŀ���� 1D ����ġ (�и� ����)
    const int wx[4] = { 1, 3, 3, 1 }; // �� 8
    const int wy[4] = { 1, 3, 3, 1 }; // �� 8
    const float norm = 1.0 / 64.0; // 8 * 8

    float3 sum = 0;

    // base-1 .. base+2 ������ Ŀ�� (�� 4x4 ��)
    // �߽��� base + (0.5, 0.5) �αٿ� �ش� �� bilinear ��Ʈ�� ����
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

    // (����) anti-firefly: threshold ��� ���ȼ� ����
    float luma = dot(cTent, float3(0.2126, 0.7152, 0.0722));
    cTent *= min(1.0, threshold / max(luma, 1e-6));

    // ����Ʈ �������
    float t0 = threshold;
    float t1 = threshold + max(knee, 1e-6);
    float soft = smoothstep(t0, t1, luma);

    outputTexture[pixel] = float4(cTent * soft, 0.0);
}
