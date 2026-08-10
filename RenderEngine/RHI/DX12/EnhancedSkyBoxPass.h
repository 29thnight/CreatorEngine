#pragma once
#include "../RHIFormat.h"
#ifndef DYNAMICCPP_EXPORTS
#include <cstdint>
#include <wrl/client.h>

#include "EnhancedRenderPass.h"

// 스카이박스 패스 (PHASE 3-6, 신규 작성).
//
// ── 기존 DX11 SkyBoxPass가 하는 일 ──
//
//   둘을 겸한다: ① 프레임마다 큐브 메시를 카메라 위치로 옮겨 큐브맵을
//   그린다 ② 씬 로드 때 IBL 체인을 생성한다(rect→큐브맵, 조도 맵,
//   프리필터 스페큘러, BRDF LUT).
//
//   이 슬라이스는 ①만 새로 쓴다. ②는 프레임 루프가 아니라 로드 시점의
//   생성 작업이라 성격이 다르고, 라이팅(IBL 소비)과 함께 붙여야 검증이
//   된다 — 별도 슬라이스로 남긴다.
//
// ── 이식 원칙 ──
//
//   셰이더 로직은 DX11(Skybox.vs/ps)을 그대로 옮긴다. 핵심은 둘이다:
//   클립 z를 w x 0.99999로 밀어 원평면 직전에 두는 것(깊이 테스트가
//   씬이 안 그린 곳에만 하늘을 남긴다 — 이게 깨지면 z/w = 1.0이 LESS를
//   통과하지 못해 하늘이 통째로 사라지고, 자가 검증이 그것을 잡는다),
//   그리고 큐브 로컬 정점을 그대로 샘플 방향으로 쓰는 것.
//
//   정점 버퍼는 두지 않는다 — 큐브 36정점을 SV_VertexID 테이블로 만든다.
//   눈이 항상 큐브 중심이라 픽셀마다 면이 하나뿐이어서 컬링을 꺼도
//   그림이 같다(면 겹침이 없다).
//
//   큐브맵 리소스는 밖에서 받는다(SetCubeMap). 엔진 큐브맵(DX11 텍스처)을
//   DX12로 나르는 일은 IBL 생성 슬라이스와 함께 정한다 — DX11 쪽 CPU
//   원본이 없어 공유 텍스처나 재생성 중 하나를 골라야 하기 때문이다.
class EnhancedSkyBoxPass : public EnhancedRenderPass
{
public:
    static constexpr RHIFormat kOutputFormat = RHIFormat::RGBA16Float;
    static constexpr RHIFormat kDepthFormat = RHIFormat::D32Float;

    const char* GetName() const override { return "SkyBox"; }

    bool Initialize(const EnhancedFrameContext& context, std::string& outError) override;
    bool PrepareFrame(const EnhancedFrameContext& context, std::string& outError) override;
    void Declare(EnhancedRenderGraph& graph, const EnhancedFrameContext& context) override;
    void Shutdown() override;

    struct Inputs
    {
        /// 하늘이 얹힐 그림(라이팅 결과). 비어 있으면 투명 위에 그린다(자가 검증).
        RGHandle color;

        /// 씬 깊이. 하늘은 씬이 안 그린 곳(깊이 1)에만 남는다.
        /// 비어 있으면 자체 transient를 만들어 지우고 쓴다(자가 검증).
        RGHandle depth;
    };

    void SetInputs(const Inputs& inputs) { m_inputs = inputs; }
    void SetKeepAlive(bool keepAlive) { m_keepAlive = keepAlive; }

    /// 그릴 큐브맵. 그래프 밖에서 이미 PIXEL_SHADER_RESOURCE 상태여야 한다.
    void SetCubeMap(ID3D12Resource* cubeMap, DXGI_FORMAT format, uint32_t mipLevels)
    {
        m_cubeMap = cubeMap;
        m_cubeMapFormat = format;
        m_cubeMapMips = mipLevels;
    }

    /// DX11의 scale 슬라이더에 해당. 눈 중심 큐브라 그림에는 영향이 없고
    /// 클립 범위 밖으로 나가지 않을 크기면 된다.
    void SetScale(float scale) { m_scale = scale; }

    RGHandle GetOutput() const { return m_output; }
    RGHandle GetDepth() const { return m_depth; }

private:
    template <typename T> using ComPtr = Microsoft::WRL::ComPtr<T>;

    bool CreatePipelines(const EnhancedFrameContext& context, std::string& outError);

    Inputs   m_inputs{};
    RGHandle m_output;
    RGHandle m_depth;
    bool     m_keepAlive{ false };

    ID3D12Resource* m_cubeMap{ nullptr };
    DXGI_FORMAT     m_cubeMapFormat{ DXGI_FORMAT_R16G16B16A16_FLOAT };
    uint32_t        m_cubeMapMips{ 1 };
    float           m_scale{ 500.f };

    // 프레임 밀봉 값(3-2).
    Mathf::Matrix  m_viewProjection{};
    Mathf::Vector4 m_eyePosition{};

    uint32_t m_width{ 0 };
    uint32_t m_height{ 0 };

    ID3D12PipelineState* m_pso{ nullptr };
    ID3D12RootSignature* m_rootSignature{ nullptr };
};

#endif
