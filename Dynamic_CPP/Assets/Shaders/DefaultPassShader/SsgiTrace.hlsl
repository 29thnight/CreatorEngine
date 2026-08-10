Texture2D<float>  gHiZ0 : register(t0);
Texture2D<float>  gHiZ1 : register(t1);
Texture2D<float>  gHiZ2 : register(t2);
Texture2D<float>  gHiZ3 : register(t3);
Texture2D<float>  gHiZ4 : register(t4);
Texture2D<float>  gHiZ5 : register(t5);
Texture2D<float>  gHiZ6 : register(t6);
Texture2D<float>  gHiZ7 : register(t7);
Texture2D<float4> gNormal   : register(t8);
Texture2D<float4> gLighting : register(t9);

RWTexture2D<float4> gOutput : register(u0);

cbuffer TraceParams : register(b0)
{
    float4x4 gInverseProjection;
    float4x4 gProjection;
    uint2    gOutputSize;
    uint2    gDepthSize;
    uint     gMipCount;
    uint     gFrameIndex;
    uint2    gLightingSize;   // 라이팅 텍스처 해상도 — GI 해상도와 다르다
    float    gMaxDistance;      // 뷰 공간 최대 추적 거리
    float    gThickness;        // 표면 두께 가정(뷰 공간)
};

static const float kPI = 3.14159265f;

float LoadHiZ(uint mip, int2 coord)
{
    // 밉마다 다른 텍스처다. 그래프가 밉 체인을 한 리소스로 다루지 않으므로
    // 밉당 텍스처를 만들었고, 그래서 여기서 분기한다.
    // (분기가 싫으면 Texture2DArray로 묶을 수 있지만 밉마다 크기가 달라
    //  배열이 성립하지 않는다.)
    switch (mip)
    {
    case 0:  return gHiZ0.Load(int3(coord, 0));
    case 1:  return gHiZ1.Load(int3(coord, 0));
    case 2:  return gHiZ2.Load(int3(coord, 0));
    case 3:  return gHiZ3.Load(int3(coord, 0));
    case 4:  return gHiZ4.Load(int3(coord, 0));
    case 5:  return gHiZ5.Load(int3(coord, 0));
    case 6:  return gHiZ6.Load(int3(coord, 0));
    default: return gHiZ7.Load(int3(coord, 0));
    }
}

uint2 MipSize(uint mip)
{
    return max(uint2(1, 1), gDepthSize >> mip);
}

float3 ViewFromDepth(float2 uv, float depth)
{
    const float4 clip = float4(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f, depth, 1.0f);
    const float4 view = mul(clip, gInverseProjection);
    return view.xyz / view.w;
}

// 해시 기반 노이즈. 프레임 인덱스를 섞어 프레임마다 다른 방향을 본다.
float Hash(uint2 p, uint frame)
{
    uint n = p.x * 73856093u ^ p.y * 19349663u ^ frame * 83492791u;
    n = (n ^ 61u) ^ (n >> 16);
    n *= 9u;
    n = n ^ (n >> 4);
    n *= 0x27d4eb2du;
    n = n ^ (n >> 15);
    return float(n & 0x00ffffffu) / float(0x01000000u);
}

// 방향에 맞는 셀 경계까지의 t 증가량.
//
// ★ 한때 floor+1(양의 방향 경계)만 봤다. 위로 가는 광선(delta.y < 0)은
//   반대쪽 경계까지의 거리를 얻어 최대 한 셀을 헛디뎠다. 성분별로
//   진행 방향의 경계를 고른다. 거의 0인 성분은 그 축으로는 영원히
//   경계를 안 만나므로 큰 값으로 밀어 min에서 빠지게 한다.
float StepToCellEdge(float2 uv, float2 direction, uint2 size)
{
    const float2 texel = 1.0f / float2(size);
    const float2 cellIndex = floor(uv / texel);
    const float2 boundary = (cellIndex + step(0.0f, direction)) * texel;

    float2 toEdge = float2(1e9f, 1e9f);
    if (abs(direction.x) > 1e-8f) toEdge.x = (boundary.x - uv.x) / direction.x;
    if (abs(direction.y) > 1e-8f) toEdge.y = (boundary.y - uv.y) / direction.y;

    return max(min(toEdge.x, toEdge.y), 1e-4f);
}

