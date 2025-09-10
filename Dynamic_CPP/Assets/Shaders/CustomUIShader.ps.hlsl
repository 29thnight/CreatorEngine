// PieSprite_PS.hlsl
cbuffer UIBuffer : register(b1)
{
    float2 centerUV; // 원 중심 (0~1, 텍스처 UV 기준)
    float radiusUV; // 외곽 반지름 (UV 단위, 0~1)
    float percent; // 0~1 (그릴 비율)
    float startAngle; // 라디안 단위. 0 = +X축, PI/2 = +Y축 방향
    int clockwise; // 1이면 시계방향, 0이면 반시계
    float featherAngle; // 각도 페더(라디안). 0.005~0.05 정도
    float innerRadius; // 0이면 꽉 찬 원, (0~radiusUV)로 링 두께 설정
    float4 tint; // 곱색
    float4 bgColor; // 배경색(프리멀티플라이드 알파 가정)
}

Texture2D Diffuse : register(t0);
SamplerState Samp : register(s0);


// 유틸: 각도 [0,2π) 정규화
static const float TWO_PI = 6.283185307179586f;
float normAngle(float a)
{
    a = fmod(a, TWO_PI);
    return (a < 0) ? a + TWO_PI : a;
}

float4 main(float4 color : COLOR0, float2 texCoord : TEXCOORD0) : SV_TARGET
{
    // 기본 텍스처 색(프리멀티플라이드 알파 권장)
    float4 base = Diffuse.Sample(Samp, texCoord) * color * tint;

    // 중심/반지름 기반 좌표
    float2 d = texCoord - centerUV;
    float r = length(d);

    // 반경 커버리지 (도넛 + 페더)
    float rFeather = max(radiusUV * 0.01, 1e-5);
    float outerEdge = 1.0 - smoothstep(radiusUV - rFeather, radiusUV, r); // 안쪽=1, 바깥=0
    float innerEdge = (innerRadius <= 0.0)
    ? 1.0 // 꽉 찬 원
    : smoothstep(innerRadius - rFeather, innerRadius, r); // r<inner=0, r>=inner=1
    float radialMask = saturate(innerEdge * outerEdge);

// 각도 계산
    float ang = atan2(d.y, d.x); // (-π, π]
    ang = normAngle(ang); // [0, 2π)

// 12시 기준 방향별 "한쪽으로만" 증가하는 상대각
    float relCW = normAngle(startAngle - ang); // 시계방향으로 증가
    float relCCW = normAngle(ang - startAngle); // 반시계방향으로 증가
    float rel = (clockwise != 0) ? relCW : relCCW;

// 잘라낼 목표 각도
    float cut = saturate(percent) * TWO_PI;

// 각도 페더(부드러운 경계)
    float fa = min(max(featherAngle, 0.0), cut); // 과한 페더 방지
    float angleMask = (fa <= 0.0)
    ? step(rel, cut) // rel <= cut → 1
    : (1.0 - smoothstep(cut - fa, cut, rel)); // cut-fa ~ cut에서만 부드럽게 1→0

    float coverage = radialMask * angleMask;
    
    // 배경색과의 프리멀티플라이드 블렌딩
    float a = coverage * base.a;
    float3 rgb = base.rgb * coverage + bgColor.rgb * (1.0 - coverage);

    return float4(rgb,a);
}
