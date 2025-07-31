// TrailVertex.hlsl - 트레일 정점 셰이더

// 상수 버퍼
cbuffer TrailConstantBuffer : register(b0)
{
    matrix g_world;
    matrix g_view;
    matrix g_projection;
    float3 g_cameraPosition;
    float g_padding;
};

// 입력 구조체 (TrailVertex와 일치)
struct VertexInput
{
    float3 position : POSITION;
    float2 texcoord : TEXCOORD0;
    float4 color : COLOR;
    float3 normal : NORMAL;
};

// 출력 구조체
struct VertexOutput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
    float4 color : COLOR;
    float3 worldPos : TEXCOORD1;
    float3 normal : NORMAL;
    float3 viewDir : TEXCOORD2;
};

VertexOutput main(VertexInput input)
{
    VertexOutput output;
    
    // 월드 공간으로 변환
    float4 worldPos = mul(float4(input.position, 1.0f), g_world);
    output.worldPos = worldPos.xyz;
    
    // 뷰 공간으로 변환
    float4 viewPos = mul(worldPos, g_view);
    
    // 프로젝션 공간으로 변환
    output.position = mul(viewPos, g_projection);
    
    // 텍스처 좌표 전달
    output.texcoord = input.texcoord;
    
    // 색상 전달
    output.color = input.color;
    
    // 법선을 월드 공간으로 변환
    output.normal = normalize(mul(input.normal, (float3x3) g_world));
    
    // 뷰 방향 계산 (카메라에서 정점으로)
    output.viewDir = normalize(g_cameraPosition - worldPos.xyz);
    
    return output;
}