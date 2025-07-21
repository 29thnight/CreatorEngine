// MeshParticleClippingPS.hlsl - 실제 메쉬 바운딩 박스 기반 클리핑 + Polar Angle 클리핑
cbuffer ClippingParams : register(b1)
{
    float clippingProgress;
    float3 clippingAxis;
    
    float clippingEnabled;
    float3 meshBoundingMin;
    float3 meshBoundingMax;
    float pad1;
};

cbuffer PolarClippingParams : register(b2)
{
    float polarClippingEnabled; // 극좌표 클리핑 활성화 여부
    float polarAngleProgress; // 0~1: 각도 진행도
    float polarStartAngle; // 시작 각도 (라디안)
    float polarDirection; // 1: 시계방향, -1: 반시계방향
    
    float3 polarCenter; // 극좌표 중심점 (월드 좌표)
    float pad2;
    
    float3 polarUpAxis; // 극좌표 위쪽 축 (일반적으로 Y축)
    float pad3;
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

// 각도를 0~2π 범위로 정규화하는 함수
float normalizeAngle(float angle)
{
    const float TWO_PI = 6.28318530718;
    angle = fmod(angle, TWO_PI);
    if (angle < 0.0)
        angle += TWO_PI;
    return angle;
}

// Polar angle 클리핑 계산 함수
bool shouldClipPolarAngle(float3 worldPos)
{
    if (polarClippingEnabled < 0.5)
        return false;
    
    // 중심점으로부터의 벡터 계산
    float3 toPixel = worldPos - polarCenter;
    
    // Up 축을 기준으로 한 평면에서의 각도 계산
    float3 upAxis = normalize(polarUpAxis);
    
    // Up 축에 수직인 평면으로 투영
    float3 projectedVector = toPixel - dot(toPixel, upAxis) * upAxis;
    
    // 투영된 벡터의 길이가 너무 작으면 클리핑하지 않음
    float projectedLength = length(projectedVector);
    if (projectedLength < 0.001)
        return false;
    
    projectedVector = normalize(projectedVector);
    
    // 기준 벡터 설정 (X축 또는 적절한 기준)
    float3 referenceVector = float3(1.0, 0.0, 0.0);
    
    // 기준 벡터가 Up 축과 평행하면 다른 축 사용
    if (abs(dot(referenceVector, upAxis)) > 0.9)
        referenceVector = float3(0.0, 0.0, 1.0);
    
    // 기준 벡터를 같은 평면으로 투영
    referenceVector = referenceVector - dot(referenceVector, upAxis) * upAxis;
    referenceVector = normalize(referenceVector);
    
    // 각도 계산
    float dotProduct = dot(projectedVector, referenceVector);
    dotProduct = clamp(dotProduct, -1.0, 1.0);
    float angle = acos(dotProduct);
    
    // 방향 결정을 위한 외적 계산
    float3 crossProduct = cross(referenceVector, projectedVector);
    float direction = dot(crossProduct, upAxis);
    
    if (direction < 0.0)
        angle = 6.28318530718 - angle; // 2π - angle
    
    // 시작 각도 적용
    angle = normalizeAngle(angle - polarStartAngle);
    
    // 진행도에 따른 클리핑 각도 계산
    float clipAngle = polarAngleProgress * 6.28318530718; // 2π
    
    // 방향에 따른 클리핑 판정
    if (polarDirection > 0.0) // 시계방향
    {
        return angle > clipAngle;
    }
    else // 반시계방향
    {
        return angle < (6.28318530718 - clipAngle);
    }
}

PixelOutput main(PixelInput input)
{
    PixelOutput output;
    
    // 기존 바운딩 박스 클리핑
    float3 boundingSize = meshBoundingMax - meshBoundingMin;
    boundingSize = max(boundingSize, float3(0.001, 0.001, 0.001));
    
    float3 clippingPos = (input.localPos - meshBoundingMin) / boundingSize;
    clippingPos = saturate(clippingPos);
    
    float3 absAxis = abs(clippingAxis);
    float axisProgress = dot(clippingPos, absAxis);
    
    bool axisIsNegative = any(clippingAxis < 0.0);
    if (axisIsNegative)
    {
        axisProgress = 1.0 - axisProgress;
    }
    
    float threshold = clippingProgress >= 0.0 ? clippingProgress : (1.0 + clippingProgress);
    bool reverseDirection = clippingProgress < 0.0;
    
    // 기존 축 기반 클리핑
    if (clippingEnabled > 0.5)
    {
        if (reverseDirection)
        {
            clip(threshold - axisProgress);
        }
        else
        {
            clip(axisProgress - threshold);
        }
    }
    
    // Polar angle 클리핑
    if (shouldClipPolarAngle(input.worldPos))
    {
        discard;
    }
    
    // 노말 벡터 정규화
    float3 normal = normalize(input.normal);
    float3 viewDir = normalize(input.viewDir);
    
    // 텍스처 샘플링
    float4 diffuseColor = gDiffuseTexture.Sample(gLinearSampler, input.texCoord);
    
    // 알파 테스트
    if (diffuseColor.a < 0.1)
        discard;
    
    if (input.alpha <= 0.01)
        discard;
    
    float3 finalColor;
    
    if (input.renderMode == 0) // Emissive 모드
    {
        finalColor = input.color.rgb * diffuseColor.rgb;
    }
    else // Lit (PBR) 모드
    {
        float3 lightDir = normalize(float3(0.5, 1.0, 0.3));
        float NdotL = max(0.0, dot(normal, lightDir));
        
        // 라이팅 계산
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