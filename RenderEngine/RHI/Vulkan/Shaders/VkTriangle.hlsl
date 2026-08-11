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
//
// ★ 상수 버퍼 하나가 있다(V8-a). 이것이 없으면 파이프라인 레이아웃이 비고,
//   빈 레이아웃은 레이아웃을 재지 않는다 — V4 가 만든 RHIPipelineLayoutDesc 가
//   Vulkan 에서 성립하는지를 아무도 모른 채로 지나간다. 레지스터는 b0 이고
//   그리드 패스의 RHILayout::Cbv(0) 와 글자 그대로 같은 자리다.

cbuffer TriangleConstants : register(b0)
{
    // 자가 검증이 이 값이 셰이더에 **실제로 닿았는지**까지 본다. 디스크립터가
    // 안 걸려도 삼각형은 그려지므로, 픽셀이 '삼각형이 있다'만 보면 이 경로는
    // 조용히 틀린 채로 통과한다(실측: 안 걸면 중앙이 (0,0,0)).
    float4 tint;
};

// ★ 텍스처와 샘플러가 V8-b 에서 들어왔다. 목적은 그림이 아니라 **A-5 의 자**다 —
//   RHIBindingTable(디스크립터 테이블)과 RHISamplerTable(샘플러)이 Vulkan 에서
//   무엇이 되는지는 소비자가 있어야 답할 수 있고, 그 소비자가 이 두 줄이다.
//
// ★ `b0` 과 `t0` 이 **둘 다 0** 이다. DX12 는 b·t·u·s 가 별개 이름공간이라
//   문제가 없지만 SPIR-V 는 binding 하나뿐이다. 그래서 굽는 쪽과 레이아웃을
//   만드는 쪽이 같은 시프트 규약을 쓴다 — `VulkanBindingModel.h`.
Texture2D    gTexture : register(t0);
SamplerState gSampler : register(s0);

struct VSOut
{
    float4 position : SV_Position;
    float3 color    : COLOR0;
    float2 uv       : TEXCOORD0;
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

    // UV 는 클립 좌표를 [0,1] 로 옮긴 것이다. 화면 위치에 따라 달라야
    // 체커보드가 여러 칸 걸쳐 나오고, 그래야 "텍스처가 단색으로 걸렸다"와
    // "제대로 걸렸다"를 픽셀로 가를 수 있다.
    const float2 uvs[3] = {
        float2(0.5f, 0.0f),
        float2(1.0f, 1.0f),
        float2(0.0f, 1.0f),
    };

    VSOut output;
    output.position = float4(positions[vertexId], 0.0f, 1.0f);
    output.color = colors[vertexId];
    output.uv = uvs[vertexId];
    return output;
}

float4 PSMain(VSOut input) : SV_Target0
{
    // ★ 텍스처는 흑백 체커보드다. 곱하면 삼각형 안이 밝은 칸과 어두운 칸으로
    //   갈리고, 자가 검증은 **두 점의 차이**로 텍스처가 닿았는지 잰다.
    //   한 점만 보면 텍스처가 단색(전부 1 이거나 전부 0)으로 걸려도 통과한다.
    const float mask = gTexture.Sample(gSampler, input.uv).r;
    return float4(input.color * tint.rgb * mask, 1.0f);
}
