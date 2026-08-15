#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include "VulkanLoader.h"

#include <vector>
#include <cstdint>
#include <mutex>

// 프레임 렌더 타깃 표 (5c-4c).
//
// ── 무엇을 대신하는가 ──
//
// `RHIRenderTargetBinding::backend` 가 드는 불투명 값의 Vulkan 쪽 뜻이다.
// DX12 는 그 자리에 RTV·DSV 힙 인덱스 둘을 담고(`DX12PackTargets`), Vulkan 은
// 이 표의 슬롯 번호를 담는다. **상위는 어느 쪽도 모른다** — 5b 가 계약을
// 불투명 값으로 만든 것이 여기서 값을 한다.
//
// ── 수명이 프레임인 이유 ──
//
// 계약이 "수명은 이 프레임이다"라고 적어 두었고, DX12 는 프레임 디스크립터
// 링이 그것을 강제한다. 이쪽은 링이 없으므로 표가 `BeginFrame` 에서 비워진다.
//
// ★ **여기서 두 API 의 비용이 갈린다.** DX12 는 매 프레임 디스크립터를 다시
//   만드는 것이 정상 비용이다(힙에 32비트 쓰기). Vulkan 의 `VkImageView` 는
//   **객체 생성**이라 같은 모델을 그대로 옮기면 프레임마다 vkCreateImageView
//   가 쌓인다.
//
//   그래서 **기본 뷰는 표가 만들지 않는다** — `VulkanResourceTable` 의 칸이
//   이미 든 것을 빌려 쓴다(5c-4a 가 칸에 뷰를 함께 둔 이유가 이것이다).
//   부분 뷰(밉·슬라이스를 갈라 보는 것)만 만들고, 만든 것은 표가 소유해
//   `Reset` 에서 놓는다.
//
//   ★ 부분 뷰가 프레임마다 다시 만들어지는 것은 아직 남는다. 캐시할지는
//     **그것을 실제로 부르는 소비자**(그림자 캐스케이드 — 슬라이스 7)가
//     설 때 정한다. 지금 캐시를 만들면 키를 무엇으로 잡을지를 소비자 없이
//     정하게 된다.

/// 렌더 타깃 묶음 — Vulkan. **백엔드 안쪽의 실물**이다.
///
/// ★ `VulkanEncoder.h` 에 있었다. 5c-4c 에서 이리로 왔다 — 만드는 쪽(표)과
///   푸는 쪽(인코더)이 둘 다 이것을 알아야 하는데, 인코더 헤더에 두면
///   표가 인코더를 물고 인코더가 표를 무는 순환이 된다.
///
/// ★ 여기 "`RHIRenderTargetBinding` 은 프레임 힙 인덱스라 Vulkan 에 그
///   모델이 없다 · 그래서 인덱스 모델을 버리고 **뷰 목록**이 돼야 한다"고
///   적혀 있었다. **절반만 맞았다(5b 정정).**
///
///   맞은 것: 동적 렌더링은 이미지 뷰·포맷·크기를 커맨드에 직접 받으므로
///   Vulkan 이 드는 것은 이 구조체다.
///
///   틀린 것: 그래서 **계약**도 뷰 목록이어야 한다는 결론. 실측하면 패스
///   10곳이 `IsValid()` 하나만 읽는다 — 계약이 나를 것은 불투명 값 하나와
///   개수뿐이고, 그 값을 이 표의 슬롯 번호로 읽는 것이 이 파일의 몫이다.
struct VulkanRenderTargetBinding
{
    static constexpr uint32_t kMaxColors = 8;

    VkImageView colorViews[kMaxColors]{};
    uint32_t    colorCount{ 0 };

    VkImageView depthView{ VK_NULL_HANDLE };

    /// 깊이를 셰이더로도 읽으면서 묶는가. `RHIDepthTargetDesc::readOnly` 다.
    ///
    /// ★ DX12 는 DSV 에 플래그를 굽는데 Vulkan 은 **렌더링 시작에 레이아웃을
    ///   준다** — 그래서 뷰가 아니라 이 묶음이 든다.
    bool        depthReadOnly{ false };

