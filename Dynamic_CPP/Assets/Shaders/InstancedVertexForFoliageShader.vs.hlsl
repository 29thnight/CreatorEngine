cbuffer PerFrame : register(b1)
{
    matrix view;
}

cbuffer PerApplication : register(b2)
{
    matrix projection;
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

StructuredBuffer<matrix> models : register(t0);  

struct AppData
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 texCoord : TEXCOORD0;
    float2 texCoord1 : TEXCOORD1;
    float3 tangent : TANGENT;
    float3 binormal : BINORMAL;
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

VertexShaderOutput main(AppData IN, uint instanceID : SV_InstanceID)
{
    matrix instanceModel = models[instanceID];

    VertexShaderOutput OUT;
    OUT.wPosition = mul(instanceModel, float4(IN.position, 1.0f));
    float wind = sin(OUT.wPosition.x * waveFrequency + totalTime * windSpeed); // 0~1
    float weight = pow(OUT.wPosition.y, 2);
    wind *= windStrength * weight;
    OUT.wPosition.xyz += windDirection * wind;
    
    matrix vp = mul(projection, view);
    OUT.position = mul(vp, OUT.wPosition);
    OUT.pos = OUT.position;
    OUT.texCoord = IN.texCoord;
    OUT.texCoord1 = IN.texCoord1;

    // assume a uniform scaling is observed
    // otherwise have have to multiply by transpose(inverse(model))
    // inverse should be calculated in the application (CPU)
    OUT.normal = normalize(mul(instanceModel, float4(IN.normal, 0)).xyz);
    OUT.tangent = normalize(mul(instanceModel, normalize(float4(IN.tangent, 0))).xyz);
    OUT.binormal = normalize(mul(instanceModel, normalize(float4(IN.binormal, 0))).xyz);
    return OUT;
}
