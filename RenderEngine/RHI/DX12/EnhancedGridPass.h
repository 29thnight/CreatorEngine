#pragma once
#include "../RHIFormat.h"
#ifndef DYNAMICCPP_EXPORTS
#include <cstdint>
#include <wrl/client.h>

#include "EnhancedRenderPass.h"

// 에디터 그리드 패스 (PHASE 3-6, Gizmo 계열 첫 슬라이스).
//
// ── 기존 DX11 GridPass가 하는 일 ──
//
//   ±10000 쿼드 하나를 깔고 픽셀 셰이더가 절차적으로 그리드 선을 그린다.
//   씬 오브젝트를 전혀 읽지 않는다 — 입력은 카메라 스냅샷과 스타일 값뿐이다.
//   Gizmo 계열(Grid·WireFrame·Gizmo·GizmoLine)에서 가장 작고 결정적이라
//   이것을 첫 슬라이스로 삼는다.
//
// ── 이식 원칙 ──
//
//   셰이더 로직은 DX11(Grid.vs/ps.hlsl)을 그대로 옮긴다. 카메라 위치를
//   int3로 잘라 그리드를 재중심하는 것, 거리 페이드가 두 번 곱해지는 것까지
//   그대로다 — 정확성 판정이 'DX11과 같은 그림인가'이므로 여기서 고치고
//   싶은 것이 있어도 참는다. 고치면 대조 자체가 성립하지 않는다.
//
//   정점 버퍼는 두지 않는다. DX11은 4정점 버퍼를 만들지만 내용이 고정
//   쿼드 하나라, UI 패스와 같은 이유로 SV_VertexID에서 만든다.
//   정점 위치가 같으므로 픽셀 대조에는 영향이 없다.
class EnhancedGridPass : public EnhancedRenderPass
{
public:
    static constexpr RHIFormat kOutputFormat = RHIFormat::RGBA16Float;
    static constexpr RHIFormat kDepthFormat = RHIFormat::D32Float;

    /// DX11 GridUniformBuffer와 같은 값·같은 기본값. 에디터 설정 UI가 이 값을
    /// 바꾸므로, 교체(3-9) 시점에 그쪽 패널이 이 구조를 채우게 된다.
    struct Style
    {
        Mathf::Color4  gridColor{ 0.45f, 0.44f, 0.43f, 0.5f };
        Mathf::Color4  checkerColor{ 0.41f, 0.38f, 0.36f, 0.f };
        float          fadeStart{ 300.f };
        float          fadeEnd{ 1000.f };
        float          unitSize{ 10.f };
        int32_t        subdivisions{ 10 };
        Mathf::Vector3 centerOffset{ 0.f, 0.f, 0.f };
        float          majorLineThickness{ 2.f };
        float          minorLineThickness{ 0.5f };
        float          minorLineAlpha{ 1.f };
    };

    const char* GetName() const override { return "Grid"; }

    bool Initialize(const EnhancedFrameContext& context, std::string& outError) override;
    bool PrepareFrame(const EnhancedFrameContext& context, std::string& outError) override;
    void Declare(EnhancedRenderGraph& graph, const EnhancedFrameContext& context) override;
    void Shutdown() override;

    struct Inputs
    {
        /// 그리드가 그 위에 얹힐 그림. 비어 있으면 투명 위에 그린다(자가 검증).
        RGHandle color;

        /// 씬 깊이. 그리드는 물체 뒤에 가려져야 하므로 깊이 테스트를 켠다.
        /// 비어 있으면 자체 transient를 만들어 지우고 쓴다(자가 검증).
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
    void SetStyle(const Style& style) { m_style = style; }

    /// 소비자가 아직 없을 때 컬링에서 살아남게 하는 표시(GBuffer와 같은 패턴).
    void SetKeepAlive(bool keepAlive) { m_keepAlive = keepAlive; }

    RGHandle GetOutput() const { return m_output; }
    RGHandle GetDepth() const { return m_depth; }

private:
    template <typename T> using ComPtr = Microsoft::WRL::ComPtr<T>;

    bool CreatePipelines(const EnhancedFrameContext& context, std::string& outError);

    Inputs   m_inputs{};
    RHIFormat m_outputFormat{ kOutputFormat };
    Style    m_style{};
    RGHandle m_output;
    RGHandle m_depth;
    bool     m_keepAlive{ false };

    // 프레임 밀봉 값. PrepareFrame에서 스냅샷으로부터 채운다 — Record가
    // 살아 있는 카메라를 읽지 않게 하기 위해서다(3-2와 같은 이유).
    Mathf::Matrix  m_viewProjection{};
    Mathf::Vector4 m_cameraPos{};

    uint32_t m_width{ 0 };
    uint32_t m_height{ 0 };

    ID3D12PipelineState* m_pso{ nullptr };
    ID3D12RootSignature* m_rootSignature{ nullptr };
};

#endif
