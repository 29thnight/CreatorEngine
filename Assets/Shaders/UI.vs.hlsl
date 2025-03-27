cbuffer PerObject : register(b0)
{
    matrix model;
    float2 size;
    float2 screensize;
}

cbuffer PerFrame : register(b1)
{
    matrix view;
}

cbuffer PerApplication : register(b2)
{
    matrix projection;
}



struct UIdata
{
    float3 position : POSITION;
    float2 texCoord : TEXCOORD;
};

struct UIVSoutput
{
    float4 position : SV_POSITION;
    float2 texCoord : TEXCOORD0;
};

UIVSoutput main(UIdata IN)
{
    UIVSoutput OUT;

    float4 worldPos = mul(float4(IN.position, 1.0f), model);
    OUT.position = worldPos;
    OUT.texCoord = IN.texCoord;


    return OUT;
}