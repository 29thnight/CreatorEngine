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

float2 getMovedTextureUV(PixelInput input)
{
    if (polarClippingEnabled < 0.5)
        return input.texCoord;
    
    float animationDuration = 1.0f;
    float useTriangleWave = 1.0f; // 0이 선형 1이 삼각파
    // 셰이더 내부에서 애니메이션 계산
    float currentTime = time;
    float cycleDuration = animationDuration; // 초 단위
    

    
    // 0 ~ 1 범위로 정규화된 시간
    float normalizedTime = fmod(currentTime, cycleDuration) / cycleDuration;
    
    float animatedProgress;
    if (useTriangleWave > 0.5)
    {
        // 삼각파: 0 -> 1 -> 0 (나타났다가 사라짐)
        if (normalizedTime <= 0.5)
        {
            animatedProgress = normalizedTime * 2.0;
        }
        else
        {
            animatedProgress = (1.0 - normalizedTime) * 2.0;
        }
    }
    else
    {
        // 선형: 0 -> 1 반복 (계속 지나감)
        animatedProgress = normalizedTime;
    }
    
    // 검기처럼 직선 이동
    float2 movedUV = input.texCoord;
    
    if (polarDirection > 0.5)
    { // 오른쪽 방향
        movedUV.x = input.texCoord.x - (animatedProgress * 1.5 - 0.75);
    }
    else
    { // 왼쪽 방향
        movedUV.x = input.texCoord.x + (animatedProgress * 1.5 - 0.75);
    }
    
    // UV 범위를 벗어나면 투명하게 처리 (래핑 방지)
    if (movedUV.x < 0.0 || movedUV.x > 1.0)
    {
        // 범위를 벗어난 부분은 완전 투명한 텍스처로 처리
        return float2(-1.0, -1.0); // 무효한 UV 반환
    }
    
    return movedUV;
}

PixelOutput main(PixelInput input)
{
    PixelOutput output;
    
    //if (shouldClipPolarAngle(input))
    //{
    //    discard;
    //}
    
    float3 normal = normalize(input.normal);
    float3 viewDir = normalize(input.viewDir);
    
    // 이동된 UV 좌표로 텍스처 샘플링
    float2 movedUV = getMovedTextureUV(input);
    
    // 무효한 UV인 경우 투명하게 처리
    if (movedUV.x < 0.0)
    {
        discard;
    }
    
    float4 diffuseColor = gDiffuseTexture.Sample(gPointSampler, movedUV);
    
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