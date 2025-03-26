// GaussianBlur11x11_X_CS.hlsl

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

    static const float kernel[6] =
    {
        0.0093, 0.028002, 0.065984, 0.121703, 0.175713, 0.198596
    };

    float4 sum = Input.Load(int3(DTid.xy, 0)) * kernel[0];

    for (int i = 1; i < 6; ++i)
    {
        int x0 = clamp(int(DTid.x) - i, 0, texSize.x - 1);
        int x1 = clamp(int(DTid.x) + i, 0, texSize.x - 1);
        sum += Input.Load(int3(x0, DTid.y, 0)) * kernel[i];
        sum += Input.Load(int3(x1, DTid.y, 0)) * kernel[i];
    }

    Output[DTid.xy] = sum;
}
