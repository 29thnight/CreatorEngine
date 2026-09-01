#pragma once
#include "../../../RHI/RHIFormat.h"
#include <array>
#include <map>
#include <memory>
#include <mathematics/color.hpp>
#include <span>
#include <vector>
#include <unordered_map>
#include <wrl/client.h>

#include "../../Graph/EnhancedRenderPass.h"
// ★ A-4. `DX12MeshCache.h` 를 물던 자리다. 메시 바인딩이 `RHIMeshBinding`
//   (중립)이 되면서 패스가 캐시 **구현 클래스**를 이름으로도 알 이유가
//   사라졌다 — 인터페이스는 `RenderFrameServices.h` 로 들어온다.
#include "../../../RHI/RHIParallelCommandPool.h"
#include "../../../RHI/RHIGraphicsPipelineRequest.h"
#include "../../../ShaderMetaHandle.h"

struct ShaderMeta;
struct ShaderMetaBindingLayout;
struct ShaderRenderState;
class RHIShaderBlob;

// GBuffer 패스 (PHASE 3-6, 첫 패스).
//
// DX11 GBufferPass를 옮기지 않고 새로 쓴다 — 방향 정정(2026-08-01)대로다.
// 다만 출력 레이아웃은 맞춘다. 정확성 판정이 'DX11과 같은 그림이 나오는가'이므로
// 타깃 구성이 다르면 대조 자체가 성립하지 않는다.
//
// DX11 쪽 구성(SceneRenderer::InitializeTextures):
//   Diffuse      R16G16B16A16_FLOAT
//   MetalRough   R16G16B16A16_FLOAT
//   Normal       R16G16B16A16_FLOAT
//   Emissive     R16G16B16A16_FLOAT
//   Bitmask      R32_UINT
//   + 깊이
//
// 이 슬라이스에서 새로 뚫는 것: 정점/인덱스 버퍼(입력 조립), MRT 5개 동시 출력,
// 깊이 버퍼, 그래프를 통한 transient 타깃 선언과 자동 배리어.
// 재질·텍스처·실제 씬 지오메트리는 씬 연결 슬라이스에서 붙인다.
class EnhancedGBufferPass : public EnhancedRenderPass
{
public:
    static constexpr uint32_t kRenderTargetCount = 5;

    // 다음 패스(Deferred)가 읽을 수 있도록 그래프 핸들을 내놓는다.
    struct Outputs
    {
        RGHandle diffuse;
        RGHandle metalRough;
        RGHandle normal;
        RGHandle emissive;
        RGHandle bitmask;
        RGHandle depth;
    };

    const char* GetName() const override { return "GBuffer"; }

    bool Initialize(const EnhancedFrameContext& context, std::string& outError) override;
    /// frame packet의 primary ShaderMeta generation을 이 패스의 default request로
    /// 전환한다. 후보 compile/PSO 생성이 끝난 뒤에만 같은 catalog slot을 retire한다.
    bool ApplyShaderMeta(const EnhancedFrameContext& context,
        ShaderMetaHandle handle, const ShaderMeta& meta,
        RHICompletionPoint retireAfter, std::string& outError);
    /// frame packet이 소유한 ShaderMeta generation의 material keyword 변형을
    /// candidate-first로 준비한다. primary와 다른 meta도 같은 pipeline layout을
    /// 만족하면 게시하며, 실패해도 default/기존 variant는 유지한다.
    bool EnsureShaderMetaVariant(const EnhancedFrameContext& context,
        ShaderMetaHandle handle, const ShaderMeta& meta,
        std::span<const std::uint16_t> keywordSelections,
        RHIShaderPermutationKey& outPermutationKey,
        std::shared_ptr<const ShaderMetaBindingLayout>& outLayout,
        std::string& outError);
    /// 이번 frame packet에서 성공적으로 준비한 generation만 남긴다. 제거할 키가
    /// 다른 키와 같은 cache handle을 공유하면 마지막 holder가 사라질 때만 retire한다.
    std::uint32_t CommitShaderMetaFrame(const EnhancedFrameContext& context,
        std::span<const ShaderMetaHandle> activeHandles,
        RHICompletionPoint retireAfter);
    bool PrepareFrame(const EnhancedFrameContext& context, std::string& outError) override;
    void Declare(EnhancedRenderGraph& graph, const EnhancedFrameContext& context) override;
    void Shutdown() override;

