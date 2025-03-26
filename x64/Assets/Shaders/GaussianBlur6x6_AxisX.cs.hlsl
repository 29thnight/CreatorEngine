// GaussianBlur6x6_X_CS.hlsl

cbuffer BlurCB : register(b0)
{
    uint2 texSize;
};

Texture2D<float4> Input : register(t0);
RWTexture2D<float4> Output : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= texSize.x || DTid.y >= texSize.y)
        return;

    static const float kernel[5] = { 0.06136, 0.24477, 0.38774, 0.24477, 0.06136 };
    float4 sum = float4(0, 0, 0, 0);

    for (int i = -2; i <= 2; ++i)
    {
        int x = clamp(int(DTid.x) + i, 0, texSize.x - 1);
        sum += Input.Load(int3(x, DTid.y, 0)) * kernel[i + 2];
    }

    Output[DTid.xy] = sum;
}
