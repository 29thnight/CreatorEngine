cbuffer MaterialProperties : register(b2)
{
    float4 tint;
    float roughness;
    float3 _materialPadding;
};

Texture2D<float4> albedoMap : register(t3);

struct VSOut
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
};

VSOut VSMain(uint id : SV_VertexID)
{
    float2 positions[3] = {
        float2(0.0f, 0.6f),
        float2(0.6f, -0.6f),
        float2(-0.6f, -0.6f),
    };

    VSOut output;
    output.position = float4(positions[id], 0.0f, 1.0f);
    output.color = tint;
    return output;
}

float4 PSMain(VSOut input) : SV_TARGET
{
    // Reflection용 fixture다. field와 texture가 linked entry layout에 남도록
    // 둘 다 결과에 관여시키며 실제 draw에는 사용하지 않는다.
    return input.color * roughness + albedoMap.Load(int3(0, 0, 0));
}
