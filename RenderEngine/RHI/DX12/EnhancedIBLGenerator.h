#pragma once
#include "../RHIFormat.h"
#ifndef DYNAMICCPP_EXPORTS
#include <cstdint>
#include <wrl/client.h>

#include "EnhancedRenderPass.h"

// IBL 생성 체인 (PHASE 3-6 — DX11 SkyBoxPass의 생성 절반).
//
// equirect HDR 한 장에서 넷을 만든다:
//   ① 큐브맵            rect→cube 투영 (스카이박스가 그린다)
//   ② 조도 맵           코사인 가중 반구 적분 (디퓨즈 앰비언트)
//   ③ 프리필터 스페큘러  GGX 중요도 샘플링, 밉마다 거칠기 0~1 (정반사 앰비언트)
//   ④ BRDF LUT          split-sum의 (scale, bias) 사전 적분
//
// 프레임 패스가 아니다 — 씬 로드 때 한 번 도는 생성 작업이라
// EnhancedRenderPass/그래프를 태우지 않고, 메시 업로드처럼 열린 프레임에
// 커맨드를 직접 기록한다(Generate는 BeginFrame과 EndFrame 사이에서 부른다).
//
// ── 이식 원칙 ──
//
//   셰이더 수식은 DX11(RectToCubeMap·IrradianceMap·SpecularPreFilter·
//   IntegrateBRDF)을 그대로 옮긴다 — 하드 클램프·휘도 폐기·로그 공간
//   평균 같은 톤 정책과, 조도 적분의 NoL 이중 가중(코사인 샘플링의 pdf에
//   이미 든 cosθ를 다시 곱한다 — 수학적으로는 cos² 가중 편향)까지
//   유지한다. 그림의 기준선이 그 결과물이다. 발견 내역은 이식 검수
//   기록에 남겼고, 고치는 것은 별도 결정이다.
//
//   큐브 메시 + 면별 직교 카메라(DX11 방식)는 두지 않는다 — 면 기저를
//   상수로 넘겨 풀스크린 삼각형의 uv에서 방향을 만든다. 만들어지는
//   방향 집합은 D3D 큐브 면 규약과 동일하므로 결과 텍셀은 같다.
class EnhancedIBLGenerator
{
public:
    static constexpr RHIFormat kFormat = RHIFormat::RGBA16Float;

    /// 프리필터 밉 수. DX11은 거칠기 i/5로 여섯 단계를 만든다.
    static constexpr uint32_t kPrefilterMips = 6;

    bool Initialize(const EnhancedFrameContext& context, std::string& outError);
    void Shutdown();

    /// equirect HDR에서 네 산출물을 전부 만든다.
    ///
    /// 열린 프레임이 필요하다(BeginFrame~EndFrame 사이) — 커맨드 기록과
    /// 리소스 생성을 겸하므로 그래프 Record에서 부르면 3-6 규약 위반이다.
    /// equirect는 PIXEL_SHADER_RESOURCE 상태여야 한다.
    /// 완료 후 네 산출물 모두 PIXEL_SHADER_RESOURCE 상태다.
    bool Generate(const EnhancedFrameContext& context,
        ID3D12Resource* equirect, DXGI_FORMAT equirectFormat,
        uint32_t cubeSize, uint32_t brdfSize, std::string& outError);

    // ★ 핸들로 낸다(V2-b). 소비처가 전부 RHIBindingDesc::SrvCube/Srv2D이고
    //   그것이 이제 핸들을 받는다. ComPtr은 여기가 계속 들고, 표에는
    //   빌려주기로 올린다 — Shutdown에서 놓는다.
    RHITextureHandle GetCubeMap() const { return m_cubeMapHandle; }
    RHITextureHandle GetIrradianceMap() const { return m_irradianceHandle; }
    RHITextureHandle GetPrefilteredMap() const { return m_prefilteredHandle; }
    RHITextureHandle GetBrdfLut() const { return m_brdfLutHandle; }

    uint32_t GetCubeSize() const { return m_cubeSize; }

private:
    template <typename T> using ComPtr = Microsoft::WRL::ComPtr<T>;

    bool CreatePipelines(const EnhancedFrameContext& context, std::string& outError);
    bool CreateTargets(ID3D12Device* device, uint32_t cubeSize, uint32_t brdfSize,
        std::string& outError);

    uint32_t m_cubeSize{ 0 };
    uint32_t m_brdfSize{ 0 };

    ComPtr<ID3D12Resource> m_cubeMap;
    ComPtr<ID3D12Resource> m_irradianceMap;
    ComPtr<ID3D12Resource> m_prefilteredMap;
    ComPtr<ID3D12Resource> m_brdfLut;

    // 표에 놓으려면 Shutdown도 서비스를 알아야 한다. Shutdown이 인자를
    // 받지 않으므로 Generate가 기억해 둔다 — 이것 없이는 Shutdown이 등록을
    // 못 놓아 재생성마다 칸 넷이 샌다.
    class IRenderDeviceServices* m_services{ nullptr };

    RHITextureHandle m_cubeMapHandle;
    RHITextureHandle m_irradianceHandle;
    RHITextureHandle m_prefilteredHandle;
    RHITextureHandle m_brdfLutHandle;

    // 6(큐브) + 6(조도) + 36(프리필터 6밉x6면) + 1(LUT)
    ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
    uint32_t                     m_rtvIncrement{ 0 };

    ID3D12PipelineState* m_rectToCubePso{ nullptr };
    ID3D12PipelineState* m_irradiancePso{ nullptr };
    ID3D12PipelineState* m_prefilterPso{ nullptr };
    ID3D12PipelineState* m_brdfPso{ nullptr };
    ID3D12RootSignature* m_rootSignature{ nullptr };
};

#endif
