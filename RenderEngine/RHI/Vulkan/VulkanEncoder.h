#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include "VulkanLoader.h"

#include "../RHIEncoder.h"      // 5c-4b — 계약 본체. 5a 가 d3d12 의존을 끊어 줬다
#include "../RHIFormat.h"
#include "../RHIHandle.h"

#include <cstdint>

class VulkanPipelineCache;
class VulkanResourceTable;

// 커맨드 기록 — Vulkan (V8-a → 5c-4b 에서 상속).
//
// ── 상속하지 못하던 이유가 사라졌다 (5c-4b) ──
//
// 아래는 V8-a 시점의 기록이고, **여덟이 전부 닫혔다**: `SetPipeline` 은
// A-1(핸들 짝) · 슬라이스 넷은 A-5a · 배리어 셋은 A-6. 그리고 마지막으로
// 남은 것은 내용이 아니라 **위치**였다 — 이 헤더들이 `RHI/DX12/` 안에서
// `d3d12.h` 를 물어서 Vulkan 이 include 할 수가 없었고, 5a 가 그것을 풀었다.
//
// 지금 상속하지 **못하는** 것은 없다. 다만 **아직 못 하는 것**은 있고,
// 그것은 조용히 넘어가지 않고 세어진다 — 아래 `GetUnimplementedCount()`.
//
// ── (V8-a 시점의 기록) 왜 RHIEncoder 를 상속하지 않는가 ──
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

/// 렌더 타깃 묶음 — Vulkan. **백엔드 안쪽의 실물**이다.
///
/// ★ 여기 "`RHIRenderTargetBinding` 은 프레임 힙 인덱스라 Vulkan 에 그
///   모델이 없다 · 그래서 인덱스 모델을 버리고 **뷰 목록**이 돼야 한다"고
///   적혀 있었다. **절반만 맞았다(5b 정정).**
///
///   맞은 것: 동적 렌더링은 이미지 뷰·포맷·크기를 커맨드에 직접 받으므로
///   Vulkan 이 드는 것은 아래 구조체다.
///
///   틀린 것: 그래서 **계약**도 뷰 목록이어야 한다는 결론. 실측하면 패스
///   10곳이 `IsValid()` 하나만 읽는다 — 계약이 나를 것은 불투명 값 하나와
///   개수뿐이고, 그 값을 이 구조체의 슬롯 번호로 읽는 것이 이 파일의 몫이다.
///
///   ★ A-5b 가 `RHIBindingTable` 에서 한 정정과 **같은 것이 두 번째**다.
///     "백엔드 안에서 모양이 다르다" 와 "계약의 모양이 달라야 한다" 는
///     다른 이야기이고, 후자는 **소비처가 필드를 읽는가**로만 답할 수 있다.
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

// ★ 여기 `VulkanBindPoint`·`VulkanPrimitiveTopology` 가 있었다 — `RHIEncoder.h`
//   의 것과 **글자까지 같은데** 그 헤더가 `d3d12.h` 를 물어 재사용할 수가
//   없어서 베낀 것이었다. 그때 "이 두 열거의 존재 자체가 V7 이 왜 필요한지의
//   증거다" 라고 적었고, 5a 가 그 헤더를 `RHI/` 로 올리면서 근거가 사라졌다.
//
//   **베낀 것이 지워지는 것이 이동의 값이다** — 폴더가 바뀐 것이 아니라
//   같은 어휘를 두 벌 들고 있던 상태가 끝났다.

/// 커맨드 버퍼 하나를 감싼다. 수명은 한 번의 기록이다 — `DX12Encoder` 와 같다.
class VulkanEncoder final : public RHIEncoder
{
public:
    /// ★ 캐시를 받는다(A-1b). DX12 인코더는 `DX12DeviceResources` 를 받아
    ///   거기 있는 표로 푸는데, Vulkan 은 캐시가 파이프라인 표를 들므로 캐시를
    ///   받는다. **핸들의 계약은 같고 표의 자리만 다르다** — 상위는 어느 쪽도
    ///   모른다.
    ///
    /// ★ 리소스 표는 따로 받는다(5c-4a). 정점·인덱스 슬라이스를 푸는 데
    ///   필요하고, 없으면 그 둘이 미구현으로 세어진다.
    VulkanEncoder(VkCommandBuffer commandBuffer, const VulkanPipelineCache* pipelines,
        const VulkanResourceTable* resources = nullptr)
        : m_commandBuffer(commandBuffer), m_pipelines(pipelines), m_resources(resources) {}

