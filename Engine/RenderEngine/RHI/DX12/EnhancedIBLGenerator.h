#pragma once
#include "../RHIFormat.h"
#include <cstdint>

#include "../../Render/Graph/EnhancedRenderPass.h"

// IBL 생성 체인 (PHASE 3-6 — DX11 SkyBoxPass의 생성 절반).
//
// equirect HDR 한 장에서 넷을 만든다:
//   ① 큐브맵            rect→cube 투영 + 밉 체인 (스카이박스가 밉 0을 그린다)
//   ② 조도 맵           코사인 가중 반구 적분 (디퓨즈 앰비언트)
//   ③ 프리필터 스페큘러  GGX 중요도 샘플링, 밉마다 거칠기 0~1 (정반사 앰비언트)
//   ④ BRDF LUT          split-sum의 (scale, bias) 사전 적분
//
// 환경 밝기와 cosine/GGX 분포를 함께 표집한다(MIS). 중요도 격자의
// radiance와 PDF는 동일한 셀을 사용하며, 거칠기 0은 원본 밉 0을 보존한다.
//
// 프레임 패스가 아니다 — 씬 로드 때 한 번 도는 생성 작업이라
// EnhancedRenderPass/그래프를 태우지 않고, 열린 프레임의 중립 즉시 인코더에
// 커맨드를 기록한다(Generate는 BeginFrame과 EndFrame 사이에서 부른다).
//
// ── 이식 이력 ──
//
//   출발은 DX11(RectToCubeMap·IrradianceMap·SpecularPreFilter·IntegrateBRDF)
//   의 이식이었고 한동안 그쪽 quirk를 그대로 뒀다 — 하드 클램프·휘도 폐기,
//   조도 적분의 NoL 이중 가중(cos² 편향). 셋 다 그 뒤 바로잡았다. 마지막까지
//   남아 있던 것이 누적값을 눌러 잡음을 잡는 방식이었는데, 형태를 바꿔도
//   에너지를 깎는다는 것이 실측으로 드러나 밉 기반 사전 필터로 갈아탔다.
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

    // 소스에서 직접 다운샘플하므로 큰 밉의 단일 드로우 비용을 제한한다.
    static constexpr uint32_t kMaxEnvironmentMips = 7;
    static constexpr uint32_t kImportanceMaxSize = 128;
    static constexpr uint32_t kImportanceSampleCount = 1024;
    static constexpr RHIFormat kImportanceFormat = RHIFormat::RGBA32Float;

    static constexpr uint32_t CubeMipCount(uint32_t cubeSize)
    {
        uint32_t mips = 1;
        while (cubeSize > 1 && mips < kMaxEnvironmentMips)
        {
            cubeSize >>= 1;
            ++mips;
        }
        return mips;
    }

    bool Initialize(const EnhancedFrameContext& context, std::string& outError);
    void Shutdown();

    /// equirect HDR에서 네 산출물을 전부 만든다.
    ///
    /// 열린 프레임이 필요하다(BeginFrame~EndFrame 사이) — 커맨드 기록과
    /// 리소스 생성을 겸하므로 그래프 Record에서 부르면 3-6 규약 위반이다.
    /// equirect는 PIXEL_SHADER_RESOURCE 상태여야 한다.
    /// 완료 후 네 산출물 모두 PIXEL_SHADER_RESOURCE 상태다.
    bool Generate(const EnhancedFrameContext& context,
        RHITextureHandle equirect, RHIFormat equirectFormat,
        uint32_t cubeSize, uint32_t brdfSize, std::string& outError);

    // 핸들로 낸다. 소비처는 RHIBindingDesc::SrvCube/Srv2D로 그대로 받으며,
    // 실물 소유권은 CreateTexture를 수행한 backend resource table에 있다.
    RHITextureHandle GetCubeMap() const { return m_cubeMapHandle; }
    RHITextureHandle GetIrradianceMap() const { return m_irradianceHandle; }
    RHITextureHandle GetPrefilteredMap() const { return m_prefilteredHandle; }
    RHITextureHandle GetBrdfLut() const { return m_brdfLutHandle; }

    uint32_t GetCubeSize() const { return m_cubeSize; }
    uint32_t GetIrradianceSize() const { return (m_cubeSize < 64u) ? m_cubeSize : 64u; }

private:
    void ReleaseTargets();
    bool CreatePipelines(const EnhancedFrameContext& context, std::string& outError);
    bool CreateTargets(uint32_t cubeSize, uint32_t brdfSize,
        std::string& outError);

    uint32_t m_cubeSize{ 0 };
    uint32_t m_brdfSize{ 0 };
    uint32_t m_importanceSize{ 0 };
    uint32_t m_importanceMip{ 0 };

    // CreateTexture가 실물 소유권까지 백엔드 표에 둔다. 생성기는 핸들만 들고,
    // 재생성·Shutdown에서 GPU 완료가 보장된 시점에 ReleaseTexture한다.
    class IRenderDeviceServices* m_resources{ nullptr };

    RHITextureHandle m_cubeMapHandle;
    // rect→cube의 착지점. 밉 체인은 여기서 읽어 최종 큐브에 굽는다 —
    // RHITransition이 텍스처 전체만 전이해서, 한 리소스 안에서 밉을 읽으며
    // 다른 밉에 쓸 수가 없다.
    RHITextureHandle m_cubeSourceHandle;
    RHITextureHandle m_irradianceHandle;
    RHITextureHandle m_prefilteredHandle;
    RHITextureHandle m_brdfLutHandle;
    RHITextureHandle m_importanceRows;
    RHITextureHandle m_importanceMarginal;
    RHITextureHandle m_importanceSamples;

    RHIPipelineHandle m_rectToCubePso;
    RHIPipelineHandle m_cubeDownsamplePso;
    RHIPipelineHandle m_irradiancePso;
    RHIPipelineHandle m_prefilterPso;
    RHIPipelineHandle m_brdfPso;
    RHIPipelineHandle m_importanceRowsPso;
    RHIPipelineHandle m_importanceMarginalPso;
    RHIPipelineHandle m_importanceSamplesPso;
};

