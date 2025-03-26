RWTexture2D<float4> TargetTexture : register(u0); // Ÿ�� �ؽ�ó (UAV)
//Texture2D SourceTexture : register(t0); // �ҽ� �ؽ�ó (SRV)
SamplerState Sampler0 : register(s0);

cbuffer CB : register(b0)
{
    float2 Offset; // Ÿ�� �ؽ�ó���� �׸� ��ġ
    float2 Size; // ������ ���� ũ��
};

[numthreads(32, 32, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    // Ÿ�� �ؽ�ó ��ǥ
    float2 targetPos = float2(DTid.xy); // 0 ~ lightmapSize

    // ���� �� �ȼ����� üũ 
    if (targetPos.x < Offset.x || targetPos.x > (Offset.x + Size.x) ||
        targetPos.y < Offset.y || targetPos.y > (Offset.y + Size.y))
        return;

    // Ÿ�� �ؽ�ó ��ǥ�� 0~1�� ����ȭ
    float2 localUV = (targetPos - (Offset)) / (Size);

    // UV ��ǥ�� �ҽ� �ؽ�ó ������ ����
    float2 sourceUV = localUV;
    //float2 sourceUV = UVOffset + (localUV * UVSize);

    
    
    // �ҽ� �ؽ�ó ���ø�
    float4 color = float4(localUV.x, localUV.y, 0, 1); //SourceTexture.SampleLevel(Sampler0, sourceUV, 0);

    // Ÿ�� �ؽ�ó�� ���
    TargetTexture[DTid.xy] = color;
}