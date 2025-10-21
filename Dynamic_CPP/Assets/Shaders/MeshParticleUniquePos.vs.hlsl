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
    float particleAge : PARTICLE_AGE;
    float particleLifeTime : PARTICLE_LIFETIME;
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

float4 EulerToQuaternion(float3 euler)
{
    float3 c = cos(euler * 0.5);
    float3 s = sin(euler * 0.5);
    
    return float4(
        s.x * c.y * c.z - c.x * s.y * s.z, // x
        c.x * s.y * c.z + s.x * c.y * s.z, // y  
        c.x * c.y * s.z - s.x * s.y * c.z, // z
        c.x * c.y * c.z + s.x * s.y * s.z  // w
    );
}

// 쿼터니언을 회전 행렬로 변환
float3x3 QuaternionToMatrix(float4 q)
{
    float xx = q.x * q.x;
    float yy = q.y * q.y;
    float zz = q.z * q.z;
    float xy = q.x * q.y;
    float xz = q.x * q.z;
    float yz = q.y * q.z;
    float wx = q.w * q.x;
    float wy = q.w * q.y;
    float wz = q.w * q.z;
    
    return float3x3(
        1.0 - 2.0 * (yy + zz), 2.0 * (xy - wz), 2.0 * (xz + wy),
        2.0 * (xy + wz), 1.0 - 2.0 * (xx + zz), 2.0 * (yz - wx),
        2.0 * (xz - wy), 2.0 * (yz + wx), 1.0 - 2.0 * (xx + yy)
    );
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
        output.localPos = input.position; // 원본 로컬 위치 유지
        output.particleScale = float3(1, 1, 1);
        output.normal = input.normal; // 원본 노말 유지
        output.texCoord = input.texCoord; // 원본 텍스처 좌표 유지
        output.color = float4(0, 0, 0, 0);
        output.viewDir = float3(0, 0, 0);
        output.alpha = 0.0;
        output.renderMode = 0;
        output.particleAge = 0.0; // 추가
        return output;
    }
    
    output.localPos = input.position;
    
    // 쿼터니언 방식으로 회전 처리
    float4 quaternion = EulerToQuaternion(particle.rotation);
    float3x3 rotMatrix = QuaternionToMatrix(quaternion);
    
    // 1. 스케일 적용
    float3 scaledPos = input.position * particle.scale;
    float3 scaledNormal = normalize(input.normal);
    
    // 2. 회전 적용
    float3 rotatedPos = mul(scaledPos, rotMatrix);
    float3 rotatedNormal = mul(scaledNormal, rotMatrix);
    
    // 3. 파티클 위치로 이동
    float3 finalPos = rotatedPos + particle.pad8;
    
    // 월드 변환
    float4 worldPosition = mul(float4(finalPos, 1.0), gWorld);
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
    output.particleAge = particle.age;
    output.particleLifeTime = particle.lifeTime;
    
    return output;
}