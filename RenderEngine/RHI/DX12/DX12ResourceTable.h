#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include <vector>
#include <wrl/client.h>
#include <d3d12.h>

#include "../RHIHandle.h"
#include "DX12ResourceEntries.h"

// 핸들 → 리소스 표 (PHASE 3-1 재정의, V2-a / V2-c1).
//
// ── 무엇을 하고 무엇을 안 하는가 ──
//
// 한다: 핸들 발급, 조회, 놓기, 놓은 칸의 재사용.
// 안 한다: 수명 정책 변경. ComPtr이 하던 참조 세기를 그대로 표가 든다 —
//         패스 소유는 Shutdown까지, transient는 그래프 수명까지, 임포트는
//         소유하지 않는다.
//
// ★ 정책을 이 슬라이스에서 함께 바꾸지 않는 것이 설계다(§7.2.1 ③).
//   표현을 바꾸는 것과 정책을 바꾸는 것을 겹치면, 회귀가 났을 때 어느 쪽
//   때문인지 못 가른다. Release는 "지금까지 ComPtr이 놓이던 그 자리에서
//   표도 놓는다"일 뿐, 누가 언제 놓는가를 바꾸지 않는다.
//
// ── V2-c1에서 왜 놓기가 생겼나 ──
//
// V2-a는 놓기를 안 넣으면서 조건을 적어 뒀다: 놓는 시점이 셋뿐이고 전부
// 그 핸들을 든 쪽이 함께 죽으니 잡을 사고가 없다고. 그 전제가 V2-c에서
// 깨진다 — 그래프가 프레임·뷰마다 새로 서는데(실측) 그래프 리소스를 표에
// 넣으면 표가 60fps로 무한히 자란다. 자라는 것을 막으려면 놓아야 하고,
// 놓은 칸을 다시 쓰면 세대가 필요해진다. V2-a가 예고한 그 순간이다.
class DX12ResourceTable
{
public:
    /// 소유하고 등록한다. 표가 ComPtr을 들고 있으므로 호출부는 핸들만 남긴다.
    RHITextureHandle AddTexture(Microsoft::WRL::ComPtr<ID3D12Resource> resource)
    {
        if (nullptr == resource.Get()) return {};
        return RHITextureHandle{ Acquire(m_textures, m_textureFree, std::move(resource), nullptr) };
    }

    RHIBufferHandle AddBuffer(Microsoft::WRL::ComPtr<ID3D12Resource> resource)
    {
        if (nullptr == resource.Get()) return {};
        return RHIBufferHandle{ Acquire(m_buffers, m_bufferFree, std::move(resource), nullptr) };
    }

    /// 소유하지 않고 등록한다 — 임포트(스왑체인 백버퍼·자가 검증이 만든 텍스처).
    ///
    /// ★ 소유 여부가 표에 남는다(owned가 비었는가로). 그래도 "누가 살려
    ///   두는가"는 지금까지대로 호출부의 책임이다 — 표는 놓으라고 할 때
    ///   자기 칸만 비우고, 남의 리소스를 죽이지 않는다.
    RHITextureHandle AddExternalTexture(ID3D12Resource* resource)
    {
        if (nullptr == resource) return {};
        return RHITextureHandle{ Acquire(m_textures, m_textureFree, nullptr, resource) };
    }

    /// 〃 버퍼 판 (A-5a). 업로드 링이 자기 버퍼를 ComPtr 로 들고 있고 표는
    /// 빌려 본다 — 슬라이스가 그 핸들로 백엔드에 닿는다.
    RHIBufferHandle AddExternalBuffer(ID3D12Resource* resource)
    {
        if (nullptr == resource) return {};
        return RHIBufferHandle{ Acquire(m_buffers, m_bufferFree, nullptr, resource) };
    }

    ID3D12Resource* Resolve(RHITextureHandle handle) const { return ResolveIn(m_textures, handle.id); }
    ID3D12Resource* Resolve(RHIBufferHandle handle) const { return ResolveIn(m_buffers, handle.id); }

    /// 칸을 비우고 세대를 올린다. 이 뒤로 같은 핸들은 nullptr로 풀린다.
    void Release(RHITextureHandle handle) { ReleaseIn(m_textures, m_textureFree, handle.id); }
    void Release(RHIBufferHandle handle) { ReleaseIn(m_buffers, m_bufferFree, handle.id); }

