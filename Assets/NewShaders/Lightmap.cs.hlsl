RWTexture2D<float4> TargetTexture : register(u0); // 타겟 텍스처 (UAV)
//Texture2D SourceTexture : register(t0); // 소스 텍스처 (SRV)
SamplerState Sampler0 : register(s0);

cbuffer CB : register(b0)
{
    float2 Offset; // 타겟 텍스처에서 그릴 위치
    float2 Size; // 복사할 영역 크기
};

[numthreads(32, 32, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    // 타겟 텍스처 좌표
    float2 targetPos = float2(DTid.xy); // 0 ~ lightmapSize

    // 범위 내 픽셀인지 체크 
    if (targetPos.x < Offset.x || targetPos.x > (Offset.x + Size.x) ||
        targetPos.y < Offset.y || targetPos.y > (Offset.y + Size.y))
        return;

    // 타겟 텍스처 좌표를 0~1로 정규화
    float2 localUV = (targetPos - (Offset)) / (Size);

    // UV 좌표를 소스 텍스처 범위에 매핑
    float2 sourceUV = localUV;
    //float2 sourceUV = UVOffset + (localUV * UVSize);

    
    
    // 소스 텍스처 샘플링
    float4 color = float4(localUV.x, localUV.y, 0, 1); //SourceTexture.SampleLevel(Sampler0, sourceUV, 0);

    // 타겟 텍스처에 기록
    TargetTexture[DTid.xy] = color;
}