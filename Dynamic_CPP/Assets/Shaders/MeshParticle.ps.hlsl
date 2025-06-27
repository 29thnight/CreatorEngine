// MeshParticlePS.hlsl - 3D 메시 파티클 픽셀 셰이더

struct PixelInput
{
    float4 position : SV_POSITION;
    float3 worldPos : WORLD_POSITION;
    float3 normal : NORMAL;
    float2 texCoord : TEXCOORD0;
    float4 color : COLOR;
    float3 viewDir : VIEW_DIR;
    float alpha : ALPHA;
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
    
    float3 normal = normalize(input.normal);
    float3 viewDir = normalize(input.viewDir);
    
    float4 diffuseColor = gDiffuseTexture.Sample(gLinearSampler, input.texCoord);
    
     // 텍스처의 알파값이 임계값 이하면 픽셀 버리기
    if (diffuseColor.a < 0.1)
        discard;
    
    // 파티클 알파값도 체크
    if (input.alpha <= 0.01)
        discard;
    
    float3 lightDir = normalize(float3(0.5, 1.0, 0.3));
    float NdotL = max(0.0, dot(normal, lightDir));
    
    float3 ambient = float3(0.3, 0.3, 0.3);
    float3 diffuse = float3(0.7, 0.7, 0.7) * NdotL;
    
    float3 reflectDir = reflect(-lightDir, normal);
    float spec = pow(max(dot(viewDir, reflectDir), 0.0), 32.0);
    float3 specular = float3(0.2, 0.2, 0.2) * spec;
    
    float3 lighting = ambient + diffuse + specular;
    
    float3 finalColor = input.color.rgb * diffuseColor.rgb * lighting;
    
    float fresnel = 1.0 - abs(dot(normal, viewDir));
    fresnel = pow(fresnel, 2.0);
    
    float finalAlpha = input.alpha * diffuseColor.a * (1.0 + fresnel * 0.3);
    finalAlpha = saturate(finalAlpha);
    
    output.color = float4(finalColor, finalAlpha);
    
    return output;
}