    ~VulkanEncoder() override { EndRenderTargets(); }

    VulkanEncoder(const VulkanEncoder&) = delete;
    VulkanEncoder& operator=(const VulkanEncoder&) = delete;

    // ── 실물 (RHIEncoder) ──

    /// ★ Y 뒤집기가 여기 있다. Vulkan 의 클립 공간 Y 는 아래로, D3D 는 위로
    ///   향한다. 높이를 음수로 주어 백엔드가 맞춘다 — 어느 층이 좌표계를
    ///   맞추는지가 계약의 문제라서 셰이더에 숨기지 않는다.
    void SetViewportAndScissor(uint32_t width, uint32_t height) override;

    /// ★ 인자 둘은 `RHIEncoder::SetPipeline` 과 같은데 **근거가 반대다.**
    ///   `RHIEncoder.h` ③은 "Vulkan 은 레이아웃이 파이프라인에 구워지므로
    ///   하나가 둘을 건다"고 적었다. 그러나 `vkCmdBindDescriptorSets` 가
    ///   레이아웃을 **다시** 요구하므로 인코더가 그것을 기억해야 한다 —
    ///   즉 둘은 따로 걸리는 것이 아니라 **짝으로 따라다닌다**(A-1 의 근거).
    void SetPipeline(RHIBindPoint bindPoint, RHIPipelineHandle pipeline) override;

    /// ★ 파이프라인이 토폴로지를 **동적 상태**로 굽고 여기서 값만 바꾼다.
    ///   V6 이 `RHIPrimitiveTopology`(드로우마다)와 `RHITopologyType`
    ///   (파이프라인마다)을 갈라 둔 것이 여기서 값을 한다.
    ///
    ///   대신 Vulkan 은 제약을 하나 더 건다: 동적으로 바꿀 수 있는 것은
    ///   **같은 부류 안에서**다(삼각형 목록 ↔ 스트립은 되고, 삼각형 ↔ 선은
    ///   보장이 없다). 그 제약은 중립 계약에 아직 적혀 있지 않다.
    void SetPrimitiveTopology(RHIPrimitiveTopology topology) override;

    /// 슬라이스를 그대로 건다 (5c-4a 의 표로 푼다).
    void SetVertexBuffer(const RHIBufferSlice& slice, uint32_t stride) override;
    void SetIndexBuffer(const RHIBufferSlice& slice, RHIFormat format) override;

    void Draw(uint32_t vertexCount, uint32_t instanceCount,
        uint32_t firstVertex = 0, uint32_t firstInstance = 0) override;
    void DrawIndexed(uint32_t indexCount, uint32_t instanceCount,
        uint32_t firstIndex = 0, int32_t baseVertex = 0, uint32_t firstInstance = 0) override;
    void Dispatch(uint32_t x, uint32_t y, uint32_t z) override;

    // ── 아직 못 하는 것 (세어진다) ──
    //
    // ★ **조용히 넘어가지 않는다.** §1.1 이 "상속하면 열넷을 '못 한다'로
    //   채워야 하고 그러면 계약이 *부를 수는 있지만 죽는다* 가 된다"고 경고한
    //   자리이고, T4 가 "도달할 수 없는 경로는 죽었는지 살았는지 알 수 없다"고
    //   적은 자리다.
    //
    //   그래서 부르면 이름과 함께 세어지고, `vk.*` 검사가 **그 수가 0 인가**를
    //   판정에 넣는다. 슬라이스 7 이 패스를 하나씩 옮길 때 무엇이 막는지가
    //   패스별로 자동으로 드러난다 — 컴파일은 서명만 보지만 이쪽은 **실제로
    //   부르는 것**만 센다.
    //
    //   막는 것: 렌더 타깃 넷은 뷰 표(5c-4c) · 바인딩 넷은 디스크립터 풀
    //   (5c-4d) · 복사·리드백 여섯은 슬라이스 7 · UAV 둘은 소비자가 없다.

    void SetBindings(RHIBindPoint bindPoint, uint32_t slot,
        const RHIBindingTable& table) override;
    void SetSamplers(RHIBindPoint bindPoint, uint32_t slot,
        const RHISamplerTable& table) override;
    void SetConstantBuffer(RHIBindPoint bindPoint, uint32_t slot,
        const RHIBufferSlice& slice) override;
    void SetRootBuffer(RHIBindPoint bindPoint, uint32_t slot,
        const RHIBufferSlice& slice) override;

