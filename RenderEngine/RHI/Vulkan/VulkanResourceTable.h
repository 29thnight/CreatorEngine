#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include <vector>

#include "VulkanLoader.h"
#include "../RHIHandle.h"
#include "../RHIFormat.h"

// 핸들 → Vulkan 리소스 표 (5c-4a).
//
// ── 왜 DX12 쪽을 그대로 베끼지 않는가 ──
//
// 규약은 같다: 슬롯+세대 불투명 정수 · 놓은 칸 재사용 · 세대가 다르면 무효.
// `DX12ResourceTable` 과 글자까지 같아도 될 부분이 많고, 실제로 그렇게 뒀다.
//
// **다른 것은 한 칸이 드는 것**이다. §7.2.1 이 V2 착수 전에 적어 둔 그대로다:
//
//   DX12    ID3D12Resource*                       하나
//   Vulkan  VkImage + VkDeviceMemory + VkImageView  셋
//
// 그래서 "포인터를 핸들로" 가 아니라 **"핸들이라야 한다"** 였다 — 포인터
// 하나로는 Vulkan 의 한 리소스를 가리킬 수가 없다. 그 예상이 여기서 실물로
// 확인된다.
//
// ── 뷰를 칸이 드는 이유 ──
//
// DX12 는 디스크립터가 힙 안에 있고 리소스와 수명이 따로다. Vulkan 은
// `VkImageView` 가 이미지에 붙은 객체라 이미지를 놓기 전에 뷰를 놓아야 한다.
// 칸이 둘 다 들면 그 순서가 한 곳에 있다 — 밖에서 지킬 규율이 아니다.
//
// ★ 지금은 칸마다 기본 뷰 하나다. 밉·슬라이스를 갈라 보는 뷰(SRV 의
//   `mostDetailedMip` 따위)는 소비자가 설 때 더한다 — 지금 미리 만들면
//   그 모양을 DX12 의 디스크립터 모델에서 베끼게 된다(§1.1).

/// 표 한 칸이 드는 이미지. 셋이 한 벌로 살고 한 벌로 죽는다.
struct VulkanImageEntry
{
    VkImage        image{ VK_NULL_HANDLE };
    VkDeviceMemory memory{ VK_NULL_HANDLE };
    VkImageView    view{ VK_NULL_HANDLE };

    // 기본 뷰는 배열 이미지도 Texture2DArray로 본다. 큐브 SRV는 같은 이미지를
    // VK_IMAGE_VIEW_TYPE_CUBE로 다시 보아야 하므로, 첫 소비 시 만든 뷰를
    // 리소스와 같은 수명으로 보관한다. 디스크립터 셋은 프레임마다 사라져도
    // 이 뷰는 그 셋이 GPU에서 읽는 동안 살아 있어야 한다.
    VkImageView    cubeView{ VK_NULL_HANDLE };
    bool           ownsCubeView{ false };
    RHIFormat      cubeViewFormat{ RHIFormat::Unknown };
    uint32_t       cubeViewBaseMip{ 0 };
    uint32_t       cubeViewMipLevels{ 0 };
    uint32_t       cubeViewFirstSlice{ 0 };

    /// 되묻기용(`DescribeTexture`). 만들 때 받은 것을 그대로 든다 —
    /// Vulkan 은 이미지에게 자기 크기를 되물을 방법을 주지 않는다.
    ///
    /// ★ DX12 는 `GetDesc()` 가 답하므로 이 필드가 없다. **되묻는 길이
    ///   백엔드마다 다르고, 그래서 `DescribeTexture` 가 계약에 있어야 했다**
    ///   (5c-1). 포인터를 돌려주는 계약이었으면 이 차이가 상위로 샜다.
    uint32_t  width{ 0 };
    uint32_t  height{ 0 };
    uint32_t  depthOrArraySize{ 1 };
    uint32_t  mipLevels{ 1 };
    RHIFormat format{ RHIFormat::Unknown };

    /// 지금 레이아웃. 배리어가 `oldLayout` 을 요구하는데 Vulkan 은 그것을
    /// 되물을 방법이 없다 — 아는 쪽이 적어 둔다(`RHITransition::before` 가
    /// 같은 이유로 before 를 받는 것과 짝이다).
    VkImageLayout layout{ VK_IMAGE_LAYOUT_UNDEFINED };

    bool IsValid() const { return VK_NULL_HANDLE != image; }
};

/// 표 한 칸이 드는 버퍼.
struct VulkanBufferEntry
{
    VkBuffer       buffer{ VK_NULL_HANDLE };
    VkDeviceMemory memory{ VK_NULL_HANDLE };
    VkDeviceSize   bytes{ 0 };

    /// 계속 매핑해 두는 경우(업로드 링). 아니면 nullptr.
    void* mapped{ nullptr };

    bool IsValid() const { return VK_NULL_HANDLE != buffer; }
};

/// 핸들 표. `DX12ResourceTable` 과 같은 규약이다.
class VulkanResourceTable
{
public:
    /// 소유하고 등록한다. 놓을 때 vkDestroy 까지 한다.
    RHITextureHandle AddImage(const VulkanImageEntry& entry)
    {
        if (!entry.IsValid()) return {};
        return RHITextureHandle{ Acquire(m_images, m_imageFree, entry, true) };
    }

