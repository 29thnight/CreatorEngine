Texture2D gDepth       : register(t0);
Texture2D gAlbedo      : register(t1);
Texture2D gNormal      : register(t2);
Texture2D gOrm         : register(t3);

Texture2D gDecalDiffuse : register(t4);
Texture2D gDecalNormal  : register(t5);
Texture2D gDecalOrm     : register(t6);

struct DecalInstance
{
    float4x4 world;
    float4x4 inverseWorld;
    uint     useFlags;
    uint     sliceX;
    uint     sliceY;
    int      sliceNum;
};
StructuredBuffer<DecalInstance> gInstances : register(t7);

SamplerState gLinearSampler : register(s0);
SamplerState gPointSampler  : register(s1);

cbuffer DecalFrame : register(b0)
{
    float4x4 gInverseView;
    float4x4 gInverseProjection;
    float4x4 gViewProjection;
    float2   gScreenDimensions;
    float2   gFramePadding;
};

#define USE_DIFFUSE (1 << 0)
#define USE_NORMAL  (1 << 1)
#define USE_ORM     (1 << 2)

// 원본 DecalPass 생성자의 정점 배열과 인덱스 배열 그대로다.
static const float3 kCubePositions[24] =
{
    float3(-0.5, -0.5, -0.5), float3( 0.5, -0.5, -0.5),
    float3( 0.5,  0.5, -0.5), float3(-0.5,  0.5, -0.5),
    float3( 0.5, -0.5,  0.5), float3(-0.5, -0.5,  0.5),
    float3(-0.5,  0.5,  0.5), float3( 0.5,  0.5,  0.5),
    float3(-0.5, -0.5,  0.5), float3(-0.5, -0.5, -0.5),
    float3(-0.5,  0.5, -0.5), float3(-0.5,  0.5,  0.5),
    float3( 0.5, -0.5, -0.5), float3( 0.5, -0.5,  0.5),
    float3( 0.5,  0.5,  0.5), float3( 0.5,  0.5, -0.5),
    float3(-0.5, -0.5, -0.5), float3(-0.5, -0.5,  0.5),
    float3( 0.5, -0.5,  0.5), float3( 0.5, -0.5, -0.5),
    float3(-0.5,  0.5,  0.5), float3(-0.5,  0.5, -0.5),
    float3( 0.5,  0.5, -0.5), float3( 0.5,  0.5,  0.5)
};

static const uint kCubeIndices[36] =
{
     0,  2,  1,  0,  3,  2,
     4,  6,  5,  4,  7,  6,
     8, 10,  9,  8, 11, 10,
    12, 14, 13, 12, 15, 14,
    16, 18, 17, 16, 19, 18,
    20, 22, 21, 20, 23, 22
};

struct VSOut
{
    float4 position : SV_Position;
    nointerpolation uint instance : TEXCOORD0;
};

VSOut VSMain(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID)
{
    const DecalInstance instance = gInstances[instanceId];
    const float3 localPosition = kCubePositions[kCubeIndices[vertexId]];
    const float4 worldPosition = mul(float4(localPosition, 1.0f), instance.world);

    VSOut output;
    output.position = mul(worldPosition, gViewProjection);
    output.instance = instanceId;
    return output;
}

struct PSOut
{
    float4 diffuse : SV_TARGET0;
    float4 normal  : SV_TARGET1;
    float4 orm     : SV_TARGET2;
};

PSOut PSMain(VSOut input)
{
    const DecalInstance instance = gInstances[input.instance];

    const float2 screenUV = input.position.xy / gScreenDimensions;
    const float  depth = gDepth.Sample(gPointSampler, screenUV).r;

    // 깊이가 1이면 하늘이다 — 얹을 표면이 없다.
    if (depth >= 1.0f)
    {
        discard;
    }

    const float2 clipUV = screenUV * float2(2.0f, -2.0f) - float2(1.0f, -1.0f);
    const float4 clipPosition = float4(clipUV, depth, 1.0f);

    float4 viewSpace = mul(clipPosition, gInverseProjection);
    viewSpace /= viewSpace.w;
    const float4 worldPosition = mul(viewSpace, gInverseView);

    const float3 decalLocal = mul(float4(worldPosition.xyz, 1.0f), instance.inverseWorld).xyz;

    // 상자(단위 큐브) 밖의 표면에는 얹지 않는다.
    if (abs(decalLocal.x) > 0.5f || abs(decalLocal.y) > 0.5f || abs(decalLocal.z) > 0.5f)
    {
        discard;
    }

    const float3 worldNormal = gNormal.Sample(gPointSampler, screenUV).xyz * 2.0f - 1.0f;
    const float4 baseAlbedo = gAlbedo.Sample(gPointSampler, screenUV);
    const float4 baseOrm = gOrm.Sample(gPointSampler, screenUV);

    const float3 up = abs(worldNormal.y) < 0.999f ? float3(0, 1, 0) : float3(1, 0, 0);
    const float3 tangent = normalize(cross(up, worldNormal));
    const float3 bitangent = normalize(cross(worldNormal, tangent));
    const float3x3 tbn = float3x3(tangent, bitangent, worldNormal);

    // 상자 지역 좌표(-0.5~0.5)를 텍스처 UV(0~1)로. y를 뒤집는 것까지 원본 그대로다.
    float2 decalUV = decalLocal.xz + 0.5f;
    decalUV.y = 1.0f - decalUV.y;

    const float2 sliceSize = 1.0f / float2(instance.sliceX, instance.sliceY);
    const float2 sliceIndex = float2(instance.sliceNum % instance.sliceX,
                                     instance.sliceNum / instance.sliceX);
    decalUV = (decalUV + sliceIndex) * sliceSize;

    float4 decalSample = gDecalDiffuse.Sample(gLinearSampler, decalUV);
    decalSample.rgb = pow(decalSample.rgb, 2.2f);

    const float3 normalTexel = gDecalNormal.Sample(gLinearSampler, decalUV).xyz * 2.0f - 1.0f;
    const float3 normalWorld = normalize(mul(normalTexel, tbn));

    const float3 decalOrm = gDecalOrm.Sample(gLinearSampler, decalUV).xyz;
    const float3 orm = float3(decalOrm.b, decalOrm.g, decalOrm.r);

    const float3 finalColor = lerp(baseAlbedo.rgb, decalSample.rgb, decalSample.a);

    PSOut output;
    output.diffuse = (instance.useFlags & USE_DIFFUSE) != 0
        ? float4(finalColor, decalSample.a) : float4(0, 0, 0, 0);
    output.normal = (instance.useFlags & USE_NORMAL) != 0
        ? float4(normalWorld * 0.5f + 0.5f, 0) : float4(0, 1, 0, 0);
    output.orm = (instance.useFlags & USE_ORM) != 0
        ? float4(orm, baseOrm.a) : float4(0, 0, 1, 0);
    return output;
}