    void BindRenderTargets(const RHIRenderTargetBinding& binding) override;
    void ClearRenderTargets(const RHIRenderTargetBinding& binding, const float rgba[4]) override;
    void ClearDepthTarget(const RHIRenderTargetBinding& binding, float depth) override;
    void ClearRenderTargetRect(const RHIRenderTargetBinding& binding,
        const float rgba[4], const RHIRect& rect) override;

    void UavBarrier(std::span<const RHITextureHandle> textures) override;
    void CopyResource(RHITextureHandle destination, RHITextureHandle source) override;
    void CopyTexture(RHITextureHandle destination, RHITextureHandle source,
        uint32_t destinationSubresource = 0, uint32_t sourceSubresource = 0) override;
    void ClearUnorderedAccess(const RHIBindingDesc& view, const float rgba[4]) override;

    void CopyToReadback(const RHIReadback& readback, RHITextureHandle source,
        uint32_t slice = 0, uint32_t sourceSubresource = 0) override;
    void CopyVolumeToReadback(const RHIReadback& readback, RHITextureHandle source,
        uint32_t sourceSubresource = 0) override;
    void CopyPartialToReadback(const RHIReadback& readback, RHITextureHandle source,
        uint32_t slice = 0, uint32_t sourceSubresource = 0) override;
    void CopyBufferToReadback(const RHIReadback& readback, RHIBufferHandle source,
        uint64_t sourceOffset = 0, uint64_t bytes = 0) override;

    /// 미구현 호출 수와 마지막 이름. `vk.*` 검사의 판정에 쓴다.
    uint32_t    GetUnimplementedCount() const { return m_unimplemented; }
    const char* GetLastUnimplemented() const { return m_lastUnimplemented; }

    // ── 백엔드 전용 경로 (계약 밖) ──
    //
    // ★ `DX12Encoder` 가 벤치용 원시 뷰 오버로드를 구체 타입에 남긴 것과 같은
    //   자리다(A-4). 여기 것은 `VulkanTrianglePass` 가 쓰고, 그 패스는 5 가
    //   끝나면 사라진다 — 그때 이 셋도 함께 지운다.

    void BindRenderTargets(const VulkanRenderTargetBinding& binding);
    void ClearRenderTargets(const VulkanRenderTargetBinding& binding, const float rgba[4]);
    void ClearDepthTarget(const VulkanRenderTargetBinding& binding, float depth);
    void SetConstantBuffer(RHIBindPoint bindPoint, uint32_t slot, VkDescriptorSet set);

    /// DX12 에 대응이 없다. `vkCmdBeginRendering` 은 반드시 닫혀야 하는데
    /// `OMSetRenderTargets` 는 여닫이가 없다 — 그 닫는 자리를 계약에 두지
    /// 않으려고 **소멸자와 다음 Bind 가 닫는다.**
    void EndRenderTargets();

private:
    /// 미구현을 센다. 이름은 리터럴이라 수명 걱정이 없다.
    void NoteUnimplemented(const char* name)
    {
        ++m_unimplemented;
        m_lastUnimplemented = name;
    }

    VkCommandBuffer            m_commandBuffer{ VK_NULL_HANDLE };
    const VulkanPipelineCache* m_pipelines{ nullptr };
    const VulkanResourceTable* m_resources{ nullptr };

    /// 지금 걸린 레이아웃. `DX12Encoder` 가 루트 시그니처를 기억하는 것과
    /// 자리는 같은데 이유가 다르다 — 저쪽은 '다시 걸지 않으려고'이고
    /// 이쪽은 **디스크립터를 걸 때 필요해서**다.
    VkPipelineLayout m_boundLayout[2]{ VK_NULL_HANDLE, VK_NULL_HANDLE };

    /// 렌더링이 열려 있는가. DX12 인코더에는 이런 상태가 없다 — Vulkan 이
    /// 커맨드 버퍼에게 "지금 렌더링이 열려 있나"를 물을 방법을 주지 않는다.
    bool m_renderingOpen{ false };

    uint32_t    m_unimplemented{ 0 };
    const char* m_lastUnimplemented{ nullptr };
};

#endif