    // 이번 프레임에 실제로 그린 드로우 수. 씬 연결이 됐는지 확인하는 값이다.
    /// 이번 프레임에 그릴 드로우 수(같은 메시라도 드로우마다 센다).
    uint32_t GetLastDrawCount() const { return m_lastDrawCount; }

    /// 올린 메시 종류와 재질 종류. 둘이 드로우 수와 다른 것이 정상이다 —
    /// 같은 메시를 여러 번 그리거나 같은 재질을 여럿이 공유한다.
    /// 재질 수가 늘 1이면 드로우별 키잉이 죽은 것이다.
    uint32_t GetLastMeshCount() const { return m_lastMeshCount; }
    uint32_t GetLastMaterialCount() const { return m_lastMaterialCount; }

    /// 실제로 발행한 DrawIndexedInstanced 횟수. 드로우 수보다 작을수록 병합이
    /// 잘 된 것이다 — 이 둘이 늘 같으면 인스턴싱이 죽은 것이다.
    uint32_t GetLastBatchCount() const { return m_lastBatchCount; }

    /// 스킨드 드로우 수와 올린 팔레트 종류. 팔레트 수가 스킨드 드로우 수와
    /// 늘 같으면 애니메이터 중복 제거가 죽은 것이다(한 캐릭터의 메시 여럿이
    /// 같은 팔레트를 공유해야 한다).
    uint32_t GetLastSkinnedCount() const { return m_lastSkinnedCount; }
    uint32_t GetLastBonePaletteCount() const
    {
        return static_cast<uint32_t>(m_boneOffsets.size());
    }

    /// 이 패스를 컬링 뿌리로 표시할지.
    ///
    /// 소비자(Deferred)가 붙기 전에는 GBuffer 출력을 읽는 패스가 없어서 컬링에
    /// 걸린다. 그때만 true로 살려 둔다. 소비자가 붙으면 false로 두는 것이 맞고,
    /// 그래도 살아남는지가 곧 3-5 컬링의 실전 확인이다.
    void SetKeepAlive(bool keepAlive) { m_keepAlive = keepAlive; }

    // Declare 뒤에 유효하다.
    const Outputs& GetOutputs() const { return m_outputs; }

    static RHIFormat GetRenderTargetFormat(uint32_t index);
    static constexpr RHIFormat kDepthFormat = RHIFormat::D32Float;
    RHIPipelineHandle GetPipelineHandle() const { return m_pipelineRequest.GetHandle(); }
    std::uint32_t GetShaderVariantCount() const
    {
        return static_cast<std::uint32_t>(m_shaderVariants.size()) +
            (m_shaderMetaHandle.IsValid() ? 1u : 0u);
    }
    RHIPipelineHandle GetShaderVariantPipeline(ShaderMetaHandle handle,
        RHIShaderPermutationKey permutationKey) const;
    ShaderMetaHandle GetShaderMetaHandle() const { return m_shaderMetaHandle; }
    std::shared_ptr<const ShaderMetaBindingLayout> GetShaderBindingLayout() const
    {
        return m_shaderBindingLayout;
    }

private:
    template <typename T> using ComPtr = Microsoft::WRL::ComPtr<T>;

    // I5-D34a/b: experimentMask가 0이 아니면 그 마스크에서 유도한 입력
    // 레이아웃 + 대응 퍼뮤테이션(core→EXPERIMENT_STATIC_VERTEX,
    // core|skin→EXPERIMENT_SKINNED_VERTEX)으로 desc를 만든다. 0이면 legacy 96B.
    bool BuildPipelineDesc(const EnhancedFrameContext& context,
        const char* shaderFile, const char* vertexEntry, const char* pixelEntry,
        const ShaderRenderState* renderState,
        const RHIShaderPermutation& permutation, uint32_t experimentMask,
        RHIGraphicsPipelineDesc& outDesc,
        RHIShaderBlob& outVs, RHIShaderBlob& outPs, std::string& outError);
    bool BuildShaderMetaPipelineDesc(const EnhancedFrameContext& context,
        const ShaderMeta& meta,
        std::span<const std::uint16_t> keywordSelections,
        uint32_t experimentMask,
        RHIGraphicsPipelineDesc& outDesc, RHIShaderBlob& outVs,
        RHIShaderBlob& outPs, RHIShaderPermutationKey& outPermutationKey,
        std::shared_ptr<const ShaderMetaBindingLayout>& outLayout,
        std::string& outError);
    bool CreatePipeline(const EnhancedFrameContext& context, std::string& outError);

