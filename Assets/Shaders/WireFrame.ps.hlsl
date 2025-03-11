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
    float3 V = normalize(input.cameraDir); // 카메라 방향 정규화
    float3 surfaceNormal = input.normal; // 예제용 법선 (실제는 버텍스에서 전달)

    // 카메라 방향과 법선의 내적을 이용한 효과 적용
    float intensity = 1 - abs(dot(V, surfaceNormal));
    
    //return float4(0, intensity, 0, 1);
    return input.color;
}
