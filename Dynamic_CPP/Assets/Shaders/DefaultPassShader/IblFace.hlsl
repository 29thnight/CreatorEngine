cbuffer IblDrawConstants : register(b0)
{
    float4 gForward;
    float4 gRight;
    float4 gUp;
    float4 gParams;
};

struct VSOut
{
    float4 position : SV_POSITION;
    float3 texCoord : TEXCOORD0;
};

VSOut VSMain(uint id : SV_VertexID)
{
    const float2 uv = float2((id << 1) & 2, id & 2);

    VSOut output;
    output.position = float4(uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    output.texCoord = gForward.xyz
        + (2.0f * uv.x - 1.0f) * gRight.xyz
        + (1.0f - 2.0f * uv.y) * gUp.xyz;
    return output;
}