    /// 소유하지 않고 등록한다 — 스왑체인 백버퍼처럼 남이 만든 것.
    ///
    /// ★ DX12 의 `AddExternalTexture` 와 같은 자리이고 이유도 같다: 표는
    ///   놓으라고 할 때 자기 칸만 비우고 남의 리소스를 죽이지 않는다.
    RHITextureHandle AddExternalImage(const VulkanImageEntry& entry)
    {
        if (!entry.IsValid()) return {};
        return RHITextureHandle{ Acquire(m_images, m_imageFree, entry, false) };
    }

    RHIBufferHandle AddBuffer(const VulkanBufferEntry& entry)
    {
        if (!entry.IsValid()) return {};
        return RHIBufferHandle{ Acquire(m_buffers, m_bufferFree, entry, true) };
    }

    RHIBufferHandle AddExternalBuffer(const VulkanBufferEntry& entry)
    {
        if (!entry.IsValid()) return {};
        return RHIBufferHandle{ Acquire(m_buffers, m_bufferFree, entry, false) };
    }

    /// 무효 핸들이면 기본값(전부 VK_NULL_HANDLE)이다 — 호출부는 `IsValid()`
    /// 하나만 검사한다.
    VulkanImageEntry  Resolve(RHITextureHandle handle) const { return ResolveIn(m_images, handle.id); }
    VulkanBufferEntry Resolve(RHIBufferHandle handle) const { return ResolveIn(m_buffers, handle.id); }

    /// 레이아웃을 적어 둔다. 전이를 기록한 쪽이 부른다.
    void SetLayout(RHITextureHandle handle, VkImageLayout layout)
    {
        Slot<VulkanImageEntry>* const slot = Find(m_images, handle.id);
        if (nullptr != slot) slot->entry.layout = layout;
    }

    /// 큐브 SRV용 뷰를 필요할 때 만든다. 이미지가 큐브 호환 배열이 아니거나
    /// 뷰 생성에 실패하면 VK_NULL_HANDLE을 돌려준다.
    VkImageView GetOrCreateCubeView(VkDevice device, RHITextureHandle handle,
        RHIFormat format, uint32_t baseMip, uint32_t mipLevels,
        uint32_t firstSlice = 0);

    /// 칸을 비우고 세대를 올린다. 소유한 칸이면 실물도 놓는다.
    ///
    /// ★ 부르는 쪽이 GPU 완료를 보장한 뒤여야 한다 — 표는 펜스를 보지 않는다
    ///   (DX12 표와 같은 계약).
    void Release(VkDevice device, RHITextureHandle handle);
    void Release(VkDevice device, RHIBufferHandle handle);

    /// 남은 것을 전부 놓는다. 디바이스 파괴 전에 한 번.
    void Shutdown(VkDevice device);

    size_t LiveImageCount()  const { return m_images.size() - m_imageFree.size(); }
    size_t LiveBufferCount() const { return m_buffers.size() - m_bufferFree.size(); }

private:
    template <typename T>
    struct Slot
    {
        T        entry{};
        uint32_t generation{ 0 };
        bool     alive{ false };
        bool     owned{ false };
    };

    template <typename T>
    static uint32_t Acquire(std::vector<Slot<T>>& slots, std::vector<uint32_t>& freeList,
        const T& entry, bool owned)
    {
        uint32_t slot = 0;
        if (!freeList.empty())
        {
            slot = freeList.back();
            freeList.pop_back();
        }
        else
        {
            if (slots.size() >= RHIHandleBits::kMaxSlots) return 0;   // 조용히 겹치지 않는다
            slot = static_cast<uint32_t>(slots.size());
            slots.emplace_back();
        }

        Slot<T>& target = slots[slot];
        target.entry = entry;
        target.alive = true;
        target.owned = owned;
        return RHIHandleBits::Encode(slot, target.generation);
    }

    template <typename T>
    static T ResolveIn(const std::vector<Slot<T>>& slots, uint32_t id)
    {
        if (0 == id) return T{};
        const uint32_t slot = RHIHandleBits::SlotOf(id);
        if (slot >= slots.size()) return T{};

        const Slot<T>& target = slots[slot];
        // 세대가 다르면 이 핸들은 이미 놓인 것이다 — 재사용된 남의 리소스를
        // 돌려주는 것이 이 검사가 막는 사고다.
        if (!target.alive || target.generation != RHIHandleBits::GenerationOf(id)) return T{};
        return target.entry;
    }

    template <typename T>
    static Slot<T>* Find(std::vector<Slot<T>>& slots, uint32_t id)
    {
        if (0 == id) return nullptr;
        const uint32_t slot = RHIHandleBits::SlotOf(id);
        if (slot >= slots.size()) return nullptr;

        Slot<T>& target = slots[slot];
        if (!target.alive || target.generation != RHIHandleBits::GenerationOf(id)) return nullptr;
        return &target;
    }

    static void DestroyImage(VkDevice device, VulkanImageEntry& entry);
    static void DestroyOwnedCubeView(VkDevice device, VulkanImageEntry& entry);
    static void DestroyBuffer(VkDevice device, VulkanBufferEntry& entry);

    std::vector<Slot<VulkanImageEntry>>  m_images;
    std::vector<Slot<VulkanBufferEntry>> m_buffers;
    std::vector<uint32_t> m_imageFree;
    std::vector<uint32_t> m_bufferFree;
};

#endif
