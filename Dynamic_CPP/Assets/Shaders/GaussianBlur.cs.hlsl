#define GAUSSIAN_RADIUS 7

Texture2D<float4> inputTexture : register(t0);
RWTexture2D<float4> outputTexture : register(u0);

cbuffer BlurParams : register(b0)
{
    float4 coefficients[(GAUSSIAN_RADIUS + 1) / 4];
    float blurSigma;
    // radius <= GAUSSIAN_RADIUS, direction 0 = horizontal, 1 = vertical
    int2 radiusAndDirection;
    float _padding;
}

[numthreads(8, 8, 1)]
void main(uint3 groupID : SV_GroupID, uint3 groupThreadID : SV_GroupThreadID, uint groupIndex : SV_GroupIndex, uint3 dispatchID : SV_DispatchThreadID)
{
    //int2 pixel = int2(dispatchID.x, dispatchID.y);

    //int radius = radiusAndDirection.x;
    //int2 dir = int2(1 - radiusAndDirection.y, radiusAndDirection.y);

    //float4 accumulatedValue = float4(0.0, 0.0, 0.0, 0.0);

    //for (int i = -radius; i <= radius; ++i)
    //{
    //    uint cIndex = (uint) abs(i);
    //    accumulatedValue += coefficients[cIndex >> 2][cIndex & 3] * inputTexture[mad(i, dir, pixel)];
    //}

    //outputTexture[pixel] = accumulatedValue;
    
    int2 pixel = int2(dispatchID.xy);

    int radius = radiusAndDirection.x;
    int2 dir = int2(1 - radiusAndDirection.y, radiusAndDirection.y); // (1,0)=H, (0,1)=V

    // 텍스처 크기
    uint w, h;
    inputTexture.GetDimensions(w, h);
    int2 maxXY = int2(int(w) - 1, int(h) - 1);

    float4 sumColor = 0.0;
    float sumW = 0.0;

    // i = 0 (중심)
    {
        uint k = 0u;
        float w0 = coefficients[k >> 2][k & 3];
        int2 coord = clamp(pixel, int2(0, 0), maxXY);
        float4 c = inputTexture.Load(int3(coord, 0));
        sumColor += c * w0;
        sumW += w0;
    }

    // 양쪽 탭(반쪽 커널 가중치 동일 적용)
    [loop]
    for (int i = 1; i <= radius; ++i)
    {
        uint k = (uint) i;
        float wTap = coefficients[k >> 2][k & 3];

        int2 p0 = clamp(pixel + i * dir, int2(0, 0), maxXY);
        int2 p1 = clamp(pixel - i * dir, int2(0, 0), maxXY);

        float4 c0 = inputTexture.Load(int3(p0, 0));
        float4 c1 = inputTexture.Load(int3(p1, 0));

        sumColor += (c0 + c1) * wTap;
        sumW += 2.0 * wTap;
    }

    // 경계에서 탭이 잘렸다면 sumW가 1이 아님 → 정규화
    outputTexture[pixel] = sumColor / max(sumW, 1e-6);
}