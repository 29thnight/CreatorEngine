#include "Includes/MaterialGraphRuntimeEffectContract.slang"

Texture2D<float> lx_sourceDepth : register(t0);

[shader("vertex")]
float4 LXRuntimeDepthVS(uint vertex : SV_VertexID) : SV_Position
{
    const float2 coordinate = float2((vertex << 1u) & 2u, vertex & 2u);
    return float4(coordinate * float2(2, -2) + float2(-1, 1), 0, 1);
}

[shader("fragment")]
float LXRuntimeDepthPS(float4 position : SV_Position) : SV_Depth
{
    // D32 부분 복사의 backend 제약을 피하면서 기존 depth의 float32 값을
    // 그대로 전달한다. 타일 viewport는 정수 원점만 달라진다.
    return lx_sourceDepth.Load(int3(uint2(position.xy) + uint2(lx_effectSourceX, lx_effectSourceY), 0));
}
