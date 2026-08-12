#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include "VulkanLoader.h"

#include "../RHIEncoder.h"      // 5c-4b — 계약 본체. 5a 가 d3d12 의존을 끊어 줬다
#include "../RHIFormat.h"
#include "../RHIHandle.h"
#include "VulkanRenderTargetTable.h"   // 5c-4c — `VulkanRenderTargetBinding` 이 여기로 갔다

#include <cstdint>
#include <vector>

class VulkanPipelineCache;
class VulkanResourceTable;
class VulkanDescriptorPool;

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

// ★ 여기 `VulkanRenderTargetBinding` 이 있었다. 5c-4c 에서
//   `VulkanRenderTargetTable.h` 로 갔다 — 만드는 쪽(표)과 푸는 쪽(인코더)이
//   둘 다 알아야 하는데 인코더 헤더에 두면 순환이 된다. A-5b 가
//   `RHIBindingTable` 에서 한 정정의 기록은 그 파일로 함께 옮겼다.

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
    ///
    /// ★ 렌더 타깃 표도 따로 받는다(5c-4c). `RHIRenderTargetBinding::backend`
    ///   가 이 표의 슬롯이라, 없으면 렌더 타깃 넷이 미구현으로 세어진다.
    ///
    /// ★ 디스크립터 풀은 디바이스와 함께 온다(5c-4d). 없으면 상수 버퍼가
    ///   미구현으로 세어진다.
    VulkanEncoder(VkCommandBuffer commandBuffer, const VulkanPipelineCache* pipelines,
        const VulkanResourceTable* resources = nullptr,
        const VulkanRenderTargetTable* renderTargets = nullptr,
        VkDevice device = VK_NULL_HANDLE, VulkanDescriptorPool* descriptors = nullptr)
        : m_commandBuffer(commandBuffer), m_pipelines(pipelines)
        , m_resources(resources), m_renderTargets(renderTargets)
        , m_device(device), m_descriptors(descriptors) {}

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

    /// 불투명 값을 표의 슬롯으로 읽어 백엔드 실물로 푼다 (5c-4c).
    ///
    /// ★ **본문이 이미 있었다.** 아래 백엔드 전용 오버로드 셋이 동적 렌더링을
    ///   이미 하고 있었고, 5c-4c 가 더한 것은 **푸는 한 줄**뿐이다 — 5b 가
    ///   계약을 불투명 값으로 만들어 둔 것의 값이 여기서 청구된다. 모델을
    ///   "뷰 목록"으로 갈았다면 계약과 실물이 둘 다 바뀌어야 했다.
    void BindRenderTargets(const RHIRenderTargetBinding& binding) override;
    void ClearRenderTargets(const RHIRenderTargetBinding& binding, const float rgba[4]) override;
    void ClearDepthTarget(const RHIRenderTargetBinding& binding, float depth) override;

    /// 상수 버퍼 하나를 슬롯에 건다 (5c-4d).
    ///
    /// ★ **즉시 걸지 않고 쌓아 둔다.** DX12 는 이 호출이 곧
    ///   `SetGraphicsRootConstantBufferView` 라 루트 파라미터마다 독립이지만,
    ///   Vulkan 은 `VulkanBindingModel` 이 정한 대로 **셋이 하나**다 — 슬롯마다
    ///   따로 걸면 뒤엣것이 앞엣것을 덮는다.
    ///
    ///   그래서 드로우 직전에 한 번 묶어 건다(`FlushDescriptors`). 계약은
    ///   그대로 두고 **소비되는 시점만 늦춘다** — `SetVertexBuffer` 의 보폭이
    ///   "같은 인자가 두 백엔드에서 다른 시점에 소비된다"였던 것과 같은 부류다.
    void SetConstantBuffer(RHIBindPoint bindPoint, uint32_t slot,
        const RHIBufferSlice& slice) override;

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
    //   막는 것: 복사·리드백 여섯은 슬라이스 7 · UAV 둘은 소비자가 없다.
    //
    //   ★ 5c-4c 가 "렌더 타깃 넷" 중 **셋**을 걷었다. 남은 하나
    //     (`ClearRenderTargetRect`)는 표가 아니라 소비자가 없어서 남는다.
    //
    //   ★ 5c-4d 가 상수 버퍼를 걷었다. 나머지 셋(`SetBindings`·`SetSamplers`·
    //     `SetRootBuffer`)은 **기계가 없어서가 아니라 소비자가 없어서** 남는다
    //     — 아래 쌓기·묶기 기계가 그것들도 그대로 받게 돼 있고, 막는 것은
    //     `CreateBindings`/`CreateSamplers` 가 무엇을 돌려줘야 하는가이며
    //     그 답은 **테이블을 쓰는 첫 패스**(슬라이스 7)가 낸다. 지금 정하면
    //     소비자 없이 모양을 정하는 것이다(§1.1).

    void SetBindings(RHIBindPoint bindPoint, uint32_t slot,
        const RHIBindingTable& table) override;
    void SetSamplers(RHIBindPoint bindPoint, uint32_t slot,
        const RHISamplerTable& table) override;
    void SetRootBuffer(RHIBindPoint bindPoint, uint32_t slot,
        const RHIBufferSlice& slice) override;

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

    /// DX12 에 대응이 없다. `vkCmdBeginRendering` 은 반드시 닫혀야 하는데
    /// `OMSetRenderTargets` 는 여닫이가 없다 — 그 닫는 자리를 계약에 두지
    /// 않으려고 **소멸자와 다음 Bind·전이·복사·프레임 경계가 닫는다.**
    void EndRenderTargets();

