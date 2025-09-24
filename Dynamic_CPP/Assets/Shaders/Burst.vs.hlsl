//// MeshParticle.hlsl - 자동 바운딩 박스 계산
//struct MeshParticleData
//{
//    float3 position;
//    float pad1;
//    float3 velocity;
//    float pad2;
//    float3 acceleration;
//    float pad3;
//    float3 rotation;
//    float pad4;
//    float3 rotationSpeed;
//    float pad5;
//    float3 scale;
//    float pad6;
//    float age;
//    float lifeTime;
//    uint isActive;
//    uint renderMode;
//    float4 color;
//    uint textureIndex;
//    float3 pad8;
//};

//cbuffer MeshConstantBuffer : register(b0)
//{
//    matrix gWorld;
//    matrix gView;
//    matrix gProjection;
//    float3 gCameraPosition;
//    float pad;
//}

//StructuredBuffer<MeshParticleData> gParticleData : register(t0);

//struct VertexInput
//{
//    float3 position : POSITION;
//    float3 normal : NORMAL;
//    float2 texCoord : TEXCOORD0;
//    float2 texCoord2 : TEXCOORD1;
//    float3 tangent : TANGENT;
//    float3 binormal : BINORMAL;
//    float4 blendIndices : BLENDINDICES;
//    float4 blendWeight : BLENDWEIGHT;
//    uint instanceID : SV_InstanceID;
//};

//struct VertexOutput
//{
//    float4 position : SV_POSITION;
//    float3 worldPos : WORLD_POSITION;
//    float3 particleCenter : PARTICLE_CENTER;
//    float3 localPos : LOCAL_POSITION; // 원본 로컬 위치
//    float3 particleScale : PARTICLE_SCALE;
//    float3 normal : NORMAL;
//    float2 texCoord : TEXCOORD0;
//    float4 color : COLOR;
//    float3 viewDir : VIEW_DIR;
//    float alpha : ALPHA;
//    uint renderMode : RENDER_MODE;
//    float particleAge : PARTICLE_AGE;
//    float particleLifeTime : PARTICLE_LIFETIME;
//};

//float3x3 CreateRotationMatrix(float3 rotation)
//{
//    float cx = cos(rotation.x);
//    float sx = sin(rotation.x);
//    float cy = cos(rotation.y);
//    float sy = sin(rotation.y);
//    float cz = cos(rotation.z);
//    float sz = sin(rotation.z);
    
//    float3x3 rotX = float3x3(
//        1, 0, 0,
//        0, cx, -sx,
//        0, sx, cx
//    );
    
//    float3x3 rotY = float3x3(
//        cy, 0, sy,
//        0, 1, 0,
//        -sy, 0, cy
//    );
    
//    float3x3 rotZ = float3x3(
//        cz, -sz, 0,
//        sz, cz, 0,
//        0, 0, 1
//    );
    
//    return mul(mul(rotZ, rotY), rotX);
//}

//float4 EulerToQuaternion(float3 euler)
//{
//    float3 c = cos(euler * 0.5);
//    float3 s = sin(euler * 0.5);
    
//    return float4(
//        s.x * c.y * c.z - c.x * s.y * s.z, // x
//        c.x * s.y * c.z + s.x * c.y * s.z, // y  
//        c.x * c.y * s.z - s.x * s.y * c.z, // z
//        c.x * c.y * c.z + s.x * s.y * s.z  // w
//    );
//}

//// 쿼터니언을 회전 행렬로 변환
//float3x3 QuaternionToMatrix(float4 q)
//{
//    float xx = q.x * q.x;
//    float yy = q.y * q.y;
//    float zz = q.z * q.z;
//    float xy = q.x * q.y;
//    float xz = q.x * q.z;
//    float yz = q.y * q.z;
//    float wx = q.w * q.x;
//    float wy = q.w * q.y;
//    float wz = q.w * q.z;
    
//    return float3x3(
//        1.0 - 2.0 * (yy + zz), 2.0 * (xy - wz), 2.0 * (xz + wy),
//        2.0 * (xy + wz), 1.0 - 2.0 * (xx + zz), 2.0 * (yz - wx),
//        2.0 * (xz - wy), 2.0 * (yz + wx), 1.0 - 2.0 * (xx + yy)
//    );
//}

//VertexOutput main(VertexInput input, uint instanceID : SV_InstanceID)
//{
//    VertexInput output;
    
//    MeshParticleData particle = gParticleData[instanceID];
    
//    if (particle.isActive != 1)
//    {
//        output.Position = float4(2.0, 2.0, 2.0, 0.0);
//        output.TexCoord = float2(0, 0);
//        output.TexIndex = 0;
//        output.Color = float4(0, 0, 0, 0);
//        output.Age = 0.0;
//        return output;
//    }
    
//    // 임펄스 방향이 pad3에 저장되어 있다면 사용
//    float3 targetRotation = particle.rotation;
//    if (length(particle.pad3) > 0.001)
//    {
//        float3 impulseDirection = normalize(particle.pad3);
        
//        // 방향 벡터를 오일러 각도로 변환
//        targetRotation = float3(
//            atan2(impulseDirection.y, length(impulseDirection.xz)), // pitch
//            atan2(impulseDirection.x, impulseDirection.z), // yaw  
//            0.0 // roll
//        );
//    }
//    else if (length(particle.velocity) > 0.001)
//    {
//        float3 normalizedVel = normalize(particle.velocity);
//        targetRotation = float3(
//            atan2(normalizedVel.y, length(normalizedVel.xz)),
//            atan2(normalizedVel.x, normalizedVel.z),
//            0.0
//        );
//    }
    
//    // MeshParticle.hlsl의 방식 사용: 쿼터니언 회전
//    float4 quaternion = EulerToQuaternion(targetRotation);
//    float3x3 rotMatrix = QuaternionToMatrix(quaternion);
    
//    // 1. 빌보드 정점에 스케일 적용
//    float3 scaledPos = float3(input.VertexPosition.x * particle.scale.x,
//                              input.VertexPosition.y * particle.scale.y,
//                              0);
    
//    // 2. 회전 적용
//    float3 rotatedPos = mul(scaledPos, rotMatrix);
    
//    // 3. 파티클 위치로 이동
//    float3 finalPos = rotatedPos + particle.position;
    
//    // 4. 월드 변환 (MeshParticle.hlsl과 동일한 방식)
//    float4 worldPosition = mul(float4(finalPos, 1.0), World);
//    float4 viewPosition = mul(worldPosition, View);
//    output.Position = mul(viewPosition, Projection);
    
//    output.TexCoord = input.TexCoord;
//    output.TexIndex = particle.textureIndex;
//    output.Color = particle.color;
//    output.Age = particle.age;
    
//    return output;
//}