// Hi-Z 행진. 히트하면 true와 히트 UV를 준다.
bool TraceHiZ(float3 origin, float3 direction, float2 startUV, out float2 hitUV)
{
    hitUV = startUV;

    // 뷰 공간 끝점을 화면으로 옮겨 화면 공간 방향을 얻는다.
    const float3 endView = origin + direction * gMaxDistance;
    float4 endClip = mul(float4(endView, 1.0f), gProjection);
    if (endClip.w <= 0.0001f) return false;
    endClip /= endClip.w;

    const float2 endUV = float2(endClip.x * 0.5f + 0.5f, 0.5f - endClip.y * 0.5f);
    float2 delta = endUV - startUV;
    if (dot(delta, delta) < 1e-12f) return false;

    // 시작 깊이(광선 원점의 화면 깊이)와 끝 깊이 사이를 선형 보간한다.
    const float startDepth = LoadHiZ(0, int2(startUV * gDepthSize));
    const float endDepth = endClip.z;

    uint mip = min(2u, gMipCount - 1u);   // 중간 밉에서 시작한다

    // ★ 시작 셀을 벗어난 지점에서 출발한다.
    //
    //   t=0에서 시작하면 첫 비교가 자기 픽셀의 깊이와 이루어지고, 시작
    //   깊이가 곧 그 셀의 깊이이므로 모든 광선이 그 자리에서 '히트'한다.
    //   실측이 그것을 드러냈다 — 히트 비율 0.5507이 비하늘 픽셀 비율
    //   (9032/16384 = 0.551)과 정확히 일치했고, 씬에 무엇을 넣어도 숫자가
    //   꿈쩍하지 않았다. 트레이스 전체가 자기 조명을 되먹이고 있었던 것이다.
    float t = StepToCellEdge(startUV, delta, gDepthSize) + 1e-4f;
    const uint kMaxSteps = 64u;

    for (uint step = 0; step < kMaxSteps; ++step)
    {
        if (t >= 1.0f) return false;

        const float2 uv = startUV + delta * t;
        if (any(uv < 0.0f) || any(uv > 1.0f)) return false;

        const uint2 size = MipSize(mip);
        const int2 cell = int2(uv * size);
        const float cellDepth = LoadHiZ(mip, cell);
        const float rayDepth = lerp(startDepth, endDepth, t);

        if (rayDepth < cellDepth)
        {
            // 이 셀은 광선보다 뒤에 있다 = 비었다. 셀 경계까지 건너뛴다.
            t += StepToCellEdge(uv, delta, size);

            if (mip + 1 < gMipCount) ++mip;   // 비었으니 더 크게 건넌다
        }
        else
        {
            if (0 == mip)
            {
                // 두께를 넘어 뚫고 지나간 것이면 히트로 보지 않는다.
                // 이것이 없으면 얇은 물체 뒤가 전부 막힌 것으로 잡힌다.
                //
                // ★ 뷰 공간에서 비교한다. 한때 클립 깊이(0~1 비선형) 차이를
                //   gThickness(뷰 공간 값)와 그대로 비교했는데, 단위가 달라
                //   검사가 사실상 죽어 있었다 — 두께를 100배 바꿔도 히트
                //   비율이 0.6% 안에서만 움직였다(실측). 클립 깊이는 원근
                //   나눗셈 때문에 먼 곳에서 촘촘해지므로, 같은 클립 차이가
                //   가까이서는 몇 cm, 멀리서는 몇 m를 뜻한다. 두께는
                //   물리량이니 뷰 공간에서 재야 한다.
                //
                //   선형화는 히트 판정 시점에만 하므로 비용은 행렬 곱 둘이다.
                const float rayViewZ = ViewFromDepth(uv, rayDepth).z;
                const float cellViewZ = ViewFromDepth(uv, cellDepth).z;
                if (rayViewZ - cellViewZ > gThickness) return false;

                hitUV = uv;
                return true;
            }
            --mip;   // 무언가 있다 — 정밀하게 본다
        }
    }

    return false;
}

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= gOutputSize.x || id.y >= gOutputSize.y) return;

    const float2 uv = (float2(id.xy) + 0.5f) / float2(gOutputSize);
    const int2 depthCoord = int2(uv * gDepthSize);

    const float depth = LoadHiZ(0, depthCoord);
    if (depth >= 1.0f)
    {
        // 하늘이다. 간접광을 모을 표면이 없다.
        gOutput[id.xy] = float4(0, 0, 0, 0);
        return;
    }

    const float3 position = ViewFromDepth(uv, depth);
    const float3 normal = normalize(gNormal.Load(int3(depthCoord, 0)).xyz * 2.0f - 1.0f);

    float3 gathered = 0.0f;
    uint   hits = 0;

    const float jitter = Hash(id.xy, gFrameIndex);

    [loop]
    for (uint slice = 0; slice < SLICES_PER_FRAME; ++slice)
    {
        // 반구에서 코사인 가중으로 방향을 뽑는다. 프레임마다 jitter가
        // 달라지므로 여러 프레임에 걸쳐 반구가 고르게 덮인다.
        const float u1 = frac(jitter + float(slice) / float(SLICES_PER_FRAME));
        const float u2 = frac(jitter * 1.61803f + float(slice) * 0.7548f);

        const float r = sqrt(u1);
        const float phi = 2.0f * kPI * u2;

        float3 tangent = abs(normal.z) < 0.999f ? float3(0, 0, 1) : float3(1, 0, 0);
        tangent = normalize(cross(tangent, normal));
        const float3 bitangent = cross(normal, tangent);

        const float3 direction = normalize(
            tangent * (r * cos(phi)) + bitangent * (r * sin(phi))
            + normal * sqrt(max(0.0f, 1.0f - u1)));

        float2 hitUV;
        if (TraceHiZ(position + normal * 0.01f, direction, uv, hitUV))
        {
            // 맞은 표면의 직접광이 이 픽셀의 간접광이 된다.
            // Load로 읽는다. 히트 지점의 색을 그대로 쓰므로 보간이 필요
            // 없고, 샘플러를 안 만들면 힙도 바인딩도 하나 준다.
            // ★ 라이팅은 자기 해상도 좌표로 읽는다. 한때 gDepthSize(1/2
            //   해상도)를 곱했는데, 실제 씬의 라이팅은 전 해상도라 왼쪽 위
            //   사분면만 읽고 있었다 — GI가 화면 절반 밖의 빛을 절대 못 봤다.
            gathered += gLighting.Load(int3(hitUV * gLightingSize, 0)).rgb;
            ++hits;
        }
    }

    // 슬라이스 수로 나눈다. 맞지 않은 방향은 0(하늘/먼 곳)으로 친다 —
    // 하늘빛은 합성 단계에서 따로 더한다.
    const float3 gi = gathered / float(SLICES_PER_FRAME);

    // a에 히트 비율을 담는다. 리졸브가 이 값으로 신뢰도를 가늠한다.
    gOutput[id.xy] = float4(gi, float(hits) / float(SLICES_PER_FRAME));
}
