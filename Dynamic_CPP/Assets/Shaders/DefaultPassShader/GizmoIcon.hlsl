struct IconInstance
{
    float4 centerSize;   // xyz 중심 · w 크기
};

StructuredBuffer<IconInstance> gIcons : register(t0);
Texture2D                      gTexture : register(t1);
SamplerState                   gSampler : register(s0);

cbuffer GizmoCamera : register(b0)
{
    float4x4 gViewProjection;
    float4   gEyePosition;
};

struct VSOut
{
    float4 position : SV_POSITION;
    float2 uv       : TEXCOORD0;
};

VSOut VSMain(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID)
{
    const IconInstance icon = gIcons[instanceId];
    const float3 center = icon.centerSize.xyz;
    const float  size = icon.centerSize.w;
    const float  halfWidth = size * 0.5f;

    float3 planeNormal = normalize(center - gEyePosition.xyz);
    float3 rightVector = normalize(cross(planeNormal, float3(0.0f, 1.0f, 0.0f))) * halfWidth;
    const float3 upVector = float3(0.0f, size, 0.0f);

    // GS와 같은 스트립 순서: C-r · C+r · C-r+up · C+r+up
    const float sideSign = (vertexId & 1u) != 0u ? 1.0f : -1.0f;
    const float upAmount = (vertexId & 2u) != 0u ? 1.0f : 0.0f;
    const float3 world = center + sideSign * rightVector + upAmount * upVector;

    // GS와 같은 UV: (0,1) (1,1) (0,0) (1,0)
    VSOut output;
    output.position = mul(float4(world, 1.0f), gViewProjection);
    output.uv = float2((vertexId & 1u) != 0u ? 1.0f : 0.0f,
        upAmount > 0.5f ? 0.0f : 1.0f);
    return output;
}

float4 PSMain(VSOut input) : SV_TARGET
{
    float4 color = gTexture.Sample(gSampler, input.uv);
    float alpha = min(color.a, 0.5f);
    return float4(color.rgb, alpha);
}
