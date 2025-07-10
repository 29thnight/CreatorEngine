// MeshParticleClippingPS.hlsl - Ŭ���� ����� �ִ� 3D �޽� ��ƼŬ �ȼ� ���̴�

cbuffer ClippingParams : register(b1)
{
    float clippingProgress;
    float3 clippingAxis;
    float3 boundsMin;
    float pad1;
    float3 boundsMax;
    float clippingEnabled;
};

struct PixelInput
{
    float4 position : SV_POSITION;
    float3 worldPos : WORLD_POSITION;
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
    
    float3 normalizedPos = (input.worldPos - boundsMin) / (boundsMax - boundsMin);
    float3 absAxis = abs(clippingAxis);
    float axisProgress = dot(normalizedPos, absAxis);

    // ���� ���� ���������� Ȯ��
    bool axisIsNegative = any(clippingAxis < 0.0);
    if (axisIsNegative)
    {
        axisProgress = 1.0 - axisProgress; // ���� ���� ������
    }

    float threshold = clippingProgress >= 0.0 ? clippingProgress : (1.0 + clippingProgress);
    bool reverseDirection = clippingProgress < 0.0;

    if (reverseDirection)
    {
        clip(threshold - axisProgress);
    }
    else
    {
        clip(axisProgress - threshold);
    }
    
    float3 normal = normalize(input.normal);
    float3 viewDir = normalize(input.viewDir);
    
    float4 diffuseColor = gDiffuseTexture.Sample(gLinearSampler, input.texCoord);
    
    // �ؽ�ó�� ���İ��� �Ӱ谪 ���ϸ� �ȼ� ������
    if (diffuseColor.a < 0.1)
        discard;
    
    // ��ƼŬ ���İ��� üũ
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