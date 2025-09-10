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
    // �⺻ �˱� ����Ʈ - ���ʿ��� ���������� ������ Ŭ����
    float progress = polarAngleProgress;
    
    // ���� �ȼ��� UV.x�� progress���� ũ�� Ŭ����
    if (input.texCoord.x > progress)
        return true;
    
    return false;
}

float2 getMovedTextureUV(PixelInput input)
{
    if (polarClippingEnabled < 0.5)
        return input.texCoord;
    
    float animationDuration = 1.0f;
    float useTriangleWave = 1.0f; // 0�� ���� 1�� �ﰢ��
    // ���̴� ���ο��� �ִϸ��̼� ���
    float currentTime = time;
    float cycleDuration = animationDuration; // �� ����
    

    
    // 0 ~ 1 ������ ����ȭ�� �ð�
    float normalizedTime = fmod(currentTime, cycleDuration) / cycleDuration;
    
    float animatedProgress;
    if (useTriangleWave > 0.5)
    {
        // �ﰢ��: 0 -> 1 -> 0 (��Ÿ���ٰ� �����)
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
        // ����: 0 -> 1 �ݺ� (��� ������)
        animatedProgress = normalizedTime;
    }
    
    // �˱�ó�� ���� �̵�
    float2 movedUV = input.texCoord;
    
    if (polarDirection > 0.5)
    { // ������ ����
        movedUV.x = input.texCoord.x - (animatedProgress * 1.5 - 0.75);
    }
    else
    { // ���� ����
        movedUV.x = input.texCoord.x + (animatedProgress * 1.5 - 0.75);
    }
    
    // UV ������ ����� �����ϰ� ó�� (���� ����)
    if (movedUV.x < 0.0 || movedUV.x > 1.0)
    {
        // ������ ��� �κ��� ���� ������ �ؽ�ó�� ó��
        return float2(-1.0, -1.0); // ��ȿ�� UV ��ȯ
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
    
    // �̵��� UV ��ǥ�� �ؽ�ó ���ø�
    float2 movedUV = getMovedTextureUV(input);
    
    // ��ȿ�� UV�� ��� �����ϰ� ó��
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