    /// 이번 프레임의 드로우 수로 조각 수를 정한다.
    ///
    /// 무조건 쪼개면 작은 씬에서 손해다 — 조각마다 상태를 다시 걸어야 하고,
    /// 그 비용이 드로우 몇 개 그리는 것보다 크다(실측으로 겪었다).
    uint32_t ComputeSliceCount() const;

    /// 같은 (메시, 재질)을 묶어 배치를 만든다. PrepareFrame이 부른다.
    void BuildBatches(const EnhancedFrameContext& context);

    // 같은 texture를 쓰더라도 ShaderMeta generation, keyword 또는 property bytes가
    // 다르면 b2/PSO 상태가 다르므로 한 draw로 합치지 않는다.
    struct MaterialKey
    {
        std::array<Texture*, 4> textures{};
        std::shared_ptr<const EnhancedMaterialDrawSnapshot> snapshot{};

        bool operator==(const MaterialKey& other) const;
        bool operator<(const MaterialKey& other) const;
    };
    MaterialKey MakeMaterialKey(const EnhancedDrawItem& draw) const;

    // 인스턴스 하나. HLSL의 StructuredBuffer 원소와 배치가 같아야 한다 —
    // 어긋나면 값이 조용히 밀려서 '재질이 이상하다'로만 드러난다.
    struct InstanceData
    {
        math::matrix4x4 world{};
        math::color   baseColorFactor{ 1.f, 1.f, 1.f, 1.f };
        float         metallic{ 0.f };
        float         roughness{ 1.f };
        uint32_t      useNormalMap{ 0 };

        /// 이 인스턴스의 본 팔레트 시작 위치. kNoSkinning이면 스키닝 없음.
        /// 팔레트를 인스턴스별 오프셋으로 두는 것이 DX11과 갈리는 지점이다 —
        /// 애니메이터가 달라도 같은 (메시·재질) 배치에 남는다.
        uint32_t      boneOffset{ kNoSkinning };
    };

    static_assert(sizeof(InstanceData) == 96u);
    static_assert(offsetof(InstanceData, baseColorFactor) == 64u);
    static_assert(std::is_same_v<decltype(InstanceData::baseColorFactor), math::color>);
    static_assert(std::is_trivially_copyable_v<InstanceData>);

    static constexpr uint32_t kNoSkinning = 0xFFFFFFFFu;

    // 한 번의 DrawIndexedInstanced로 그릴 묶음.
    //
    // 같은 메시와 같은 재질을 쓰는 드로우들이다. 둘 중 하나라도 다르면
    // 정점 버퍼나 SRV 테이블을 바꿔야 하므로 같은 드로우로 묶을 수 없다.
    struct DrawBatch
    {
        // I6-C — 배치 키는 신원 값이다(포인터가 아니다).
        std::size_t  geometryKey{ 0 };
        MaterialKey  material{};
        RHIPipelineHandle pipeline{};
        uint32_t     firstInstance{ 0 };   // m_instances 안에서의 시작
        uint32_t     instanceCount{ 0 };
    };

    struct ShaderVariantKey
    {
        ShaderMetaHandle meta{};
        RHIShaderPermutationKey permutation{};

        bool operator<(const ShaderVariantKey& other) const
        {
            if (meta.slot != other.meta.slot) return meta.slot < other.meta.slot;
            if (meta.generation != other.meta.generation)
                return meta.generation < other.meta.generation;
            if (permutation.hi != other.permutation.hi)
                return permutation.hi < other.permutation.hi;
            return permutation.lo < other.permutation.lo;
        }
    };

    struct ShaderVariant
    {
        RHIGraphicsPipelineRequest request;
        // I5-D34a/b: 같은 permutation의 experiment 레이아웃 짝 둘(core / skin).
        // variant 생성 시점에는 어떤 메시가 이 재질로 그려질지 모르므로 셋을
        // 함께 만들고, 배치가 메시 바인딩의 마스크로 고른다.
        RHIGraphicsPipelineRequest experimentRequest;
        RHIGraphicsPipelineRequest experimentSkinnedRequest;
        std::shared_ptr<const ShaderMetaBindingLayout> layout{};
    };

    // vertexAttributeMask는 RHIMeshBinding의 것이다 — 0이면 legacy 96B PSO,
    // 0이 아니면 experiment 레이아웃 PSO를 돌려준다.
    bool ResolveShaderVariant(const EnhancedMaterialDrawSnapshot& snapshot,
        uint32_t vertexAttributeMask, RHIPipelineHandle& outPipeline,
        std::shared_ptr<const ShaderMetaBindingLayout>& outLayout) const;

