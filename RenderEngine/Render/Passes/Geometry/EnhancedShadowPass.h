#pragma once
#include "../../../RHI/RHIFormat.h"
#ifndef DYNAMICCPP_EXPORTS
#include <array>
#include <atomic>
#include <unordered_map>
#include <vector>
#include <wrl/client.h>

#include "../../Graph/EnhancedRenderPass.h"
// ★ A-4. `DX12MeshCache.h` 를 물던 자리다. 메시 바인딩이 `RHIMeshBinding`
//   (중립)이 되면서 패스가 캐시 **구현 클래스**를 이름으로도 알 이유가
//   사라졌다 — 인터페이스는 `RenderFrameServices.h` 로 들어온다.
#include "../../../RHI/RHIParallelCommandPool.h"

// 그림자 패스 (PHASE 3-6, 세 번째 패스).
//
// 방향광 하나에 대한 3단 캐스케이드. 1차에서는 단일 캐스케이드로 뚫어야 할 넷을
// 먼저 확인했고(①깊이 전용 렌더 ②라이트 공간 행렬 ③다음 패스에서 SRV로 읽기
// ④비교 샘플러) 이제 그 위에 캐스케이드를 얹는다.
//
// 캐스케이드가 있어야 하는 이유는 해상도 배분이다. 한 장으로 프러스텀 전체를
// 덮으면 텍셀 하나가 먼 곳에서는 몇 미터를 담당하게 되고, 가까운 곳에 맞추면
// 먼 곳이 통째로 빠진다. 카메라 거리에 따라 여러 장을 나눠 덮으면 눈에 가까운
// 곳일수록 촘촘해진다.
//
// 세 가지 설계 결정:
//
//   맵을 텍스처 배열로 둔다. 텍스처 셋보다 낫다 — 리소스가 하나라 배리어도
//   하나고(전이가 전체 서브리소스 단위다), 셰이더에서 캐스케이드 번호가
//   좌표의 일부라 분기 없이 고를 수 있다. 아틀라스(가로로 이어 붙이기)보다도
//   낫다 — 아틀라스는 UV를 다시 매핑해야 하고 경계에서 필터가 옆 캐스케이드를
//   물어 온다.
//
//   캐스케이드마다 그릴 것을 추린다. 세 장을 그냥 다 그리면 정점 처리가 세
//   배가 된다. 캐스케이드 경계 구를 광원 방향으로 늘린 원기둥에 메시의 경계
//   구가 닿는지만 보면 되고, 이 판정은 싸다(내적 하나와 길이 하나).
//   '가리는 것'은 상자 밖에 있어도 상자 안에 그림자를 드리울 수 있으므로
//   상자만으로 자르면 그림자가 사라진다 — 늘리는 이유가 그것이다.
//
//   분할은 PSSM(로그·균등 혼합)으로 정한다. 저작 비율을 고정으로 두면 far가
//   바뀔 때마다 손봐야 하는데, 혼합식은 near/far에서 유도되므로 따라온다.
class EnhancedShadowPass : public EnhancedRenderPass
{
public:
    static constexpr uint32_t    kCascadeCount = kShadowCascadeCount;
    static constexpr uint32_t    kShadowMapSize = 2048;
    static constexpr RHIFormat kShadowFormat = RHIFormat::D32Float;

    // 로그 분할과 균등 분할의 혼합 비율. 1이면 순수 로그(가까운 쪽에 극단적으로
    // 몰린다), 0이면 균등(먼 쪽이 낭비된다). 0.75는 관행적인 절충이다.
    static constexpr float kSplitLambda = 0.75f;

    const char* GetName() const override { return "Shadow"; }

    bool Initialize(const EnhancedFrameContext& context, std::string& outError) override;
    bool PrepareFrame(const EnhancedFrameContext& context, std::string& outError) override;
    void Declare(EnhancedRenderGraph& graph, const EnhancedFrameContext& context) override;
    void Shutdown() override;

    RGHandle GetShadowMap() const { return m_shadowMap; }

    // 라이팅 패스가 그대로 상수 버퍼에 얹는 형태로 준다.
    const EnhancedShadowData& GetShadowData() const { return m_shadowData; }

    // 그림자를 드리울 방향광을 찾았는가. 없으면 그림자 없이 도는 것이 정상이다.
    bool HasDirectionalLight() const { return m_hasDirectionalLight; }

    // 캐스케이드 전체에서 실제로 그린 드로우 수와, 컬링으로 뺀 수.
    // 컬링이 도는지는 뺀 수가 말해 준다 — 0만 나오면 판정이 늘 참인 것이다.
    uint32_t GetLastDrawCount() const { return m_lastDrawCount.load(std::memory_order_relaxed); }
    uint32_t GetLastCulledCount() const { return m_lastCulledCount.load(std::memory_order_relaxed); }

