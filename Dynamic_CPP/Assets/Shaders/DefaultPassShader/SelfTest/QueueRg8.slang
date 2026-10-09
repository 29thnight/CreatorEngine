RWStructuredBuffer<uint> outputValues : register(u0);
StructuredBuffer<uint> inputValues : register(t0);
cbuffer FrameConstants : register(b0)
{
    uint frameOffset;
};

[shader("compute")]
[numthreads(64, 1, 1)]
void Produce(uint3 id : SV_DispatchThreadID)
{
    outputValues[id.x] = id.x + 11;
}

[shader("compute")]
[numthreads(64, 1, 1)]
void Transform(uint3 id : SV_DispatchThreadID)
{
    outputValues[id.x] = inputValues[id.x] * 3 + 7;
}

[shader("compute")]
[numthreads(64, 1, 1)]
void FrameProduce(uint3 id : SV_DispatchThreadID)
{
    outputValues[id.x] = inputValues[id.x] + frameOffset;
}
