RWTexture2D<float>  gDepth  : register(u0);
#ifdef __spirv__
[[vk::image_format("rgba16f")]]
#endif
RWTexture2D<float4> gNormal : register(u1);

cbuffer SceneParams : register(b0)
{
    uint2 gSize;
    float gNearZ;
    float gFarZ;
    float gLeftViewZ;
    float gRightViewZ;
    uint2 gPad;
};

float DepthFromViewZ(float viewZ)
{
    // XMMatrixPerspectiveFovLH의 z 변환. 검증이 패스와 같은 규약을 쓰는지가
    // 여기서 갈린다 — 다르면 AO가 엉뚱한 거리를 본다.
    const float a = gFarZ / (gFarZ - gNearZ);
    return a * (1.0f - gNearZ / viewZ);
}

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= gSize)) return;

    const bool right = (id.x >= gSize.x / 2);
    const float viewZ = right ? gRightViewZ : gLeftViewZ;

    gDepth[id.xy] = DepthFromViewZ(viewZ);
    gNormal[id.xy] = float4(0.5f, 0.5f, 0.0f, 1.0f);   // (0,0,-1) 인코딩
}
