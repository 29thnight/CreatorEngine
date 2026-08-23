#pragma once
#include "VulkanLoader.h"

#include "../RHIFormat.h"
#include "../RHIResourceState.h"

// RHIResourceState → Vulkan 배리어 삼종 (5c-4c).
//
// ── 이 파일이 §1.1 의 근거를 실물로 확인한다 ──
//
// `RHIResourceState.h` 가 "D3D12_RESOURCE_STATES 하나가 레이아웃 + 접근
// 마스크 + 파이프라인 스테이지 **셋을 뭉쳐 놓은 것**이라, 원시 상수를 상위가
// 적는 순간 Vulkan 백엔드는 그 뭉침을 되돌릴 수 없다"고 적어 두었다.
//
// 여기가 그 되돌림이 실제로 셋으로 펼쳐지는 자리다. 예상이 맞았다 — 상위가
// `D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE` 를 적고 있었다면 이 함수를
// 쓸 자리가 없다.
//
// ── 예상이 **틀린** 자리도 하나 나왔다 ──
//
// ★ **같은 상태가 배리어의 어느 쪽이냐에 따라 다른 레이아웃이 된다.**
//   `Common` 이 그렇다: `before` 쪽이면 `UNDEFINED`(내용을 버려도 좋다는
//   뜻이고, Vulkan 은 `oldLayout` 에서만 이것을 허용한다), `after` 쪽이면
//   `GENERAL` 이다. DX12 에는 이 구분이 없다 — `COMMON` 은 어느 쪽이든
//   `COMMON` 이다.
//
//   그래서 `ToVulkan` 이 `isSource` 를 받는다. 중립 계약을 고칠 일은 아니다:
//   `RHITransition` 은 before/after 를 이미 가려서 주므로 백엔드가 그
//   위치를 안다. **한쪽에만 있는 구분이 계약을 안 건드리고 흡수된 사례**이고,
//   `RHITextureDesc::clearColor`(한쪽에만 최적화)와 같은 부류다.
//
// ── 일부 스테이지는 의도적으로 넓다 ──
//
// ★ `ShaderResource` 와 `UnorderedAccess` 를 `ALL_COMMANDS` 로 준다. 중립
//   어휘가 "무엇에 쓰는가"만 말하고 **어느 셰이더 단계가 쓰는가**는 말하지
//   않기 때문이다. 넓게 잡으면 느리지만 **틀리지는 않는다** — 반대로 좁게
//   잡고 틀리면 경합이 조용히 난다(§4 "조용히 틀리는 부류").
//
//   좁히려면 RHI usage가 shader stage를 추가로 말해야 한다. 현재 공용 패스의
//   정확성에는 넓은 mask가 충분하며, 단계 세분화는 G-2의 중립화와 분리한
//   성능 최적화로 남긴다.

/// 배리어 한쪽이 요구하는 셋.
struct VulkanBarrierState
{
    VkImageLayout         layout{ VK_IMAGE_LAYOUT_UNDEFINED };
    VkPipelineStageFlags2 stage{ VK_PIPELINE_STAGE_2_NONE };
    VkAccessFlags2        access{ VK_ACCESS_2_NONE };
};

/// `isSource` 는 배리어의 `before` 쪽인가다 (위 ★ 참고).
inline VulkanBarrierState ToVulkan(RHIResourceState state, bool isSource)
{
    switch (state)
    {
    case RHIResourceState::RenderTarget:
        return { VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                 VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                 VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT };

    case RHIResourceState::DepthWrite:
        return { VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                 VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                 VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT };

    case RHIResourceState::DepthRead:
        return { VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
                 VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                 VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT };

    // ★ 깊이를 DSV 로 걸어 두고 같은 프레임에 셰이더로도 읽는다. Vulkan 은
    //   이 조합이 **레이아웃 하나로 표현된다** — READ_ONLY_OPTIMAL 이면
    //   샘플링과 깊이 테스트가 함께 된다. DX12 가 읽기 전용 DSV 플래그를
    //   따로 요구하는 것과 달리 여기서는 플래그가 아니라 레이아웃이다.
    case RHIResourceState::DepthReadShaderResource:
        return { VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
                 VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT
                     | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                 VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_SHADER_READ_BIT };

    case RHIResourceState::ShaderResource:
        return { VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                 VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                 VK_ACCESS_2_SHADER_READ_BIT };

    // ★ 여기만 스테이지를 좁힌다. 중립 어휘가 **이것 하나는 단계를 말하기
    //   때문이다** — `PixelShaderResource` 는 "픽셀 셰이더만 읽는다"이고,
    //   그 어휘가 있는 이유가 정확히 "Vulkan 에서도 갈린다"였다.
    //   어휘가 말해 주는 만큼만 좁아진다는 것이 여기서 눈에 보인다.
    case RHIResourceState::PixelShaderResource:
        return { VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                 VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                 VK_ACCESS_2_SHADER_READ_BIT };

    case RHIResourceState::UnorderedAccess:
        return { VK_IMAGE_LAYOUT_GENERAL,
                 VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                 VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT };

    case RHIResourceState::CopySource:
        return { VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                 VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT };

    case RHIResourceState::CopyDest:
        return { VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                 VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT };

    case RHIResourceState::Common:
    default:
        // 위 ★ — 어느 쪽이냐가 답을 바꾸는 유일한 상태다.
        return isSource
            ? VulkanBarrierState{ VK_IMAGE_LAYOUT_UNDEFINED,
                                  VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_NONE }
            : VulkanBarrierState{ VK_IMAGE_LAYOUT_GENERAL,
                                  VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_NONE };
    }
}

/// 포맷이 정하는 aspect. DX12 는 이것을 물을 자리가 없다 — 서브리소스 번호가
/// plane 을 포함하므로 계산이 숨어 있다.
inline VkImageAspectFlags AspectOf(RHIFormat format)
{
    switch (format)
    {
    case RHIFormat::D16Unorm:
    case RHIFormat::D32Float:
        return VK_IMAGE_ASPECT_DEPTH_BIT;

    case RHIFormat::D24UnormS8Uint:
    case RHIFormat::D32FloatS8Uint:
        return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;

    default:
        return VK_IMAGE_ASPECT_COLOR_BIT;
    }
}

inline bool IsDepthFormat(RHIFormat format)
{
    return 0 != (AspectOf(format) & VK_IMAGE_ASPECT_DEPTH_BIT);
}

