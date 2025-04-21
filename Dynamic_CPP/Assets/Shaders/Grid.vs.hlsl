// 상수 버퍼: 카메라의 뷰-프로젝션 행렬을 전달합니다.
cbuffer GridConstantBuffer : register(b0)
{
    matrix world;
    matrix view;
    matrix projection;
}

cbuffer cameraPos : register(b1)
{
    float4 cameraPos;
}

// 입력 정점 구조체: 간단히 월드 좌표상의 위치만 전달
struct VS_INPUT
{
    float3 pos : POSITION;
};

// 출력 구조체: 클립 좌표와 함께 월드 좌표를 TEXCOORD0로 전달합니다.
struct VS_OUTPUT
{
    float4 pos : SV_POSITION;
    float3 worldPos : TEXCOORD0;
};

// Vertex Shader: 입력 위치를 월드 좌표로 그대로 사용하고, viewProj를 곱해 클립 좌표로 변환합니다.
VS_OUTPUT main(VS_INPUT input)
{
    VS_OUTPUT output;
    
    int3 cPos = cameraPos;
    cPos.y = 0;
    
    // 입력 정점을 월드 공간으로 변환
    float4 worldPos = mul(world, float4(input.pos + cPos.xyz, 1.0));
    output.worldPos = worldPos.xyz;
    
    // 월드 좌표를 뷰, 프로젝션을 거쳐 클립 공간으로 변환
    float4 viewPos = mul(view, worldPos);
    output.pos = mul(projection, viewPos);
    
    return output;
}