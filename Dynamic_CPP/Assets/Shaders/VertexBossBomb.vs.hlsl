cbuffer PerObject : register(b0)
{
    matrix model;
}

cbuffer PerFrame : register(b1)
{
    matrix view;
}

cbuffer PerApplication : register(b2)
{
    matrix projection;
}

cbuffer BoneTransformation : register(b3)
{
    matrix BoneTransforms[50];
}

cbuffer TimeBuffer : register(b4)
{
    float totalTime;
    float deltaTime;
    uint totalFrame;
}

cbuffer WindBuffer : register(b5)
{
    float3 windDirection;
    float windStrength;
    float windSpeed;
    float waveFrequency;
}

cbuffer Param : register(b6)
{
    float lerpValue;
    float maxScale;
    float scaleFrequency;
    float rotFrequency;
}

StructuredBuffer<matrix> models : register(t0);
StructuredBuffer<matrix> BoneTransforms2 : register(t1);

struct AppData
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 texCoord : TEXCOORD0;
    float2 texCoord1 : TEXCOORD1;
    float3 tangent : TANGENT;
    float3 binormal : BINORMAL;
    float4 boneIds : BLENDINDICES;
    float4 boneWeight : BLENDWEIGHT;
};

struct VertexShaderOutput
{
    float4 position : SV_POSITION;
    float4 pos : POSITION0;
    float4 wPosition : POSITION1;
    float3 normal : NORMAL;
    float3 tangent : TANGENT;
    float3 binormal : BINORMAL;
    float2 texCoord : TEXCOORD0;
    float2 texCoord1 : TEXCOORD1;
};

VertexShaderOutput main(AppData IN)
{
    if (IN.boneWeight[0] > 0)
    {
        matrix boneTransform = IN.boneWeight[0] * BoneTransforms[IN.boneIds[0]];
        for (int i = 1; i < 4; ++i)
        {
            boneTransform += IN.boneWeight[i] * BoneTransforms[IN.boneIds[i]];
        }
        IN.position = mul(boneTransform, float4(IN.position, 1.0f));
        IN.normal = normalize(mul(boneTransform, float4(IN.normal, 0.0f)));
        IN.tangent = normalize(mul(boneTransform, float4(IN.tangent, 0.0f)));
        IN.binormal = normalize(mul(boneTransform, float4(IN.binormal, 0.0f)));
        
    }

    float zRotation = sin(lerpValue * 3.141592 * rotFrequency) * 45.f * 0.0174533;
    float scale = sin(lerpValue * 3.141592 * scaleFrequency) * 0.5 + 0.5;
    scale = lerp(1, maxScale, scale);
    
    float4x4 rotMat =
    {
        { cos(zRotation), -sin(zRotation), 0, 0 },
        { sin(zRotation), cos(zRotation), 0, 0 },
        { 0, 0, 1, 0 },
        { 0, 0, 0, 1 }
    };
    matrix scaleMat =
    {
        { scale, 0, 0, 0 },
        { 0, scale, 0, 0 },
        { 0, 0, scale, 0 },
        { 0, 0, 0, 1 }
    };
    
    float4x4 world = mul(rotMat, scaleMat);
    world = mul(model, world);
    
    VertexShaderOutput OUT;
    OUT.wPosition = mul(world, float4(IN.position, 1.0f));
    matrix vp = mul(projection, view);
    OUT.position = mul(vp, OUT.wPosition);
    OUT.pos = OUT.position;
    OUT.texCoord = IN.texCoord;
    OUT.texCoord1 = IN.texCoord1;

    // assume a uniform scaling is observed
    // otherwise have have to multiply by transpose(inverse(model))
    // inverse should be calculated in the application (CPU)
    OUT.normal = normalize(mul(model, float4(IN.normal, 0)).xyz);
    OUT.tangent = normalize(mul(model, normalize(float4(IN.tangent, 0))).xyz);
    OUT.binormal = normalize(mul(model, normalize(float4(IN.binormal, 0))).xyz);
    return OUT;
}
