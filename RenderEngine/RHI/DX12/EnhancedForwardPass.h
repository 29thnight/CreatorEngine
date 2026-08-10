#pragma once
#include "../RHIFormat.h"
#ifndef DYNAMICCPP_EXPORTS
#include <cstdint>
#include <map>
#include <wrl/client.h>

#include "EnhancedRenderPass.h"
#include "DX12TextureCache.h"

// Forward+ 패스 (PHASE 3-6, 신규 작성).
//
// ── 왜 Forward+인가 ──
//
// 기존 DX11 ForwardPass는 드로우마다 전체 광원을 상수 버퍼로 받아 픽셀마다
// 모든 광원을 돈다. 광원이 N개면 픽셀 비용이 N에 비례하고, 실제로는 대부분의
// 픽셀에 닿는 광원이 몇 개뿐이므로 그 차이가 전부 낭비다.
//
// Forward+(타일드 포워드)는 상용 엔진(언리얼 모바일 포워드, 유니티 URP
// Forward+)이 쓰는 방식이다. 화면을 타일로 나누고, 컴퓨트가 타일마다
// '이 타일에 닿는 광원 목록'을 만든 뒤, 픽셀 셰이더는 자기 타일의 목록만
// 돈다. 비용이 '광원 수'가 아니라 '내 타일에 닿는 광원 수'에 비례한다.
//
// ── 구조 (두 단계) ──
//
//   1. 광원 컬링(컴퓨트) — 타일(16x16 픽셀)마다:
//      · 깊이 버퍼에서 타일의 min/max 깊이를 구한다 (불투명 깊이 재사용 —
//        GBuffer가 이미 채웠으므로 공짜다)
//      · 타일 프러스텀(4개 옆면 + min/max 깊이)을 만든다
//      · 광원 구를 프러스텀과 교차 검사해 통과한 인덱스를 타일 목록에 쓴다
//   2. 포워드 셰이딩(그래픽스) — forwardQueue의 드로우를 그리며, 픽셀
//      셰이더가 SV_Position에서 타일 번호를 계산해 그 목록의 광원만 돈다.
//
// ── 자료 배치 ──
//
//   광원 배열     StructuredBuffer<EnhancedLight> — 프레임마다 업로드 링에서
//   타일 목록     RWStructuredBuffer<uint>, 타일당 kMaxLightsPerTile 슬롯 고정
//   타일 카운트   RWStructuredBuffer<uint>, 타일당 1개
//
//   가변 길이 목록(오프셋 + 컴팩션) 대신 고정 슬롯을 쓴다. 컴팩션은 전역
//   원자 카운터가 필요해 컬링이 직렬화 지점을 갖게 되는데, 타일당 몇 개
//   안 되는 씬에서는 고정 슬롯의 메모리 낭비(타일 수 × 슬롯 × 4바이트)가
//   그 복잡성보다 싸다. 1920x1080 = 8160 타일 × 32슬롯 × 4B ≈ 1MB.
//
// ── 판정 ──
//
//   정확성: 같은 씬을 '전 광원 루프'(참조 경로)로도 그려 픽셀을 대조한다.
//     컬링이 광원을 잘못 떨어뜨리면 경계 타일이 어두워지는데, 그 증상은
//     눈으로 잡기 어렵다 — 대조가 잡아야 한다.
//   성능: 광원 수를 늘려 가며 GPU 시간을 잰다. 컬링 비용 + 셰이딩 비용이
//     전 광원 루프보다 싸지는 교차점이 어디인지 본다. 광원이 몇 개 안 되면
//     Forward+가 오히려 손해일 수 있다 — 그 경계를 실측으로 적는다.
//
// ── 실측 (1280x720, dx12.forwardscale) ──
//
//   광원     1 — 컬링 0.017 + 셰이딩 0.017 = 0.035 ms · 참조 0.016 ms
//   광원     4 — 컬링 0.017 + 셰이딩 0.017 = 0.035 ms · 참조 0.033 ms
//   광원    16 — 컬링 0.018 + 셰이딩 0.018 = 0.037 ms · 참조 0.101 ms  ← 경계
//   광원    64 — 컬링 0.020 + 셰이딩 0.029 = 0.049 ms · 참조 0.377 ms
//   광원   256 — 컬링 0.034 + 셰이딩 0.083 = 0.117 ms · 참조 1.479 ms
//   광원  1024 — (타일 3446개 넘침 — 광원을 버린 수라 경계 판정에서 제외)
//
//   ★ 경계는 16개다. 그 아래에서는 컬링 디스패치(약 0.017ms)가 그대로
//     손해라 전 광원 루프가 낫다. 광원 넷까지는 참조가 빠르고, 여덟쯤에서
//     비슷해진다.
//
//   ★ 참조 경로는 광원 수에 정직하게 비례한다(1→1024에서 약 370배).
//     Forward+는 8.3배에 그친다 — 픽셀당 도는 광원이 타일 목록 길이로
//     묶이기 때문이고, 이것이 이 구조를 쓰는 이유 전부다.
//
//   ★ 실제 씬은 광원이 화면 전체에 이렇게 고르게 깔리지 않으므로 이 수는
//     상한이 아니라 기준점이다. 다만 '16개 아래면 안 쓰는 게 낫다'는 방향은
//     배치와 무관하게 성립한다 — 컬링 비용이 광원 수와 거의 무관하기 때문이다.
//
// SSGI에서 배운 규율을 그대로 쓴다: 셰이더는 부르는 자리가 생겨야 컴파일
// 오류가 드러난다(자가 검증 먼저), 진단 리드백 없이는 '도는 것처럼 보이는'
// 상태를 구분할 수 없다(타일 카운트 리드백을 처음부터), 상수는 실측으로.
class EnhancedForwardPass : public EnhancedRenderPass
{
public:
    /// 타일 한 변(픽셀). 16은 컴퓨트 스레드 그룹과 일치시키기 좋고,
    /// 상용 엔진들의 통상값이다. 실측으로 바꿀 근거가 생기면 바꾼다.
    static constexpr uint32_t kTileSize = 16;

