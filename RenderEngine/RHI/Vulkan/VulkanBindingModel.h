#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include <cstdint>

// HLSL 레지스터 → SPIR-V binding 매핑 규약 (V8-b).
//
// ── 왜 이 파일이 생겼는가 ──
//
// V8-a 는 레이아웃 파라미터를 이렇게 옮겼다:
//
//     binding.binding = param.shaderRegister;
//
// 그리고 그 줄에 "지금은 b 하나뿐이라 성립한다 — 성립하는 이유를 적어 둔다"
// 고 달아 두었다. §7.2.5 의 예상 8 도 "`shaderRegister` 충돌은 이번에 안
// 드러난다"고 미뤄 두었다. **V8-b 가 그 청구서다.**
//
// DX12 는 `b`·`t`·`u`·`s` 가 **각각 별개의 이름공간**이라 `b0` 과 `t0` 이
// 공존한다. SPIR-V 에는 `binding` 하나뿐이다. 삼각형에 텍스처를 물리는
// 순간 둘 다 0 이 되어 충돌하고, 검증 레이어가 잡는다.
//
// ── 정한 것: 종류마다 구간을 나눈다 ──
//
// 아래 시프트를 레지스터 번호에 더해 binding 을 만든다. 같은 규약을
// **셰이더를 굽는 쪽과 레이아웃을 만드는 쪽이 함께** 알아야 한다:
//
//   · dxc 가 `-fvk-{b,t,u,s}-shift <값> <공간>` 으로 SPIR-V 를 낸다
//   · `VulkanPipelineCache` 가 같은 값으로 `VkDescriptorSetLayoutBinding` 을 만든다
//
// ★ **두 벌이 어긋나면 조용히 틀린다.** 검증 레이어는 "셋 레이아웃에 없는
//   binding 을 셰이더가 읽는다"를 잡아 주지만, 그것은 구간이 겹쳤을 때의
//   이야기다. 겹치지 않게 어긋나면(예: 셰이더는 t→100, 레이아웃은 t→200)
//   디스크립터가 안 걸린 채로 그려지고 **삼각형은 여전히 나온다** — V8-a 가
//   음성 대조로 확인한 그 실패 모양이다.
//
//   그래서 값을 여기 한 벌만 두고, 빌드 스크립트가 이 파일에서 **읽어 간다**
//   (`scripts/build_vk_shaders.ps1`). 스크립트가 자기 숫자를 들면 그것이 곧
//   두 벌이다.
//
// ── 왜 셋(set)이 아니라 구간인가 ──
//
// 종류마다 디스크립터 셋을 따로 두는 길도 있다(b→set 0, t→set 1 …).
// 그러면 DX12 의 루트 파라미터 번호와 Vulkan 의 셋 번호가 서로 다른 것을
// 세게 되어, `SetBindings(slot, …)` 의 `slot` 이 백엔드마다 다른 뜻이 된다.
// 구간으로 나누면 **셋은 하나로 두고 레지스터 번호만 옮겨** 그 문제를 피한다.
//
// ★ 이 선택의 대가도 적어 둔다: 한 셰이더가 같은 종류의 레지스터를 128개
//   넘게 쓰면 구간이 겹친다. 실측(패스 17종)의 최대는 t 가 열몇 개다.

namespace VulkanBindingModel
{
    /// 종류별 binding 구간의 시작. 레지스터 번호를 여기에 더한다.
    ///
    /// ★ 값을 바꾸면 `scripts/build_vk_shaders.ps1` 로 셰이더를 **전부 다시
    ///   구워야 한다.** 스크립트가 이 파일을 읽으므로 값만 고치면 되지만,
    ///   굽지 않으면 이미 박혀 있는 SPIR-V 가 옛 규약을 들고 남는다.
    constexpr uint32_t kConstantBufferShift = 0;     // b
    constexpr uint32_t kShaderResourceShift = 100;   // t
    constexpr uint32_t kUnorderedAccessShift = 200;  // u
    constexpr uint32_t kSamplerShift = 300;          // s
}

#endif
