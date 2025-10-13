#include "Sampler.hlsli"
Texture2D<uint> MaskedTexture : register(t0);
RWTexture2D<float4> OutputTexture : register(u0);

static const uint flag1 = 1;
static const uint flag2 = 2;
static const uint flag3 = 4;
static const uint flag4 = 8; 
static const uint flag5 = 16;
static const uint flag6 = 32; 
static const uint flag7 = 64; 
static const uint flag8 = 128; 

static const uint allflag = flag1 | flag2 | flag3 | flag4 | flag5 | flag6 | flag7 | flag8;

cbuffer Params : register(b0)
{
    float4 colors[8];
    float2 screenSize;
    float outlineVelocity;
}

[numthreads(16, 16, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    //float2 packed = MaskedTexture[DTid.xy];
    //uint lo = (uint) (packed.x);
    //uint hi = (uint) (packed.y);
    //uint reconstructed = (hi << 16) | lo;
    
    float2 invScreenSize = 1.0 / screenSize;
    float2 uv = DTid.xy * invScreenSize;
    //float mask[9] =
    //{
    //    -1, -1, -1,
    //      -1, 8, -1,
    //      -1, -1, -1
    //}; // Laplacian Filter
    //       
    //float coord[3] = { -1, 0, +1 };
    //float4 c0 = 0;
    //uint centerflag = MaskedTexture.Load(int3(DTid.xy, 0));
    //
    //for (int i = 0; i < 9; i++)
    //{
    //    uint flag = MaskedTexture.Load(int3(DTid.xy + uint2(coord[i % 3], coord[i / 3]), 0));
    //    
    //    for (int j = 0; j < 8; j++)
    //    {
    //        if (flag & (1 << j))
    //        {
    //            c0 += mask[i] * colors[j - 1];
    //        }
    //    }
    //         //MaskedTexture.SampleLevel(LinearSampler, uv + float2(coord[i % 3] * invScreenSize.x, coord[i / 3] * invScreenSize.y), 0);

    //}
    //c0 *= outlineVelocity;


    float4 color = 0;
    uint centerflag = MaskedTexture.Load(int3(DTid.xy, 0));
    int2 pos[4] = {
        {0,1},{-1,0},{1,0},{0,-1}
    };
    
    int flagnumber = firstbitlow(centerflag);
    int colorVelocity = colors[flagnumber - 1].a;
    
    for (int i = 0; i < 4; i++) 
    {
        uint otherflag = MaskedTexture.Load(int3((int2) DTid.xy + pos[i] * colorVelocity, 0));

        if (((otherflag ^ centerflag) & allflag) != 0) 
        {
            for (int j = 0; j < 8; j++) 
            {
                if (centerflag & (1 << j))
                {
                    color += colors[j - 1] * colors[j - 1].a;
                }
            }
        }
    }
	//color *= outlineVelocity;

    OutputTexture[uint2(DTid.xy)] = (color);

}