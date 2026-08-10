// Vulkan 골격의 삼각형. HLSL 그대로 두고 dxc 로 SPIR-V 를 뽑는다.
//
// ★ 셰이더 언어를 갈지 않는다. 리포 전체가 HLSL 이고, SDK 의 dxc 가 SPIR-V 를
//   낸다(Windows SDK 에 딸린 dxc 는 SPIR-V CodeGen 이 꺼져 있어 안 된다).
//   골격이 "언어까지 갈아야 하나"라는 별개의 질문을 끌고 들어오지 않게 한다 —
//   그것은 V5(셰이더 컴파일 중립화)의 몫이다.
//
// ★ 정점 버퍼가 없다. SV_VertexID 로 세 점을 만든다 — 입력 레이아웃은
//   RHIEncoder 의 D3D12_VERTEX_BUFFER_VIEW 서명에 묶여 있어서(§7.2.2) 골격이
//   쓸 수 없다. 그 자리를 피해 가는 것이 아니라, 피할 수밖에 없다는 것이
//   측정 결과다.

struct VSOut
{
    float4 position : SV_Position;
    float3 color    : COLOR0;
};

VSOut VSMain(uint vertexId : SV_VertexID)
{
    // 화면 중앙을 덮되 네 구석은 비운다 — 자가 검증이 중앙/구석 두 점으로
    // "그려졌다"와 "지워졌다"를 가른다.
    const float2 positions[3] = {
        float2( 0.0f, -0.7f),
        float2( 0.7f,  0.7f),
        float2(-0.7f,  0.7f),
    };
    const float3 colors[3] = {
        float3(1.0f, 0.0f, 0.0f),
        float3(0.0f, 1.0f, 0.0f),
        float3(0.0f, 0.0f, 1.0f),
    };

    VSOut output;
    output.position = float4(positions[vertexId], 0.0f, 1.0f);
    output.color = colors[vertexId];
    return output;
}

float4 PSMain(VSOut input) : SV_Target0
{
    return float4(input.color, 1.0f);
}
