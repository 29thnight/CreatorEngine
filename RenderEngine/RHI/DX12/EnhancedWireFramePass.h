#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include <cstdint>
#include <unordered_map>
#include <vector>
#include <wrl/client.h>

#include "EnhancedRenderPass.h"
#include "DX12MeshCache.h"

// 와이어프레임 패스 (PHASE 3-6, Gizmo 계열 4차 슬라이스).
//
// ── 기존 DX11 WireFramePass가 하는 일 ──
//
//   씬의 모든 MeshRenderer를 FILL_WIREFRAME 래스터라이저로 초록(0,1,0,1)
//   오버레이한다. 디버그 토글(GizmoRenderer::SetWireFrame)로 켠다.
//
//   오브젝트마다 모델 상수를 갱신하고 Mesh::Draw가 나간다 — 다른 기즈모
//   패스들과 같은 per-object 패턴이다.
//
// ── 다시 쓰면서 바꾸는 것 ──
//
//   같은 메시를 쓰는 드로우를 인스턴스로 묶어 메시당 DrawIndexedInstanced
//   한 번으로 낸다(GBuffer와 같은 규칙 — 와이어프레임은 불투명 선이라
//   순서가 그림을 바꾸지 않으므로 묶어도 된다).
//
//   메시는 DX12MeshCache를 그대로 쓴다 — GBuffer가 올린 메시면 업로드
//   0회로 재사용된다(캐시를 나누면 같은 메시가 두 번 올라간다).
//
//   스키닝(본 변형)은 이 슬라이스에 없다. EnhancedDrawItem이 본 행렬을
//   아직 나르지 않는다 — GBuffer 애니메이티드 경로와 함께 붙일 영역이다.
class EnhancedWireFramePass : public EnhancedRenderPass
{
public:
    static constexpr DXGI_FORMAT kOutputFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
    static constexpr DXGI_FORMAT kDepthFormat = DXGI_FORMAT_D32_FLOAT;

    const char* GetName() const override { return "WireFrame"; }

    bool Initialize(const EnhancedFrameContext& context, std::string& outError) override;
    bool PrepareFrame(const EnhancedFrameContext& context, std::string& outError) override;
    void Declare(EnhancedRenderGraph& graph, const EnhancedFrameContext& context) override;
    void Shutdown() override;

    struct Inputs
    {
        /// 오버레이가 얹힐 그림. 비어 있으면 투명 위에 그린다(자가 검증).
        RGHandle color;

        /// 씬 깊이. 비어 있으면 자체 transient를 만들어 지우고 쓴다.
        RGHandle depth;
    };

    void SetInputs(const Inputs& inputs) { m_inputs = inputs; }
    void SetKeepAlive(bool keepAlive) { m_keepAlive = keepAlive; }

    RGHandle GetOutput() const { return m_output; }
    RGHandle GetDepth() const { return m_depth; }

    /// 인스턴싱이 도는지 보는 수. 드로우 수와 배치 수가 같으면 한 건도
    /// 안 묶인 것이다(GBuffer '드로우 704 배치 704' 교훈).
    uint32_t GetLastDrawItemCount() const { return m_lastDrawItemCount; }
    uint32_t GetLastBatchCount() const { return m_lastBatchCount; }

private:
    template <typename T> using ComPtr = Microsoft::WRL::ComPtr<T>;

    bool CreatePipelines(const EnhancedFrameContext& context, std::string& outError);
    void CollectDraws(const std::vector<EnhancedDrawItem>* draws);

    struct Batch
    {
        Mesh*    mesh{ nullptr };
        uint32_t first{ 0 };
        uint32_t count{ 0 };
    };

    Inputs   m_inputs{};
    RGHandle m_output;
    RGHandle m_depth;
    bool     m_keepAlive{ false };

    // 메시별 인스턴스 수집(프레임마다 다시 만든다).
    std::vector<Batch>         m_batches;
    std::vector<Mathf::Matrix> m_instanceWorlds;   // 전치된 월드, 배치 순서로 연속

    std::unordered_map<Mesh*, DX12MeshCache::Entry> m_geometry;

    // 프레임 밀봉 값(3-2).
    Mathf::Matrix m_viewProjection{};

    uint32_t m_lastDrawItemCount{ 0 };
    uint32_t m_lastBatchCount{ 0 };

    uint32_t m_width{ 0 };
    uint32_t m_height{ 0 };

    ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
    ComPtr<ID3D12DescriptorHeap> m_dsvHeap;

    ID3D12PipelineState* m_pso{ nullptr };
    ID3D12RootSignature* m_rootSignature{ nullptr };
};

#endif
