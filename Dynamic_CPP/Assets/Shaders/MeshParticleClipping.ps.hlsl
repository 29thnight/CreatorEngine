// MeshParticleClippingPS.hlsl - ���ο��� ȸ�� �簢�� Ŭ���� ó��
cbuffer PolarClippingParams : register(b2)
{
    float polarClippingEnabled;
    float polarAngleProgress;
    float polarStartAngle;
    float polarDirection;
    
    float3 polarCenter;
    float pad1; // ���� pad1 �ڸ��� time �߰�
    
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
    
    // �Ÿ� ��� ���̵� �߰�
    float distance = length(uv);
    if (distance < 0.1) // �߽� ��ó�� �׻� ���̰�
        return false;
    
    float angle = atan2(uv.y, uv.x);
    
    const float TWO_PI = 6.28318530718;
    if (angle < 0.0)
        angle += TWO_PI;
    
    angle = normalizeAngle(angle - polarStartAngle);
    
    // �ε巯�� Ŭ������ ���� �������� �߰�
    float clipAngle = polarAngleProgress * TWO_PI;
    float fadeRange = 0.3; // ���̵� ����
    
    if (polarDirection > 0.0)
    {
        if (angle > clipAngle + fadeRange)
            return true;
        else if (angle > clipAngle)
        {
            // �ε巯�� ���̵�
            float fade = (angle - clipAngle) / fadeRange;
            if (fade > frac(time * 2.0)) // �ð��� ���� ���� ȿ��
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
    
    // �ʽ´� ȣ�� �߽ɼ� ��� (�� �ڿ������� �)
    float2 center = float2(0.3, 0.5);
    float2 toPixel = uv - center;
    
    // ���� ȣ�� ���� ���� ���
    float angle = atan2(toPixel.y, toPixel.x);
    float radius = length(toPixel);
    
    // �ʽ´� ȣ�� �߽� ������ (�뷫��)
    float targetRadius = 0.3;
    float radiusThickness = 0.12; // ȣ�� �β�
    
    // ������ 0~1�� ����ȭ (�ʽ´� ������ �°�)
    float normalizedAngle = (angle + 3.14159) / 6.28318; // -��~�� 0~1��
    
    // ���൵�� ���� Ŭ����
    float clipPosition = progress;
    if (normalizedAngle > clipPosition)
        return true;
    
    // �˱� ���κ� ���̵�
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