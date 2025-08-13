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
Texture2D gNoiseTexture : register(t1);
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
    float progress = polarAngleProgress;
    float2 uv = input.texCoord;
    
    // 초승달 호의 중심선 계산 (더 자연스러운 곡선)
    float2 center = float2(0.3, 0.5);
    float2 toPixel = uv - center;
    
    // 원형 호를 따라 각도 계산
    float angle = atan2(toPixel.y, toPixel.x);
    float radius = length(toPixel);
    
    // 초승달 호의 중심 반지름 (대략적)
    float targetRadius = 0.3;
    float radiusThickness = 0.12; // 호의 두께
    
    // 각도를 0~1로 정규화 (초승달 범위에 맞게)
    float normalizedAngle = (angle + 3.14159) / 6.28318; // -π~π를 0~1로
    
    // 진행도에 따른 클리핑
    float clipPosition = progress;
    if (normalizedAngle > clipPosition)
        return true;
    
    // 검기 끝부분 페이드
    float fadeLength = 0.1;
    if (normalizedAngle > clipPosition - fadeLength)
    {
        float fade = (clipPosition - normalizedAngle) / fadeLength;
        if (fade < frac(time * 6.0 + angle * 3.0))
            return true;
    }
    
    return false;
}

PixelOutput main(PixelInput input)
{
    PixelOutput output;
    
    if (shouldClipPolarAngle(input))
    {
        discard;
    }
    
    float3 normal = normalize(input.normal);
    float3 viewDir = normalize(input.viewDir);
    
    float4 diffuseColor = gDiffuseTexture.Sample(gLinearSampler, input.texCoord);
    
    if (diffuseColor.a < 0.1)
        discard;
    
    if (input.alpha <= 0.01)
        discard;
    
    float3 finalColor;
    
    if (input.renderMode == 0)
    {
        finalColor = input.color.rgb * diffuseColor.rgb;
    }
    else
    {
        float3 lightDir = normalize(float3(0.5, 1.0, 0.3));
        float NdotL = max(0.0, dot(normal, lightDir));
        
        float3 ambient = float3(0.3, 0.3, 0.3);
        float3 diffuse = float3(0.7, 0.7, 0.7) * NdotL;
        
        float3 reflectDir = reflect(-lightDir, normal);
        float spec = pow(max(dot(viewDir, reflectDir), 0.0), 32.0);
        float3 specular = float3(0.2, 0.2, 0.2) * spec;
        
        float3 lighting = ambient + diffuse + specular;
        finalColor = input.color.rgb * diffuseColor.rgb * lighting;
    }
    
    float finalAlpha = input.alpha * diffuseColor.a;
    output.color = float4(finalColor, finalAlpha);
    
    return output;
}