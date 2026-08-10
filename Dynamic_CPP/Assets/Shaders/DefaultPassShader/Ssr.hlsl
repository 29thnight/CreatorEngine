Texture2D       gDepth      : register(t0);
Texture2D       gColor      : register(t1);
Texture2D       gMetalRough : register(t2);
Texture2D       gNormal     : register(t3);
Texture2D<uint> gBitmask    : register(t4);

SamplerState gLinearSampler : register(s0);
SamplerState gPointSampler  : register(s1);

cbuffer SSRConstants : register(b0)
{
    float4x4 gInverseProjection;
    float4x4 gInverseView;
    float4x4 gViewProjection;
    float4   gCameraPosition;
    float    gStepSize;
    float    gMaxThickness;
    float    gTime;
    int      gMaxRayCount;
    float2   gScreenSize;
    float2   gPadding;
};

struct VSOut
{
    float4 position : SV_POSITION;
    float2 texCoord : TEXCOORD0;
};

VSOut VSMain(uint id : SV_VertexID)
{
    VSOut output;
    output.texCoord = float2((id << 1) & 2, id & 2);
    output.position = float4(output.texCoord * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f),
        0.0f, 1.0f);
    return output;
}

float3 ReconstructWorldPosFromDepth(float2 uv, float depth)
{
    // 클립 공간 [-1,1]. y를 뒤집어 위가 +y가 되게 한다.
    float2 clipXY = uv * 2.0f - 1.0f;
    clipXY.y = -clipXY.y;

    // 깊이는 선형화되지 않았고 DirectX 규약대로 [0,1]이다.
    float4 clipSpace = float4(clipXY, depth, 1.0f);
    float4 viewSpace = mul(clipSpace, gInverseProjection);

    viewSpace /= viewSpace.w;

    float4 worldSpace = mul(viewSpace, gInverseView);
    return worldSpace.xyz;
}

float SsrNoise(float2 seed)
{
    return frac(sin(dot(seed.xy, float2(12.9898f, 78.233f))) * 43758.5453f);
}

float4 Raytrace(float3 reflectionWorld, const int maxCount, float stepSize, float3 pos, float2 uv)
{
    float4 color = float4(0.0f, 0.0f, 0.0f, 0.0f);
    float3 step = stepSize * reflectionWorld;

    [loop]
    for (int i = 1; i <= maxCount; i++)
    {
        float3 ray = (i + SsrNoise(uv + gTime)) * step;
        float3 rayPos = pos + ray;
        float4 vpPos = mul(float4(rayPos, 1.0f), gViewProjection);

        float  rayDepth = vpPos.z / vpPos.w;
        float2 rayUv = vpPos.xy / vpPos.w * 0.5f + 0.5f;
        rayUv.y = 1.0f - rayUv.y;   // y 뒤집기(DirectX 좌표계)

        float gbufferDepth = gDepth.Sample(gLinearSampler, rayUv).r;
        if (rayUv.x < 0.0f || rayUv.x > 1.0f || rayUv.y < 0.0f || rayUv.y > 1.0f)
            continue;

        // 광선이 표면 바로 뒤로 들어갔는가. 두께를 넘으면 그 표면 뒤의
        // 빈 공간을 지나간 것이라 반사로 치지 않는다.
        if (rayDepth - gbufferDepth > 0 && rayDepth - gbufferDepth < gMaxThickness)
        {
            float a = 0.3f * pow(min(1.0f, (stepSize * maxCount / 2) / length(ray)), 2.0f);
            color = color * (1.0f - a) + float4(gColor.Sample(gLinearSampler, rayUv).rgb, 1.0f) * a;
            break;
        }
    }

    return color;
}

float4 PSMain(VSOut input) : SV_TARGET
{
    float4 color = gColor.Sample(gLinearSampler, input.texCoord);

    // ★ 원본 그대로: gScreenSize가 (0,0)이라 모든 픽셀이 텍셀 (0,0)을 짚는다.
    if (gBitmask.Load(int3(int2(input.texCoord * gScreenSize), 0)) & 1 << 9)
    {
        return color;
    }

    float depth = gDepth.Sample(gLinearSampler, input.texCoord).r;

    // ★ 원본 그대로: return이 없다. 아래로 계속 흘러 마지막 줄이 덮어쓴다.
    float4 output = color;
    if (depth >= 1.0f)
        output = color;

    float3 normal = gNormal.Sample(gLinearSampler, input.texCoord).xyz;
    normal = normalize(normal * 2.0f - 1.0f);

    float2 metalRough = gMetalRough.Sample(gLinearSampler, input.texCoord).rg;

    float4 worldSpacePosition = float4(
        ReconstructWorldPosFromDepth(input.texCoord, depth), 1.0f);
    float3 camDir = normalize(worldSpacePosition.xyz - gCameraPosition.xyz);
    float3 refDir = (normalize(reflect(camDir, normal))).rgb;

    // ★ 원본 그대로: reflectFactor와 edgeFade를 구하지만 최종 줄이 쓰지 않는다.
    float reflectFactor = (1.0f - metalRough.y) * (0.04f * (1.0f - metalRough.x) + metalRough.x);

    float4 reflectedColor = Raytrace(refDir, gMaxRayCount, gStepSize,
        worldSpacePosition.rgb, input.texCoord);

    float edgeFade = saturate(1.0f - pow(length(input.texCoord.xy - 0.5f) * 2.0f, 2.0f));
    reflectFactor *= edgeFade;

    output = lerp(color, color + reflectedColor, metalRough.x);
    return output;
}
