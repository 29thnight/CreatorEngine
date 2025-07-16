// MeshParticleClippingPS.hlsl - 실제 메쉬 바운딩 박스 기반 클리핑
cbuffer ClippingParams : register(b1)
{
    float clippingProgress;
    float3 clippingAxis;
    
    float clippingEnabled;
    float3 meshBoundingMin;

    float3 meshBoundingMax;
    float pad1;
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

PixelOutput main(PixelInput input)
{
    PixelOutput output;
    
    // CPU에서 전달받은 실제 메쉬 바운딩 박스 사용
    float3 boundingSize = meshBoundingMax - meshBoundingMin;
    
    // 0으로 나누기 방지
    boundingSize = max(boundingSize, float3(0.001, 0.001, 0.001));
    
    // 로컬 위치를 실제 바운딩 박스 기준으로 정규화 (0~1 범위)
    float3 clippingPos = (input.localPos - meshBoundingMin) / boundingSize;
    
    // 안전하게 0~1 범위로 클램프
    clippingPos = saturate(clippingPos);
    
    // 클리핑 축에 따른 진행도 계산
    float3 absAxis = abs(clippingAxis);
    float axisProgress = dot(clippingPos, absAxis);
    
    // 축이 음수인지 확인하여 진행 방향 결정
    bool axisIsNegative = any(clippingAxis < 0.0);
    if (axisIsNegative)
    {
        axisProgress = 1.0 - axisProgress;
    }
    
    // 클리핑 임계값 계산
    float threshold = clippingProgress >= 0.0 ? clippingProgress : (1.0 + clippingProgress);
    bool reverseDirection = clippingProgress < 0.0;
    
    // 클리핑 수행
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