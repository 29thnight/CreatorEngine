// ComputeShader.hlsl
Texture2D<float4> InputTexture : register(t0); // 입력 텍스처
RWTexture2D<float4> OutputTexture : register(u0); // 출력 텍스처

[numthreads(16, 16, 1)] // 워크그룹 크기
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    float4 color = InputTexture[dispatchThreadID.xy];
    color.rgb *= 1.3f; // 밝기 조정
    OutputTexture[dispatchThreadID.xy] = saturate(color); // 색상 클램프
}
