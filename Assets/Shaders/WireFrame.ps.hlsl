struct PixelShaderInput
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
    float3 cameraDir : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float3 pos : TEXCOORD2;
};

float4 main(PixelShaderInput input) : SV_TARGET
{
    float3 V = normalize(input.cameraDir); // ī�޶� ���� ����ȭ
    float3 surfaceNormal = input.normal; // ������ ���� (������ ���ؽ����� ����)

    // ī�޶� ����� ������ ������ �̿��� ȿ�� ����
    float intensity = 1 - abs(dot(V, surfaceNormal));
    
    //return float4(0, intensity, 0, 1);
    return input.color;
}
