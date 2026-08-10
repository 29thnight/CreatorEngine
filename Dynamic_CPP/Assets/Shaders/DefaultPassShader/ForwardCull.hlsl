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
