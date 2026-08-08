#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include <span>
#include <cstdint>
#include <d3d12.h>

#include "RenderFrameServices.h"

// 커맨드 기록 인터페이스 (PHASE 3-1 재정의, R3).
//
// ── 무엇을 없애는가 ──
//
// R2a·R2b·R2c가 뷰 생성·렌더 타깃·리소스 생성·리드백을 걷고 나면, 패스에
// 남은 DX12 접촉은 "커맨드를 적어 넣는 것" 하나다. 약 211건이고 그 전부가
// 이 인터페이스의 사정권이다. 여기가 끝나면 패스 본문에서 commandList->가
// 사라진다.
//
// ── 계획서 §3.2의 스케치를 세 군데 고쳤다 ──
//
// 착수 전에 실사용을 다시 세니 스케치가 3분의 1을 빠뜨리고 있었다.
// 고친 것과 그 근거:
//
// ① 바인드 포인트가 없었다 (컴퓨트 39건)
//
//    스케치의 SetBindings(slot, handle)은 그래픽스와 컴퓨트를 구분하지
//    않는다. DX12는 둘의 루트 상태가 완전히 별개다.
//
//    ★ 그리고 이것은 백엔드 하나의 사정이 아니다 — Vulkan도
//      VK_PIPELINE_BIND_POINT_GRAPHICS/COMPUTE로 같은 구분을 요구한다.
//      중립화해도 사라지지 않으므로 인터페이스에 있는 것이 맞다.
//
// ② 루트 직결 버퍼가 없었다 (13건)
//
//    본 팔레트·라이트 목록·인스턴스 버퍼는 테이블을 거치지 않고 GPU 주소로
//    직접 걸린다. 스케치의 SetConstants(slot, data, bytes)는 업로드와 바인딩을
//    붙여 놓은 형태라 이 자리에 못 쓴다 — 실제 패스는 길이가 프레임마다 다르고
//    (m_bonePalettes.size()), 한 번 할당해 루프로 채운 뒤 건다. 업로드와
//    바인딩이 나뉘어 있어야 한다.
//
// ③ 루트 시그니처의 자리가 없었다 (23건)
//
//    DX12는 PSO와 루트 시그니처가 별개 객체이고 패스가 둘을 따로 건다.
//    Vulkan은 파이프라인 레이아웃이 파이프라인에 구워진다.
//
//    ★ Vulkan 쪽 모양을 택한다 — SetPipeline 하나가 둘을 건다. 그래야
//      "루트 시그니처를 안 걸고 루트를 건드린다"가 표현 불가능해진다.
//      그것이 실제로 나는 실수다: 인코더 벤치를 처음 돌렸을 때 루트
//      시그니처를 안 걸고 SetGraphicsRoot*를 불러 Release에서 죽었다.
//      Debug는 검증 레이어가 경고만 남기고 넘어가는 자리라 더 위험하다.
//
// ── 배리어는 UAV만 있다 ──
//
// 계획서가 처음에 "배리어가 없다 — 부를 방법 자체가 없다"고 적었으나 틀렸다.
// 상태 *전이*는 그래프의 몫이 맞지만, 한 패스 안에서 디스패치 둘 사이의 UAV
// 배리어는 그래프의 어휘로 표현할 자리가 없다(그래프는 패스 間 전이만
// 추적한다). 표현만 막으면 패스는 배리어를 못 걸어 조용히 틀린 결과를 낸다.
//
// 그래서 UavBarrier만 둔다. 전이를 부를 방법이 없다는 계약은 그대로다.
//
// ── 가상 함수를 그대로 쓴다 ──
//
// ★ 계획서 §5가 "드로우 루프 안쪽은 비가상 인라인으로 둘 수도 있다"고 적어
//   두었는데, 재 보니 그럴 이유가 없다. dx12.encoderbench 실측(Release):
//   가상 dispatch는 호출당 0.9ns 안팎이고 드로우 16,384개에서도 +0.077ms다.
//   현실적인 1,000 드로우면 +0.004ms — 프레임 CPU 2~7ms의 0.1% 수준이다.
//
//   그래서 특별 취급하지 않는다. 미리 걱정해 둔 복잡도를 하나 던다.

enum class RHIBindPoint : uint8_t
{
    Graphics,
    Compute,
};

enum class RHIPrimitiveTopology : uint8_t
{
    TriangleList,
    TriangleStrip,
    LineList,
    PointList,
};

/// 패스가 커맨드를 적는 유일한 통로.
///
/// 수명은 한 번의 기록이다. AddSplitPass의 조각들이 서로 다른 워커 커맨드
/// 리스트에 적으므로, 인코더는 조각마다 하나이고 프레임 전역이 아니다 —
/// ExecuteContext가 주는 것을 쓰고 따로 만들지 않는다.
class RHIEncoder
{
public:
    virtual ~RHIEncoder() = default;

    // ── 렌더 상태 ──

    /// 뷰포트와 시저를 같은 크기로 건다. 지금 패스 열넷이 전부 이 형태다
    /// (RSSetViewports와 RSSetScissorRects를 늘 같은 값으로 짝지어 부른다).
    /// 따로 필요해지면 그때 나눈다.
    virtual void SetViewportAndScissor(uint32_t width, uint32_t height) = 0;

    /// 파이프라인과 그 루트 시그니처를 함께 건다(위 ③).
    virtual void SetPipeline(RHIBindPoint bindPoint,
        ID3D12PipelineState* pipeline, ID3D12RootSignature* rootSignature) = 0;

