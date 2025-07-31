// TrailVertex.hlsl - 개선된 트레일 정점 셰이더
cbuffer TrailConstantBuffer : register(b0)
{
    matrix g_world;
    matrix g_view;
    matrix g_projection;
    float3 g_cameraPosition;
    float g_time; // padding 대신 시간 값 활용
};

struct VertexInput
{
    float3 position : POSITION;
    float2 texcoord : TEXCOORD0;
    float4 color : COLOR;
    float3 normal : NORMAL;
};

struct VertexOutput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
    float4 color : COLOR;
    float3 worldPos : TEXCOORD1;
    float3 normal : NORMAL;
    float3 viewDir : TEXCOORD2;
    float depth : TEXCOORD3; // 깊이 기반 효과용
};

VertexOutput main(VertexInput input)
{
    VertexOutput output;
    
    // 월드-뷰-프로젝션 변환을 한번에
    float4 worldPos = mul(float4(input.position, 1.0f), g_world);
    float4 viewPos = mul(worldPos, g_view);
    output.position = mul(viewPos, g_projection);
    
    output.worldPos = worldPos.xyz;
    output.texcoord = input.texcoord;
    output.color = input.color;
    
    // 법선 변환 (스케일 고려)
    output.normal = normalize(mul(input.normal, (float3x3) g_world));
    
    // 뷰 방향
    output.viewDir = normalize(g_cameraPosition - worldPos.xyz);
    
    // 선형 깊이 (0~1)
    output.depth = output.position.z / output.position.w;
    
    return output;
}
