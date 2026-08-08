#ifndef DYNAMICCPP_EXPORTS
#include "EnhancedForwardPass.h"
#include "DX12DeviceResources.h"
#include "DX12PSOManager.h"
#include "DX12RootSignatureCache.h"
#include "EnhancedRenderGraph.h"
#include "RHIEncoder.h"
#include "EnhancedSceneRenderer.h"

// ★ 자기가 쓰는 것은 자기가 포함한다.
//
// 전에는 이 둘이 없어도 빌드가 됐다. 유니티 빌드가 같은 덩어리에 묶은
// 다른 파일이 끌어와 주고 있었기 때문이다. 파일 하나를 프로젝트에 더한
// 것만으로 묶음이 바뀌어 갑자기 'Vertex는 선언되지 않은 식별자'가 났다.
// 남의 include에 얹혀 있으면 이런 식으로 무관한 변경에서 터진다.
#include "DX12MeshCache.h"
#include "../../Mesh.h"

#include <d3dcompiler.h>
#include <algorithm>
#include <sstream>
#include <vector>

#pragma comment(lib, "d3dcompiler.lib")

// 남은 단계(순서대로 채운다):
//   [v] 1. 광원 컬링 컴퓨트 — 타일 프러스텀 vs 광원 구, 타일 목록 쓰기
//   [v] 2. 컬링 자가 검증 — 타일 카운트 리드백, 알려진 배치로 단정
//   [v] 3. 포워드 셰이딩 — draws 드로우 + 타일 목록 조회
//   [v] 4. 참조 경로(전 광원 루프)와 픽셀 대조 — EnhancedForwardShadeTest.cpp
//   [v] 5. 광원 수 스케일링 실측 — EnhancedForwardScaleTest.cpp
//
// 다섯 단계가 다 섰고 실제 씬(dx12.scene)에도 붙었다. 씬에서의 실측:
//   불투명만  — 컬링 0.0031 ms · 셰이딩 생략(포워드 큐 비어 있음)
//   투명 4건  — 컬링 0.0031 ms · 셰이딩 0.184 ms

namespace
{
    // 유니티 빌드에서 익명 네임스페이스가 파일 간 합쳐지므로 이름을 고유하게 둔다.
    std::string FwdHrToString(HRESULT hr)
    {
        std::ostringstream oss;
        oss << "HRESULT 0x" << std::hex << static_cast<unsigned long>(hr);
        return oss.str();
    }

    // ── 광원 컬링 ──
    //
    // 스레드 그룹 하나가 타일 하나다(16x16 = 256스레드). 세 단계:
    //   ① 각 스레드가 자기 픽셀의 깊이를 읽어 그룹 공유 min/max에 모은다.
    //      깊이는 float이지만 InterlockedMin/Max는 uint만 받는다 — 양수
    //      float은 비트 패턴의 대소가 값의 대소와 같으므로 asuint로 겪는다.
    //   ② 타일 프러스텀을 만든다. 옆면 4개는 뷰 공간에서 원점을 지나는
    //      평면이라 타일 코너의 뷰 방향 둘의 외적이 곧 법선이다.
    //   ③ 광원을 256개씩 나눠 병렬로 검사한다. 통과하면 공유 카운터로
    //      슬롯을 받아 목록에 쓴다.
    //
    // 하늘만 있는 타일은 광원을 받지 않는다 — min/max가 비어 있으면 어떤
    // 표면도 없다는 뜻이고, 그 타일의 픽셀 셰이더는 어차피 그릴 것이 없다.
    constexpr const char* kCullShader = R"(
Texture2D<float>             gDepth  : register(t0);
StructuredBuffer<float4>     gLights : register(t1);   // EnhancedLight = float4 x4

RWStructuredBuffer<uint> gTileCount : register(u0);
RWStructuredBuffer<uint> gTileList  : register(u1);

cbuffer CullParams : register(b0)
{
    float4x4 gView;
    float4x4 gInverseProjection;
    uint2    gScreenSize;
    uint2    gTileGrid;
    uint     gLightCount;
    uint3    gPad;
};

groupshared uint sMinDepth;
groupshared uint sMaxDepth;
groupshared uint sCount;

float ViewZFromClip(float depth)
{
    const float4 view = mul(float4(0.0f, 0.0f, depth, 1.0f), gInverseProjection);
    return view.z / view.w;
}

// 타일 코너 uv의 뷰 공간 방향. 옆면 평면의 재료다.
float3 CornerRay(float2 uv)
{
    const float4 clip = float4(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f, 1.0f, 1.0f);
    const float4 view = mul(clip, gInverseProjection);
    return view.xyz / view.w;
}

[numthreads(TILE_SIZE, TILE_SIZE, 1)]
void CSMain(uint3 groupId : SV_GroupID, uint3 threadId : SV_GroupThreadID,
    uint groupIndex : SV_GroupIndex)
{
    if (0 == groupIndex)
    {
        sMinDepth = 0x7F7FFFFFu;   // +FLT_MAX의 비트
        sMaxDepth = 0u;
        sCount = 0u;
    }
    GroupMemoryBarrierWithGroupSync();

    // ① 타일 깊이 범위
    const uint2 pixel = groupId.xy * TILE_SIZE + threadId.xy;
    if (all(pixel < gScreenSize))
    {
        const float depth = gDepth.Load(int3(pixel, 0));
        if (depth < 1.0f)
        {
            const uint bits = asuint(depth);
            InterlockedMin(sMinDepth, bits);
            InterlockedMax(sMaxDepth, bits);
        }
    }
    GroupMemoryBarrierWithGroupSync();

    // ★ 타일 깊이 구간을 앞쪽으로 열어 둔다.
    //
    //   이 목록을 쓰는 유일한 소비자가 '투명 포워드'인데, 투명 기하는
    //   GBuffer에 없다. 그래서 불투명 깊이로 구간을 좁히면 투명이 그 구간
    //   밖에 있을 때 필요한 광원이 잘린다.
    //
    //   예전에는 하늘만 있는 타일(emptyTile)에서 광원을 통째로 버렸는데,
    //   그러면 그 자리의 투명면이 새까맣게 나온다 — 지평선 위는 검고
    //   아래는 밝은, 물체 음영과 무관한 경계가 생겼다(실측으로 잡았다).
    //
    //   앞쪽은 카메라(0)로 연다: 투명은 불투명보다 앞에 있을 수 있다.
    //   뒤쪽만 불투명 최대 깊이로 자른다 — 그보다 뒤의 광원은 투명에도
    //   불투명에도 닿지 않으므로 컬링의 이득은 그대로 남는다. 하늘만 있는
    //   타일은 자를 불투명이 없으니 뒤쪽도 연다.
    //
    //   대가: 근거리 컬링이 모든 타일에서 사라지고, 하늘 타일은 옆면
    //   평면으로만 걸러진다. 광원이 지평선에 몰린 야외 씬에서는 타일당
    //   목록이 kMaxLightsPerTile(32)에 닿아 조용히 잘릴 수 있다 —
    //   그 수는 GetLastOverflowTileCount가 알려 준다(늘 0이 아닌지 볼 것).
    const bool emptyTile = (0u == sMaxDepth);

    const float minViewZ = 0.0f;
    const float maxViewZ = emptyTile
        ? 3.402823466e+38f : ViewZFromClip(asfloat(sMaxDepth));

    // ② 타일 옆면 4개. 화면 경계로 자른다 — 가장자리 타일이 화면 밖까지
    //    걸치면 이웃에 없는 광원까지 줍는다.
    const float2 tileMin = float2(groupId.xy * TILE_SIZE) / float2(gScreenSize);
    const float2 tileMax = min(float2((groupId.xy + 1) * TILE_SIZE), float2(gScreenSize))
        / float2(gScreenSize);

    const float3 rayTL = CornerRay(float2(tileMin.x, tileMin.y));
    const float3 rayTR = CornerRay(float2(tileMax.x, tileMin.y));
    const float3 rayBL = CornerRay(float2(tileMin.x, tileMax.y));
    const float3 rayBR = CornerRay(float2(tileMax.x, tileMax.y));

    // 안쪽을 향하는 법선. dot(n, p) >= -r 이면 광원 구가 평면 안쪽에 걸친다.
    //
    // ★ 외적 순서를 한 번 틀렸었다. cross(BL, TL)로 만든 왼쪽 법선을 손으로
    //   계산하면 (-X, -Z) — 바깥쪽이고, 그러면 중앙 광원조차 dot이 음수가
    //   되어 모든 타일이 0이 된다(실제로 그랬다: 켜진 타일 0/64).
    //   LH 뷰 공간에서 안쪽 법선이 나오는 순서로 고정한다.
    const float3 planeLeft   = normalize(cross(rayTL, rayBL));
    const float3 planeRight  = normalize(cross(rayBR, rayTR));
    const float3 planeTop    = normalize(cross(rayTR, rayTL));
    const float3 planeBottom = normalize(cross(rayBL, rayBR));

    // ③ 광원 병렬 검사
    const uint tileIndex = groupId.y * gTileGrid.x + groupId.x;

    for (uint lightIndex = groupIndex; lightIndex < gLightCount;
        lightIndex += TILE_SIZE * TILE_SIZE)
    {
        const float4 position = gLights[lightIndex * 4 + 0];
        const float4 attenuation = gLights[lightIndex * 4 + 3];

        bool inside;
        if (position.w < 0.5f)
        {
            // 방향광은 모든 타일에 닿는다. 하늘만 있는 타일도 마찬가지다 —
            // 그 자리에 투명 기하가 있을 수 있다.
            inside = true;
        }
        else
        {
            const float3 viewPos = mul(float4(position.xyz, 1.0f), gView).xyz;
            const float radius = attenuation.w;

            inside = (viewPos.z + radius >= minViewZ)
                && (viewPos.z - radius <= maxViewZ)
                && (dot(planeLeft, viewPos) >= -radius)
                && (dot(planeRight, viewPos) >= -radius)
                && (dot(planeTop, viewPos) >= -radius)
                && (dot(planeBottom, viewPos) >= -radius);
        }

        if (inside)
        {
            uint slot;
            InterlockedAdd(sCount, 1u, slot);
            if (slot < MAX_LIGHTS_PER_TILE)
            {
                gTileList[tileIndex * MAX_LIGHTS_PER_TILE + slot] = lightIndex;
            }
        }
    }
    GroupMemoryBarrierWithGroupSync();

    if (0 == groupIndex)
    {
        // 앞쪽 절반은 셰이딩이 읽는 값이라 상한으로 자른다.
        gTileCount[tileIndex] = min(sCount, MAX_LIGHTS_PER_TILE);

        // ★ 뒤쪽 절반에 자르기 전의 수를 그대로 남긴다.
        //
        // 처음에는 '카운트가 상한과 같으면 잘린 것'으로 읽으면 된다고 적었는데,
        // 그것으로는 정확히 32개인 타일과 200개가 잘린 타일이 구분되지 않는다.
        // 성능 실측에서 이 구분이 결론을 뒤집는다 — 광원을 버리고 빨라진 것을
        // '빨라졌다'로 읽으면 안 되기 때문이다. 타일 하나당 4바이트면
        // 1080p에서 32KB고, 그 값으로 '조용히 사라짐'이 수가 된다.
        gTileCount[gTileGrid.x * gTileGrid.y + tileIndex] = sCount;
    }
}
)";

    // ── 포워드 셰이딩 ──
    //
    // Forward+의 핵심이 여기 한 줄이다:
    //
    //   for (i = 0; i < gTileCount[tile]; ++i)   ← 전체 광원이 아니라 타일 목록
    //
    // 기존 포워드는 픽셀마다 gLightCount를 돌았다. 여기서는 자기 타일에
    // 닿는 광원만 돈다. 씬 광원이 수백 개여도 타일당 몇 개면 그만큼만 돈다.
    //
    // 참조 경로(REFERENCE_PATH)는 같은 셰이더에서 매크로로 갈린다. 전 광원을
    // 도는 옛 방식이고, 픽셀 대조의 정답지 역할을 한다 — 컬링이 광원을
    // 잘못 떨어뜨리면 두 결과가 갈린다. 셰이더를 따로 쓰면 조명 계산 자체가
    // 달라져 무엇 때문에 다른지 알 수 없으므로, 광원을 고르는 부분만 다르게
    // 하고 나머지는 같은 코드를 쓴다.
    constexpr const char* kShadeShader = R"(