    virtual void SetPrimitiveTopology(RHIPrimitiveTopology topology) = 0;

    // ── 바인딩 ──

    /// R2a가 만들어 둔 테이블을 슬롯에 건다.
    virtual void SetBindings(RHIBindPoint bindPoint, uint32_t slot,
        const RHIBindingTable& table) = 0;

    /// 샘플러 테이블을 슬롯에 건다. 힙이 달라 타입도 다르다 —
    /// RHISamplerTable 주석 참고.
    virtual void SetSamplers(RHIBindPoint bindPoint, uint32_t slot,
        const RHISamplerTable& table) = 0;

    /// 상수 버퍼를 루트에 직접 건다(업로드 링이 준 주소).
    virtual void SetConstantBuffer(RHIBindPoint bindPoint, uint32_t slot,
        D3D12_GPU_VIRTUAL_ADDRESS address) = 0;

    /// 구조화 버퍼를 루트에 직접 건다(위 ②).
    virtual void SetRootBuffer(RHIBindPoint bindPoint, uint32_t slot,
        D3D12_GPU_VIRTUAL_ADDRESS address) = 0;

    // ── 드로우 · 디스패치 ──

    virtual void SetVertexBuffer(const D3D12_VERTEX_BUFFER_VIEW& view) = 0;
    virtual void SetIndexBuffer(const D3D12_INDEX_BUFFER_VIEW& view) = 0;

    virtual void Draw(uint32_t vertexCount, uint32_t instanceCount,
        uint32_t firstVertex = 0, uint32_t firstInstance = 0) = 0;
    virtual void DrawIndexed(uint32_t indexCount, uint32_t instanceCount,
        uint32_t firstIndex = 0, int32_t baseVertex = 0, uint32_t firstInstance = 0) = 0;

    virtual void Dispatch(uint32_t x, uint32_t y, uint32_t z) = 0;

    // ── 렌더 타깃 (R4-1) ──
    //
    // ★ 만드는 것과 거는 것을 갈랐다. CreateRenderTargets는 뷰를 힙에 만드는
    //   일이라 디바이스 서비스에 남고, 아래 셋은 커맨드를 적는 일이라 여기다.
    //   R2b가 이 셋을 서비스에 둔 것은 그때 인코더가 없었기 때문이고, 그
    //   탓에 패스가 커맨드 리스트를 서비스에 넘기려고 계속 들고 있어야 했다 —
    //   R3가 commandList->를 0으로 만들고도 commandList 변수를 못 걷은 이유다.
    //
    //   실측으로 범위를 정했다: 이 셋의 호출부 27곳이 전부 그래프 패스 안이라
    //   인코더가 닿는다. BindDescriptorHeaps는 그래프 밖 호출자가 있어 함께
    //   옮기지 않았다.

    // ── 디스크립터 힙은 부르지 않는다 ──
    //
    // ★ 여기 BindDescriptorHeaps가 없는 것이 의도다.
    //
    //   힙 바인딩을 빠뜨리면 테이블 핸들이 다른 힙을 가리켜 엉뚱한 리소스를
    //   읽는다 — 이 저장소에서 실제로 SetComputeRootDescriptorTable에서 죽은
    //   적이 있고, 그 뒤로 패스마다 주석을 달아 가며 손으로 불러 왔다.
    //   "빠뜨리면 죽는다"를 열여덟 곳이 기억하는 구조였다.
    //
    //   이제 인코더가 SetBindings·SetSamplers 첫 호출에 스스로 건다. 부를
    //   방법이 없으니 빠뜨릴 방법도 없다 — 인코더 수명을 패스로 좁힌 것,
    //   그래프의 서비스를 생성자 인자로 만든 것과 같은 수다.

    virtual void BindRenderTargets(const RHIRenderTargetBinding& binding) = 0;
    virtual void ClearRenderTargets(const RHIRenderTargetBinding& binding,
        const float rgba[4]) = 0;
    virtual void ClearDepthTarget(const RHIRenderTargetBinding& binding, float depth) = 0;

    // ── 배리어 ──

    /// 같은 리소스에 쓰고 나서 읽기 전. 전이(Transition)는 없다 — 그래프의 몫이다.
    virtual void UavBarrier(std::span<ID3D12Resource* const> resources) = 0;

    // ── 복사 · 클리어 ──

    /// 같은 크기·포맷의 리소스 통째 복사.
    virtual void CopyResource(ID3D12Resource* destination, ID3D12Resource* source) = 0;

    /// UAV를 값 하나로 채운다. 짝 맞추기는 디바이스 서비스에 있고 여기는
    /// 그리로 흘린다(RenderFrameServices::ClearUnorderedAccess 참고).
    ///
    /// ★ R3-1이 여기 두면서 "이 하나가 VolumetricFog의 두 번째 힙을 없앤다"고
    ///   적었는데, 호출부의 위치를 안 보고 적은 것이었다. 그 패스의 클리어는
    ///   PrepareFrame에 있어 ExecuteContext도 인코더도 없다 — 이 메서드는
    ///   R3-2에서 서비스 쪽을 만들 때까지 호출자가 하나도 없었다.
    ///
    ///   힙이 없어진 것은 맞고, 통로가 인코더가 아니었다. 여기는 그래프 안의
    ///   패스가 같은 일을 하게 되면 쓰라고 남겨 둔다.
    virtual void ClearUnorderedAccess(ID3D12Resource* resource,
        const RHIBindingDesc& view, const float rgba[4]) = 0;
};

#endif
