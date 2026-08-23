// vk.selftest 의 삼각형. HLSL 그대로 두고 dxc 로 SPIR-V 를 뽑는다.
//
// ★ 셰이더 언어를 갈지 않는다. 리포 전체가 HLSL 이고 dxc 가 SPIR-V 를 낸다 —
//   골격이 "언어까지 갈아야 하나"라는 별개의 질문을 끌고 들어오지 않게 한다.
//
// ★ 텍스처·샘플러(t0·s0)가 여기 있었다. V8-b 가 바인딩 규약(구간 시프트)의
//   자로 쓰던 것인데, 5 마무리에서 소비자(VulkanTrianglePass)와 함께 갔다 —
//   그 자의 역할은 이제 실제 패스가 한다: 그리드가 b0 을 픽셀로 재고 있고
//   (vk.grid), t·s 는 슬라이스 7 의 첫 텍스처 패스가 같은 방식으로 잰다.
//   규약 자체는 `VulkanBindingModel.h` 한 벌이 그대로 들고 있다.
//
// ★ 상수 버퍼 하나. 레지스터가 b0 이고 그리드 패스의 `RHILayout::Cbv(0)` 와
//   글자 그대로 같은 자리다 — 자가 검증이 이 값이 셰이더에 **실제로
//   닿았는지**를 픽셀로 본다. 디스크립터가 안 걸려도 삼각형은 그려지므로
//   (그리기는 상수를 안 쓴다), 안 걸리면 검은 삼각형이 나와 판정이 가른다.

cbuffer TriangleConstants : register(b0)
{
    float4 tint;
};

struct VSOut
{
    float4 position : SV_Position;
};

VSOut VSMain(uint vertexId : SV_VertexID)
{
    // 화면 중앙을 덮되 네 구석은 비운다 — 검증이 안/밖 두 점으로
    // "그려졌다"와 "지워졌다"를 가른다.
    const float2 positions[3] = {
        float2( 0.0f, -0.7f),
        float2( 0.7f,  0.7f),
        float2(-0.7f,  0.7f),
    };

    VSOut output;
    output.position = float4(positions[vertexId], 0.0f, 1.0f);
    return output;
}

float4 PSMain(VSOut input) : SV_Target0
{
    // 틴트를 그대로 낸다 — 상수가 안 닿으면 (0,0,0)이라 픽셀 하나로 갈린다.
    return float4(tint.rgb, 1.0f);
}
