// �Է� HDR �ؽ�ó (��: ���� ������ �н��� ��� �Ǵ� �� ū �� ����)
Texture2D<float4> g_InputTexture : register(t0);

// �ٿ���ø��� ����� ������ ��� �ؽ�ó (��: �������� ���� �� ����)
RWTexture2D<float4> g_OutputTexture : register(u0);

// �ֵ� ����� ���� ��� (ǥ�� Rec. 709)
static const float3 LuminanceFactors = float3(0.2126, 0.7152, 0.0722);

// ���� ���� ������ 0 ����
static const float EPSILON = 1e-4f;

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint2 inputDims;
    g_InputTexture.GetDimensions(inputDims.x, inputDims.y);

    uint2 outputDims;
    g_OutputTexture.GetDimensions(outputDims.x, outputDims.y);

    uint2 inputCoordStart = DTid.xy * 2;

    float sumLogLum = 0.0;
    int numSamples = 0;

    [unroll]
    for (int y = 0; y < 2; ++y)
    {
        [unroll]
        for (int x = 0; x < 2; ++x)
        {
            uint2 sampleCoord = inputCoordStart + uint2(x, y);

            if (sampleCoord.x < inputDims.x && sampleCoord.y < inputDims.y)
            {
                float3 rgb = g_InputTexture[sampleCoord].rgb;
                float lum = dot(rgb, LuminanceFactors);

                // �α� �����̽��� ��ȯ
                sumLogLum += log2(lum + EPSILON);
                numSamples++;
            }
        }
    }

    float avgLogLum = sumLogLum / max(1, numSamples);

    // �α� �����̽� ����� �ٽ� exp2�� �ǵ���
    float logAverageLum = exp2(avgLogLum);

    g_OutputTexture[DTid.xy] = float4(logAverageLum, logAverageLum, logAverageLum, 1.0);
}
