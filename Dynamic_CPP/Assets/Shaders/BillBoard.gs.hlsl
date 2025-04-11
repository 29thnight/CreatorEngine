// 빌보드용 지오메트리 셰이더

cbuffer ModelBuffer : register(b0)
{
    matrix World;
    matrix View;
    matrix Projection;
}

struct VS_OUTPUT
{
    float4 position : SV_POSITION;
    float2 size : TEXCOORD0;
    float3 worldPos : TEXCOORD1;
    uint id : TEXCOORD2;
    float4 color : COLOR0;
};

struct GSOutput
{
    float4 position : SV_POSITION;
    float2 texCoord : TEXCOORD0;
    float depth : TEXCOORD1;
    //float distToCamera : TEXCOORD2;
    float4 color : COLOR0;
};

[maxvertexcount(6)]
void main(point VS_OUTPUT input[1], inout TriangleStream<GSOutput> outputStream)
{
    // 빌보드의 중심 위치 (월드 공간)
    float3 worldPos = input[0].worldPos;
    
    // 카메라 위치 계산 (View 행렬의 역행렬의 마지막 행)
    float3 cameraPos = float3(
        -dot(View[0].xyz, View[3].xyz),
        -dot(View[1].xyz, View[3].xyz),
        -dot(View[2].xyz, View[3].xyz)
    );
    
    // 카메라 기준 좌표계 설정
    float3 up = float3(0, 1, 0);
    float3 look = normalize(worldPos - cameraPos);
    float3 right = normalize(cross(up, look));
    up = normalize(cross(look, right));
    
    // 빌보드 사이즈
    float2 halfSize = input[0].size * 0.5f;
    
    // 사각형의 네 꼭지점 생성 (월드 공간)
    float3 corners[4];
    corners[0] = worldPos - right * halfSize.x - up * halfSize.y; // 좌하단
    corners[1] = worldPos - right * halfSize.x + up * halfSize.y; // 좌상단
    corners[2] = worldPos + right * halfSize.x - up * halfSize.y; // 우하단
    corners[3] = worldPos + right * halfSize.x + up * halfSize.y; // 우상단
    
    // 텍스처 좌표
    float2 texCoords[4];
    texCoords[0] = float2(0, 1); // 좌하단
    texCoords[1] = float2(0, 0); // 좌상단
    texCoords[2] = float2(1, 1); // 우하단
    texCoords[3] = float2(1, 0); // 우상단
    
    // 깊이값 조정을 위한 오프셋 - 값 증가
    float depthOffset = 0.0005f;
    
    // 두 개의 삼각형으로 사각형 생성
    GSOutput output;
    
    // 첫 번째 삼각형 (0-1-2)
    // 정점 처리 함수
    [unroll]
    for (int i = 0; i < 3; i++)
    {
        int idx = i == 2 ? 2 : i; // 0, 1, 2 순서
        
        float4 viewPos = mul(float4(corners[idx], 1.0f), View);
        float4 projPos = mul(viewPos, Projection);
        
        // 정규화된 깊이 계산 및 오프셋 적용
        float normalizedDepth = projPos.z / projPos.w;
        normalizedDepth += depthOffset; // 깊이 증가 (더 멀리 보임)
        
        // 새 깊이 적용
        projPos.z = projPos.w * normalizedDepth;
        
        output.position = projPos;
        output.depth = normalizedDepth;
        output.texCoord = texCoords[idx];
        output.color = input[0].color;
        outputStream.Append(output);
    }
    
    outputStream.RestartStrip();
    
    // 두 번째 삼각형 (1-3-2)
    int indices[3] = { 1, 3, 2 }; // 1, 3, 2 순서
    
    [unroll]
    for (int i = 0; i < 3; i++)
    {
        float4 viewPos = mul(float4(corners[indices[i]], 1.0f), View);
        float4 projPos = mul(viewPos, Projection);
        
        // 정규화된 깊이 계산 및 오프셋 적용
        float normalizedDepth = projPos.z / projPos.w;
        normalizedDepth += depthOffset; // 깊이 증가 (더 멀리 보임)
        
        // 새 깊이 적용
        projPos.z = projPos.w * normalizedDepth;
        
        output.position = projPos;
        output.depth = normalizedDepth;
        output.texCoord = texCoords[indices[i]];
        output.color = input[0].color;
        outputStream.Append(output);
    }
    
    outputStream.RestartStrip();
}