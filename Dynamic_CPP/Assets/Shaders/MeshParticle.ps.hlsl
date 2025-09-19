// MeshParticlePS.hlsl - 3D 메시 파티클 픽셀 셰이더

struct PixelInput
{
    float4 position : SV_POSITION;
    float3 worldPos : WORLD_POSITION;
    float3 particleCenter : PARTICLE_CENTER;
    float3 localPos : LOCAL_POSITION; // 원본 로컬 위치
    float3 particleScale : PARTICLE_SCALE;
    float3 normal : NORMAL;
    float2 texCoord : TEXCOORD0;
    float4 color : COLOR;
    float3 viewDir : VIEW_DIR;
    float alpha : ALPHA;
    uint renderMode : RENDER_MODE;
    float particleAge : PARTICLE_AGE;
    float particleLifeTime : PARTICLE_LIFETIME;
};
struct PixelOutput
{
    float4 color : SV_Target;
};

cbuffer TimeBuffer : register(b3)
{
    float gTime;
    float3 gPadding;
};

Texture2D gDiffuseTexture : register(t0);
SamplerState gLinearSampler : register(s0);
SamplerState gPointSampler : register(s1);

PixelOutput main(PixelInput input)
{
    PixelOutput output;
    
    float4 diffuseColor = gDiffuseTexture.Sample(gLinearSampler, input.texCoord);
    
    if (diffuseColor.a < 0.1)
        discard;
    
    if (input.alpha <= 0.01)
        discard;
    
    float3 finalColor;
    
    if (input.renderMode == 0) // Emissive
    {
        finalColor = input.color.rgb * diffuseColor.rgb;
    }
    else // Lit (PBR)
    {
        float3 normal = normalize(input.normal);
        float3 viewDir = normalize(input.viewDir);
        
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