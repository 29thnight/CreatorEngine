struct RectInstance
{
    float4 bounds;   // left · top · right · bottom (픽셀)
    float4 uv;       // uvLeft · uvTop · uvRight · uvBottom
    float4 color;
    float4 rotation; // x = radians
};

StructuredBuffer<RectInstance> gRects : register(t0);
Texture2D                      gTexture : register(t1);
SamplerState                   gSampler : register(s0);

cbuffer UIParams : register(b0)
{
    float2 gScreenSize;
    float2 gPad;
};

struct VSOut
{
    float4 position : SV_POSITION;
    float2 uv       : TEXCOORD0;
    float4 color    : COLOR0;
};

VSOut VSMain(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID)
{
    const RectInstance rect = gRects[instanceId];

    // 0,1,2,3 → (0,0) (1,0) (0,1) (1,1). 삼각형 스트립 순서다.
    const float2 corner = float2(float(vertexId & 1u), float((vertexId >> 1u) & 1u));

    const float2 center = (rect.bounds.xy + rect.bounds.zw) * 0.5f;
    float2 local = (corner - 0.5f) * (rect.bounds.zw - rect.bounds.xy);
    const float s = sin(rect.rotation.x);
    const float c = cos(rect.rotation.x);
    local = float2(local.x * c - local.y * s, local.x * s + local.y * c);
    const float2 pixel = center + local;

    // 픽셀 좌표를 클립으로. y는 화면이 아래로 커지고 클립은 위로 커지므로 뒤집는다.
    VSOut output;
    output.position = float4(
        pixel.x / gScreenSize.x * 2.0f - 1.0f,
        1.0f - pixel.y / gScreenSize.y * 2.0f,
        0.0f, 1.0f);
    output.uv = lerp(rect.uv.xy, rect.uv.zw, corner);
    output.color = rect.color;
    return output;
}

float4 PSMain(VSOut input) : SV_TARGET
{
    return gTexture.Sample(gSampler, input.uv) * input.color;
}