    // 실제로 낸 드로우 콜 수. 드로우 수와 나란히 봐야 병합이 도는지 알 수 있다 —
    // GBuffer에서 배치 수를 안 찍었다가 병합이 통째로 죽어 있는 것을 놓칠 뻔했다.
    uint32_t GetLastBatchCount() const { return m_lastBatchCount.load(std::memory_order_relaxed); }

    /// 스킨드 캐스터로 그린 인스턴스 수. 0인데 씬에 스킨드가 있으면 그림자가
    /// 바인드 포즈로 나오고 있다는 뜻이다.
    uint32_t GetLastSkinnedDrawCount() const
    {
        return m_lastSkinnedDrawCount.load(std::memory_order_relaxed);
    }
    uint32_t GetLastBonePaletteCount() const
    {
        return static_cast<uint32_t>(m_boneOffsets.size());
    }

    // 기본 편향. 캐스케이드별로는 반지름 비만큼 키워 쓴다.
    void SetBias(float bias) { m_baseBias = bias; }

    // 경사 비례 계수 — 최종 편향 = 캐스케이드 편향 x (1 + 계수 x tan(경사각)).
    // 0이면 상수 편향만 쓴다(자가 검증의 A/B 재료).
    void SetSlopeScale(float scale) { m_slopeScale = scale; }

    // 캐스케이드 경계 블렌딩 폭(분할 깊이 비율). 0이면 하드 스위치.
    void SetCascadeBlendBand(float band) { m_blendBand = band; }

private:
    template <typename T> using ComPtr = Microsoft::WRL::ComPtr<T>;

    // 월드 행렬은 여기서 빠졌다 — 인스턴스 버퍼(t0)로 옮겼다. 광원 행렬은
    // 캐스케이드마다 한 번만 바뀌므로 상수 버퍼에 남는다.
    struct ShadowConstants
    {
        Mathf::Matrix lightViewProjection{};
    };

    /// 인스턴스 하나. HLSL의 ShadowInstance와 배치가 같아야 한다.
    /// 정적 경로도 같은 구조를 읽는다(boneOffset을 무시할 뿐).
    struct ShadowInstance
    {
        Mathf::Matrix world{};
        uint32_t      boneOffset{ kNoSkinning };
        uint32_t      padding[3]{};
    };

    static constexpr uint32_t kNoSkinning = 0xFFFFFFFFu;

    // 한 캐스케이드가 덮는 범위. 컬링 판정에 중심과 반지름이 필요해서 행렬만
    // 들고 있지 않는다.
    struct Cascade
    {
        Mathf::Matrix  lightViewProjection{};
        Mathf::Vector3 center{};
        float          radius{ 0.f };
        float          splitDepth{ 0.f };
    };

    struct Geometry
    {
        RHIMeshBinding entry{};
        float                boundRadius{ 0.f };
    };

    bool CreatePipeline(const EnhancedFrameContext& context, std::string& outError);

    /// 조각 수. 캐스케이드가 최소 단위이고 그 위로는 드로우 수를 본다.
    uint32_t ComputeSliceCount() const;
    void ComputeCascades(const EnhancedFrameContext& context);

    // 이 메시가 해당 캐스케이드에 그림자를 드리울 수 있는가.
    bool CastsInto(const Cascade& cascade, const Mathf::Vector3& center, float radius) const;

    RGHandle m_shadowMap;

    std::unordered_map<Mesh*, Geometry> m_drawGeometry;
    std::array<Cascade, kCascadeCount>  m_cascades{};

    EnhancedShadowData m_shadowData{};
    Mathf::Vector3     m_lightDirection{ 0.f, -1.f, 0.f };
    bool               m_hasDirectionalLight{ false };
    float              m_baseBias{ 0.0015f };

    // 경사 비례 계수 2는 관행 범위(1~3)의 가운데다. tan은 셰이더에서 8로
    // 상한을 두므로 최악에도 편향이 기본의 17배를 넘지 않는다.
    float              m_slopeScale{ 2.f };
    float              m_blendBand{ 0.15f };
    // 병렬 기록에서는 여러 워커가 동시에 센다. 단순 증가면 값이 조용히
    // 작아지고, 그러면 '컬링이 도는가'라는 단정이 우연히 통과한다.
    uint32_t              m_lastCasterCandidates{ 0 };
    std::atomic<uint32_t> m_lastDrawCount{ 0 };
    std::atomic<uint32_t> m_lastCulledCount{ 0 };
    std::atomic<uint32_t> m_lastBatchCount{ 0 };
    std::atomic<uint32_t> m_lastSkinnedDrawCount{ 0 };

    // 메시 기준으로 정렬한 드로우 인덱스. 원본을 건드리지 않으려고 인덱스만 든다.
    std::vector<size_t> m_sortedDraws;

    // 프레임의 본 팔레트(GBuffer와 같은 수집 규칙). 스킨드 캐스터가 없으면 빈다.
    std::vector<Mathf::Matrix>             m_bonePalettes;
    std::unordered_map<uint64_t, uint32_t> m_boneOffsets;

    RHIPipelineHandle m_pso;
    RHIPipelineHandle m_skinnedPso;
};

#endif
