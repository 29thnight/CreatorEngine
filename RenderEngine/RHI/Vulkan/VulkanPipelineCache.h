#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include "VulkanLoader.h"

#include "../RHIFormat.h"
#include "../RHIPipelineLayout.h"
#include "../RHIPipelineState.h"
#include "../IRenderPipelineCache.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

// 파이프라인 · 레이아웃 캐시 — Vulkan (V8-a → A-1b).
//
// ── V8-a 가 세운 것, A-1 이 접은 것 ──
//
// V8-a 때 이 클래스는 **아무것도 상속하지 못했다.** 하는 일은 정확히
// `IRenderPipelineCache` · `IRenderRootSignatureCache` 였는데 반환형이
// `ID3D12PipelineState*` · `DX12RootSignatureEntry` 였기 때문이다.
//
// ★ A-1a 가 그 둘을 핸들로 갈고 A-1b 가 선언을 `RHI/` 로 옮기자 상속이 됐다.
//   **§7.2.5 가 "접히지 않으면 A 가 덜 된 것이다"라고 적어 둔 판정이 여기서
//   성립한다.**
//
// ★ 그리고 `VulkanGraphicsPipelineDesc` 가 없어졌다. V8-a 가 그것을 일부러
//   중복으로 두고 "21 필드 중 20 이 같다"를 세어 두었는데, 갈리던 하나가
//   핸들이 되면서 두 구조체가 `RHIGraphicsPipelineDesc` 하나로 접혔다.
//
// ── 표가 여기 있다 (DX12 와 갈리는 자리) ──
//
// ★ DX12 는 표를 `DX12DeviceResources` 에 뒀다. 인코더가 그것을 이미 들고
//   있었고, 그래프 밖에서 원시 커맨드 리스트를 쓰는 자리(IBL 생성기)도
//   그 인터페이스로 닿아야 했기 때문이다.
//
//   Vulkan 은 그럴 이유가 없다. 원시 경로가 없고 인코더가 캐시를 직접 받으면
//   되므로, **만드는 쪽이 표도 든다.** 같은 계약(핸들 → 짝)을 백엔드마다
//   다른 자리에 둘 수 있다는 것이 핸들이 값을 하는 방식이다 — 상위는 표가
//   어디 있는지 모른다.

/// 표 한 칸: 파이프라인과 그것이 구워진 레이아웃.
struct VulkanPipelineEntry
{
    VkPipeline       pipeline{ VK_NULL_HANDLE };
    VkPipelineLayout layout{ VK_NULL_HANDLE };

    bool IsValid() const { return VK_NULL_HANDLE != pipeline; }
};

/// 표 한 칸: 레이아웃과 그 디스크립터 셋 레이아웃.
///
/// ★ DX12 쪽 표(`DX12PipelineLayoutEntry`)는 `{signature, 안정해시}` 인데
///   이쪽은 `{layout, setLayout}` 이다. **핸들은 같은 뜻인데 푸는 것이 다르다** —
///   Vulkan 은 디스크립터 셋을 **할당하려면** 파이프라인 레이아웃이 아니라
///   셋 레이아웃이 필요하고, DX12 에는 그 개념이 없다.
///
///   안정 해시가 없는 것도 대칭이 아니다. Vulkan 쪽에는 PSO 디스크 캐시가
///   아직 없어서 실행을 넘어 안정할 이유가 없다 — 생기면 그때 든다.
struct VulkanPipelineLayoutEntry
{
    VkPipelineLayout      layout{ VK_NULL_HANDLE };
    VkDescriptorSetLayout setLayout{ VK_NULL_HANDLE };

    bool IsValid() const { return VK_NULL_HANDLE != layout; }
};

class VulkanPipelineCache : public IRenderPipelineCache, public IRenderRootSignatureCache
{
public:
    VulkanPipelineCache() = default;
    ~VulkanPipelineCache() override;