    // ── 파이프라인 (A-1) ──
    //
    // ★ 리소스와 달리 **소유하지 않는다.** 파이프라인과 루트 시그니처는 캐시가
    //   ComPtr 로 들고 있고 표는 빌려 볼 뿐이다 — `AddExternalTexture` 와 같은
    //   부류다. 캐시가 살아 있는 동안만 유효하다는 계약도 그대로다.
    //
    // ★ 그런데 세대는 리소스와 같은 이유로 필요하다. 지금은 놓는 호출자가
    //   0 이라 세대가 돌 일이 없지만(캐시가 앱 수명이다), Vulkan 에서
    //   `VkPipeline` 은 참조 계수가 없어 언젠가 반드시 놓아야 하고 그때
    //   "이미 놓인 핸들이 남의 파이프라인으로 풀리는" 사고가 성립한다.
    //   **모델이 그것을 허용해 두는 것**이 V8-a 가 요구한 것이다(§7.2.5).

    RHIPipelineHandle AddPipeline(ID3D12PipelineState* pipeline, ID3D12RootSignature* signature)
    {
        if (nullptr == pipeline) return {};
        return RHIPipelineHandle{ AcquirePipeline(m_pipelines, m_pipelineFree,
            DX12PipelineEntry{ pipeline, signature }) };
    }

    RHIPipelineLayoutHandle AddPipelineLayout(ID3D12RootSignature* signature, uint64_t stableHash)
    {
        if (nullptr == signature) return {};
        return RHIPipelineLayoutHandle{ AcquireLayout(m_layouts, m_layoutFree,
            DX12PipelineLayoutEntry{ signature, stableHash }) };
    }

    DX12PipelineEntry Resolve(RHIPipelineHandle handle) const
    {
        return ResolveEntry(m_pipelines, handle.id);
    }

    DX12PipelineLayoutEntry Resolve(RHIPipelineLayoutHandle handle) const
    {
        return ResolveEntry(m_layouts, handle.id);
    }

    /// ★ 호출자 0 (2026-08-11). 캐시가 앱 수명이라 놓을 일이 없다. 세어 둔다 —
    ///   이 저장소는 호출자 없는 인터페이스에 두 번 데였고(`RHIEncoder` 의 ★),
    ///   적어 두지 않으면 다음 사람이 "쓰이는 것"으로 읽는다.
    ///
    ///   지우지 않는 이유: Vulkan 백엔드가 들어오면 반드시 필요해진다.
    ///   그때도 호출자가 0 이면 그때는 지우는 것이 맞다.
    void Release(RHIPipelineHandle handle) { ReleaseEntry(m_pipelines, m_pipelineFree, handle.id); }
    void Release(RHIPipelineLayoutHandle handle) { ReleaseEntry(m_layouts, m_layoutFree, handle.id); }

    void Clear()
    {
        m_textures.clear();
        m_buffers.clear();
        m_textureFree.clear();
        m_bufferFree.clear();
        m_pipelines.clear();
        m_layouts.clear();
        m_pipelineFree.clear();
        m_layoutFree.clear();
    }

    /// 살아 있는 칸 수 — 진단용. 프레임마다 늘면 누가 안 놓고 있다는 뜻이다.
    size_t LiveTextureCount() const { return m_textures.size() - m_textureFree.size(); }
    size_t LiveBufferCount() const { return m_buffers.size() - m_bufferFree.size(); }

private:
    struct Slot
    {
        Microsoft::WRL::ComPtr<ID3D12Resource> owned;      // 소유할 때만
        ID3D12Resource*                        external{ nullptr };  // 임포트일 때만
        uint32_t                               generation{ 0 };
        bool                                   alive{ false };

        ID3D12Resource* Get() const { return owned ? owned.Get() : external; }
    };

    static uint32_t Acquire(std::vector<Slot>& slots, std::vector<uint32_t>& freeList,
        Microsoft::WRL::ComPtr<ID3D12Resource> owned, ID3D12Resource* external)
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

