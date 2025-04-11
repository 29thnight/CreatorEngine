struct PS_INPUT
{
    float4 position : SV_POSITION;
    float3 localPos : TEXCOORD0; // 월드 공간의 좌표 전달
    float3 localNor : TEXCOORD1; // 월드 공간의 노멀 좌표 전달
};

struct Output
{
    float4 positionMap : SV_TARGET0;
    float4 normalMap : SV_TARGET1;
};

Output main(PS_INPUT input) : SV_Target
{
    //return float4(0, 1, 0, 1);
    //return float4(input.localPos, 1.0f); // 월드 좌표 저장
    
    Output output;
    output.positionMap = float4(input.localPos, 1.0f); // 월드 좌표 저장
    output.normalMap = float4(input.localNor, 1.0f); // 월드 노멀 저장
    return output;
}