    uint32_t width{ 0 };
    uint32_t height{ 0 };

    // ★ **포맷을 들지 않는다.** 넣을 뻔했고, 안 넣은 것이 맞다 —
    //   `VkRenderingAttachmentInfo` 에는 포맷 칸이 없다. 동적 렌더링에서
    //   타깃 포맷은 **파이프라인**이 든다(`VkPipelineRenderingCreateInfo`).
    //
    //   DX12 도 같다(PSO 의 `RTVFormats`). 즉 이것은 두 API 가 **일치하는**
    //   자리이고, 일치하는 자리를 백엔드 구조체에 중복해 두면 파이프라인이
    //   구운 값과 어긋날 자리가 생긴다.

    bool HasColor() const { return 0 != colorCount; }
    bool HasDepth() const { return VK_NULL_HANDLE != depthView; }
    bool IsValid()  const { return (HasColor() || HasDepth()) && 0 != width && 0 != height; }
};

/// 슬롯 → 묶음. 프레임마다 비운다.
class VulkanRenderTargetTable
{
public:
    /// 등록하고 슬롯을 준다.
    ///
    /// ★ **슬롯 0 이 유효한 값이다.** `RHIRenderTargetBinding` 이 `backend` 로
    ///   판정하지 않고 `colorCount`/`hasDepth` 로 판정하는 이유가 이것이고,
    ///   그 주석이 "인덱스 0 이 유효한 값이라 0 을 무효로 쓸 수 없다"고 미리
    ///   적어 둔 그대로다.
    uint64_t Add(const VulkanRenderTargetBinding& binding)
    {
        const std::lock_guard lock(m_mutex);
        m_bindings.push_back(binding);
        return static_cast<uint64_t>(m_bindings.size() - 1);
    }

    /// 범위 밖이면 무효 묶음이다 — 부르는 쪽은 `IsValid()` 하나만 검사한다.
    VulkanRenderTargetBinding Resolve(uint64_t slot) const
    {
        const std::lock_guard lock(m_mutex);
        if (slot >= m_bindings.size()) return VulkanRenderTargetBinding{};
        return m_bindings[static_cast<size_t>(slot)];
    }

    /// 표가 만든 부분 뷰를 맡긴다. `Reset` 이 놓는다.
    void Own(VkImageView view)
    {
        const std::lock_guard lock(m_mutex);
        if (VK_NULL_HANDLE != view) m_ownedViews.push_back(view);
    }

    /// 프레임 시작에 비운다.
    ///
    /// ★ 이 슬롯의 펜스를 이미 기다린 뒤라야 한다 — `BeginFrame` 이 그것을
    ///   먼저 하고 부른다. 표는 펜스를 보지 않는다(`VulkanResourceTable` 과
    ///   같은 계약).
    void Reset(VkDevice device)
    {
        const std::lock_guard lock(m_mutex);
        if (VK_NULL_HANDLE != device)
        {
            // ★ 이름을 한정한다. 진입점이 `VulkanApi` 안에 있고, 헤더에서
            //   `using namespace` 를 하면 이 헤더를 무는 모든 번역 단위에
            //   그것이 샌다 — 유니티 빌드에서 특히 조용히 번진다.
            for (VkImageView view : m_ownedViews)
            {
                VulkanApi::vkDestroyImageView(device, view, nullptr);
            }
        }
        m_ownedViews.clear();
        m_bindings.clear();
    }

    size_t OwnedViewCount() const
    {
        const std::lock_guard lock(m_mutex);
        return m_ownedViews.size();
    }

private:
    std::vector<VulkanRenderTargetBinding> m_bindings;
    std::vector<VkImageView>               m_ownedViews;
    mutable std::mutex                     m_mutex;
};

#endif
