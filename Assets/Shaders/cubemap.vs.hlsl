
struct VertexIn
{
    float3 PosL : POSITION;
};

cbuffer	MVPBuffer			: register(b0)
{
	matrix MVP;
};

struct VertexOut
{
    float3 PosL : POSITION;
    float4 PosH : SV_POSITION;
};

VertexOut main(VertexIn vin)
{
    VertexOut vout;
    
    // Set z = w so that z/w = 1 (i.e., skydome always on far plane).
    vout.PosH = mul(float4(vin.PosL, 1.0f), MVP).xyww;
    
    // Use local vertex position as cubemap lookup vector.
    vout.PosL = vin.PosL;
    
    return vout;
}
