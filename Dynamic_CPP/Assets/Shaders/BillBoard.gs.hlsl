// ������� ������Ʈ�� ���̴�

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
    // �������� �߽� ��ġ (���� ����)
    float3 worldPos = input[0].worldPos;
    
    // ī�޶� ��ġ ��� (View ����� ������� ������ ��)
    float3 cameraPos = float3(
        -dot(View[0].xyz, View[3].xyz),
        -dot(View[1].xyz, View[3].xyz),
        -dot(View[2].xyz, View[3].xyz)
    );
    
    // ī�޶� ���� ��ǥ�� ����
    float3 up = float3(0, 1, 0);
    float3 look = normalize(worldPos - cameraPos);
    float3 right = normalize(cross(up, look));
    up = normalize(cross(look, right));
    
    // ������ ������
    float2 halfSize = input[0].size * 0.5f;
    
    // �簢���� �� ������ ���� (���� ����)
    float3 corners[4];
    corners[0] = worldPos - right * halfSize.x - up * halfSize.y; // ���ϴ�
    corners[1] = worldPos - right * halfSize.x + up * halfSize.y; // �»��
    corners[2] = worldPos + right * halfSize.x - up * halfSize.y; // ���ϴ�
    corners[3] = worldPos + right * halfSize.x + up * halfSize.y; // ����
    
    // �ؽ�ó ��ǥ
    float2 texCoords[4];
    texCoords[0] = float2(0, 1); // ���ϴ�
    texCoords[1] = float2(0, 0); // �»��
    texCoords[2] = float2(1, 1); // ���ϴ�
    texCoords[3] = float2(1, 0); // ����
    
    // ���̰� ������ ���� ������ - �� ����
    float depthOffset = 0.0005f;
    
    // �� ���� �ﰢ������ �簢�� ����
    GSOutput output;
    
    // ù ��° �ﰢ�� (0-1-2)
    // ���� ó�� �Լ�
    [unroll]
    for (int i = 0; i < 3; i++)
    {
        int idx = i == 2 ? 2 : i; // 0, 1, 2 ����
        
        float4 viewPos = mul(float4(corners[idx], 1.0f), View);
        float4 projPos = mul(viewPos, Projection);
        
        // ����ȭ�� ���� ��� �� ������ ����
        float normalizedDepth = projPos.z / projPos.w;
        normalizedDepth += depthOffset; // ���� ���� (�� �ָ� ����)
        
        // �� ���� ����
        projPos.z = projPos.w * normalizedDepth;
        
        output.position = projPos;
        output.depth = normalizedDepth;
        output.texCoord = texCoords[idx];
        output.color = input[0].color;
        outputStream.Append(output);
    }
    
    outputStream.RestartStrip();
    
    // �� ��° �ﰢ�� (1-3-2)
    int indices[3] = { 1, 3, 2 }; // 1, 3, 2 ����
    
    [unroll]
    for (int i = 0; i < 3; i++)
    {
        float4 viewPos = mul(float4(corners[indices[i]], 1.0f), View);
        float4 projPos = mul(viewPos, Projection);
        
        // ����ȭ�� ���� ��� �� ������ ����
        float normalizedDepth = projPos.z / projPos.w;
        normalizedDepth += depthOffset; // ���� ���� (�� �ָ� ����)
        
        // �� ���� ����
        projPos.z = projPos.w * normalizedDepth;
        
        output.position = projPos;
        output.depth = normalizedDepth;
        output.texCoord = texCoords[indices[i]];
        output.color = input[0].color;
        outputStream.Append(output);
    }
    
    outputStream.RestartStrip();
}