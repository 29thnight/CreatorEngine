#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include "VulkanLoader.h"

#include "../RHIFormat.h"
#include "../RHIHandle.h"

class VulkanPipelineCache;

#include <cstdint>

// 커맨드 기록 — Vulkan (V8-a).
//
// ── 왜 RHIEncoder 를 상속하지 않는가 ──
//
// 24개 메서드 중 **8개의 서명에 DX12 토큰이 직접** 있고(`SetPipeline` ·
// `SetConstantBuffer` · `SetRootBuffer` · `SetVertexBuffer` · `SetIndexBuffer` ·
// `UavBarrier` · `CopyResource` · `ClearRenderTargetRect`), 여섯 개가 구조체를
// 통해 묻어 온다. 상속하면 그 열넷을 전부 '못 한다'로 채워야 하는데, 그러면
// 계약이 "부를 수는 있지만 죽는다"가 된다.
//
// ★ 그래서 **이름과 인자 순서를 그대로 두고 클래스만 가른다.** 갈린 자리가
//   어디인지 서명 대조로 바로 읽히는 것이 이 파일의 산출물이다.
//
// ── 무엇이 살고 무엇이 죽는가 (A 이후) ──
//
// ★ 갈라 둔 것이 다 임시는 아니다. 둘을 섞으면 다음 사람이 백엔드 구현까지
//   걷어내려 하거나, 반대로 계측기를 구조로 읽는다.
//
//   **산다** — 이 클래스 자체. `VulkanEncoder final : public RHIEncoder` 가
//     되고 `DX12Encoder` 와 나란히 선다. 백엔드마다 인코더 구현이 있는 것은
//     마땅하다.
//
//   **죽는다** — 아래 열거 둘(`VulkanBindPoint`·`VulkanPrimitiveTopology`)과
//     `VulkanRenderTargetBinding`. 앞의 둘은 이미 중립인 어휘를 베낀 것이고
//     (V7 이 `RHIEncoder.h` 에서 풀어 준다), 뒤엣것은 A 가
//     `RHIRenderTargetBinding` 을 인덱스 모델에서 뷰 목록으로 갈면 그쪽이
//     이 자리를 받는다.

/// 렌더 타깃 묶음 — Vulkan.
///
/// ★ `RHIRenderTargetBinding` 은 필드가 셋이다(`rtvIndex`·`colorCount`·
///   `dsvIndex`) — **프레임 힙 안의 인덱스**다. 그 모델이 Vulkan 에는 없다.
///   동적 렌더링은 이미지 뷰와 포맷과 크기를 커맨드에 직접 받으므로,
///   같은 뜻을 담는 데 필드가 셋에서 아래만큼으로 는다.
///
///   그래서 A 는 "인덱스 셋을 핸들 하나로" 가 아니라 **"인덱스 모델을 버리고
///   뷰 목록으로"** 여야 한다. §7.2.5 예상 5 가 이것이다.
struct VulkanRenderTargetBinding
{
    static constexpr uint32_t kMaxColors = 8;

    VkImageView colorViews[kMaxColors]{};
    uint32_t    colorCount{ 0 };

    VkImageView depthView{ VK_NULL_HANDLE };

    uint32_t width{ 0 };
    uint32_t height{ 0 };

    bool HasColor() const { return 0 != colorCount; }
    bool HasDepth() const { return VK_NULL_HANDLE != depthView; }
    bool IsValid()  const { return (HasColor() || HasDepth()) && 0 != width && 0 != height; }
};

// ★ 아래 둘은 `RHIEncoder.h` 의 `RHIBindPoint`·`RHIPrimitiveTopology` 와
//   글자까지 같다. 그런데 재사용할 수가 없다 — 그 헤더가 `d3d12.h` 를 문다.
//   **이미 중립인 어휘가 DX12 헤더 안에 갇혀 있는 것**이고, 이 두 열거의
//   존재 자체가 V7(이동)이 왜 필요한지의 증거다. 접히는 것은 그때다.

enum class VulkanBindPoint : uint8_t
{
    Graphics,
    Compute,
};

enum class VulkanPrimitiveTopology : uint8_t
{
    TriangleList,
    TriangleStrip,
    LineList,
    PointList,
};

/// 커맨드 버퍼 하나를 감싼다. 수명은 한 번의 기록이다 — `DX12Encoder` 와 같다.
class VulkanEncoder
{
public:
    /// ★ 캐시를 받는다(A-1b). DX12 인코더는 `DX12DeviceResources` 를 받아
    ///   거기 있는 표로 푸는데, Vulkan 은 캐시가 표를 들므로 캐시를 받는다.
    ///   **핸들의 계약은 같고 표의 자리만 다르다** — 상위는 어느 쪽도 모른다.
    VulkanEncoder(VkCommandBuffer commandBuffer, const VulkanPipelineCache* pipelines)
        : m_commandBuffer(commandBuffer), m_pipelines(pipelines) {}

    ~VulkanEncoder() { EndRenderTargets(); }

    VulkanEncoder(const VulkanEncoder&) = delete;
    VulkanEncoder& operator=(const VulkanEncoder&) = delete;

    // ── 서명이 그대로인 것 ──

    /// ★ Y 뒤집기가 여기 있다. Vulkan 의 클립 공간 Y 는 아래로, D3D 는 위로
    ///   향한다. 높이를 음수로 주어 백엔드가 맞춘다 — 어느 층이 좌표계를
    ///   맞추는지가 계약의 문제라서 셰이더에 숨기지 않는다.
    ///
    ///   §7.2.2 의 골격은 이것을 검사 파일 안에서 했다. 인코더로 내려오면서
    ///   **패스가 그 사실을 몰라도 되는 자리**가 됐다.
    void SetViewportAndScissor(uint32_t width, uint32_t height);

