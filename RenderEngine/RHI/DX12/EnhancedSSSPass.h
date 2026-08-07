#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include <cstdint>
#include <wrl/client.h>

#include "EnhancedRenderPass.h"

// 서브서피스 스캐터링 패스 (PHASE 3-6, 미구현 패스 이식 1차).
//
// ── 기존 DX11 SubsurfaceScatteringPass가 하는 일 ──
//
//   25샘플 커널로 방향성 블러를 두 번 돌린다(가로 → 세로). 각 샘플은
//   깊이를 함께 읽어 표면을 벗어나면 중심 색으로 되돌린다 — 그래야
//   실루엣 밖으로 피부색이 번지지 않는다.
//
//   ★ 읽으면서 쓸 수 없으므로 매 패스마다 씬 컬러를 통째로 복사한다
//     (CopyResource 2회, 화면 크기). 그리고 원본에 in-place로 덮어쓴다.
//
// ── 다시 쓰면서 바꾸는 것 ──
//
//   복사를 없앤다. 그래프에서 가로 블러가 transient에 쓰고 세로 블러가
//   그것을 읽어 최종에 쓰면, 같은 결과가 복사 없이 나온다 — 화면 크기
//   복사 두 번이 통째로 사라진다.
//
//   블렌드를 명시한다. DX11은 블렌드 상태를 세우지 않고 앞 패스가 남긴
//   것에 얹혀 갔다(SSR·VolumetricFog도 같다). DX12는 PSO에 박아야 하므로
//   숨어 있던 암묵 상태를 결정해야 한다 — 블러 결과는 덮어쓰는 것이 맞다.
//
//   MetalRough 바인딩은 뺀다. DX11이 t2에 물리지만 셰이더가 선언만 하고
//   읽지 않는다(SSS.ps.hlsl) — 죽은 바인딩을 옮길 이유가 없다.
//
//   direction 슬라이더도 뺀다. DX11 ControlPanel에 있지만 코드가 항상
//   (1,0)·(0,1)로 덮어써 값이 무시된다. 분리 블러는 축이 고정이어야
//   맞으므로 하드코딩이 옳고, 슬라이더 쪽이 죽은 UI다.
class EnhancedSSSPass : public EnhancedRenderPass
{
public:
    static constexpr DXGI_FORMAT kOutputFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;

    /// DX11 기본값 그대로. 그림의 기준선이 이 값이다.
    struct Tuning
    {
        float strength{ 1.35f };
        float width{ 0.013f };
    };

    const char* GetName() const override { return "SSS"; }

    bool Initialize(const EnhancedFrameContext& context, std::string& outError) override;
    bool PrepareFrame(const EnhancedFrameContext& context, std::string& outError) override;
    void Declare(EnhancedRenderGraph& graph, const EnhancedFrameContext& context) override;
    void Shutdown() override;

    struct Inputs
    {
        /// 산란시킬 그림(라이팅 결과).
        RGHandle color;

        /// 씬 깊이. 표면을 벗어난 샘플을 되돌리는 데 쓴다.
        RGHandle depth;
    };

    void SetInputs(const Inputs& inputs) { m_inputs = inputs; }
    void SetTuning(const Tuning& tuning) { m_tuning = tuning; }
    const Tuning& GetTuning() const { return m_tuning; }
    void SetKeepAlive(bool keepAlive) { m_keepAlive = keepAlive; }

    /// 꺼져 있으면 아무것도 선언하지 않고 GetOutput()이 입력을 그대로
    /// 돌려준다 — 뒤 패스가 분기 없이 이어진다(SSR과 같은 규약).
    ///
    /// 기본이 켜짐인 것은 DX11 기본값(isOn = true)과 자가 검증 때문이다.
    /// 라이브 배선은 여기 기본값을 쓰지 않고 저작이 고른 값을 넣는다 —
    /// 이 패스는 재질 마스크 없이 화면 전체를 번지게 하므로 켜고 끄는 것이
    /// 모든 씬의 그림을 바꾼다(포그와 같은 부류라 같은 처리를 한다).
    void SetEnabled(bool enabled) { m_enabled = enabled; }
    bool IsEnabled() const { return m_enabled; }

    /// 최종 결과(세로 블러까지 끝난 것).
    RGHandle GetOutput() const { return m_output; }

    /// 가로 블러 중간 결과. 검증이 '두 축이 각각 도는가'를 보는 데 쓴다.
    RGHandle GetHorizontal() const { return m_horizontal; }

private:
    template <typename T> using ComPtr = Microsoft::WRL::ComPtr<T>;

    bool CreatePipelines(const EnhancedFrameContext& context, std::string& outError);

    Inputs   m_inputs{};
    Tuning   m_tuning{};
    RGHandle m_output;
    RGHandle m_horizontal;
    bool     m_keepAlive{ false };
    bool     m_enabled{ true };

    // 프레임 밀봉 값(3-2). Record가 스냅샷을 다시 읽지 않는다.
    float m_cameraFov{ 0.f };

    uint32_t m_width{ 0 };
    uint32_t m_height{ 0 };

    // 블러 두 축이 각자 RTV를 쓴다.
    ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
    uint32_t                     m_rtvIncrement{ 0 };

    ID3D12PipelineState* m_pso{ nullptr };
    ID3D12RootSignature* m_rootSignature{ nullptr };
};

#endif