    // 드로우가 쓸 텍스처. PrepareFrame이 채우고 Record가 읽는다.
    struct DrawTextures
    {
        RHITextureHandle resources[4]{};
        RHIFormat       formats[4]{};
        uint32_t        mipLevels[4]{};
    };

    Outputs m_outputs;

    // 이번 프레임 드로우가 쓸 지오메트리. PrepareFrame에서 채우고 Record가 읽는다 —
    // Record가 메시 캐시를 직접 부르면 기록 중에 리소스를 만들게 되어 규약을 어긴다.
    // I6-C — 지오메트리 맵의 키를 신원 값으로 옮겼다.
    std::unordered_map<std::size_t, RHIMeshBinding> m_drawGeometry;

    // 재질은 메시가 아니라 재질로 키잉한다.
    //
    // 전에는 Mesh*를 키로 썼는데, 그러면 같은 메시를 다른 재질로 두 번 그릴 때
    // 두 번째가 첫 번째의 텍스처로 그려진다. 메시 중복 제거 블록 안에 재질
    // 업로드가 들어 있어서 두 번째는 통째로 건너뛰어졌다.
    //
    // 키는 텍스처 포인터 넷이다. 여기서 포인터를 키로 쓰는 것은 옳다 —
    // 우리가 묻는 것이 '같은 텍스처 객체인가'(동일성)이기 때문이다. 루트
    // 시그니처·입력 레이아웃 때 겪은 함정은 '같은 내용인가'(동등성)를 물어야
    // 하는데 주소를 해시한 것이라 성격이 다르다.
    //
    // 해시맵 대신 정렬 맵을 쓴다. 재질 종류는 프레임당 많아야 수십이고,
    // 배열 키에 해시를 손으로 붙이면 그 해시가 또 검증 대상이 된다.
    std::map<MaterialKey, DrawTextures>             m_drawTextures;

    // PrepareFrame이 채우고 Record가 읽는다. 인스턴스 데이터는 배치 순서대로
    // 연속이라, 배치 하나가 [firstInstance, +instanceCount) 구간을 가리킨다.
    std::vector<InstanceData> m_instances;
    std::vector<DrawBatch>    m_batches;

    // 프레임의 모든 본 팔레트를 이어 붙인 것. 애니메이터별로 한 번씩만 담기고,
    // 인스턴스의 boneOffset이 자기 구간의 시작을 가리킨다.
    std::vector<math::matrix4x4>          m_bonePalettes;
    std::unordered_map<uint64_t, uint32_t> m_boneOffsets;   // 애니메이터 키 → 오프셋
    RHISamplerTable                                 m_sampler{};

    // 프레임 밀봉된 뷰·투영을 곱해 둔 것. Record에서 스냅샷을 다시 읽지 않는다.
    math::matrix4x4 m_frameViewProjection{ math::matrix4x4::identity() };
    uint32_t m_lastDrawCount{ 0 };
    uint32_t m_lastMeshCount{ 0 };
    uint32_t m_lastMaterialCount{ 0 };
    uint32_t m_lastBatchCount{ 0 };
    uint32_t m_lastSkinnedCount{ 0 };
    bool     m_keepAlive{ true };

    // 타깃별 RTV와 깊이 DSV. 그래프가 만든 transient에 매 프레임 뷰를 만든다 —
    // 리소스가 프레임마다 바뀔 수 있으므로 뷰를 캐시하지 않는다.

    // LivePipeline 하나에 GBuffer pass도 하나뿐이다. default request/layout은 frame
    // 상수의 root layout 정본이며, primary의 material keyword와 같은 layout을 쓰는
    // secondary ShaderMeta 조합은 아래 variant map에서 별도 PSO로 보관한다. 새 primary
    // generation은 같은 slot만, frame commit은 빠진 secondary key만 retire한다.
    RHIGraphicsPipelineRequest m_pipelineRequest;
    // I5-D34a/b: default request의 experiment 레이아웃 짝 둘(위 ShaderVariant와
    // 같은 지위).
    RHIGraphicsPipelineRequest m_experimentPipelineRequest;
    RHIGraphicsPipelineRequest m_experimentSkinnedPipelineRequest;
    ShaderMetaHandle           m_shaderMetaHandle{};
    RHIShaderPermutationKey     m_defaultPermutationKey{};
    std::shared_ptr<const ShaderMetaBindingLayout> m_shaderBindingLayout{};
    std::map<ShaderVariantKey, ShaderVariant> m_shaderVariants;
};