    VulkanPipelineCache(const VulkanPipelineCache&) = delete;
    VulkanPipelineCache& operator=(const VulkanPipelineCache&) = delete;

    void Initialize(VkDevice device) { m_device = device; }

    /// ★ 여기가 수명이 갈리는 자리다. `DX12PSOManager` 는 `ComPtr` 로 들고
    ///   있어서 캐시를 안 비워도 프로세스가 끝나면 COM 이 정리한다. Vulkan 은
    ///   참조 계수가 없어 **누군가 반드시 vkDestroy* 를 불러야** 하고, 부르는
    ///   시점에 GPU 가 그 파이프라인을 안 쓰고 있어야 한다.
    ///
    ///   그래서 이 함수는 "디바이스가 놀고 있을 때"라는 전제를 갖는다.
    ///   DX12 쪽 캐시에는 그 전제가 없다 — 핸들이 담아야 했던 것이 이것이다.
    void Shutdown();

    // ── IRenderRootSignatureCache ──
    RHIPipelineLayoutHandle GetOrCreate(const RHIPipelineLayoutDesc& desc,
        std::string& outError) override;

    // ── IRenderPipelineCache ──
    RHIPipelineHandle GetOrCreate(const RHIGraphicsPipelineDesc& desc,
        std::string& outError) override;

    /// ★ 소비자가 없다. 삼각형은 컴퓨트를 안 쓴다 — 구현하면 **틀려도 아무도
    ///   모르는 코드**가 된다(`RHIPipelineState.h` 의 규칙). 조용히 빈 핸들을
    ///   주지 않고 실패로 멈춘다.
    RHIPipelineHandle GetOrCreateCompute(const RHIComputePipelineDesc& desc,
        std::string& outError) override;

    /// 핸들 → 짝. 인코더가 부른다.
    VulkanPipelineEntry       Resolve(RHIPipelineHandle handle) const;
    VulkanPipelineLayoutEntry Resolve(RHIPipelineLayoutHandle handle) const;

    struct Stats
    {
        uint32_t memoryHits{ 0 };
        uint32_t compiles{ 0 };
        uint32_t failures{ 0 };
    };
    Stats GetStats() const { return m_stats; }

private:
    VkPipeline CreateOne(const RHIGraphicsPipelineDesc& desc, VkPipelineLayout layout,
        std::string& outError);

    /// desc 를 내용으로 해시한다. DX12 쪽과 달리 레이아웃은 **핸들 값 그대로**
    /// 넣어도 된다 — 디스크 캐시가 없어 실행을 넘어 안정할 이유가 없다.
    uint64_t ComputeHash(const RHIGraphicsPipelineDesc& desc) const;

    VkDevice m_device{ VK_NULL_HANDLE };

    std::unordered_map<uint64_t, RHIPipelineLayoutHandle> m_layoutByHash;
    std::unordered_map<uint64_t, RHIPipelineHandle>       m_pipelineByHash;

    // 핸들의 슬롯이 곧 이 배열의 인덱스다. 놓는 호출자가 0 이라 세대는 아직
    // 안 든다 — 표가 자라지 않고(같은 desc 는 같은 핸들) 캐시가 앱 수명이다.
    //
    // ★ 세대가 필요해지는 조건은 적어 둔다: 파이프라인을 실제로 놓기
    //   시작하면(셰이더 리로드가 Vulkan 에도 생기면) 그때 DX12ResourceTable
    //   과 같은 모양이 필요하다.
    std::vector<VulkanPipelineEntry>       m_pipelines;
    std::vector<VulkanPipelineLayoutEntry> m_layouts;

    // 셰이더 모듈은 파이프라인이 다 구워지면 지워도 되지만, 캐시가 같은
    // 바이트코드로 두 번 불릴 수 있으므로 여기 모아 두고 Shutdown 에서 놓는다.
    std::vector<VkShaderModule> m_modules;

    Stats m_stats;
};

#endif
