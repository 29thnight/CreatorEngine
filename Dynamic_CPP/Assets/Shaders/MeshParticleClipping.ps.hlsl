// MeshParticleClippingPS.hlsl - Ŭ���� ����� �ִ� 3D �޽� ��ƼŬ �ȼ� ���̴�

cbuffer ClippingParams : register(b1)
{
    float clippingProgress;
    float3 clippingAxis;
    float4x4 invWorldMatrix;
    float clippingEnabled;
    float3 pad;
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
    
    // ���� ��ǥ�� ���� ��ǥ�� ��ȯ
    float3 localPos = mul(float4(input.worldPos, 1.0), invWorldMatrix).xyz;
    
    // ���� �������� ����ȭ�� ��ǥ ��� (-1~1 ������ 0~1�� ��ȯ)
    float3 clippingPos = (localPos + 1.0) * 0.5;
    
    // Ŭ���� �࿡ ���� ���൵ ���
    float3 absAxis = abs(clippingAxis);
    float axisProgress = dot(clippingPos, absAxis);
    
    // ���� �������� Ȯ���Ͽ� ���� ���� ����
    bool axisIsNegative = any(clippingAxis < 0.0);
    if (axisIsNegative)
    {
        axisProgress = 1.0 - axisProgress;
    }
    
    // Ŭ���� �Ӱ谪 ���
    float threshold = clippingProgress >= 0.0 ? clippingProgress : (1.0 + clippingProgress);
    bool reverseDirection = clippingProgress < 0.0;
    
    // Ŭ���� ����
    if (reverseDirection)
    {
        clip(threshold - axisProgress);
    }
    else
    {
        clip(axisProgress - threshold);
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