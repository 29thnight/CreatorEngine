// BillboardCompute.hlsl
struct BillboardInput
{
    float3 Position;
    float Padding1;
    float2 Scale;
    uint TexIndex;
    float Padding2;
    float4 Color;
};

struct VertexOutput
{
    float4 Position;
    float2 TexCoord;
    uint TexIndex;
    float Padding;
    float4 Color;
};

cbuffer CameraConstants : register(b0)
{
    matrix View;
    matrix Projection;
    float3 CameraRight;
    float Padding;
    float3 CameraUp;
    uint BillboardCount;
};

// 빌보드 입력 데이터
StructuredBuffer<BillboardInput> InputBillboards : register(t0);

// 출력 버퍼 (변환된 정점)
RWStructuredBuffer<VertexOutput> OutputVertices : register(u0);

// 빌보드 테이블 (각 정점의 로컬 좌표와 텍스처 좌표)
static const float2 QuadPositions[4] =
{
    float2(-1, -1), // 좌하단
    float2(1, -1), // 우하단 
    float2(-1, 1), // 좌상단
    float2(1, 1) // 우상단
};

static const float2 QuadTexCoords[4] =
{
    float2(0, 1), // 좌하단
    float2(1, 1), // 우하단
    float2(0, 0), // 좌상단
    float2(1, 0) // 우상단
};

[numthreads(64, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint billboardIndex = DTid.x;
    
    // 인덱스가 범위를 벗어나면 종료
    if (billboardIndex >= BillboardCount)
        return;
    
    // 빌보드 데이터 가져오기
    BillboardInput billboard = InputBillboards[billboardIndex];
    
    // 4개의 정점 처리 (빌보드 당 쿼드)
    for (int i = 0; i < 4; i++)
    {
        // 쿼드의 로컬 좌표 가져오기
        float2 localPos = QuadPositions[i];
        
        // 빌보드 위치 계산
        float3 worldPos = billboard.Position;
        worldPos += CameraRight * localPos.x * billboard.Scale.x;
        worldPos += CameraUp * localPos.y * billboard.Scale.y;
        
        // 월드-뷰-투영 변환
        float4 clipPos = mul(mul(float4(worldPos, 1.0f), View), Projection);
        
        // 결과 저장 (빌보드 당 4개의 정점)
        uint vertexIndex = billboardIndex * 4 + i;
        
        VertexOutput vertex;
        vertex.Position = clipPos;
        vertex.TexCoord = QuadTexCoords[i];
        vertex.Padding = 0;
        vertex.TexIndex = billboard.TexIndex;
        vertex.Color = billboard.Color;
        
        OutputVertices[vertexIndex] = vertex;
    }
}