private:
    // ★ 여기 "백엔드 전용 경로 (계약 밖)" 공개 구간이 있었다 — 렌더 타깃
    //   셋(`VulkanRenderTargetBinding` 오버로드)과 `SetConstantBuffer
    //   (VkDescriptorSet)`. `VulkanTrianglePass` 가 유일한 소비자였고 5 가
    //   끝나며 함께 갔다(5 마무리).
    //
    //   렌더 타깃 셋은 지워지지 않고 **내려갔다** — 5c-4c 부터 중립 오버라이드
    //   의 구현 본체다. 공개로 남겨 두면 "표를 안 거치고 뷰를 직접 거는 길"이
    //   API 로 남는 것이고, 그 길을 쓰는 코드는 계수(미구현·표 잔량)에 안
    //   잡힌다. `SetConstantBuffer(VkDescriptorSet)` 는 소비자가 0 이 되어
    //   지워졌다 — 중립 오버로드가 풀·번호표로 같은 일을 한다(5c-4d).

    void BindRenderTargets(const VulkanRenderTargetBinding& binding);
    void ClearRenderTargets(const VulkanRenderTargetBinding& binding, const float rgba[4]);
    void ClearDepthTarget(const VulkanRenderTargetBinding& binding, float depth);

    /// 불투명 값 → 백엔드 실물 (5c-4c).
    VulkanRenderTargetBinding ResolveTargets(const RHIRenderTargetBinding& binding);

    /// 쌓아 둔 것을 셋 하나로 묶어 건다 (5c-4d). 드로우·디스패치가 부른다.
    ///
    /// ★ 쌓인 것이 없으면 아무것도 안 한다 — 이미 걸린 셋이 그대로 산다.
    ///   같은 바인딩으로 여러 번 그리는 흔한 경우에 셋을 다시 자르지 않는다.
    void FlushDescriptors(RHIBindPoint bindPoint);

    /// 아직 안 걸린 디스크립터 하나.
    ///
    /// ★ `VkWriteDescriptorSet` 을 바로 쌓지 않는다. 그것은
    ///   `VkDescriptorBufferInfo` 를 **포인터로** 가리키므로, 벡터가 자라면
    ///   앞서 쌓은 것의 포인터가 무효가 된다 — 값으로 들고 묶는 시점에
    ///   조립한다.
    struct PendingBinding
    {
        uint32_t               param{ 0 };
        VkDescriptorBufferInfo buffer{};
    };

    /// 미구현을 센다. 이름은 리터럴이라 수명 걱정이 없다.
    void NoteUnimplemented(const char* name)
    {
        ++m_unimplemented;
        m_lastUnimplemented = name;
    }

    VkCommandBuffer                m_commandBuffer{ VK_NULL_HANDLE };
    const VulkanPipelineCache*     m_pipelines{ nullptr };
    const VulkanResourceTable*     m_resources{ nullptr };
    const VulkanRenderTargetTable* m_renderTargets{ nullptr };
    VkDevice                       m_device{ VK_NULL_HANDLE };
    VulkanDescriptorPool*          m_descriptors{ nullptr };

    std::vector<PendingBinding> m_pending[2];

    /// 지금 걸린 파이프라인이 구워진 셋 레이아웃과 그 레이아웃 핸들 (5c-4d).
    /// 앞엣것은 셋을 **할당**하는 데, 뒤엣것은 슬롯 번호를 **binding 번호로
    /// 옮기는** 데 쓴다 — 둘 다 파이프라인을 걸어야 알 수 있다.
    VkDescriptorSetLayout   m_boundSetLayout[2]{ VK_NULL_HANDLE, VK_NULL_HANDLE };
    RHIPipelineLayoutHandle m_boundLayoutHandle[2];

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