struct VSIn
{
    float3 position  : POSITION;
    float3 normal    : NORMAL;
    float2 uv        : TEXCOORD;
    float3 tangent   : TANGENT;
    float3 bitangent : BINORMAL;
};

struct VSOut
{
    float4 position  : SV_POSITION;
    float3 worldPos  : TEXCOORD0;
    float3 normal    : TEXCOORD1;
    float2 uv        : TEXCOORD2;
    float3 tangent   : TEXCOORD5;
    float3 bitangent : TEXCOORD6;
    // 인스턴스마다 상수라 보간하지 않는다. 보간을 켜 두면 값은 같아도
    // 래스터라이저가 헛일을 한다.
    nointerpolation float4 baseColor : TEXCOORD3;
    // x 금속성 · y 거칠기 · z 노멀맵 · w 재질 텍스처가 묶였는가
    nointerpolation float4 material  : TEXCOORD4;
};

struct ShadeInstance
{
    float4x4 world;
    float4   baseColor;
    float    metallic;
    float    roughness;
    uint     useNormalMap;
    uint     hasMaterialTextures;
};

StructuredBuffer<ShadeInstance> gInstances : register(t0);
StructuredBuffer<float4>        gLights    : register(t1);
StructuredBuffer<uint>          gTileCount : register(t2);
StructuredBuffer<uint>          gTileList  : register(t3);

// 재질 텍스처 넷. 슬롯 의미와 채널 규약은 GBuffer와 같아야 한다 —
// 다르면 같은 재질이 불투명과 투명에서 갈린다.
Texture2D gBaseColorMap    : register(t4);
Texture2D gNormalMap       : register(t5);
Texture2D gOccRoughMetal   : register(t6);
Texture2D gEmissiveMap     : register(t7);

// IBL 셋(Deferred와 같은 split-sum). 없으면 앰비언트가 0이다.
TextureCube gIrradiance  : register(t8);
TextureCube gPrefiltered : register(t9);
Texture2D   gBrdfLut     : register(t10);

// 캐스케이드 그림자 맵. Deferred가 t5로 읽는 것과 같은 자원이다.
Texture2DArray gShadowMap : register(t11);

// 생성기(EnhancedIBLGenerator::kPrefilterMips)와 같아야 한다.
#define IBL_PREFILTER_MIPS 6

// EnhancedRenderPass.h의 kShadowCascadeCount와 같아야 한다.
#define SHADOW_CASCADE_COUNT 3

SamplerState gSampler    : register(s0);
// IBL용 선형 클램프. 거칠기가 밉 좌표라 포인트로 읽으면 단차가 띠로 보인다.
SamplerState gIblSampler : register(s1);
// 그림자 비교 샘플러. 하드웨어가 이웃 텍셀 넷을 비교·평균해 준다(2x2 PCF).
SamplerComparisonState gShadowSampler : register(s2);

cbuffer ShadeParams : register(b0)
{
    float4x4 gViewProjection;
    uint2    gTileGrid;
    uint     gLightCount;
    uint     gPad0;
    float4   gEyePosition;   // xyz — 시선 방향의 기준
    uint     gHasIbl;
    uint3    gPad1;

    // ── 그림자 (Deferred의 LightingConstants와 같은 뜻) ──
    float4x4 gLightViewProjection[SHADOW_CASCADE_COUNT];
    float4   gCameraForward;    // 뷰 깊이를 재는 축
    float4   gCascadeSplits;    // xyz = 각 캐스케이드가 끝나는 뷰 깊이
    float4   gShadowBias;       // xyz = 캐스케이드별 편향 · w = 경사 계수
    uint     gHasShadow;
    float    gCascadeBlendBand; // 경계 블렌딩 폭 · 0이면 하드 스위치
    uint2    gPad2;
};

// ── 캐스케이드 그림자 (EnhancedDeferredPass와 같은 식) ──
//
// 두 함수 모두 Deferred에서 그대로 가져왔다. GGX 삼총사와 같은 사정이라
// 같은 주의가 붙는다: 값이 갈리면 같은 자리에서 불투명은 그늘인데 투명만
// 밝은, 물체와 무관한 경계가 생긴다 — 고칠 때는 두 곳을 함께 고칠 것.
float SampleShadowCascade(uint cascade, float3 worldPosition, float slopeTan)
{
    const float4 lightSpace = mul(float4(worldPosition, 1.0f), gLightViewProjection[cascade]);
    const float3 projected = lightSpace.xyz / lightSpace.w;

    // 라이트 상자 밖은 그림자 정보가 없다. 가려진 것으로 치면 맵 경계에
    // 검은 띠가 생기므로 빛을 받는 것으로 둔다.
    if (any(abs(projected.xy) > 1.0f) || projected.z > 1.0f) return 1.0f;

    const float2 uv = float2(projected.x * 0.5f + 0.5f, 0.5f - projected.y * 0.5f);
    const float bias = gShadowBias[cascade] * (1.0f + gShadowBias.w * slopeTan);

    return gShadowMap.SampleCmpLevelZero(gShadowSampler,
        float3(uv, (float)cascade), projected.z - bias);
}

// 방향광 캐스케이드 그림자. 반환값 1은 빛을 받음, 0은 가려짐.
float SampleShadow(float3 worldPosition, float3 normal, float3 toLight)
{
    if (gHasShadow == 0) return 1.0f;

    const float viewDepth = dot(worldPosition - gEyePosition.xyz, gCameraForward.xyz);

    uint cascade = 0;
    if (viewDepth > gCascadeSplits.x) cascade = 1;
    if (viewDepth > gCascadeSplits.y) cascade = 2;

    const float ndotl = saturate(dot(normal, toLight));
    const float slopeTan = min(sqrt(max(1.0f - ndotl * ndotl, 0.0f)) / max(ndotl, 1e-3f), 8.0f);

    float shadow = SampleShadowCascade(cascade, worldPosition, slopeTan);

    if (gCascadeBlendBand > 0.0f && cascade < SHADOW_CASCADE_COUNT - 1)
    {
        const float split = (cascade == 0) ? gCascadeSplits.x : gCascadeSplits.y;
        const float bandStart = split * (1.0f - gCascadeBlendBand);
        if (viewDepth > bandStart)
        {
            const float blend = saturate((viewDepth - bandStart) / max(split - bandStart, 1e-4f));
            shadow = lerp(shadow,
                SampleShadowCascade(cascade + 1, worldPosition, slopeTan), blend);
        }
    }

    return shadow;
}

VSOut VSMain(VSIn input, uint instanceId : SV_InstanceID)
{
    const ShadeInstance instance = gInstances[instanceId];

    const float4 worldPosition = mul(float4(input.position, 1.0f), instance.world);

    VSOut output;
    output.position  = mul(worldPosition, gViewProjection);
    output.worldPos  = worldPosition.xyz;
    output.uv        = input.uv;
    output.baseColor = instance.baseColor;
    output.material  = float4(instance.metallic, instance.roughness,
        (float)instance.useNormalMap, (float)instance.hasMaterialTextures);

    // 법선·탄젠트는 회전만 적용한다(GBuffer와 같은 처리). 비균등 스케일에는
    // 역전치가 필요한데, 그건 GBuffer가 아직 안 하므로 여기서만 하면 두
    // 경로가 갈린다.
    output.normal    = mul(input.normal,    (float3x3)instance.world);
    output.tangent   = mul(input.tangent,   (float3x3)instance.world);
    output.bitangent = mul(input.bitangent, (float3x3)instance.world);
    return output;
}

// ── GGX 정반사 (EnhancedDeferredPass와 같은 식) ──
//
// 세 함수 모두 Deferred에서 그대로 가져왔다. 손으로 옮긴 것이라 값이
// 갈리면 같은 재질이 불투명과 투명에서 다르게 보인다 — 고칠 때는 두 곳을
// 함께 고칠 것.
float DistributionGGX(float ndoth, float alpha)
{
    const float a2 = alpha * alpha;
    const float d = ndoth * ndoth * (a2 - 1.0f) + 1.0f;
    return a2 / max(3.14159265f * d * d, 1e-6f);
}

float VisibilitySmith(float ndotv, float ndotl, float alpha)
{
    // 높이 상관 Smith. 분모의 4*NdotV*NdotL까지 흡수한 형태라 호출부에서
    // 다시 나누지 않는다.
    const float a2 = alpha * alpha;
    const float v = ndotl * sqrt(ndotv * ndotv * (1.0f - a2) + a2);
    const float l = ndotv * sqrt(ndotl * ndotl * (1.0f - a2) + a2);
    return 0.5f / max(v + l, 1e-6f);
}

float3 FresnelSchlick(float3 f0, float vdoth)
{
    return f0 + (1.0f - f0) * pow(saturate(1.0f - vdoth), 5.0f);
}

// 광원 하나의 기여. 컬링 경로와 참조 경로가 같은 코드를 쓴다 —
// 다른 것은 '어떤 광원을 도는가'뿐이어야 대조가 뜻을 갖는다.
// 셰이딩에 필요한 표면 값. 광원마다 같으므로 루프 밖에서 한 번 만든다.
struct Surface
{
    float3 worldPos;
    float3 normal;
    float3 viewDirection;
    float3 diffuseAlbedo;   // albedo / PI
    float3 f0;
    float  metallic;
    float  alpha;           // roughness^2
    float  ndotv;
};

