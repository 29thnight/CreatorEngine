struct PS_INPUT
{
    float4 position : SV_POSITION;
    float3 localPos : TEXCOORD0; // 월드 공간의 좌표 전달
};

float4 main(PS_INPUT input) : SV_Target
{
    //return float4(0, 1, 0, 1);
    return float4(input.localPos, 1.0f); // 월드 좌표 저장
}