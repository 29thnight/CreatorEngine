// MeshParticleClippingPS.hlsl - Polar Angle 클리핑 전용

cbuffer PolarClippingParams : register(b2)
{
    float polarClippingEnabled;
    float polarAngleProgress;
    float polarStartAngle;
    float polarDirection;
    
    float3 polarCenter;
    float pad1;
    
    float3 polarUpAxis;
    float pad2;
    
    float3 polarReferenceDir;
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

float normalizeAngle(float angle)
{
    const float TWO_PI = 6.28318530718;
    angle = fmod(angle, TWO_PI);
    if (angle < 0.0)
        angle += TWO_PI;
    return angle;
}

bool shouldClipPolarAngle(float3 worldPos)
{
    if (polarClippingEnabled < 0.5)
        return false;
    
    float3 toPixel = worldPos - polarCenter;
    float3 upAxis = normalize(polarUpAxis);
    float3 projectedVector = toPixel - dot(toPixel, upAxis) * upAxis;
    
    float projectedLength = length(projectedVector);
    if (projectedLength < 0.001)
        return false;
    
    projectedVector = normalize(projectedVector);
    
    float3 referenceVector = normalize(polarReferenceDir);
    
    referenceVector = referenceVector - dot(referenceVector, upAxis) * upAxis;
    float refLength = length(referenceVector);
    
    if (refLength < 0.1)
    {
        float3 temp = abs(upAxis.x) < 0.9 ? float3(1, 0, 0) : float3(0, 1, 0);
        referenceVector = cross(upAxis, temp);
    }
    else
    {
        referenceVector = referenceVector / refLength;
    }
    
    float dotProduct = dot(projectedVector, referenceVector);
    dotProduct = clamp(dotProduct, -1.0, 1.0);
    float angle = acos(dotProduct);
    
    float3 crossProduct = cross(referenceVector, projectedVector);
    float direction = dot(crossProduct, upAxis);
    
    if (direction < 0.0)
        angle = 6.28318530718 - angle;
    
    angle = normalizeAngle(angle - polarStartAngle);
    
    float clipAngle = polarAngleProgress * 6.28318530718;
    
    if (polarDirection > 0.0)
    {
        return angle > clipAngle;
    }
    else
    {
        return angle < (6.28318530718 - clipAngle);
    }
}

PixelOutput main(PixelInput input)
{
    PixelOutput output;
    
    // Polar angle 클리핑만 체크
    if (shouldClipPolarAngle(input.worldPos))
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