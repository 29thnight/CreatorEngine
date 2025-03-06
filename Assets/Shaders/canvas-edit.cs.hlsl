// ComputeShader.hlsl
Texture2D<float4> InputTexture : register(t0); // �Է� �ؽ�ó
RWTexture2D<float4> OutputTexture : register(u0); // ��� �ؽ�ó

[numthreads(16, 16, 1)] // ��ũ�׷� ũ��
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    float4 color = InputTexture[dispatchThreadID.xy];
    color.rgb *= 1.3f; // ��� ����
    OutputTexture[dispatchThreadID.xy] = saturate(color); // ���� Ŭ����
}
