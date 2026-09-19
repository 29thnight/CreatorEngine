#pragma once
cbuffer SSAOParams : register(b0)
{
    float4x4 gInverseProjection;
    float4x4 gProjection;    // 참조 경로가 표본을 클립으로 투영할 때 쓴다
    uint2    gSize;          // 반해상도 크기
    uint2    gFullSize;      // 원본 깊이 크기
    float    gRadius;
    float    gThickness;
    float    gIntensity;
    float    gDepthSigma;
    uint     gFrameIndex;
    uint3    gPad;
    float4x4 gView;          // GBuffer world normal -> view-space AO
};

static const float kPi = 3.14159265f;

// 깊이(NDC)에서 뷰 공간 위치. w로 나누는 것을 잊으면 원근이 사라져
// 먼 곳의 AO가 통째로 틀리는데, 화면에서는 '멀리가 좀 이상하다'로만 보인다.
float3 ViewFromDepth(float2 uv, float depth)
{
    const float4 clip = float4(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f, depth, 1.0f);
    const float4 view = mul(clip, gInverseProjection);
    return view.xyz / view.w;
}

// Pixel/frame sample rotation. This pass currently uses spatial filtering only.
float Hash21(uint2 pixel, uint frame)
{
    const float2 p = float2(pixel) + float2(frame * 0.7548f, frame * 0.5698f);
    return frac(sin(dot(p, float2(12.9898f, 78.233f))) * 43758.5453f);
}
