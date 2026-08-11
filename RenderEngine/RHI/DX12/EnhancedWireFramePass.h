#pragma once
#include "../RHIFormat.h"
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
// ── 스키닝(PHASE 3-6 잔여 정리에서 추가) ──
//
//   DX11 WireFrame.vs는 본 스키닝을 한다(boneWeight[0] > 0이면 네 본의
//   가중 합). 이 패스를 처음 쓸 때는 EnhancedDrawItem이 본 행렬을 나르지
//   않아 빼 두었고, GBuffer 스키닝 슬라이스에서 그 통로가 생겼다.
//   빠진 채로 두면 애니메이션 중인 캐릭터의 와이어프레임만 바인드 포즈에
//   멈춰 있게 된다 — 디버그 오버레이가 실제 그림과 어긋나는 셈이다.
//
//   GBuffer와 같은 규약을 쓴다: 팔레트를 애니메이터별로 한 번만 올리고
//   인스턴스마다 시작 오프셋(boneOffset)을 들려 보낸다. 그래서 애니메이터가
//   달라도 같은 메시면 한 배치에 남는다.
//
//   PSO는 하나다. 그림자 패스는 스킨드/비스킨드를 매크로로 갈라 둘을
//   두었지만, 여기는 디버그 오버레이라 분기 하나가 PSO를 늘리는 것보다 싸다.
class EnhancedWireFramePass : public EnhancedRenderPass
{
public:
    static constexpr RHIFormat kOutputFormat = RHIFormat::RGBA16Float;
    static constexpr RHIFormat kDepthFormat = RHIFormat::D32Float;

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

    /// 출력 색 포맷. Initialize보다 먼저 부른다(PSO가 이 값으로 만들어진다).
    ///
    /// 기본값은 HDR이다 — 자가 검증은 자체 transient에 그리기 때문이다.
    /// 상시 러너는 포스트 체인의 LDR 결과 위에 그리므로 그 포맷을 넘긴다:
    /// 입력이 있으면 그 텍스처에 직접 그리는 구조라(Declare 참조) PSO의
    /// RTV 포맷이 입력과 어긋나면 커맨드 리스트가 통째로 무효가 되고,
    /// 그 증상은 Close 실패(E_INVALIDARG)에 이은 디바이스 제거다(실측).
    void SetOutputFormat(RHIFormat format) { m_outputFormat = format; }
    void SetInputs(const Inputs& inputs) { m_inputs = inputs; }
    void SetKeepAlive(bool keepAlive) { m_keepAlive = keepAlive; }

    RGHandle GetOutput() const { return m_output; }
    RGHandle GetDepth() const { return m_depth; }

    /// 인스턴싱이 도는지 보는 수. 드로우 수와 배치 수가 같으면 한 건도
    /// 안 묶인 것이다(GBuffer '드로우 704 배치 704' 교훈).
    uint32_t GetLastDrawItemCount() const { return m_lastDrawItemCount; }
    uint32_t GetLastBatchCount() const { return m_lastBatchCount; }

    /// 스킨드 드로우 수와 올린 팔레트 종류.
    ///
    /// 팔레트 수가 스킨드 드로우 수와 늘 같으면 애니메이터 중복 제거가
    /// 죽은 것이다 — 한 캐릭터의 메시 여럿이 같은 팔레트를 공유해야 한다.
    /// 그림만 봐서는 512행렬을 몇 번 올렸는지 알 길이 없다.
    uint32_t GetLastSkinnedCount() const { return m_lastSkinnedCount; }
    uint32_t GetLastBonePaletteCount() const
    {
        return static_cast<uint32_t>(m_boneOffsets.size());
    }

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

    static constexpr uint32_t kNoSkinning = 0xFFFFFFFFu;

    /// 인스턴스 하나. HLSL의 StructuredBuffer 원소와 배치가 같아야 한다 —
    /// 어긋나면 값이 조용히 밀려 '와이어프레임이 엉뚱한 곳에 있다'로만 드러난다.
    struct InstanceData
    {
        Mathf::Matrix world{};

        /// 이 인스턴스의 본 팔레트 시작 위치. kNoSkinning이면 스키닝 없음.
        uint32_t boneOffset{ kNoSkinning };
        uint32_t padding[3]{};
    };

    Inputs   m_inputs{};
    RHIFormat m_outputFormat{ kOutputFormat };
    RGHandle m_output;
    RGHandle m_depth;
    bool     m_keepAlive{ false };

    // 메시별 인스턴스 수집(프레임마다 다시 만든다).
    std::vector<Batch>        m_batches;
    std::vector<InstanceData> m_instances;   // 배치 순서로 연속

    // 본 팔레트. 애니메이터별로 한 번만 담고 인스턴스가 오프셋으로 가리킨다.
    std::vector<Mathf::Matrix>              m_bonePalettes;
    std::unordered_map<uint64_t, uint32_t>  m_boneOffsets;

    std::unordered_map<Mesh*, DX12MeshCache::Entry> m_geometry;

    // 프레임 밀봉 값(3-2).
    Mathf::Matrix m_viewProjection{};

    uint32_t m_lastDrawItemCount{ 0 };
    uint32_t m_lastBatchCount{ 0 };
    uint32_t m_lastSkinnedCount{ 0 };

    uint32_t m_width{ 0 };
    uint32_t m_height{ 0 };

    RHIPipelineHandle m_pso;
};

#endif