float3 Contribute(uint lightIndex, Surface surface)
{
    const float4 position    = gLights[lightIndex * 4 + 0];
    const float4 direction   = gLights[lightIndex * 4 + 1];
    const float4 color       = gLights[lightIndex * 4 + 2];
    const float4 attenuation = gLights[lightIndex * 4 + 3];

    float3 toLight;
    float  falloff = 1.0f;

    if (position.w < 0.5f)
    {
        // 방향광 — Deferred와 같은 규약으로 direction 슬롯을 읽는다.
        //
        // ★ 예전에는 position을 방향으로 봤다("direction 슬롯은 여기서 안
        //   쓴다"고 적혀 있었다). 그런데 밀봉이 position에는 광원의 위치를,
        //   direction에는 방향을 따로 싣는다 — 해가 씬 위에 놓여 있으면
        //   -position이 아래를 가리켜 온 세상이 밑에서만 조명받는다.
        //   투명을 배선하고 나서야 눈에 보였다(그 전에는 소비자가 없었다).
        toLight = -normalize(direction.xyz);

        // 그림자는 방향광에만 붙는다 — Deferred가 같다(점광·스포트는
        // 그림자 맵 자체가 없다). gHasShadow가 0이면 1이 나오므로 그림자
        // 없는 경로(자가 검증)는 이 줄이 있어도 값이 그대로다.
        falloff = SampleShadow(surface.worldPos, surface.normal, toLight);
    }
    else
    {
        const float3 delta = position.xyz - surface.worldPos;
        const float  distance = length(delta);
        toLight = delta / max(distance, 1e-6f);

        // 반경 밖은 0. 컬링이 반경으로 자르므로 셰이딩도 같은 기준이라야
        // 경계에서 두 경로가 갈리지 않는다.
        const float radius = attenuation.w;
        if (distance > radius) return float3(0, 0, 0);

        const float denominator = attenuation.x
            + attenuation.y * distance
            + attenuation.z * distance * distance;
        falloff = 1.0f / max(denominator, 1e-6f);

        // ★ 반경에서 0으로 잦아드는 테이퍼. Deferred가 쓰는 것과 같은 식이다
        //   (EnhancedDeferredPass의 광원 루프). 이것이 없으면 반경 경계에서
        //   딱 끊겨, 같은 광원 아래 불투명면은 부드럽게 어두워지는데 투명면만
        //   선이 생긴다 — 투명을 합성하기 전에는 안 보이던 어긋남이다.
        //   반경에서 정확히 0이라 위의 하드 컷과도 어긋나지 않는다.
        falloff *= saturate(1.0f - distance / max(radius, 1e-4f));

        if (position.w > 1.5f)
        {
            // 스포트 — 원뿔 밖을 부드럽게 끈다(Deferred와 같은 식).
            // 이것이 없으면 투명면에서 스포트가 점광원처럼 사방을 비춘다.
            const float cosAngle = dot(-toLight, normalize(direction.xyz));
            const float cutoff = cos(direction.w * 0.5f);
            falloff *= smoothstep(cutoff, lerp(cutoff, 1.0f, 0.2f), cosAngle);
        }
    }

    // ── BRDF (Deferred의 광원 루프와 같은 식) ──
    const float ndotl = saturate(dot(surface.normal, toLight));
    if (ndotl <= 0.0f || falloff <= 0.0f) return float3(0, 0, 0);

    const float3 halfVector = normalize(toLight + surface.viewDirection);
    const float  ndoth = saturate(dot(surface.normal, halfVector));
    const float  vdoth = saturate(dot(surface.viewDirection, halfVector));

    const float3 fresnel = FresnelSchlick(surface.f0, vdoth);
    const float3 specular = fresnel
        * DistributionGGX(ndoth, surface.alpha)
        * VisibilitySmith(surface.ndotv, ndotl, surface.alpha);

    // 에너지 보존: 반사되지 않은 몫만 확산으로 간다. 금속은 확산이 없다.
    const float3 kd = (1.0f - fresnel) * (1.0f - surface.metallic);

    const float3 radiance = color.rgb * color.a * ndotl * falloff;
    return (kd * surface.diffuseAlbedo + specular) * radiance;
}

float4 PSMain(VSOut input) : SV_TARGET
{
    // ── 재질 ──
    //
    // 채널 규약은 GBuffer와 같다: ORM은 glTF대로 G가 거칠기, B가 금속성이고,
    // 거칠기는 계수를 곱하고 금속성은 더한다(GBuffer의 metalRough 조립과
    // 같은 식). occlusion(R)은 Deferred가 읽지 않으므로 여기서도 안 쓴다 —
    // 한쪽만 적용하면 같은 재질이 불투명과 투명에서 갈린다.
    //
    // ★ 텍스처는 묶였을 때만 읽는다. 없는 슬롯에 폴백을 물리는 방법도
    //   있지만(GBuffer가 그렇게 한다), 이 패스는 텍스처 캐시가 없는 자가
    //   검증에서도 돌아야 한다 — 그때는 만들 폴백조차 없다.
    const bool hasTextures = (0.0f != input.material.w);

    float4 albedo = input.baseColor;
    float3 emissive = 0.0f;
    float  rough = input.material.y;
    float  metallic = input.material.x;

    if (hasTextures)
    {
        albedo *= gBaseColorMap.Sample(gSampler, input.uv);
        emissive = gEmissiveMap.Sample(gSampler, input.uv).rgb;

        const float3 orm = gOccRoughMetal.Sample(gSampler, input.uv).rgb;
        rough = orm.g * input.material.y;
        metallic = orm.b + input.material.x;
    }
    rough = saturate(rough);
    metallic = saturate(metallic);

    // ── 법선 ──
    float3 normal = normalize(input.normal);
    if (hasTextures && 0.0f != input.material.z)
    {
        // 접선 공간 변환. GBuffer와 같은 그람-슈미트 재직교화 + 저장된
        // 종법선에서 핸디드니스만 가져오는 처리다 — cross로 다시 만들면
        // UV가 뒤집힌 메시에서 조명이 반대로 나온다.
        const float3 n = normal;
        const float3 t = normalize(input.tangent - n * dot(n, input.tangent));
        const float  handedness = (dot(cross(n, t), input.bitangent) < 0.0f) ? -1.0f : 1.0f;
        const float3 b = cross(n, t) * handedness;

        const float3 sampled = gNormalMap.Sample(gSampler, input.uv).rgb * 2.0f - 1.0f;
        normal = normalize(sampled.x * t + sampled.y * b + sampled.z * n);
    }

    // ── 표면 항 (Deferred와 같은 구성) ──
    Surface surface;
    surface.worldPos = input.worldPos;
    surface.normal = normal;
    surface.viewDirection = normalize(gEyePosition.xyz - input.worldPos);
    surface.ndotv = saturate(dot(normal, surface.viewDirection)) + 1e-5f;
    // 유전체의 기본 반사율 0.04는 관행값이고, 금속은 알베도가 곧 반사색이다.
    surface.f0 = lerp(0.04f, albedo.rgb, metallic);
    surface.diffuseAlbedo = albedo.rgb / 3.14159265f;
    surface.metallic = metallic;
    // 거칠기를 그대로 쓰면 0에서 정반사가 점 하나가 되어 사라진다. 제곱해
    // 쓰는 것이 관행이고 하한을 두어 완전 거울을 피한다.
    surface.alpha = max(rough * rough, 1e-3f);

    float3 lit = 0.0f;

#ifdef REFERENCE_PATH
    // 참조: 전 광원을 돈다(옛 포워드 방식).
    for (uint i = 0; i < gLightCount; ++i)
    {
        lit += Contribute(i, surface);
    }
#else
    // Forward+: 자기 타일의 목록만 돈다.
    const uint2 tile = uint2(input.position.xy) / TILE_SIZE;
    const uint  tileIndex = min(tile.y, gTileGrid.y - 1) * gTileGrid.x
        + min(tile.x, gTileGrid.x - 1);

    const uint count = gTileCount[tileIndex];
    for (uint i = 0; i < count; ++i)
    {
        const uint lightIndex = gTileList[tileIndex * MAX_LIGHTS_PER_TILE + i];
        lit += Contribute(lightIndex, surface);
    }
#endif

    // ── IBL 앰비언트 (split-sum, Deferred와 같은 식) ──
    //
    // 이것이 없으면 투명이 앰비언트 구간에서 불투명보다 어둡게 나온다.
    float3 ambient = 0.0f;
    if (0 != gHasIbl)
    {
        const float3 irradiance = gIrradiance.SampleLevel(gIblSampler, normal, 0.0f).rgb;

        const float3 reflection = reflect(-surface.viewDirection, normal);
        const float3 prefiltered = gPrefiltered.SampleLevel(gIblSampler,
            reflection, rough * (IBL_PREFILTER_MIPS - 1)).rgb;
        const float2 environmentBrdf = gBrdfLut.SampleLevel(gIblSampler,
            float2(surface.ndotv, rough), 0.0f).rg;

        const float3 fresnelAmbient = FresnelSchlick(surface.f0, surface.ndotv);
        const float3 kdAmbient = (1.0f - fresnelAmbient) * (1.0f - metallic);

        ambient = kdAmbient * irradiance * albedo.rgb
            + prefiltered * (surface.f0 * environmentBrdf.x + environmentBrdf.y);
    }

    // 알파는 베이스 컬러에서 온다 — 이것이 블렌드를 성립시킨다.
    return float4(lit + ambient + emissive, albedo.a);
}
)";

    // HLSL의 ShadeInstance와 크기·배치가 같아야 한다(구조화 버퍼 보폭).
    struct ShadeInstance
    {
        Mathf::Matrix  world{};
        Mathf::Vector4 baseColor{};
        float          metallic{ 0.f };
        float          roughness{ 1.f };
        uint32_t       useNormalMap{ 0 };
        uint32_t       hasMaterialTextures{ 0 };
    };
    // ★ 보폭이 어긋나면 컴파일도 검증도 통과하고 GPU만 엉뚱한 필드를 읽는다.
    //   마지막 넷이 정확히 16바이트를 채우는 것이 핵심 — 그래야 구조화
    //   버퍼의 타이트 패킹과 16바이트 논스트래들 규칙이 같은 답을 낸다.
    //   필드를 더할 때 이 수를 함께 고칠 것.
    static_assert(sizeof(ShadeInstance) == 96,
        "HLSL ShadeInstance와 구조화 버퍼 보폭이 어긋났다");

    struct ShadeParams
    {
        Mathf::Matrix  viewProjection{};
        uint32_t       tileGridX{ 0 };
        uint32_t       tileGridY{ 0 };
        uint32_t       lightCount{ 0 };
        uint32_t       pad0{ 0 };
        Mathf::Vector4 eyePosition{};
        uint32_t       hasIbl{ 0 };
        uint32_t       pad1[3]{};

        // 그림자. Deferred의 LightingConstants와 같은 값을 같은 뜻으로 싣는다.
        Mathf::Matrix  lightViewProjection[kShadowCascadeCount]{};
        Mathf::Vector4 cameraForward{};
        Mathf::Vector4 cascadeSplits{};
        Mathf::Vector4 shadowBias{};
        uint32_t       hasShadow{ 0 };
        float          cascadeBlendBand{ 0.f };
        uint32_t       pad2[2]{};
    };
    // ShadeInstance와 같은 이유의 가드. cbuffer는 16바이트 경계·논스트래들
    // 규칙이라 뒤에 필드를 더할 때 이 수도 함께 고쳐야 한다.
    static_assert(sizeof(ShadeParams) == 368,
        "HLSL ShadeParams와 cbuffer 보폭이 어긋났다");

    struct CullParams
    {
        Mathf::Matrix view{};
        Mathf::Matrix inverseProjection{};
        uint32_t      screenWidth{ 0 };
        uint32_t      screenHeight{ 0 };
        uint32_t      tileGridX{ 0 };
        uint32_t      tileGridY{ 0 };
        uint32_t      lightCount{ 0 };
        uint32_t      pad[3]{};
    };

    bool CompileFwdShaderEntry(const char* source, const D3D_SHADER_MACRO* defines,
        const char* entry, const char* target,
        Microsoft::WRL::ComPtr<ID3DBlob>& outBlob, std::string& outError)
    {
        Microsoft::WRL::ComPtr<ID3DBlob> errors;
        const HRESULT hr = D3DCompile(source, strlen(source), nullptr, defines, nullptr,
            entry, target, 0, 0, &outBlob, &errors);
        if (FAILED(hr))
        {
            outError = std::string("Forward+ 셰이더 컴파일 실패(") + entry + "): ";
            if (errors) outError += static_cast<const char*>(errors->GetBufferPointer());
            else        outError += FwdHrToString(hr);
            return false;
        }
        return true;
    }

    bool CompileFwdShader(const char* source, const D3D_SHADER_MACRO* defines,
        Microsoft::WRL::ComPtr<ID3DBlob>& outBlob, std::string& outError)
    {
        Microsoft::WRL::ComPtr<ID3DBlob> errors;
        const HRESULT hr = D3DCompile(source, strlen(source), nullptr, defines, nullptr,
            "CSMain", "cs_5_0", 0, 0, &outBlob, &errors);
        if (FAILED(hr))
        {
            outError = "Forward+ 셰이더 컴파일 실패: ";
            if (errors) outError += static_cast<const char*>(errors->GetBufferPointer());
            else        outError += FwdHrToString(hr);
            return false;
        }
        return true;
    }
}

