// MeshParticle.hlsl - 수정된 3D 메시 파티클 버텍스 셰이더
struct MeshParticleData
{
    float3 position; // 12 bytes
    float pad1; // 4 bytes -> 16 bytes total
    
    float3 velocity; // 12 bytes  
    float pad2; // 4 bytes -> 32 bytes total
    
    float3 acceleration; // 12 bytes
    float pad3; // 4 bytes -> 48 bytes total
    
    float3 rotation; // 12 bytes
    float pad4; // 4 bytes -> 64 bytes total
    
    float3 rotationSpeed; // 12 bytes
    float pad5; // 4 bytes -> 80 bytes total
    
    float3 scale; // 12 bytes
    float pad6; // 4 bytes -> 96 bytes total
    
    float age; // 4 bytes
    float lifeTime; // 4 bytes
    uint isActive; // 4 bytes
    uint renderMode; // 4 bytes -> 112 bytes total
    
    float4 color; // 16 bytes -> 128 bytes total
    
    uint textureIndex; // 4 bytes
    float3 pad8; // 12 bytes -> 144 bytes total
};

cbuffer MeshConstantBuffer : register(b0)
{
    matrix gWorld;
    matrix gView;
    matrix gProjection;
    float3 gCameraPosition;
    float pad;
}

StructuredBuffer<MeshParticleData> gParticleData : register(t0);

struct VertexInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 texCoord : TEXCOORD0;
    float2 texCoord2 : TEXCOORD1;
    float3 tangent : TANGENT;
    float3 binormal : BINORMAL;
    float4 blendIndices : BLENDINDICES;
    float4 blendWeight : BLENDWEIGHT;
    
    uint instanceID : SV_InstanceID;
};

struct VertexOutput
{
    float4 position : SV_POSITION;
    float3 worldPos : WORLD_POSITION;
    float3 normal : NORMAL;
    float2 texCoord : TEXCOORD0;
    float4 color : COLOR;
    float3 viewDir : VIEW_DIR;
    float alpha : ALPHA;
    uint renderMode : RENDER_MODE;
};

float3x3 CreateRotationMatrix(float3 rotation)
{
    float cx = cos(rotation.x);
    float sx = sin(rotation.x);
    float cy = cos(rotation.y);
    float sy = sin(rotation.y);
    float cz = cos(rotation.z);
    float sz = sin(rotation.z);
    
    float3x3 rotX = float3x3(
        1, 0, 0,
        0, cx, -sx,
        0, sx, cx
    );
    
    float3x3 rotY = float3x3(
        cy, 0, sy,
        0, 1, 0,
        -sy, 0, cy
    );
    
    float3x3 rotZ = float3x3(
        cz, -sz, 0,
        sz, cz, 0,
        0, 0, 1
    );
    
    return mul(mul(rotZ, rotY), rotX);
}

VertexOutput main(VertexInput input)
{
    VertexOutput output;
    
    MeshParticleData particle = gParticleData[input.instanceID];
    
    if (particle.isActive == 0)
    {
        // 비활성 파티클은 화면 밖으로 이동
        output.position = float4(0, 0, 0, 0);
        output.worldPos = float3(0, 0, 0);
        output.normal = float3(0, 0, 0);
        output.texCoord = float2(0, 0);
        output.color = float4(0, 0, 0, 0);
        output.viewDir = float3(0, 0, 0);
        output.alpha = 0.0;
        return output;
    }
    
    // 로컬 변환: 스케일 -> 회전
    float3x3 rotMatrix = CreateRotationMatrix(particle.rotation);
    float3 scaledPos = input.position * particle.scale;
    float3 rotatedPos = mul(scaledPos, rotMatrix);
    float3 rotatedNormal = mul(input.normal, rotMatrix);
    
    // 월드 변환: 로컬 변환된 버텍스를 파티클 위치로 이동
    float3 worldPos = rotatedPos + particle.position;
    
    // 최종 월드 매트릭스 적용 (gWorld는 전역 변환용)
    float4 worldPosition = mul(float4(worldPos, 1.0), gWorld);
    
    // 뷰 및 프로젝션 변환
    float4 viewPosition = mul(worldPosition, gView);
    output.position = mul(viewPosition, gProjection);
    
    // 출력 설정
    output.worldPos = worldPosition.xyz;
    output.normal = normalize(mul(rotatedNormal, (float3x3) gWorld));
    output.texCoord = input.texCoord;
    output.color = particle.color;
    output.viewDir = normalize(gCameraPosition - worldPosition.xyz);
    output.alpha = particle.color.a;
    output.renderMode = particle.renderMode;
    
    return output;
}