    /// 타일당 최대 광원 수. 넘치면 앞에서부터 자르고, 자르기 전의 수를
    /// 타일 카운트 버퍼 뒤쪽 절반에 그대로 남긴다 — 조용히 자르면
    /// '광원이 가끔 사라진다'가 되어 원인을 못 찾는다.
    ///
    /// ★ 실측(1280x720, 3600타일)에서 이 상한이 성능 결론을 뒤집었다.
    ///   광원 1024개 배치에서 3446타일이 넘쳤고(최대 60), 그 지점의
    ///   Forward+ 시간은 '빨라진 것'이 아니라 '덜 그린 것'이었다.
    ///   256개까지는 최대 15로 여유가 있다.
    static constexpr uint32_t kMaxLightsPerTile = 32;

    static constexpr RHIFormat kOutputFormat = RHIFormat::RGBA16Float;

    /// GBuffer가 만든 깊이를 그대로 쓴다. 포맷이 어긋나면 PSO 생성은
    /// 통과하고 실행에서 검증 레이어가 운다.
    static constexpr RHIFormat kDepthFormat = RHIFormat::D32Float;

    const char* GetName() const override { return "Forward+"; }

    bool Initialize(const EnhancedFrameContext& context, std::string& outError) override;
    bool PrepareFrame(const EnhancedFrameContext& context, std::string& outError) override;
    void Declare(EnhancedRenderGraph& graph, const EnhancedFrameContext& context) override;
    void Shutdown() override;

    struct Inputs
    {
        RGHandle depth;      // GBuffer 깊이 — 타일 min/max의 원천
        RGHandle lighting;   // 불투명 결과 — 포워드는 이 위에 그린다
    };

    void SetInputs(const Inputs& inputs) { m_inputs = inputs; }

    /// 참조 경로(전 광원 루프)로 그린다. 대조와 성능 기준선 측정용이다.
    void SetUseReferencePath(bool use) { m_useReferencePath = use; }

    RGHandle GetOutput() const { return m_output; }

    /// 컬링 통계. 타일 목록이 실제로 채워지는지 밖에서 본다 —
    /// SSGI의 누적 카운터와 같은 역할이다. 늘 0이면 컬링이 죽은 것이다.
    uint32_t GetLastCulledLightCount() const { return m_lastCulledLights; }
    uint32_t GetLastOverflowTileCount() const { return m_lastOverflowTiles; }

    /// 타일 버퍼. 소유는 패스이고(프레임을 넘겨 산다), 상태는 그래프가
    /// 관리한다 — Declare가 매 프레임 Import하고 writeback으로 끝 상태를
    /// 돌려받는다(R4-2b).
    ID3D12Resource* GetTileCountBuffer() const { return m_tileCountBuffer.Get(); }
    ID3D12Resource* GetTileListBuffer() const { return m_tileListBuffer.Get(); }

    /// Declare가 그래프에 들인 타일 버퍼의 핸들. 자가 검증의 리드백 패스가
    /// 자기 usage를 선언하는 데 쓴다 — 예전처럼 손 배리어로 before=UAV를
    /// 단정하면 셰이딩이 SRV로 바꾼 프레임에서 어긋난다(실제로 그렇게 잡혔다).
    RGHandle GetTileCountHandle() const { return m_tileCountHandle; }
    RGHandle GetTileListHandle() const { return m_tileListHandle; }

