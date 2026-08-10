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

// 픽셀·프레임마다 도는 해시. 이웃끼리 다른 각을 보게 해서 잡음을 만들고,
// 그 잡음을 디노이즈가 걷어낸다 — 잡음을 안 만들려고 표본을 늘리는 것보다
// 이쪽이 싸다. 프레임 번호를 빼면 잡음이 화면에 박혀 지워지지 않는다.
float Hash21(uint2 pixel, uint frame)
{
    const float2 p = float2(pixel) + float2(frame * 0.7548f, frame * 0.5698f);
    return frac(sin(dot(p, float2(12.9898f, 78.233f))) * 43758.5453f);
}
