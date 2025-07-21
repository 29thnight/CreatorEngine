// MeshParticleClippingPS.hlsl - ���� �޽� �ٿ�� �ڽ� ��� Ŭ���� + Polar Angle Ŭ����
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
    float polarClippingEnabled; // ����ǥ Ŭ���� Ȱ��ȭ ����
    float polarAngleProgress; // 0~1: ���� ���൵
    float polarStartAngle; // ���� ���� (����)
    float polarDirection; // 1: �ð����, -1: �ݽð����
    
    float3 polarCenter; // ����ǥ �߽��� (���� ��ǥ)
    float pad2;
    
    float3 polarUpAxis; // ����ǥ ���� �� (�Ϲ������� Y��)
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

// ������ 0~2�� ������ ����ȭ�ϴ� �Լ�
float normalizeAngle(float angle)
{
    const float TWO_PI = 6.28318530718;
    angle = fmod(angle, TWO_PI);
    if (angle < 0.0)
        angle += TWO_PI;
    return angle;
}

// Polar angle Ŭ���� ��� �Լ�
bool shouldClipPolarAngle(float3 worldPos)
{
    if (polarClippingEnabled < 0.5)
        return false;
    
    // �߽������κ����� ���� ���
    float3 toPixel = worldPos - polarCenter;
    
    // Up ���� �������� �� ��鿡���� ���� ���
    float3 upAxis = normalize(polarUpAxis);
    
    // Up �࿡ ������ ������� ����
    float3 projectedVector = toPixel - dot(toPixel, upAxis) * upAxis;
    
    // ������ ������ ���̰� �ʹ� ������ Ŭ�������� ����
    float projectedLength = length(projectedVector);
    if (projectedLength < 0.001)
        return false;
    
    projectedVector = normalize(projectedVector);
    
    // ���� ���� ���� (X�� �Ǵ� ������ ����)
    float3 referenceVector = float3(1.0, 0.0, 0.0);
    
    // ���� ���Ͱ� Up ��� �����ϸ� �ٸ� �� ���
    if (abs(dot(referenceVector, upAxis)) > 0.9)
        referenceVector = float3(0.0, 0.0, 1.0);
    
    // ���� ���͸� ���� ������� ����
    referenceVector = referenceVector - dot(referenceVector, upAxis) * upAxis;
    referenceVector = normalize(referenceVector);
    
    // ���� ���
    float dotProduct = dot(projectedVector, referenceVector);
    dotProduct = clamp(dotProduct, -1.0, 1.0);
    float angle = acos(dotProduct);
    
    // ���� ������ ���� ���� ���
    float3 crossProduct = cross(referenceVector, projectedVector);
    float direction = dot(crossProduct, upAxis);
    
    if (direction < 0.0)
        angle = 6.28318530718 - angle; // 2�� - angle
    
    // ���� ���� ����
    angle = normalizeAngle(angle - polarStartAngle);
    
    // ���൵�� ���� Ŭ���� ���� ���
    float clipAngle = polarAngleProgress * 6.28318530718; // 2��
    
    // ���⿡ ���� Ŭ���� ����
    if (polarDirection > 0.0) // �ð����
    {
        return angle > clipAngle;
    }
    else // �ݽð����
    {
        return angle < (6.28318530718 - clipAngle);
    }
}

PixelOutput main(PixelInput input)
{
    PixelOutput output;
    
    // ���� �ٿ�� �ڽ� Ŭ����
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
    
    // ���� �� ��� Ŭ����
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
    
    // Polar angle Ŭ����
    if (shouldClipPolarAngle(input.worldPos))
    {
        discard;
    }
    
    // �븻 ���� ����ȭ
    float3 normal = normalize(input.normal);
    float3 viewDir = normalize(input.viewDir);
    
    // �ؽ�ó ���ø�
    float4 diffuseColor = gDiffuseTexture.Sample(gLinearSampler, input.texCoord);
    
    // ���� �׽�Ʈ
    if (diffuseColor.a < 0.1)
        discard;
    
    if (input.alpha <= 0.01)
        discard;
    
    float3 finalColor;
    
    if (input.renderMode == 0) // Emissive ���
    {
        finalColor = input.color.rgb * diffuseColor.rgb;
    }
    else // Lit (PBR) ���
    {
        float3 lightDir = normalize(float3(0.5, 1.0, 0.3));
        float NdotL = max(0.0, dot(normal, lightDir));
        
        // ������ ���
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