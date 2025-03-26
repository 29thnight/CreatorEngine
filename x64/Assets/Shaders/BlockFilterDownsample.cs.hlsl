// BlockFilterDownsampleCS.hlsl

cbuffer DownsampleCB : register(b0)
{
    float2 texelScale; // ���� �ػ� / Ÿ�� �ػ� (ex. (8.0, 8.0))
    float2 invSrcSize; // 1 / ���� �ػ� (ex. (1/1920, 1/1080))
};

Texture2D<float4> Input : register(t0);
RWTexture2D<float4> Output : register(u0);

SamplerState samplerLinearClamp : register(s0);

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    float2 uvBase = DTid.xy * texelScale * invSrcSize;

    float4 sum = float4(0, 0, 0, 0);
    int sampleCount = 0;

    // ��� ���͸�: texelScale ũ�⸸ŭ ���� ���
    for (int y = 0; y < (int) texelScale.y; ++y)
    {
        for (int x = 0; x < (int) texelScale.x; ++x)
        {
            float2 offsetUV = uvBase + float2(x, y) * invSrcSize;
            sum += Input.SampleLevel(samplerLinearClamp, offsetUV, 0);
            sampleCount++;
        }
    }

    Output[DTid.xy] = sum / sampleCount;
}