bool EnhancedForwardPass::Initialize(const EnhancedFrameContext& context, std::string& outError)
{
    if (nullptr == context.resources || nullptr == context.psoManager ||
        nullptr == context.rootSignatures)
    {
        outError = "Forward+ 패스 컨텍스트가 불완전하다";
        return false;
    }

    return CreatePipelines(context, outError);
}

bool EnhancedForwardPass::CreatePipelines(const EnhancedFrameContext& context, std::string& outError)
{
    // 컬링 루트 시그니처: b0 상수 · t0 깊이(테이블) · t1 광원(루트 SRV)
    // · u0/u1 타일 버퍼(테이블).
    D3D12_DESCRIPTOR_RANGE srvRange{};
    srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors = 1;
    srvRange.BaseShaderRegister = 0;

    D3D12_DESCRIPTOR_RANGE uavRange{};
    uavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    uavRange.NumDescriptors = 2;
    uavRange.BaseShaderRegister = 0;

    D3D12_ROOT_PARAMETER params[4]{};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[0].Descriptor.ShaderRegister = 0;

    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].DescriptorTable.NumDescriptorRanges = 1;
    params[1].DescriptorTable.pDescriptorRanges = &srvRange;

    // 광원 배열은 루트 SRV — 업로드 링 조각의 GPU 주소를 그대로 꽂는다.
    params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    params[2].Descriptor.ShaderRegister = 1;

    params[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[3].DescriptorTable.NumDescriptorRanges = 1;
    params[3].DescriptorTable.pDescriptorRanges = &uavRange;

    D3D12_ROOT_SIGNATURE_DESC rootDesc{};
    rootDesc.NumParameters = _countof(params);
    rootDesc.pParameters = params;

    const auto root = context.rootSignatures->GetOrCreate(rootDesc, outError);
    if (!root.IsValid()) return false;
    m_cullRootSignature = root.signature;

    const std::string tileSize = std::to_string(kTileSize);
    const std::string maxLights = std::to_string(kMaxLightsPerTile);
    const D3D_SHADER_MACRO defines[] = {
        { "TILE_SIZE", tileSize.c_str() },
        { "MAX_LIGHTS_PER_TILE", maxLights.c_str() },
        { nullptr, nullptr }
    };

    ComPtr<ID3DBlob> blob;
    if (!CompileFwdShader(kCullShader, defines, blob, outError)) return false;

    DX12ComputePipelineDesc desc{};
    desc.csBytecode = blob->GetBufferPointer();
    desc.csSize = blob->GetBufferSize();
    desc.rootSignature = root.signature;
    desc.rootSignatureId = root.id;

    m_cullPSO = context.psoManager->GetOrCreateCompute(desc, outError);
    if (nullptr == m_cullPSO) return false;

    // ── 셰이딩 루트 시그니처 ──
    //
    // b0 상수 · t0 인스턴스 · t1 광원 · t2 타일 카운트 · t3 타일 목록.
    // 넷 다 구조화 버퍼라 루트 SRV로 꽂는다 — 디스크립터 테이블을 만들 이유가
    // 없고, 드로우마다 인스턴스 버퍼 주소만 바뀌므로 이쪽이 싸다.
    //
    // t4(재질 텍스처)와 s0(샘플러)만 테이블이다. 텍스처는 루트 SRV로 못
    // 꽂는다 — 루트 SRV는 버퍼 전용이고 Texture2D는 디스크립터를 요구한다.
    D3D12_DESCRIPTOR_RANGE materialRange{};
    materialRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    materialRange.NumDescriptors = 4;      // baseColor · normal · ORM · emissive
    materialRange.BaseShaderRegister = 4;  // t4~t7

    // 프레임 내내 같은 텍스처들 — IBL 셋과 그림자 맵. 드로우마다 바뀌는
    // 재질과 달리 한 번만 걸면 되므로 한 테이블에 묶는다. 차원이 섞여 있지만
    // (TextureCube · Texture2D · Texture2DArray) SRV 범위는 그것을 가리지
    // 않는다 — Deferred도 아홉을 한 범위로 묶는다.
    D3D12_DESCRIPTOR_RANGE frameTextureRange{};
    frameTextureRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    frameTextureRange.NumDescriptors = 4;  // 조도 · 프리필터 · BRDF LUT · 그림자
    frameTextureRange.BaseShaderRegister = 8;   // t8~t11

    D3D12_DESCRIPTOR_RANGE samplerRange{};
    samplerRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
    samplerRange.NumDescriptors = 3;       // 재질 · IBL · 그림자 비교

    D3D12_ROOT_PARAMETER shadeParams[8]{};
    shadeParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    shadeParams[0].Descriptor.ShaderRegister = 0;

    for (uint32_t i = 0; i < 4; ++i)
    {
        shadeParams[1 + i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        shadeParams[1 + i].Descriptor.ShaderRegister = i;
    }

    shadeParams[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    shadeParams[5].DescriptorTable.NumDescriptorRanges = 1;
    shadeParams[5].DescriptorTable.pDescriptorRanges = &materialRange;
    shadeParams[5].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    shadeParams[6].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    shadeParams[6].DescriptorTable.NumDescriptorRanges = 1;
    shadeParams[6].DescriptorTable.pDescriptorRanges = &frameTextureRange;
    shadeParams[6].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    shadeParams[7].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    shadeParams[7].DescriptorTable.NumDescriptorRanges = 1;
    shadeParams[7].DescriptorTable.pDescriptorRanges = &samplerRange;
    shadeParams[7].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC shadeRootDesc{};
    shadeRootDesc.NumParameters = _countof(shadeParams);
    shadeRootDesc.pParameters = shadeParams;
    shadeRootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    const auto shadeRoot = context.rootSignatures->GetOrCreate(shadeRootDesc, outError);
    if (!shadeRoot.IsValid()) return false;
    m_shadeRootSignature = shadeRoot.signature;

    // 샘플러 둘을 연속으로 만든다 — 테이블은 연속이어야 하므로 따로 만들어
    // 인접을 기대하면 안 된다(Deferred와 같은 이유).
    {
        D3D12_SAMPLER_DESC samplers[3]{};

        // 재질용. GBuffer와 같은 설정(선형 · WRAP)이라 같은 UV가 같은 텍셀을
        // 집는다 — 다르면 불투명과 투명이 같은 재질에서 갈린다.
        samplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        samplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        samplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        samplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        samplers[0].MaxLOD = D3D12_FLOAT32_MAX;

        // IBL용 선형 클램프. 거칠기가 프리필터의 밉 좌표라 밉 사이 보간이
        // 필요하다 — 포인트로 읽으면 거칠기 단차가 띠로 보인다.
        samplers[1].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        samplers[1].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplers[1].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplers[1].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplers[1].MaxLOD = D3D12_FLOAT32_MAX;

        // 그림자 비교 샘플러. Deferred와 같은 설정이라야 같은 자리에서 두
        // 경로가 같은 그늘을 낸다 — 경계 색 1은 '맵 밖은 빛을 받음'이다.
        samplers[2].Filter = D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
        samplers[2].AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
        samplers[2].AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
        samplers[2].AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
        samplers[2].ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
        samplers[2].BorderColor[0] = 1.f;
        samplers[2].BorderColor[1] = 1.f;
        samplers[2].BorderColor[2] = 1.f;
        samplers[2].BorderColor[3] = 1.f;
        samplers[2].MaxLOD = D3D12_FLOAT32_MAX;

        m_sampler = RHISamplerTable{
            context.resources->GetSamplerHeap().CreateRange(samplers, 3) };
        if (!m_sampler.IsValid())
        {
            outError = "Forward+ 샘플러 생성 실패";
            return false;
        }
    }

    static const D3D12_INPUT_ELEMENT_DESC kInputElements[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,  0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TANGENT",  0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 40, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "BINORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 52, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    // GBuffer와 같은 정점 레이아웃을 쓰므로 같은 단정을 건다.
    static_assert(offsetof(Vertex, normal) == 12, "Vertex 레이아웃이 바뀌었다");
    static_assert(offsetof(Vertex, uv0) == 24, "Vertex 레이아웃이 바뀌었다");
    static_assert(offsetof(Vertex, tangent) == 40, "Vertex 레이아웃이 바뀌었다");
    static_assert(offsetof(Vertex, bitangent) == 52, "Vertex 레이아웃이 바뀌었다");

    // 컬링 경로와 참조 경로를 둘 다 만든다. 참조는 대조의 정답지다.
    struct ShadeVariant
    {
        bool                  reference;
        ID3D12PipelineState** target;
    };
    const ShadeVariant variants[] = {
        { false, &m_shadePSO },
        { true,  &m_referencePSO },
    };

    for (const ShadeVariant& variant : variants)
    {
        std::vector<D3D_SHADER_MACRO> shadeDefines;
        shadeDefines.push_back({ "TILE_SIZE", tileSize.c_str() });
        shadeDefines.push_back({ "MAX_LIGHTS_PER_TILE", maxLights.c_str() });
        if (variant.reference) shadeDefines.push_back({ "REFERENCE_PATH", "1" });
        shadeDefines.push_back({ nullptr, nullptr });

        ComPtr<ID3DBlob> vsBlob;
        ComPtr<ID3DBlob> psBlob;
        if (!CompileFwdShaderEntry(kShadeShader, shadeDefines.data(),
                "VSMain", "vs_5_0", vsBlob, outError)) return false;
        if (!CompileFwdShaderEntry(kShadeShader, shadeDefines.data(),
                "PSMain", "ps_5_0", psBlob, outError)) return false;

        DX12GraphicsPipelineDesc shadeDesc{};
        shadeDesc.inputElements = kInputElements;
        shadeDesc.inputElementCount = _countof(kInputElements);
        shadeDesc.vsBytecode = vsBlob->GetBufferPointer();
        shadeDesc.vsSize = vsBlob->GetBufferSize();
        shadeDesc.psBytecode = psBlob->GetBufferPointer();
        shadeDesc.psSize = psBlob->GetBufferSize();
        shadeDesc.rootSignature = shadeRoot.signature;
        shadeDesc.rootSignatureId = shadeRoot.id;
        // 깊이 테스트를 켠다. 포워드 물체는 이미 그려진 불투명 기하 뒤에
        // 있으면 가려져야 한다 — 끄면 벽 뒤 물체가 비쳐 보이는데, 그것은
        // 화면을 봐야만 알 수 있고 수치 검증에는 안 잡히는 부류다.
        shadeDesc.depthEnable = true;
        // ★ 깊이는 보되 쓰지 않는다. 투명이 깊이를 쓰면 뒤에 있는 다른
        //   투명면이 그 깊이에 가려져 사라진다 — 앞의 유리가 뒤의 유리를
        //   지워 버리는 그 증상이다. 테스트(LESS)는 유지해 불투명 기하가
        //   투명을 가리는 것은 그대로 둔다.
        shadeDesc.depthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
        // 알파 블렌딩(SRC_ALPHA/INV_SRC_ALPHA). 이것이 없으면 알파를 내도
        // 덮어쓰기라 투명이 성립하지 않는다.
        shadeDesc.blendEnable = true;
        shadeDesc.dsvFormat = kDepthFormat;
        // ★ 뒷면을 자른다.
        //
        //   GBuffer는 CULL_MODE_NONE인데도 멀쩡하다 — 깊이를 쓰기 때문에
        //   앞면이 깊이 테스트로 뒷면을 막는다. 투명은 깊이를 쓰지 않으므로
        //   (위의 depthWriteMask=ZERO) 그 보호가 없다: 닫힌 물체의 뒷면이
        //   앞면 위에 덧그려져 법선이 반대인 어두운 얼룩이 생긴다. 구에서
        //   먼저 눈에 띄었다.
        //
        //   양면 투명(잎사귀·천)은 뒷면→앞면 2패스가 정석이지만, 그건
        //   재질이 '양면인가'를 알려 줘야 성립한다. 지금 재질에 그 표시가
        //   없으므로 흔한 기본값(닫힌 물체)을 택한다.
        shadeDesc.cullMode = D3D12_CULL_MODE_BACK;
        shadeDesc.numRenderTargets = 1;
        shadeDesc.rtvFormats[0] = kOutputFormat;

        *variant.target = context.psoManager->GetOrCreate(shadeDesc, outError);
        if (nullptr == *variant.target) return false;
    }

    return true;
}

bool EnhancedForwardPass::EnsureTileBuffers(const EnhancedFrameContext& context,
    std::string& outError)
{
    const uint32_t tileTotal = m_tileCountX * m_tileCountY;
    if (0 == tileTotal) return true;

    // 크기가 그대로면 다시 만들지 않는다(SSGI 히스토리와 같은 계약).
    if (nullptr != m_tileCountBuffer)
    {
        if (m_allocatedTiles >= tileTotal) return true;
    }

    RHIBufferDesc desc{};
    desc.allowUnorderedAccess = true;

    // 두 배다. 앞 절반은 셰이딩이 읽는 자른 값, 뒤 절반은 자르기 전의
    // 원래 값 — 넘침이 얼마나 났는지가 수로 남아야 성능 수치를 믿을 수 있다.
    //
    // 초기 상태는 기본값(COMMON)을 쓴다. 버퍼는 첫 사용에서 승격되고,
    // UAV를 초기 상태로 주면 검증 레이어가 '무시한다'고 경고만 남긴다.
    desc.bytes = static_cast<uint64_t>(tileTotal) * 2ull * sizeof(uint32_t);
    desc.debugName = L"Forward+.TileCount";
    if (!context.resources->CreateBuffer(desc, m_tileCountBuffer, outError))
    {
        outError = "타일 카운트 버퍼 — " + outError;
        return false;
    }

    desc.bytes = static_cast<uint64_t>(tileTotal) * kMaxLightsPerTile * sizeof(uint32_t);
    desc.debugName = L"Forward+.TileList";
    if (!context.resources->CreateBuffer(desc, m_tileListBuffer, outError))
    {
        outError = "타일 목록 버퍼 — " + outError;
        return false;
    }

    m_allocatedTiles = tileTotal;
    return true;
}

bool EnhancedForwardPass::PrepareFrame(const EnhancedFrameContext& context, std::string& outError)
{
    // 타일 수. 화면이 타일 크기로 나누어떨어지지 않으면 가장자리 타일이
    // 화면 밖까지 걸치는데, 컬링 셰이더가 화면 경계로 자른다.
    m_tileCountX = (context.width + kTileSize - 1) / kTileSize;
    m_tileCountY = (context.height + kTileSize - 1) / kTileSize;

    // ── 재질 텍스처 운반 ──
    //
    // 기록 중에 올리지 않는다(GBuffer와 같은 규약). 캐시가 없는 자가 검증
    // 경로에서는 통째로 건너뛰고, 그때는 셰이더가 계수만으로 그린다.
    m_materialTextures.clear();
    if (nullptr != context.textureCache && nullptr != context.forwardDraws)
    {
        for (const EnhancedDrawItem& draw : *context.forwardDraws)
        {
            const MaterialKey key{
                draw.baseColor, draw.normalMap, draw.occRoughMetal, draw.emissive };
            if (m_materialTextures.find(key) != m_materialTextures.end()) continue;

            // 슬롯 의미를 따르는 폴백. 흰색 하나로 전부 때우면 뜻이 뒤집힌다 —
            // emissive에 흰색이면 텍스처 없는 재질이 전부 자체발광이고,
            // ORM에 흰색이면 B(금속)가 1이라 확산이 통째로 죽는다.
            // GBuffer와 같은 규칙이라야 같은 재질이 두 경로에서 같아 보인다.
            MaterialTextures textures{};
            bool anyValid = false;
            for (uint32_t i = 0; i < 4; ++i)
            {
                // ★ 실패를 IsValid로 거르려 하면 안 된다. GetOrUpload는 복구
                //   가능한 실패(2D 아님·멀티샘플·업로드 실패)에서 폴백을
                //   돌려주므로 IsValid는 늘 참이고, 그 자리에서 건너뛰면
                //   textureError가 통째로 사라진다 — 텍스처가 안 나오는데
                //   로그도 없는 상태가 된다. GBuffer처럼 무조건 전달한다.
                std::string textureError;
                DX12TextureCache::Entry uploaded{};
                if (nullptr == key[i] && 2 == i)
                {
                    uploaded = context.textureCache->GetOrmNeutralTexture(textureError);
                }
                else if (nullptr == key[i] && 3 == i)
                {
                    uploaded = context.textureCache->GetBlackTexture(textureError);
                }
                else
                {
                    uploaded = context.textureCache->GetOrUpload(key[i], textureError);
                }
                if (!textureError.empty()) outError = textureError;

                textures.resources[i] = uploaded.resource;
                textures.formats[i] = uploaded.format;
                textures.mipLevels[i] = uploaded.mipLevels;
                anyValid = anyValid || uploaded.IsValid();
            }

            if (anyValid) m_materialTextures.emplace(key, textures);
        }
    }

    return EnsureTileBuffers(context, outError);
}

void EnhancedForwardPass::Declare(EnhancedRenderGraph& graph, const EnhancedFrameContext& context)
{
    m_output = RGHandle{};

    if (!m_inputs.depth.IsValid() || nullptr == m_cullPSO ||
        nullptr == m_tileCountBuffer || nullptr == context.lights)
    {
        return;
    }

    // ── 광원 컬링 ──
    //
    // 그래프는 텍스처만 다루므로 타일 버퍼는 패스가 소유하고(영속),
    // 상태 전이도 패스가 책임진다. 지금은 UAV로 고정 — 셰이딩(3단계)이
    // 붙으면 SRV 전이가 여기 들어온다.
    graph.AddPass("Forward+.Cull",
        { { m_inputs.depth, RGResourceState::ShaderResource } },
        [this, &context](const EnhancedRenderGraph::ExecuteContext& executeContext)
        {
            auto* commandList = executeContext.commandList;

            const uint32_t lightCount =
                static_cast<uint32_t>(context.lights->size());

            // 광원 배열 업로드. EnhancedLight가 float4 넷이므로 그대로 복사한다.
            // 0개여도 최소 한 칸은 할당한다 — 빈 할당은 링이 거절한다.
            const uint64_t lightBytes =
                static_cast<uint64_t>((0 == lightCount) ? 1 : lightCount)
                * sizeof(EnhancedLight);
            const auto lightUpload = context.resources->GetUploadRing().Allocate(
                lightBytes, 16);
            if (!lightUpload.IsValid()) return;
            if (0 != lightCount)
            {
                memcpy(lightUpload.cpuAddress, context.lights->data(),
                    static_cast<size_t>(lightCount) * sizeof(EnhancedLight));
            }

            CullParams params{};
            if (nullptr != context.camera)
            {
                params.view = XMMatrixTranspose(context.camera->view);
                params.inverseProjection = XMMatrixTranspose(
                    XMMatrixInverse(nullptr, context.camera->projection));
            }
            params.screenWidth = context.width;
            params.screenHeight = context.height;
            params.tileGridX = m_tileCountX;
            params.tileGridY = m_tileCountY;
            params.lightCount = lightCount;

            const auto cb = context.resources->GetUploadRing().Allocate(
                sizeof(CullParams), DX12UploadRing::kConstantBufferAlignment);
            if (!cb.IsValid()) return;
            memcpy(cb.cpuAddress, &params, sizeof(params));

            const uint32_t tileTotal = m_tileCountX * m_tileCountY;

            const RHIBindingDesc srvs[] = {
                RHIBindingDesc::SrvDepth(executeContext.Resolve(m_inputs.depth)),
            };
            const RHIBindingDesc uavs[] = {
                RHIBindingDesc::UavBuffer(m_tileCountBuffer.Get(),
                    tileTotal * 2, sizeof(uint32_t)),
                RHIBindingDesc::UavBuffer(m_tileListBuffer.Get(),
                    tileTotal * kMaxLightsPerTile, sizeof(uint32_t)),
            };
            const RHIBindingTable srvTable = context.resources->CreateBindings(srvs);
            const RHIBindingTable uavTable = context.resources->CreateBindings(uavs);
            if (!srvTable.IsValid() || !uavTable.IsValid()) return;

            RHIEncoder& encoder = *executeContext.encoder;
            encoder.SetPipeline(RHIBindPoint::Compute, m_cullPSO, m_cullRootSignature);
            encoder.SetConstantBuffer(RHIBindPoint::Compute, 0, cb.gpuAddress);
            encoder.SetBindings(RHIBindPoint::Compute, 1, srvTable);
            encoder.SetRootBuffer(RHIBindPoint::Compute, 2, lightUpload.gpuAddress);
            encoder.SetBindings(RHIBindPoint::Compute, 3, uavTable);

            encoder.Dispatch(m_tileCountX, m_tileCountY, 1);

            // 다음 소비자(셰이딩·리드백)가 결과를 보기 전에 쓰기가 끝나야 한다.
            //
            // ★ 인코더에 UavBarrier를 둔 근거가 이 한 자리다. 그래프가 이것을
            //   대신 걸 수 없는 이유는 R3-1이 적은 "한 패스 안 디스패치 둘
            //   사이라서"가 아니라 — 소비자는 뒤따르는 다른 패스다 — 타일
            //   버퍼가 그래프 밖 리소스이기 때문이다. 그래프는 자기가 아는
            //   리소스의 전이만 만든다.
            ID3D12Resource* const tileBuffers[2] = {
                m_tileCountBuffer.Get(), m_tileListBuffer.Get() };
            encoder.UavBarrier(tileBuffers);
        },
        // 결과(타일 버퍼)가 그래프 밖 리소스라 컬링이 이 패스를 못 본다 —
        // 뿌리로 표시해야 걷어내지지 않는다. 셰이딩 출력도 지금은 소비자가
        // 없으므로 둘 다 뿌리다. 후처리가 붙으면 셰이딩만 남기고 뗀다.
        true);

    // 그릴 포워드 물체가 없으면 셰이딩을 선언하지 않는다.
    //
    // 빈 패스를 도는 낭비를 아끼는 것이 아니라, 선언 자체가 깊이를 DepthWrite로
    // 요구하기 때문이다 — 깊이 텍스처가 DSV로 만들어지지 않은 경로(컬링만 쓰는
    // 자가 검증 등)에서는 그 요구가 곧 검증 레이어 오류이자 디바이스 제거다.
    // 실제로 컬링 검증이 이것으로 죽었다.
    if (nullptr == m_shadePSO ||
        nullptr == context.forwardDraws || context.forwardDraws->empty())
    {
        return;
    }

    // ── 포워드 셰이딩 ──
    //
    // 타일 버퍼를 UAV에서 SRV로 넘긴다. 그래프가 버퍼를 모르므로 배리어를
    // 여기서 직접 건다 — 빼먹으면 셰이딩이 컬링 이전의 값을 읽는다.
    // ── 어디에 그리는가 ──
    //
    // lighting이 주어지면 그 위에 직접 그린다. 투명은 배경과 섞여야 하는데,
    // 별도 타깃에 그려 놓고 나중에 합성하려면 프리멀티플라이드 알파를
    // 따로 관리해야 하고 겹친 투명면의 누적 알파가 어긋난다 — 실제 배경에
    // 바로 블렌드하면 그 문제가 통째로 사라진다.
    //
    // 주지 않으면 예전처럼 자기 타깃을 만들어 지운다. 자가 검증들이 그
    // 경로를 쓴다(그쪽은 배경이 없어야 픽셀 대조가 성립한다).
    const bool drawsOntoLighting = m_inputs.lighting.IsValid();
    if (drawsOntoLighting)
    {
        m_output = m_inputs.lighting;
    }
    else
    {
        RGTextureDesc outputDesc{};
        outputDesc.width = context.width;
        outputDesc.height = context.height;
        outputDesc.format = kOutputFormat;
        outputDesc.allowRenderTarget = true;
        outputDesc.name = "Forward+.Shade";
        m_output = graph.CreateTexture(outputDesc);
    }

    // 깊이는 DepthWrite로 선언하되 PSO의 쓰기 마스크가 0이라 실제로는 쓰지
    // 않는다. DepthRead로 낮추려면 DSV를 READ_ONLY_DEPTH로 만들어야 하는데
    // (RGResourceState의 계약), 자가 검증까지 함께 바뀌므로 그대로 둔다 —
    // DEPTH_WRITE는 상위 상태라 읽기만 하는 것에 문제가 없다.
    // 그림자 맵은 있을 때만 선언한다. 없는데 선언하면 그래프가 무효 핸들을
    // 전이 대상으로 삼고, 자가 검증 경로는 애초에 그림자 패스가 없다.
    std::vector<EnhancedRenderGraph::RGPassUsage> shadeUsages = {
        { m_output, RGResourceState::RenderTarget },
        { m_inputs.depth, RGResourceState::DepthWrite },
    };
    const bool hasShadowMap = m_shadowMap.IsValid();
    if (hasShadowMap)
    {
        shadeUsages.push_back({ m_shadowMap, RGResourceState::ShaderResource });
    }

    graph.AddPass("Forward+.Shade", shadeUsages,
        [this, &context, drawsOntoLighting, hasShadowMap](
            const EnhancedRenderGraph::ExecuteContext& executeContext)
        {
            auto* commandList = executeContext.commandList;
            auto* device = context.resources->GetDevice();

            const uint32_t lightCount = (nullptr == context.lights)
                ? 0u : static_cast<uint32_t>(context.lights->size());

            TransitionTileBuffers(commandList,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE |
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

            // 깊이는 GBuffer가 채운 것을 그대로 쓴다. 지우지 않는다 —
            // 지우면 이미 그려진 불투명 기하가 포워드 물체를 가리지 못한다.
            ID3D12Resource* const colors[] = { executeContext.Resolve(m_output) };
            const auto depthDesc = RHIDepthTargetDesc::Depth(
                executeContext.Resolve(m_inputs.depth), kDepthFormat);
            const auto targets = context.resources->CreateRenderTargets(colors, &depthDesc);
            if (!targets.IsValid()) return;

            RHIEncoder& encoder = *executeContext.encoder;
            encoder.SetViewportAndScissor(context.width, context.height);
            encoder.BindRenderTargets(targets);

            // 라이팅 위에 그릴 때는 지우면 안 된다 — 지우는 순간 배경이
            // 사라져 투명이 섞일 대상이 없어진다.
            if (!drawsOntoLighting)
            {
                constexpr float kZero[4] = { 0.f, 0.f, 0.f, 0.f };
                encoder.ClearRenderTargets(targets, kZero);
            }

            const bool drew = RecordShading(encoder, commandList, context,
                m_useReferencePath ? m_referencePSO : m_shadePSO, lightCount,
                hasShadowMap ? executeContext.Resolve(m_shadowMap) : nullptr);
            (void)drew;

            // UAV로 돌려놓는다. '그래프가 끝나면 타일 버퍼는 UAV'라는 불변을
            // 지키기 위해서다 — 셰이딩이 도는지 여부에 따라 끝 상태가 달라지면
            // 뒤에 붙는 패스(리드백 등)가 어느 상태를 가정할지 알 수 없게 된다.
            TransitionTileBuffers(commandList,
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE |
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        },
        true);
}

// 타일 버퍼는 그래프 밖 리소스다 — 그래프가 상태를 모르니 전이도 여기서
// 직접 한다. 인코더에 전이가 없는 것이 R3의 계약이고, 이 자리는 계약의
// 예외가 아니라 계약 밖이다(포그의 초기화·SSGI의 히스토리와 같은 부류).
void EnhancedForwardPass::TransitionTileBuffers(ID3D12GraphicsCommandList* commandList,
    D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) const
{
    D3D12_RESOURCE_BARRIER barriers[2]{};
    ID3D12Resource* const buffers[2] = { m_tileCountBuffer.Get(), m_tileListBuffer.Get() };
    for (uint32_t i = 0; i < 2; ++i)
    {
        barriers[i].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[i].Transition.pResource = buffers[i];
        barriers[i].Transition.StateBefore = before;
        barriers[i].Transition.StateAfter = after;
        barriers[i].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    }
    commandList->ResourceBarrier(2, barriers);
}

// 드로우 기록. 컬링 경로와 참조 경로가 이 함수를 공유한다 — 대조가 뜻을
// 가지려면 PSO 말고는 아무것도 달라선 안 된다. 같은 것을 두 곳에서 따로
// 기록하면 그 차이가 결과에 섞여 무엇이 원인인지 알 수 없게 된다.
bool EnhancedForwardPass::RecordShading(RHIEncoder& encoder,
    ID3D12GraphicsCommandList* commandList,
    const EnhancedFrameContext& context, ID3D12PipelineState* pso, uint32_t lightCount,
    ID3D12Resource* shadowResource)
{
    if (nullptr == pso || nullptr == context.forwardDraws ||
        context.forwardDraws->empty()) return false;

    auto& uploadRing = context.resources->GetUploadRing();

    // 광원 배열. 컬링이 올린 것과 같은 내용이지만 다시 올린다 — 컬링의
    // 할당을 셰이딩까지 들고 오면 두 패스가 링 수명으로 묶인다.
    const uint64_t lightBytes =
        static_cast<uint64_t>((0 == lightCount) ? 1 : lightCount) * sizeof(EnhancedLight);
    const auto lightUpload = uploadRing.Allocate(lightBytes, 16);
    if (!lightUpload.IsValid()) return false;
    if (0 != lightCount)
    {
        memcpy(lightUpload.cpuAddress, context.lights->data(),
            static_cast<size_t>(lightCount) * sizeof(EnhancedLight));
    }

    // 인스턴스는 한 번에 올리고 드로우마다 주소만 옮긴다. 루트 SRV는 주소를
    // 받으므로 드로우별 재업로드가 필요 없다.
    const size_t drawCount = context.forwardDraws->size();
    const auto instanceUpload = uploadRing.Allocate(
        drawCount * sizeof(ShadeInstance), sizeof(ShadeInstance));
    if (!instanceUpload.IsValid()) return false;

    auto* instances = static_cast<ShadeInstance*>(instanceUpload.cpuAddress);
    for (size_t i = 0; i < drawCount; ++i)
    {
        const EnhancedDrawItem& draw = (*context.forwardDraws)[i];
        instances[i].world = XMMatrixTranspose(draw.worldMatrix);
        instances[i].baseColor = Mathf::Vector4{ draw.baseColorFactor.x,
            draw.baseColorFactor.y, draw.baseColorFactor.z, draw.baseColorFactor.w };
        instances[i].metallic = draw.metallic;
        instances[i].roughness = draw.roughness;
        instances[i].useNormalMap = draw.useNormalMap;

        const MaterialKey key{
            draw.baseColor, draw.normalMap, draw.occRoughMetal, draw.emissive };
        instances[i].hasMaterialTextures =
            (m_materialTextures.find(key) != m_materialTextures.end()) ? 1u : 0u;
    }

    ShadeParams params{};
    if (nullptr != context.camera)
    {
        params.viewProjection = XMMatrixTranspose(
            XMMatrixMultiply(context.camera->view, context.camera->projection));
    }
    params.tileGridX = m_tileCountX;
    params.tileGridY = m_tileCountY;
    params.lightCount = lightCount;
    if (nullptr != context.camera)
    {
        XMStoreFloat4(&params.eyePosition, context.camera->eyePosition);
        params.eyePosition.w = 1.f;
    }
    const bool hasIbl = (nullptr != m_iblIrradiance)
        && (nullptr != m_iblPrefiltered) && (nullptr != m_iblBrdfLut);
    params.hasIbl = hasIbl ? 1u : 0u;

    // 그림자. 자원이 없으면 데이터가 있어도 끈다 — 셰이더가 널 SRV를 읽어
    // 0을 얻으면 온 세상이 그늘이 된다.
    const bool hasShadow = m_shadowData.enabled && (nullptr != shadowResource);
    if (hasShadow)
    {
        for (uint32_t i = 0; i < kShadowCascadeCount; ++i)
        {
            params.lightViewProjection[i] =
                XMMatrixTranspose(m_shadowData.lightViewProjection[i]);
        }
        params.cameraForward = m_shadowData.cameraForward;
        params.cascadeSplits = m_shadowData.splitDepths;
        params.shadowBias = m_shadowData.bias;
        params.cascadeBlendBand = m_shadowData.cascadeBlendBand;
    }
    params.hasShadow = hasShadow ? 1u : 0u;

    const auto cb = uploadRing.Allocate(
        sizeof(ShadeParams), DX12UploadRing::kConstantBufferAlignment);
    if (!cb.IsValid()) return false;
    memcpy(cb.cpuAddress, &params, sizeof(params));

    encoder.SetPipeline(RHIBindPoint::Graphics, pso, m_shadeRootSignature);
    encoder.SetPrimitiveTopology(RHIPrimitiveTopology::TriangleList);

    encoder.SetConstantBuffer(RHIBindPoint::Graphics, 0, cb.gpuAddress);
    encoder.SetRootBuffer(RHIBindPoint::Graphics, 2, lightUpload.gpuAddress);
    encoder.SetRootBuffer(RHIBindPoint::Graphics, 3,
        m_tileCountBuffer->GetGPUVirtualAddress());
    encoder.SetRootBuffer(RHIBindPoint::Graphics, 4,
        m_tileListBuffer->GetGPUVirtualAddress());

    encoder.SetSamplers(RHIBindPoint::Graphics, 7, m_sampler);

    // IBL 셋도 드로우 밖에서 한 번. 재질과 달리 프레임 내내 같다.
    //
    // 없으면 널 디스크립터를 깐다 — 링에서 잘라 온 자리는 초기화되지 않은
    // 쓰레기라, 셰이더가 gHasIbl로 안 읽더라도 유효한 디스크립터가 있어야
    // 한다(Deferred가 같은 이유로 같은 처리를 한다).
    //
    // 포맷은 Deferred가 쓰는 것과 같아야 한다 — 생성기가 만든 리소스의
    // 실제 포맷이라 여기서만 다르게 적으면 어긋난 뷰가 된다.
    {
        // ★ 실패하면 그린 뒤가 아니라 여기서 접는다.
        //
        //   SetGraphicsRootSignature가 직전에 모든 루트 인자를 무효화했으므로,
        //   이 테이블을 안 걸고 드로우로 넘어가면 루트 파라미터 6이 바인딩되지
        //   않은 채로 그린다 — 검증 레이어가 잡는 미초기화 루트 인자이고,
        //   실제 하드웨어에서는 쓰레기 디스크립터 주소를 읽는다.
        //   위의 링 할당들(광원·인스턴스·상수)과 같은 처리다.
        constexpr DXGI_FORMAT kIblFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;

        const RHIBindingDesc frameSrvs[] = {
            RHIBindingDesc::SrvCube(hasIbl ? m_iblIrradiance : nullptr,
                kIblFormat, 1).OrNull(),
            RHIBindingDesc::SrvCube(hasIbl ? m_iblPrefiltered : nullptr,
                kIblFormat, hasIbl ? m_iblPrefilterMips : 1).OrNull(),
            RHIBindingDesc::Srv2D(hasIbl ? m_iblBrdfLut : nullptr, kIblFormat).OrNull(),
            // 그림자 맵. 깊이 배열이라 포맷과 차원을 둘 다 바꿔 봐야 한다
            // (Deferred가 같은 설명으로 같은 자원을 읽는다).
            RHIBindingDesc::SrvArray(hasShadow ? shadowResource : nullptr,
                DXGI_FORMAT_R32_FLOAT, kShadowCascadeCount).OrNull(),
        };
        const RHIBindingTable frameTable = context.resources->CreateBindings(frameSrvs);
        if (!frameTable.IsValid()) return false;

        encoder.SetBindings(RHIBindPoint::Graphics, 6, frameTable);
    }

    bool drewAnything = false;
    for (size_t i = 0; i < drawCount; ++i)
    {
        const EnhancedDrawItem& draw = (*context.forwardDraws)[i];
        if (nullptr == draw.mesh || nullptr == context.meshCache) continue;

        std::string uploadError;
        const auto entry = context.meshCache->GetOrUpload(draw.mesh, uploadError);
        if (!entry.IsValid()) continue;

        // ── 재질 텍스처 ──
        //
        // 테이블은 드로우마다 건다. 텍스처가 없는 드로우에도 걸어 두는 이유는
        // 바인딩된 테이블의 디스크립터는 '초기화돼 있어야' 하기 때문이다 —
        // 셰이더가 안 읽더라도(hasBaseColorMap=0) 비워 두면 규약 위반이다.
        // 널 리소스 SRV는 유효한 디스크립터이고 읽으면 0을 준다.
        //
        // ★ 링은 프레임당 4096개를 전 패스가 나눠 쓴다. 여기서 소진되면
        //   남은 투명이 조용히 빠지는 데서 끝나지 않고, 뒤에 오는 패스
        //   (UI·기즈모)가 예산 부족을 겪어 엉뚱한 자리에서 증상이 난다 —
        //   투명이 많아지면 DX12DescriptorRing의 overflows를 볼 것.
        {
            const MaterialKey key{
                draw.baseColor, draw.normalMap, draw.occRoughMetal, draw.emissive };
            const auto found = m_materialTextures.find(key);

            // 없는 슬롯은 널 디스크립터다(OrNull) — 셰이더가 hasXxxMap으로
            // 안 읽더라도 테이블 칸은 초기화돼 있어야 한다.
            const auto slotDesc = [&](uint32_t slot)
            {
                const bool has = (found != m_materialTextures.end())
                    && (nullptr != found->second.resources[slot]);
                return RHIBindingDesc::Srv2D(
                    has ? found->second.resources[slot] : nullptr,
                    has ? found->second.formats[slot] : DXGI_FORMAT_R8G8B8A8_UNORM,
                    0, has ? found->second.mipLevels[slot] : 1).OrNull();
            };
            const RHIBindingDesc materialSrvs[] = {
                slotDesc(0), slotDesc(1), slotDesc(2), slotDesc(3) };
            const RHIBindingTable srvTable =
                context.resources->CreateBindings(materialSrvs);
            if (!srvTable.IsValid()) break;

            encoder.SetBindings(RHIBindPoint::Graphics, 5, srvTable);
        }

        encoder.SetRootBuffer(RHIBindPoint::Graphics, 1,
            instanceUpload.gpuAddress + static_cast<uint64_t>(i) * sizeof(ShadeInstance));

        encoder.SetVertexBuffer(entry.vertexView);
        encoder.SetIndexBuffer(entry.indexView);
        encoder.DrawIndexed(entry.indexCount, 1);
        drewAnything = true;
    }

    return drewAnything;
}

void EnhancedForwardPass::Shutdown()
{
    m_tileCountBuffer.Reset();
    m_tileListBuffer.Reset();
    m_allocatedTiles = 0;
    m_tileCountX = 0;
    m_tileCountY = 0;
    m_lastCulledLights = 0;
    m_lastOverflowTiles = 0;

    m_cullPSO = nullptr;
    m_shadePSO = nullptr;
    m_referencePSO = nullptr;
    m_cullRootSignature = nullptr;
    m_shadeRootSignature = nullptr;

    m_materialTextures.clear();
    m_sampler = {};

    m_iblIrradiance = nullptr;
    m_iblPrefiltered = nullptr;
    m_iblBrdfLut = nullptr;
    m_iblPrefilterMips = 1;

    m_shadowMap = RGHandle{};
    m_shadowData = {};
}

// ── 자가 검증 ──
//
// 알려진 배치로 컬링을 단정한다. 광원 하나를 화면 중앙 표면 위에 두면
// 중앙 타일의 카운트는 1, 구석 타일은 0이어야 한다. 이것이 틀리는 방향이
// 둘 다 위험하다 — 중앙이 0이면 광원이 사라지고(어두워짐), 구석이 1이면
// 컬링이 안 도는 것이다(Forward+의 이득이 통째로 사라진다).
bool EnhancedSceneRenderer::RunForwardPlusTest(std::string& outLog)
{
    using Microsoft::WRL::ComPtr;

    constexpr uint32_t kWidth = 128;
    constexpr uint32_t kHeight = 128;

    outLog += "── Forward+ 검증 (PHASE 3-6) ──\n";

    std::string error;

    DX12DeviceResources resources;
    if (!resources.Initialize(kWidth, kHeight, error))
    {
        outLog += "[1/3] DX12 초기화 실패: " + error + "\n";
        return false;
    }

    DX12PSOManager psoManager;
    DX12RootSignatureCache rootSignatures;
    if (!psoManager.Initialize(resources.GetDevice(), L"dx12_fwd.cache", error) ||
        !rootSignatures.Initialize(resources.GetDevice(), error))
    {
        outLog += "[1/3] 캐시 초기화 실패: " + error + "\n";
        resources.Shutdown();
        return false;
    }

    // 카메라: 원점에서 +Z를 본다. 깊이 0.6의 표면이 화면 전체에 깔린다.
    FrameCameraSnapshot camera{};
    camera.view = XMMatrixIdentity();
    camera.projection = XMMatrixPerspectiveFovLH(XM_PIDIV2, 1.f, 0.1f, 100.f);
    camera.inverseView = XMMatrixIdentity();
    camera.inverseProjection = XMMatrixInverse(nullptr, camera.projection);

    // 광원: 깊이 0.6이 뷰 z ≈ 0.25이므로 그 근처 화면 중앙에 반경 0.05짜리
    // 점광 하나. 중앙 타일에는 닿고 구석에는 절대 닿지 않는 크기다.
    const float surfaceViewZ = 0.25f;
    std::vector<EnhancedLight> lights(1);
    lights[0].position = Mathf::Vector4(0.f, 0.f, surfaceViewZ, 1.f);   // 점광
    lights[0].attenuation = Mathf::Vector4(1.f, 0.f, 0.f, 0.05f);       // 반경

    EnhancedFrameContext frameContext{};
    frameContext.resources = &resources;
    frameContext.psoManager = &psoManager;
    frameContext.rootSignatures = &rootSignatures;
    frameContext.width = kWidth;
    frameContext.height = kHeight;
    frameContext.camera = &camera;
    frameContext.lights = &lights;

    EnhancedForwardPass forward;
    if (!forward.Initialize(frameContext, error))
    {
        outLog += "[1/3] Forward+ 초기화 실패: " + error + "\n";
        resources.Shutdown();
        return false;
    }
    outLog += "[1/3] 컬링 셰이더 컴파일·PSO 생성 통과\n";

    bool passed = true;
    {
        // 깊이 텍스처를 0.6으로 채운다. ClearUnorderedAccessView가 제일
        // 짧지만 디스크립터 두 벌(셰이더 가시 + 비가시)이 필요해 오히려
        // 길다 — 업로드 텍스처 복사로 채운다.
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_RESOURCE_DESC depthDesc{};
        depthDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        depthDesc.Width = kWidth;
        depthDesc.Height = kHeight;
        depthDesc.DepthOrArraySize = 1;
        depthDesc.MipLevels = 1;
        depthDesc.Format = DXGI_FORMAT_R32_FLOAT;
        depthDesc.SampleDesc.Count = 1;

        ComPtr<ID3D12Resource> depth;
        if (FAILED(resources.GetDevice()->CreateCommittedResource(&heap,
            D3D12_HEAP_FLAG_NONE, &depthDesc, D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr, IID_PPV_ARGS(&depth))))
        {
            outLog += "[2/3] 깊이 생성 실패\n";
            forward.Shutdown();
            resources.Shutdown();
            return false;
        }

        if (!resources.BeginFrame(error))
        {
            outLog += "[2/3] BeginFrame 실패: " + error + "\n";
            forward.Shutdown();
            resources.Shutdown();
            return false;
        }

        // 깊이 채우기: 업로드 링에서 행 정렬 규칙에 맞춰 복사한다.
        {
            const uint32_t rowPitch =
                ((kWidth * 4u) + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1u)
                & ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1u);
            const auto upload = resources.GetUploadRing().Allocate(
                static_cast<uint64_t>(rowPitch) * kHeight, 512);
            if (upload.IsValid())
            {
                for (uint32_t y = 0; y < kHeight; ++y)
                {
                    auto* row = reinterpret_cast<float*>(
                        static_cast<uint8_t*>(upload.cpuAddress)
                        + static_cast<size_t>(y) * rowPitch);
                    for (uint32_t x = 0; x < kWidth; ++x) row[x] = 0.6f;
                }

                D3D12_TEXTURE_COPY_LOCATION dst{};
                dst.pResource = depth.Get();
                dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;

                D3D12_TEXTURE_COPY_LOCATION src{};
                src.pResource = upload.resource;
                src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                src.PlacedFootprint.Offset = upload.offset;
                src.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R32_FLOAT;
                src.PlacedFootprint.Footprint.Width = kWidth;
                src.PlacedFootprint.Footprint.Height = kHeight;
                src.PlacedFootprint.Footprint.Depth = 1;
                src.PlacedFootprint.Footprint.RowPitch = rowPitch;

                resources.GetCommandList()->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

                D3D12_RESOURCE_BARRIER toSrv{};
                toSrv.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                toSrv.Transition.pResource = depth.Get();
                toSrv.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
                toSrv.Transition.StateAfter =
                    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
                toSrv.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                resources.GetCommandList()->ResourceBarrier(1, &toSrv);
            }
        }

        if (!forward.PrepareFrame(frameContext, error))
        {
            outLog += "[2/3] PrepareFrame 실패: " + error + "\n";
            passed = false;
        }

        EnhancedRenderGraph graph(resources);

        EnhancedForwardPass::Inputs inputs{};
        inputs.depth = graph.ImportTexture(depth.Get(),
            RGResourceState::ShaderResource, "Fwd.TestDepth");
        forward.SetInputs(inputs);

        forward.Declare(graph, frameContext);

        // 타일 카운트 리드백.
        const uint32_t tileGrid = kWidth / EnhancedForwardPass::kTileSize;   // 8
        const uint32_t tileTotal = tileGrid * tileGrid;

        D3D12_HEAP_PROPERTIES readbackHeap{};
        readbackHeap.Type = D3D12_HEAP_TYPE_READBACK;

        D3D12_RESOURCE_DESC readbackDesc{};
        readbackDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        readbackDesc.Width = tileTotal * sizeof(uint32_t);
        readbackDesc.Height = 1;
        readbackDesc.DepthOrArraySize = 1;
        readbackDesc.MipLevels = 1;
        readbackDesc.SampleDesc.Count = 1;
        readbackDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        ComPtr<ID3D12Resource> readback;
        if (FAILED(resources.GetDevice()->CreateCommittedResource(&readbackHeap,
            D3D12_HEAP_FLAG_NONE, &readbackDesc, D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr, IID_PPV_ARGS(&readback))))
        {
            outLog += "[2/3] 리드백 생성 실패\n";
            passed = false;
        }

        if (passed)
        {
            graph.AddPass("Fwd.Readback",
                { { inputs.depth, RGResourceState::ShaderResource } },
                [&](const EnhancedRenderGraph::ExecuteContext& executeContext)
                {
                    (void)executeContext;
                    auto* commandList = executeContext.commandList;

                    // 타일 버퍼는 그래프 밖 리소스라 전이도 여기서 직접 한다.
                    D3D12_RESOURCE_BARRIER toCopy{};
                    toCopy.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                    toCopy.Transition.pResource = forward.GetTileCountBuffer();
                    toCopy.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
                    toCopy.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
                    toCopy.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                    commandList->ResourceBarrier(1, &toCopy);

                    commandList->CopyBufferRegion(readback.Get(), 0,
                        forward.GetTileCountBuffer(), 0,
                        tileTotal * sizeof(uint32_t));

                    D3D12_RESOURCE_BARRIER back = toCopy;
                    back.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
                    back.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
                    commandList->ResourceBarrier(1, &back);
                }, true);

            if (!graph.Compile(resources.GetDevice(), error))
            {
                outLog += "[2/3] Compile 실패: " + error + "\n";
                passed = false;
            }
            if (passed && !graph.Execute(resources.GetCommandList(), error))
            {
                outLog += "[2/3] Execute 실패: " + error + "\n";
                passed = false;
            }
        }

        if (!resources.EndFrame(error))
        {
            outLog += "[2/3] EndFrame 실패: " + error + "\n";
            passed = false;
        }
        else
        {
            resources.WaitForGpu();
        }

        // ── 단정 ──
        if (passed)
        {
            void* mapped = nullptr;
            D3D12_RANGE range{ 0, tileTotal * sizeof(uint32_t) };
            if (FAILED(readback->Map(0, &range, &mapped)))
            {
                outLog += "[3/3] 리드백 Map 실패\n";
                passed = false;
            }
            else
            {
                const auto* counts = static_cast<const uint32_t*>(mapped);

                const uint32_t centerTile = (tileGrid / 2) * tileGrid + (tileGrid / 2);
                const uint32_t cornerTile = 0;

                uint32_t litTiles = 0;
                for (uint32_t i = 0; i < tileTotal; ++i)
                {
                    if (0 != counts[i]) ++litTiles;
                }

                char line[224]{};
                std::snprintf(line, sizeof(line),
                    "[3/3] 타일 %ux%u — 중앙 %u · 구석 %u · 켜진 타일 %u/%u\n",
                    tileGrid, tileGrid, counts[centerTile], counts[cornerTile],
                    litTiles, tileTotal);
                outLog += line;

                // 중앙이 0이면 광원이 사라진다(어두워짐). 구석이 1이면 컬링이
                // 안 도는 것이다(전 타일에 다 들어가 Forward+의 이득이 없다).
                if (0 == counts[centerTile])
                {
                    outLog += "중앙 타일에 광원이 없다 — 컬링이 광원을 떨어뜨렸다\n";
                    passed = false;
                }
                if (0 != counts[cornerTile])
                {
                    outLog += "구석 타일에 광원이 있다 — 컬링이 자르지 않는다\n";
                    passed = false;
                }
                if (litTiles == tileTotal)
                {
                    outLog += "모든 타일이 켜졌다 — 반경 컬링이 죽었다\n";
                    passed = false;
                }

                readback->Unmap(0, nullptr);
            }
        }
    }

    std::string validation;
    const uint32_t problems = resources.DrainDebugMessages(validation);
    if (0 != problems)
    {
        passed = false;
        outLog += "검증 레이어 문제 " + std::to_string(problems) + "건\n" + validation;
    }

    forward.Shutdown();
    rootSignatures.Shutdown();
    psoManager.Shutdown();
    resources.Shutdown();

    outLog += passed ? "Forward+ 검증 통과\n" : "Forward+ 검증 실패\n";
    return passed;
}

#endif
