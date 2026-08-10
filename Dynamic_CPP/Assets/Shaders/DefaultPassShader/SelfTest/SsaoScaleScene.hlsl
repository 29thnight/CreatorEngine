RWTexture2D<float>  gDepth  : register(u0);
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

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= gSize)) return;

    const float viewZ = (id.x >= gSize.x / 2) ? gRightViewZ : gLeftViewZ;
    const float a = gFarZ / (gFarZ - gNearZ);

    gDepth[id.xy] = a * (1.0f - gNearZ / viewZ);
    gNormal[id.xy] = float4(0.5f, 0.5f, 0.0f, 1.0f);
}