    /// ★ 파이프라인이 토폴로지를 **동적 상태**로 굽고 여기서 배열만 바꾼다.
    ///   V6 이 `RHIPrimitiveTopology`(드로우마다)와 `RHITopologyType`
    ///   (파이프라인마다)을 갈라 둔 것이 여기서 값을 한다 — 겹쳐 두었다면
    ///   이 호출이 "파이프라인을 다시 구워라"가 됐을 자리다.
    ///
    ///   대신 Vulkan 은 제약을 하나 더 건다: 동적으로 바꿀 수 있는 것은
    ///   **같은 부류 안에서**다(삼각형 목록 ↔ 스트립은 되고, 삼각형 ↔ 선은
    ///   보장이 없다). 그 제약은 중립 계약에 아직 적혀 있지 않다.
    void SetPrimitiveTopology(VulkanPrimitiveTopology topology);

    void Draw(uint32_t vertexCount, uint32_t instanceCount,
        uint32_t firstVertex = 0, uint32_t firstInstance = 0);

    // ── 객체가 갈리는 것 ──

    /// ★ 인자 둘은 `RHIEncoder::SetPipeline` 과 같은데 **근거가 반대다.**
    ///   `RHIEncoder.h` ③은 "Vulkan 은 레이아웃이 파이프라인에 구워지므로
    ///   하나가 둘을 건다"고 적었다. 그러나 `vkCmdBindDescriptorSets` 가
    ///   레이아웃을 **다시** 요구하므로, 인코더는 그것을 기억해야 한다
    ///   (아래 m_boundLayout). 즉 둘은 따로 걸리는 것이 아니라 **짝으로
    ///   따라다닌다** — A 의 핸들이 둘이 아니라 짝 하나여야 하는 이유다.
    void SetPipeline(VulkanBindPoint bindPoint, RHIPipelineHandle pipeline);

    /// ★ 셋째 인자가 `D3D12_GPU_VIRTUAL_ADDRESS`(8바이트 주소)에서
    ///   디스크립터 셋으로 갈린다. 그리고 Vulkan 에서는 이 함수와
    ///   `SetBindings`·`SetRootBuffer` 가 **같은 호출**(vkCmdBindDescriptorSets)
    ///   로 접힌다 — DX12 에서 셋인 이유는 루트 시그니처가 세 종류의 파라미터를
    ///   갖기 때문이지 기록 동작이 셋이기 때문이 아니다.
    ///
    ///   `slot` 의 뜻도 갈린다: DX12 는 루트 파라미터 번호이고 여기서는
    ///   디스크립터 셋 번호다.
    void SetConstantBuffer(VulkanBindPoint bindPoint, uint32_t slot, VkDescriptorSet set);

    // ── 렌더 타깃 ──

    /// ★ DX12 의 `OMSetRenderTargets` 는 여닫이가 없고
    ///   `vkCmdBeginRendering` 은 반드시 닫혀야 한다. 그 닫는 자리를 계약에
    ///   두지 않으려고 **인코더 소멸자와 다음 Bind 가 닫는다** — R3 가
    ///   인코더 수명을 패스 하나로 좁혀 둔 것이 여기서 값을 한다.
    void BindRenderTargets(const VulkanRenderTargetBinding& binding);

    /// ★ 여기가 계약이 비싼 길을 강제하는 자리다. Vulkan 의 클리어는 렌더링을
    ///   **여는 시점의 load op** 이라 사실상 공짜인데, 중립 계약은
    ///   '걸고 나서 지운다' 순서라 그 자리를 놓친다. 그래서
    ///   `vkCmdClearAttachments` 로 간다 — 이미 열린 렌더링 안에서 화면을
    ///   한 번 덮는 진짜 작업이다.
    ///
    ///   즉 계약이 '무엇을 지울지'를 **거는 시점에** 알아야 한다.
    void ClearRenderTargets(const VulkanRenderTargetBinding& binding, const float rgba[4]);

    void ClearDepthTarget(const VulkanRenderTargetBinding& binding, float depth);

    /// DX12 에 대응이 없다. 위 ★ 참고.
    void EndRenderTargets();

private:
    VkCommandBuffer            m_commandBuffer{ VK_NULL_HANDLE };
    const VulkanPipelineCache* m_pipelines{ nullptr };

    /// 지금 걸린 레이아웃. `DX12Encoder` 가 루트 시그니처를 기억하는 것과
    /// 자리는 같은데 이유가 다르다 — 저쪽은 '다시 걸지 않으려고'이고
    /// 이쪽은 **디스크립터를 걸 때 필요해서**다.
    VkPipelineLayout m_boundLayout[2]{ VK_NULL_HANDLE, VK_NULL_HANDLE };

    /// 렌더링이 열려 있는가. DX12 인코더에는 이런 상태가 없다.
    ///
    /// ★ 인코더가 드는 상태가 이것 하나 늘었다. DX12Encoder 는 "상태를
    ///   들수록 인코더가 기억하는 것과 커맨드 리스트가 기억하는 것이 갈려
    ///   어긋난다"고 적어 두었는데, 이쪽은 안 들 수가 없다 — Vulkan 이
    ///   커맨드 버퍼에게 "지금 렌더링이 열려 있나"를 물을 방법을 주지 않는다.
    bool m_renderingOpen{ false };
};

#endif
