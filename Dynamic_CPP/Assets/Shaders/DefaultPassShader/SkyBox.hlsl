cbuffer SkyBoxConstants : register(b0)
{
    float4x4 gViewProjection;
    float4   gEyePosition;   // w = 스케일
};

TextureCube  gCubeMap : register(t0);
SamplerState gSampler : register(s0);

struct VSOut
{
    float4 position : SV_POSITION;
    float3 texCoord : TEXCOORD0;
};

VSOut VSMain(uint vertexId : SV_VertexID)
{
    // 단위 큐브 36정점. 눈이 항상 중심이라 픽셀마다 면이 하나뿐이어서
    // 감김 방향은 그림에 영향이 없다(컬링도 끈다).
    const float3 kCorners[8] = {
        float3(-1, -1, -1), float3( 1, -1, -1), float3( 1,  1, -1), float3(-1,  1, -1),
        float3(-1, -1,  1), float3( 1, -1,  1), float3( 1,  1,  1), float3(-1,  1,  1),
    };
    const uint kIndices[36] = {
        0, 1, 2,  0, 2, 3,     // -Z
        5, 4, 7,  5, 7, 6,     // +Z
        4, 0, 3,  4, 3, 7,     // -X
        1, 5, 6,  1, 6, 2,     // +X
        3, 2, 6,  3, 6, 7,     // +Y
        4, 5, 1,  4, 1, 0,     // -Y
    };

    const float3 local = kCorners[kIndices[vertexId]];
    const float3 world = local * gEyePosition.w + gEyePosition.xyz;

    VSOut output;
    output.position = mul(float4(world, 1.0f), gViewProjection);
    output.position.z = output.position.w * 0.99999f;
    output.texCoord = local;
    return output;
}

float4 PSMain(VSOut input) : SV_TARGET
{
    return float4(gCubeMap.SampleLevel(gSampler, input.texCoord, 0.0f).rgb, 1.0f);
}
