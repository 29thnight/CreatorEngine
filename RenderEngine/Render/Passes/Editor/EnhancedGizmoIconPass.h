#pragma once
#include "../../../RHI/RHIFormat.h"
#ifndef DYNAMICCPP_EXPORTS
#include <cstdint>
#include <vector>
#include <wrl/client.h>

#include "../../Graph/EnhancedRenderPass.h"

class Texture;

// 기즈모 아이콘(빌보드) 패스 (PHASE 3-6, Gizmo 계열 3차 슬라이스).
//
// ── 기존 DX11 GizmoPass가 하는 일과 그 값 ──
//
//   라이트·카메라 오브젝트 자리에 아이콘을 카메라를 향한 쿼드로 그린다.
//   포인트 하나를 지오메트리 셰이더가 쿼드로 확장하고 텍스처를 입힌다.
//
//   ★ 아이콘마다 위치 버퍼·크기 버퍼를 갱신하고 드로우가 한 번 나간다.
//     UIPass의 '프록시마다 Begin/End'와 같은 부류다 — 아이콘이 30개면
//     버퍼 갱신 60회 + 드로우 30회다.
//
// ── 다시 쓰면서 바꾸는 것 ──
//
//   1. 지오메트리 셰이더를 없앤다. 쿼드 확장은 SV_VertexID 네 꼭짓점을
//      VS에서 만드는 것으로 충분하고, GS는 대부분의 GPU에서 느린 경로다.
//      확장 수식(center-eye 노멀, cross 기반 right, up=(0,Size,0))과
//      UV 배치는 GS의 것을 그대로 옮긴다 — 그림이 기준선이다.
//
//   2. 아이콘을 인스턴스로 모으고, 연속해서 같은 텍스처인 구간을 한
//      드로우로 묶는다(UI 패스와 같은 규칙 — 순서는 목록 순서 그대로).
//
//   3. PS의 alpha = min(a, 0.5)도 그대로 둔다. 아이콘이 반투명 상한을
//      갖는 것이 원본의 의도된 그림이다.
//
//   깊이는 원본대로 안 본다(DSV 미바인딩) — 아이콘은 물체 뒤에서도 보인다.
class EnhancedGizmoIconPass : public EnhancedRenderPass
{
public:
    static constexpr RHIFormat kOutputFormat = RHIFormat::RGBA16Float;

    /// 아이콘 하나. 위치는 월드 좌표 그대로 받는다 — DX11 호출부의
    /// 'y -= 0.5' 보정은 씬 연결 쪽 책임이다(패스가 하면 숨은 규칙이 된다).
    struct Icon
    {
        Mathf::Vector3 position{};
        float          size{ 1.f };

        /// 없으면 1x1 흰색이 묶인다.
        Texture* texture{ nullptr };
    };

    const char* GetName() const override { return "GizmoIcon"; }

    bool Initialize(const EnhancedFrameContext& context, std::string& outError) override;
    bool PrepareFrame(const EnhancedFrameContext& context, std::string& outError) override;
    void Declare(EnhancedRenderGraph& graph, const EnhancedFrameContext& context) override;
    void Shutdown() override;

    struct Inputs
    {
        /// 아이콘이 그 위에 얹힐 그림. 비어 있으면 투명 위에 그린다(자가 검증).
        RGHandle color;
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
    void SetIcons(const std::vector<Icon>* icons) { m_icons = icons; }
    void SetKeepAlive(bool keepAlive) { m_keepAlive = keepAlive; }

    RGHandle GetOutput() const { return m_output; }

    /// 배칭이 도는지 보는 수(UI 패스와 같은 이유 — 그림으로는 안 드러난다).
    uint32_t GetLastIconCount() const { return m_lastIconCount; }
    uint32_t GetLastBatchCount() const { return m_lastBatchCount; }

private:
    template <typename T> using ComPtr = Microsoft::WRL::ComPtr<T>;

    bool CreatePipelines(const EnhancedFrameContext& context, std::string& outError);

    /// 인스턴스 자료. 셰이더의 구조체와 정확히 같아야 한다.
    struct IconInstance
    {
        Mathf::Vector4 centerSize{};   // xyz 중심 · w 크기
    };

    struct Batch
    {
        uint32_t first{ 0 };
        uint32_t count{ 0 };
        Texture* texture{ nullptr };
        RHITextureEntry uploaded;
    };

    Inputs   m_inputs{};
    RHIFormat m_outputFormat{ kOutputFormat };
    RGHandle m_output;
    bool     m_keepAlive{ false };

    const std::vector<Icon>* m_icons{ nullptr };

    std::vector<IconInstance> m_instances;
    std::vector<Batch>        m_batches;

    // 프레임 밀봉 값(3-2).
    Mathf::Matrix  m_viewProjection{};
    Mathf::Vector4 m_eyePosition{};

    uint32_t m_lastIconCount{ 0 };
    uint32_t m_lastBatchCount{ 0 };

    uint32_t m_width{ 0 };
    uint32_t m_height{ 0 };

    RHIPipelineHandle m_pso;
};

#endif