    /// 셰이딩 파이프라인. 자가 검증이 컬링 경로와 참조 경로를 같은 기하로
    /// 그려 픽셀을 대조한다 — 둘이 다르면 컬링이 광원을 잘못 떨어뜨린 것이다.
    ID3D12PipelineState* GetShadePSO() const { return m_shadePSO; }
    ID3D12PipelineState* GetReferencePSO() const { return m_referencePSO; }
    ID3D12RootSignature* GetShadeRootSignature() const { return m_shadeRootSignature; }

    /// IBL 앰비언트. Deferred가 받는 것과 같은 자원을 넘겨야 한다 —
    /// 한쪽만 앰비언트를 받으면 같은 재질이 투명일 때만 어둡게 보인다.
    /// 널을 넘기면 앰비언트가 0이 된다(기존 동작).
    void SetIBL(ID3D12Resource* irradiance, ID3D12Resource* prefiltered,
        uint32_t prefilterMips, ID3D12Resource* brdfLut)
    {
        m_iblIrradiance = irradiance;
        m_iblPrefiltered = prefiltered;
        m_iblPrefilterMips = (0 == prefilterMips) ? 1u : prefilterMips;
        m_iblBrdfLut = brdfLut;
    }

    /// 캐스케이드 그림자. Deferred가 받는 것과 같은 자원·같은 구조를 넘겨야
    /// 한다 — 한쪽만 그림자를 받으면 같은 자리에서 불투명은 그늘인데 투명만
    /// 밝은, 물체와 무관한 경계가 생긴다. 안 넣으면 그림자 없이 돈다.
    ///
    /// ★ 받는 쪽만이다. 투명 기하는 그림자 맵에 그려지지 않으므로(그림자
    ///   패스가 context.draws만 래스터한다) 자기 그림자도, 남에게 드리우는
    ///   그림자도 없다. 유리에 필요한 것은 어차피 다른 계산이라 그 자리에
    ///   불투명 캐스터를 밀어 넣는 것은 답이 아니다.
    void SetShadow(RGHandle shadowMap, const EnhancedShadowData& data)
    {
        m_shadowMap = shadowMap;
        m_shadowData = data;
    }

private:
    template <typename T> using ComPtr = Microsoft::WRL::ComPtr<T>;

    bool CreatePipelines(const EnhancedFrameContext& context, std::string& outError);
    bool EnsureTileBuffers(const EnhancedFrameContext& context, std::string& outError);

    /// 드로우 기록. 컬링 경로와 참조 경로가 이 함수를 공유한다 — 대조가
    /// 뜻을 가지려면 PSO 말고는 아무것도 달라선 안 된다.
    /// shadowResource는 그래프가 푼 그림자 맵이다. 핸들이 아니라 포인터로
    /// 받는 이유는 이 함수가 ExecuteContext를 안 보기 때문이다 — 자가 검증도
    /// 같은 함수를 쓰고, 그쪽에는 그래프가 없다.
    /// 인코더만 받는다. R4-1b에서 함께 받던 커맨드 리스트는 디스크립터 힙
    /// 바인딩 하나에만 쓰였고, R4-1c가 그것을 인코더의 지연 바인딩으로
    /// 옮기면서 마지막 쓰임이 사라졌다.
    bool RecordShading(class RHIEncoder& encoder,
        const EnhancedFrameContext& context, ID3D12PipelineState* pso, uint32_t lightCount,
        ID3D12Resource* shadowResource);

    Inputs   m_inputs{};
    RGHandle m_output;
    RGHandle m_tileList;     // 타일별 광원 인덱스 (고정 슬롯)
    RGHandle m_tileCount;    // 타일별 광원 수

    uint32_t m_tileCountX{ 0 };
    uint32_t m_tileCountY{ 0 };

    // 타일 버퍼는 그래프 transient가 아니다 — 프레임을 넘겨 살아야 한다
    // (transient는 프레임마다 풀로 돌아간다). 다만 그래프 밖 리소스는 아니다:
    // Declare가 매 프레임 Import한다. "그래프가 버퍼를 지원하지 않는다"고
    // 적혀 있던 것은 재 보니 틀렸다 — ImportTexture는 desc를 안 건드려
    // 버퍼도 그대로 들어간다.
    ComPtr<ID3D12Resource> m_tileCountBuffer;
    ComPtr<ID3D12Resource> m_tileListBuffer;
    uint32_t               m_allocatedTiles{ 0 };