        Slot& entry = slots[slot];
        entry.owned = std::move(owned);
        entry.external = external;
        entry.alive = true;
        return RHIHandleBits::Encode(slot, entry.generation);
    }

    static ID3D12Resource* ResolveIn(const std::vector<Slot>& slots, uint32_t id)
    {
        if (0 == id) return nullptr;
        const uint32_t slot = RHIHandleBits::SlotOf(id);
        if (slot >= slots.size()) return nullptr;

        const Slot& entry = slots[slot];
        // 세대가 다르면 이 핸들은 이미 놓인 것이다 — 재사용된 남의 리소스를
        // 돌려주는 것이 이 검사가 막는 사고다.
        if (!entry.alive || entry.generation != RHIHandleBits::GenerationOf(id)) return nullptr;
        return entry.Get();
    }

    static void ReleaseIn(std::vector<Slot>& slots, std::vector<uint32_t>& freeList, uint32_t id)
    {
        if (0 == id) return;
        const uint32_t slot = RHIHandleBits::SlotOf(id);
        if (slot >= slots.size()) return;

        Slot& entry = slots[slot];
        if (!entry.alive || entry.generation != RHIHandleBits::GenerationOf(id)) return;  // 두 번 놓기

        entry.owned.Reset();
        entry.external = nullptr;
        entry.alive = false;
        ++entry.generation;
        freeList.push_back(slot);
    }

    // ── 파이프라인 칸 (A-1) ──
    //
    // ★ 리소스 칸과 합치지 않는다. 저쪽은 소유(ComPtr)와 임포트를 가르는
    //   필드가 있는데 이쪽은 늘 빌려 보기이고, 드는 값도 짝이라 모양이 다르다.
    //   한 Slot 에 몰면 쓰지 않는 필드가 생기고 그것을 채우는 호출부가 반드시
    //   생긴다(`RHIStaticSamplerDesc` 를 힙 샘플러와 가른 것과 같은 이유).
    template <typename T>
    struct EntrySlot
    {
        T        entry{};
        uint32_t generation{ 0 };
        bool     alive{ false };
    };

    template <typename T>
    static uint32_t AcquireEntry(std::vector<EntrySlot<T>>& slots,
        std::vector<uint32_t>& freeList, const T& value)
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

        EntrySlot<T>& target = slots[slot];
        target.entry = value;
        target.alive = true;
        return RHIHandleBits::Encode(slot, target.generation);
    }

    static uint32_t AcquirePipeline(std::vector<EntrySlot<DX12PipelineEntry>>& slots,
        std::vector<uint32_t>& freeList, const DX12PipelineEntry& value)
    {
        return AcquireEntry(slots, freeList, value);
    }

    static uint32_t AcquireLayout(std::vector<EntrySlot<DX12PipelineLayoutEntry>>& slots,
        std::vector<uint32_t>& freeList, const DX12PipelineLayoutEntry& value)
    {
        return AcquireEntry(slots, freeList, value);
    }

    template <typename T>
    static T ResolveEntry(const std::vector<EntrySlot<T>>& slots, uint32_t id)
    {
        if (0 == id) return T{};
        const uint32_t slot = RHIHandleBits::SlotOf(id);
        if (slot >= slots.size()) return T{};

        const EntrySlot<T>& target = slots[slot];
        if (!target.alive || target.generation != RHIHandleBits::GenerationOf(id)) return T{};
        return target.entry;
    }

    template <typename T>
    static void ReleaseEntry(std::vector<EntrySlot<T>>& slots, std::vector<uint32_t>& freeList,
        uint32_t id)
    {
        if (0 == id) return;
        const uint32_t slot = RHIHandleBits::SlotOf(id);
        if (slot >= slots.size()) return;

        EntrySlot<T>& target = slots[slot];
        if (!target.alive || target.generation != RHIHandleBits::GenerationOf(id)) return;

        target.entry = T{};
        target.alive = false;
        ++target.generation;
        freeList.push_back(slot);
    }

    std::vector<Slot>     m_textures;
    std::vector<Slot>     m_buffers;
    std::vector<uint32_t> m_textureFree;
    std::vector<uint32_t> m_bufferFree;

    std::vector<EntrySlot<DX12PipelineEntry>>       m_pipelines;
    std::vector<EntrySlot<DX12PipelineLayoutEntry>> m_layouts;
    std::vector<uint32_t>                           m_pipelineFree;
    std::vector<uint32_t>                           m_layoutFree;
};

#endif
