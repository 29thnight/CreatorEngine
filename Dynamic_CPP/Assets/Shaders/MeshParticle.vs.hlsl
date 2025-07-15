// MeshParticle.hlsl - 자동 바운딩 박스 계산
struct MeshParticleData
{
    float3 position;
    float pad1;
    float3 velocity;
    float pad2;
    float3 acceleration;
    float pad3;
    float3 rotation;
    float pad4;
    float3 rotationSpeed;
    float pad5;
    float3 scale;
    float pad6;
    float age;
    float lifeTime;
    uint isActive;
    uint renderMode;
    float4 color;
    uint textureIndex;
    float3 pad8;
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
    float3 particleCenter : PARTICLE_CENTER;
    float3 localPos : LOCAL_POSITION; // 원본 로컬 위치
    float3 particleScale : PARTICLE_SCALE;
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
        output.position = float4(0, 0, 0, 0);
        output.worldPos = float3(0, 0, 0);
        output.particleCenter = float3(0, 0, 0);
        output.localPos = float3(0, 0, 0);
        output.particleScale = float3(1, 1, 1);
        output.normal = float3(0, 0, 0);
        output.texCoord = float2(0, 0);
        output.color = float4(0, 0, 0, 0);
        output.viewDir = float3(0, 0, 0);
        output.alpha = 0.0;
        output.renderMode = 0;
        return output;
    }
    
    // 원본 로컬 위치 저장 (스케일/회전 적용 전)
    output.localPos = input.position;
    
    // 로컬 변환: 스케일 -> 회전
    float3x3 rotMatrix = CreateRotationMatrix(particle.rotation);
    float3 scaledPos = input.position * particle.scale;
    float3 rotatedPos = mul(scaledPos, rotMatrix);
    float3 rotatedNormal = mul(input.normal, rotMatrix);
    
    // 월드 변환
    float3 worldPos = rotatedPos + particle.position;
    float4 worldPosition = mul(float4(worldPos, 1.0), gWorld);
    float4 particleCenterWorld = mul(float4(particle.position, 1.0), gWorld);
    
    // 뷰 및 프로젝션 변환
    float4 viewPosition = mul(worldPosition, gView);
    output.position = mul(viewPosition, gProjection);
    
    // 출력 설정
    output.worldPos = worldPosition.xyz;
    output.particleCenter = particleCenterWorld.xyz;
    output.particleScale = particle.scale;
    output.normal = normalize(mul(rotatedNormal, (float3x3) gWorld));
    output.texCoord = input.texCoord;
    output.color = particle.color;
    output.viewDir = normalize(gCameraPosition - worldPosition.xyz);
    output.alpha = particle.color.a;
    output.renderMode = particle.renderMode;
    
    return output;
}