    /// 매 프레임 Declare가 그래프에 들이고 받는다.
    RGHandle m_tileCountHandle;
    RGHandle m_tileListHandle;

    /// 프레임 끝 상태. 그래프가 writeback으로 적어 준다.
    ///
    /// ★ '늘 UAV'로 가정하면 안 된다 — 셰이딩이 도는 프레임은 SRV로,
    ///   리드백이 붙은 프레임은 CopySource로 끝난다. 예전 코드는 패스가
    ///   프레임 끝에 손으로 UAV로 되돌려 그 가정을 지켰는데, 이제 그래프가
    ///   실제 끝 상태를 적어 주므로 되돌릴 필요가 없다.
    ///
    /// 초기값 Common은 CreateBuffer의 초기 상태다. 버퍼를 다시 만들면
    /// (EnsureTileBuffers) 여기도 Common으로 되돌려야 한다 — 새 리소스는
    /// COMMON인데 멤버가 옛 상태를 들고 있으면 첫 배리어의 before가 틀린다.
    RGResourceState m_tileCountState{ RGResourceState::Common };
    RGResourceState m_tileListState{ RGResourceState::Common };

    // 셰이딩 출력의 RTV 자리. RTV는 CBV/SRV/UAV 링에서 자를 수 없어
    // 별도 힙이 필요하다(GBuffer도 같은 이유로 자체 힙을 든다).

    uint32_t m_lastCulledLights{ 0 };
    uint32_t m_lastOverflowTiles{ 0 };
    bool     m_useReferencePath{ false };

    ID3D12PipelineState* m_cullPSO{ nullptr };
    ID3D12PipelineState* m_shadePSO{ nullptr };

    // 참조 경로(전 광원 루프) PSO. 대조의 정답지이자 성능 비교의 기준선이다.
    // 셰이더는 같고 매크로 하나만 다르다 — 조명 계산이 달라지면 무엇 때문에
    // 결과가 다른지 알 수 없게 된다.
    ID3D12PipelineState* m_referencePSO{ nullptr };

    ID3D12RootSignature* m_cullRootSignature{ nullptr };
    ID3D12RootSignature* m_shadeRootSignature{ nullptr };

    // 재질 텍스처 샘플러. 샘플러 힙이 중복을 걸러 주므로 GBuffer와 같은
    // 설정이면 같은 핸들이 온다.
    RHISamplerTable             m_sampler{};

    // 이번 프레임에 올려 둔 베이스 컬러. PrepareFrame이 채우고 기록은
    // 조회만 한다 — GetOrUpload는 업로드 링과 커맨드 리스트를 쓰므로
    // 그래프 실행 중에 부르면 기록 한가운데에 복사가 끼어든다
    // (GBuffer가 같은 이유로 PrepareFrame에서 올린다).
    //
    // 키는 재질 텍스처 포인터 넷의 동일성이다 — GBuffer의 MaterialKey와
    // 같은 근거이고, 메시가 아니라 재질로 키잉해야 같은 메시를 다른 재질로
    // 두 번 그릴 때 두 번째가 첫 번째의 텍스처로 그려지지 않는다.
    struct MaterialKey
    {
        Texture* textures[4]{};   // baseColor · normal · ORM · emissive
        Texture* operator[](size_t i) const { return textures[i]; }
        bool operator<(const MaterialKey& other) const
        {
            for (size_t i = 0; i < 4; ++i)
            {
                if (textures[i] != other.textures[i]) return textures[i] < other.textures[i];
            }
            return false;
        }
    };

    struct MaterialTextures
    {
        ID3D12Resource* resources[4]{};
        DXGI_FORMAT     formats[4]{};
        uint32_t        mipLevels[4]{};
    };

    // 해시맵 대신 정렬 맵. 재질 종류는 프레임당 많아야 수십이고, 배열 키에
    // 해시를 손으로 붙이면 그 해시가 또 검증 대상이 된다(GBuffer와 같은 판단).
    std::map<MaterialKey, MaterialTextures> m_materialTextures;

    RGHandle           m_shadowMap;
    EnhancedShadowData m_shadowData{};

    // IBL 셋. Deferred와 같은 자원을 받는다 — 앰비언트가 두 경로에서
    // 갈리면 같은 재질이 투명일 때만 어둡게 보인다.
    ID3D12Resource* m_iblIrradiance{ nullptr };
    ID3D12Resource* m_iblPrefiltered{ nullptr };
    ID3D12Resource* m_iblBrdfLut{ nullptr };
    uint32_t        m_iblPrefilterMips{ 1 };
};

#endif
