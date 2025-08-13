// MeshParticleClippingPS.hlsl - 내부에서 회전 사각형 클리핑 처리
cbuffer PolarClippingParams : register(b2)
{
    float polarClippingEnabled;
    float polarAngleProgress;
    float polarStartAngle;
    float polarDirection;
    
    float3 polarCenter;
    float pad1; // 기존 pad1 자리에 time 추가
    
    float3 polarUpAxis;
    float pad2;
    
    float3 polarReferenceDir;
    float pad3;
};

cbuffer TimeParams : register(b3)
{
    float time;
    float3 padd1;
};

struct PixelInput
{
    float4 position : SV_POSITION;
    float3 worldPos : WORLD_POSITION;
    float3 particleCenter : PARTICLE_CENTER;
    float3 localPos : LOCAL_POSITION;
    float3 particleScale : PARTICLE_SCALE;
    float3 normal : NORMAL;
    float2 texCoord : TEXCOORD0;
    float4 color : COLOR;
    float3 viewDir : VIEW_DIR;
    float alpha : ALPHA;
    uint renderMode : RENDER_MODE;
};

struct PixelOutput
{
    float4 color : SV_Target;
};

Texture2D gDiffuseTexture : register(t0);
SamplerState gLinearSampler : register(s0);
SamplerState gPointSampler : register(s1);

float normalizeAngle(float angle)
{
    const float TWO_PI = 6.28318530718;
    angle = fmod(angle, TWO_PI);
    if (angle < 0.0)
        angle += TWO_PI;
    return angle;
}

bool shouldClipPolarAngle(PixelInput input)
{
    if (polarClippingEnabled < 0.5)
        return false;
    
    float2 center = float2(0.3, 0.5);
    float2 uv = input.texCoord - center;
    
    // 거리 기반 페이드 추가
    float distance = length(uv);
    if (distance < 0.1) // 중심 근처는 항상 보이게
        return false;
    
    float angle = atan2(uv.y, uv.x);
    
    const float TWO_PI = 6.28318530718;
    if (angle < 0.0)
        angle += TWO_PI;
    
    angle = normalizeAngle(angle - polarStartAngle);
    
    // 부드러운 클리핑을 위한 여유각도 추가
    float clipAngle = polarAngleProgress * TWO_PI;
    float fadeRange = 0.3; // 페이드 범위
    
    if (polarDirection > 0.0)
    {
        if (angle > clipAngle + fadeRange)
            return true;
        else if (angle > clipAngle)
        {
            // 부드러운 페이드
            float fade = (angle - clipAngle) / fadeRange;
            if (fade > frac(time * 2.0)) // 시간에 따른 랜덤 효과
                return true;
        }
    }
    else
    {
        return angle < (TWO_PI - clipAngle);
    }
    
    return false;
}

bool shouldClipTextureBasedSword(PixelInput input)
{
    // 기본 검기 이펙트 - 왼쪽에서 오른쪽으로 순차적 클리핑
    float progress = polarAngleProgress;
    
    // 현재 픽셀의 UV.x가 progress보다 크면 클리핑
    if (input.texCoord.x > progress)
        return true;
    
    return false;
}

PixelOutput main(PixelInput input)
{
    PixelOutput output;
    
    if (shouldClipPolarAngle(input))
    {
        discard;
    }
    
    float2 uv = input.texCoord;
    
    // time이 0인지 확인
    if (time < 0.01)
    {
        output.color = float4(1.0, 0.0, 0.0, 1.0); // 빨간색으로 표시
        return output;
    }
    
    // 상하 구조 파동
    //float timeOffset = uv.x * 3.0 + uv.y * 1.5;
    float timeOffset = uv.y * 3.0 + uv.x * 1.5;
    float animatedTime = time + timeOffset;
    
    //float wave1 = sin(animatedTime * 4.0 + uv.x * 10.0) * 0.5 + 0.5;
    float wave1 = sin(animatedTime * 4.0 + uv.y * 10.0) * 0.5 + 0.5;
    float wave2 = cos(animatedTime * 3.0 + uv.y * 8.0) * 0.5 + 0.5;
    float wave3 = sin(animatedTime * 5.0 + (uv.x + uv.y) * 6.0) * 0.5 + 0.5;
    
    float wavePattern = (wave1 * 0.4 + wave2 * 0.3 + wave3 * 0.3);
    
    // 하늘색 기본 색상 (고정)
    float3 baseColor = float3(0.3, 0.7, 1.0);
    float3 brightColor = float3(0.9, 0.3, 3.0);
    
    // 파동에 따라 두 색상 사이를 보간
    float3 finalColor = lerp(baseColor, brightColor, wavePattern);
    
    output.color = float4(finalColor, 0.8);
    
    return output;
}