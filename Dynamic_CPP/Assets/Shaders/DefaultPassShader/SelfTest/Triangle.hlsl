struct VSOut { float4 pos : SV_POSITION; float4 color : COLOR; };

VSOut VSMain(uint id : SV_VertexID)
{
    // 정점 버퍼 없이 SV_VertexID로 삼각형 — 입력 조립 검증은 이후 슬라이스에서.
    float2 positions[3] = { float2(0.0f, 0.6f), float2(0.6f, -0.6f), float2(-0.6f, -0.6f) };
    float4 colors[3] = {
        float4(1, 0.25f, 0.25f, 1), float4(0.25f, 1, 0.25f, 1), float4(0.25f, 0.5f, 1, 1) };

    VSOut output;
    output.pos = float4(positions[id], 0, 1);
    output.color = colors[id];
    return output;
}

float4 PSMain(VSOut input) : SV_TARGET
{
    return input.color;
}

// ── 텍스처 블릿(완료 기준 후반부) ──
Texture2D    gTexture : register(t0);
SamplerState gSampler : register(s0);

struct VSQuadOut { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; };

VSQuadOut VSQuad(uint id : SV_VertexID)
{
    // 좌하단 사분면에 트라이앵글 스트립 쿼드. 삼각형과 겹치지 않는 위치라
    // 픽셀 검증이 서로를 오염시키지 않는다.
    float2 corners[4] = {
        float2(-0.9f, -0.3f), float2(-0.3f, -0.3f),
        float2(-0.9f, -0.9f), float2(-0.3f, -0.9f) };
    float2 uvs[4] = { float2(0, 0), float2(1, 0), float2(0, 1), float2(1, 1) };

    VSQuadOut output;
    output.pos = float4(corners[id], 0, 1);
    output.uv = uvs[id];
    return output;
}

float4 PSQuad(VSQuadOut input) : SV_TARGET
{
    return gTexture.Sample(gSampler, input.uv);
}

// 컴퓨트 PSO 검증용 — 내용은 중요하지 않고, 컴퓨트 경로가 캐시를 타는지가 목적.
RWTexture2D<float4> gOutput : register(u0);

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    gOutput[id.xy] = float4(id.x / 64.0f, id.y / 64.0f, 0, 1);
}
