#define PI 3.141592
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

cbuffer ImpulseScale : register(b6)
{
    float maxImpulse;
    float lerpValue;
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
    
    // lerpValue 0~1이라 가정
    float sinV = sin(saturate(lerpValue) * PI);
    float impulse = lerp(1, maxImpulse, sinV);
    matrix Scale =
    {
        { impulse, 0, 0, 0 },
        { 0, impulse, 0, 0 },
        { 0, 0, impulse, 0 },
        { 0, 0, 0, 1 }
    };
    
    Scale = mul(model, Scale);
    
    VertexShaderOutput OUT;
    OUT.wPosition = mul(Scale, float4(IN.position, 1.0f));
    matrix vp = mul(projection, view);
    OUT.position = mul(vp, OUT.wPosition);
    OUT.pos = OUT.position;
    OUT.texCoord = IN.texCoord;
    OUT.texCoord1 = IN.texCoord1;

    // assume a uniform scaling is observed
    // otherwise have have to multiply by transpose(inverse(model))
    // inverse should be calculated in the application (CPU)
    OUT.normal = normalize(mul(Scale, float4(IN.normal, 0)).xyz);
    OUT.tangent = normalize(mul(Scale, normalize(float4(IN.tangent, 0))).xyz);
    OUT.binormal = normalize(mul(Scale, normalize(float4(IN.binormal, 0))).xyz);
    return OUT;
}
