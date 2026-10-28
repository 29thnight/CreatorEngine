#include "../../../Utility_Framework/Foundation/EngineNodeMap.h"
#include "../../../Containers/ContainerAPI.h"
#pragma once
#include "../../../Utility_Framework/Foundation/EngineFunc.h"
#include "../../../Utility_Framework/Foundation/StandardLibrary.h"

#include "../Graph/EnhancedRenderGraph.h"
#include "../Graph/EnhancedRenderPass.h"
#include "RenderFeatureContributor.h"

enum class LiveResourceCardinality : uint8_t
{
    Single,
    List
};
enum class LiveResourceExtent : uint8_t
{
    Independent,
    Render,
    HalfRender,
    Display,
    RenderOrDisplay
};
enum class LiveResourceSemantic : uint8_t
{
    Generic,
    LinearColor,
    DisplayColor,
    Albedo,
    Material,
    Normal,
    Emissive,
    Bitmask,
    Depth,
    Visibility,
    Motion,
    Mask,
    AmbientOcclusion,
    Geometry,
    VisibleList,
    Commands,
    MaterialTiles
};

/// A feature contract describes what a handle means, independently of its current
/// graph index, native allocation or frame. Unknown format is permitted only when
/// the feature deliberately accepts every format of the stated semantic.
struct LiveResourceContract
{
    RGResourceKind kind{RGResourceKind::Texture};
    LiveResourceCardinality cardinality{LiveResourceCardinality::Single};
    LiveResourceSemantic semantic{LiveResourceSemantic::Generic};
    RHIFormat format{RHIFormat::Unknown};
    LiveResourceExtent extent{LiveResourceExtent::Independent};
    bool optional{};
    uint32_t listSize{}; // Zero is variable length; nonzero is an exact cardinality.

    bool Accepts(const LiveResourceContract& producer) const noexcept;
};

template<LiveResourceSemantic Semantic, RGResourceKind Kind = RGResourceKind::Texture,
         LiveResourceCardinality Cardinality = LiveResourceCardinality::Single>
struct LiveResourceSlot
{
    const char* name;
    RHIFormat format{RHIFormat::Unknown};
    LiveResourceExtent extent{LiveResourceExtent::Independent};
    bool optional{};
    uint32_t listSize{};

    static constexpr RGResourceKind kKind = Kind;
    static constexpr LiveResourceCardinality kCardinality = Cardinality;
    constexpr LiveResourceContract Contract() const noexcept
    {
        return {Kind, Cardinality, Semantic, format, extent, optional, listSize};
    }
};

struct LiveResourceBinding
{
    std::string name;
    LiveResourceContract contract;

    template<LiveResourceSemantic Semantic, RGResourceKind Kind, LiveResourceCardinality Cardinality>
    LiveResourceBinding(LiveResourceSlot<Semantic, Kind, Cardinality> slot) : name(slot.name), contract(slot.Contract())
    {
    }
    LiveResourceBinding(std::string slotName, LiveResourceContract slotContract)
        : name(std::move(slotName)), contract(slotContract)
    {
    }
};

// 라이브 파이프라인 기술(記述) — LivePipelineDesc (PHASE 3-10, 슬라이스 1).
//
// ── 이것은 그래프가 아니다 ──
//
// 살아 있는 그래프는 EnhancedRenderGraph 하나다(옛 RenderGraphBuilder는 참조
// 0으로 제거됐다). 이 층은 저작 순서로 슬롯의 버전을 연결한다. 실제 실행
// 순서는 EnhancedRenderGraph가 그 버전의 의존성에서 컴파일한다.
// 별도 실행 그래프를 소유하지 않으므로 그래프가 아니라 기술이다.
//
//   LivePipelineDesc      영속. 무엇을 · 어떤 순서로 · 꺼지면 어떻게 흘릴지  ← 조립
//   EnhancedRenderGraph   프레임당. 그 선언을 받아 배리어 · 컬링 · 실행       ← 실행
//
// 그래프에 흡수하지 않은 근거 셋(LivePipelineDescPlan.md §0):
//   ① 어휘가 다르다 — AddPass는 패스 객체가 아니라 해석이 끝난 핸들과 람다를
//      받는다. EnhancedRenderPass도, 수명도, 활성 조건도 그래프는 모른다.
//   ② 수명이 정반대다 — 그래프는 프레임마다·슬롯마다 새로 서고(최대 6개 동시),
//      조립은 파이프라인이 설 때 한 번 정해진다.
//   ③ 그래프를 직접 세우는 자가 검증이 30여 곳이다. 거기 패스 수명·활성
//      조건을 얹으면 검증된 호출 지점이 전부 흔들린다.
//
// ── 왜 필요한가 ──
//
// 배선이 RenderOnce에 하드코딩돼 있었다. 패스 하나를 이으려면 흩어진 접점
// 10종을 찾아 고쳐야 했고, 그중 순서를 사람이 지켜야 하는 곳이 넷인데
// (Initialize · Shutdown 역순 · PrepareFrame · Declare) 셋은 틀려도 컴파일러가
// 침묵한다. 같은 사실("SSS는 Forward 뒤")이 세 곳에 따로 적히고 셋을 잇는
// 것은 사람의 기억뿐이었다.

/// 블랙보드 슬롯 이름. 문자열 오타는 Validate가 세울 때 잡지만(발행 안 된
/// 슬롯 읽기 = 오류), 상수로 두면 오타 자체가 컴파일 오류가 된다.
///
/// 여기(공개 헤더)에 있는 이유: 슬롯 이름은 노드 사이의 계약이고, 파이프라인에
/// 노드를 기여하는 Host(IRenderFeatureContributor)도 같은 계약으로 잇는다.
/// 기여자끼리만 쓰는 슬롯(예: 기즈모 깊이)은 이 어휘에 넣지 않는다 — Core가
/// 몰라야 하는 이름이다.
namespace LiveSlots
{
    constexpr const char* kGBufferDiffuse    = "GBuffer.Diffuse";
    constexpr const char* kGBufferMetalRough = "GBuffer.MetalRough";
    constexpr const char* kGBufferNormal     = "GBuffer.Normal";
    constexpr const char* kGBufferEmissive   = "GBuffer.Emissive";
    constexpr const char* kGBufferBitmask    = "GBuffer.Bitmask";
    constexpr const char* kGBufferDepth      = "GBuffer.Depth";
    inline constexpr const char* kDecalBaseDiffuse = "METEOR.Scene.Decal.BaseDiffuse";
    inline constexpr const char* kDecalBaseOrm = "METEOR.Scene.Decal.BaseOrm";
    inline constexpr const char* kDecalBaseNormal = "METEOR.Scene.Decal.BaseNormal";
    inline constexpr const char* kVisibility = "METEOR.Scene.Visibility";

    /// METEOR 단계 계약. 각 단계가 발행한 그래프 핸들을 다음 단계가 그대로 받는다.
    /// 그리기 없는 프레임은 무효 핸들을 발행한다.
    inline constexpr const char* kShadowStaticDepths = "METEOR.Shadow.Static.Depths";
    inline constexpr const char* kShadowFirstLists = "METEOR.Shadow.Cull.FirstLists";
    inline constexpr const char* kShadowOccluderDepths = "METEOR.Shadow.Occluder.Depths";
    inline constexpr const char* kShadowFinalLists = "METEOR.Shadow.Cull.FinalLists";
    inline constexpr const char* kMeteorGeometry = "METEOR.Scene.Geometry.Table";
    inline constexpr const char* kMeteorFirstList = "METEOR.Scene.Cull.FirstList";
    inline constexpr const char* kMeteorOccluderDepth = "METEOR.Scene.Occluder.Depth";
    inline constexpr const char* kMeteorLayerCommands = "METEOR.Scene.Commands.Layer";
    inline constexpr const char* kMeteorMaterialTiles = "METEOR.Scene.Material.Tiles";
    inline constexpr const char* kMeteorMaterialArguments = "METEOR.Scene.Material.Arguments";
    inline constexpr const char* kMeteorBlendedCommands = "METEOR.Scene.Commands.Blended";
    inline constexpr const char* kMeteorBlendedList = "METEOR.Scene.Cull.BlendedList";
    inline constexpr const char* kMeteorFinalList = "METEOR.Scene.Cull.FinalList";

    constexpr const char* kTemporalMotion = "Temporal.Motion";
    constexpr const char* kTemporalReactive = "Temporal.Reactive";
    constexpr const char* kTemporalTransparency = "Temporal.Transparency";
    constexpr const char* kTemporalResponsive = "Temporal.Responsive";
    constexpr const char* kTemporalDepth = "Temporal.Depth";
    constexpr const char* kHudlessLdr = "Display.Hudless";
    constexpr const char* kUiLayer = "Display.UI";
    constexpr const char* kShadowMap         = "Shadow.Map";
    constexpr const char* kAmbientOcclusion  = "Scene.AmbientOcclusion";

    /// 라이팅 결과. METEOR.Scene.Color가 발행하고 SkyBox·SSGI·SSS·SSR·Volume·
    /// Blended·Sprite·Fog·Upscale이 차례로 수정한다.
    constexpr const char* kLitColor          = "Scene.LitColor";

    /// 포스트 체인 뒤의 LDR. 화면 UI와 기여 오버레이가 이 위에 얹힌다.
    constexpr const char* kDisplayLdr        = "Display.LDR";
}

/// The built-in feature vocabulary. A plugin may define another typed slot without
/// changing this catalog. Names remain stable for diagnostics and old consumers.
namespace LiveResources
{
    /// Reserved engine outputs. Contributors may read all available built-ins;
    /// stage-specific mutation is limited to scene HDR or display LDR. Other
    /// names are private feature resources and use ordinary dependency checks.
    bool IsBuiltin(std::string_view slot) noexcept;
    inline constexpr LiveResourceSlot<LiveResourceSemantic::Albedo> kGBufferDiffuse{
        LiveSlots::kGBufferDiffuse, RHIFormat::RGBA16Float, LiveResourceExtent::Render};
    inline constexpr LiveResourceSlot<LiveResourceSemantic::Material> kGBufferMetalRough{
        LiveSlots::kGBufferMetalRough, RHIFormat::RGBA16Float, LiveResourceExtent::Render};
    inline constexpr LiveResourceSlot<LiveResourceSemantic::Normal> kGBufferNormal{
        LiveSlots::kGBufferNormal, RHIFormat::RGBA16Float, LiveResourceExtent::Render};
    inline constexpr LiveResourceSlot<LiveResourceSemantic::Emissive> kGBufferEmissive{
        LiveSlots::kGBufferEmissive, RHIFormat::RGBA16Float, LiveResourceExtent::Render};
    inline constexpr LiveResourceSlot<LiveResourceSemantic::Bitmask> kGBufferBitmask{
        LiveSlots::kGBufferBitmask, RHIFormat::R32Uint, LiveResourceExtent::Render};
    inline constexpr LiveResourceSlot<LiveResourceSemantic::Depth> kGBufferDepth{
        LiveSlots::kGBufferDepth, RHIFormat::D32Float, LiveResourceExtent::Render};
    inline constexpr LiveResourceSlot<LiveResourceSemantic::Albedo> kDecalBaseDiffuse{
        LiveSlots::kDecalBaseDiffuse, RHIFormat::RGBA16Float, LiveResourceExtent::Render, true};
    inline constexpr LiveResourceSlot<LiveResourceSemantic::Material> kDecalBaseOrm{
        LiveSlots::kDecalBaseOrm, RHIFormat::RGBA16Float, LiveResourceExtent::Render, true};
    inline constexpr LiveResourceSlot<LiveResourceSemantic::Normal> kDecalBaseNormal{
        LiveSlots::kDecalBaseNormal, RHIFormat::RGBA16Float, LiveResourceExtent::Render, true};
    inline constexpr LiveResourceSlot<LiveResourceSemantic::Visibility> kVisibility{
        LiveSlots::kVisibility, RHIFormat::RG32Uint, LiveResourceExtent::Render};

    inline constexpr LiveResourceSlot<LiveResourceSemantic::Depth, RGResourceKind::Texture,
                                      LiveResourceCardinality::List>
        kShadowStaticDepths{LiveSlots::kShadowStaticDepths, RHIFormat::D32Float, LiveResourceExtent::Independent, true,
                            3};
    inline constexpr LiveResourceSlot<LiveResourceSemantic::VisibleList, RGResourceKind::Buffer,
                                      LiveResourceCardinality::List>
        kShadowFirstLists{LiveSlots::kShadowFirstLists, RHIFormat::Unknown, LiveResourceExtent::Independent, true, 3};
    inline constexpr LiveResourceSlot<LiveResourceSemantic::Depth, RGResourceKind::Texture,
                                      LiveResourceCardinality::List>
        kShadowOccluderDepths{LiveSlots::kShadowOccluderDepths, RHIFormat::D32Float, LiveResourceExtent::Independent,
                              true, 3};
    inline constexpr LiveResourceSlot<LiveResourceSemantic::VisibleList, RGResourceKind::Buffer,
                                      LiveResourceCardinality::List>
        kShadowFinalLists{LiveSlots::kShadowFinalLists, RHIFormat::Unknown, LiveResourceExtent::Independent, true, 3};
    inline constexpr LiveResourceSlot<LiveResourceSemantic::Geometry, RGResourceKind::Buffer> kMeteorGeometry{
        LiveSlots::kMeteorGeometry, RHIFormat::Unknown, LiveResourceExtent::Independent, true};
    inline constexpr LiveResourceSlot<LiveResourceSemantic::VisibleList, RGResourceKind::Buffer> kMeteorFirstList{
        LiveSlots::kMeteorFirstList, RHIFormat::Unknown, LiveResourceExtent::Independent, true};
    inline constexpr LiveResourceSlot<LiveResourceSemantic::Depth> kMeteorOccluderDepth{
        LiveSlots::kMeteorOccluderDepth, RHIFormat::D32Float, LiveResourceExtent::Render, true};
    inline constexpr LiveResourceSlot<LiveResourceSemantic::Commands, RGResourceKind::Buffer> kMeteorLayerCommands{
        LiveSlots::kMeteorLayerCommands, RHIFormat::Unknown, LiveResourceExtent::Independent, true};
    inline constexpr LiveResourceSlot<LiveResourceSemantic::MaterialTiles, RGResourceKind::Buffer> kMeteorMaterialTiles{
        LiveSlots::kMeteorMaterialTiles, RHIFormat::Unknown, LiveResourceExtent::Independent, true};
    inline constexpr LiveResourceSlot<LiveResourceSemantic::Commands, RGResourceKind::Buffer> kMeteorMaterialArguments{
        LiveSlots::kMeteorMaterialArguments, RHIFormat::Unknown, LiveResourceExtent::Independent, true};
    inline constexpr LiveResourceSlot<LiveResourceSemantic::Commands, RGResourceKind::Buffer,
                                      LiveResourceCardinality::List>
        kMeteorBlendedCommands{LiveSlots::kMeteorBlendedCommands, RHIFormat::Unknown, LiveResourceExtent::Independent,
                               true};
    inline constexpr LiveResourceSlot<LiveResourceSemantic::VisibleList, RGResourceKind::Buffer> kMeteorBlendedList{
        LiveSlots::kMeteorBlendedList, RHIFormat::Unknown, LiveResourceExtent::Independent, true};
    inline constexpr LiveResourceSlot<LiveResourceSemantic::VisibleList, RGResourceKind::Buffer> kMeteorFinalList{
        LiveSlots::kMeteorFinalList, RHIFormat::Unknown, LiveResourceExtent::Independent, true};

    inline constexpr LiveResourceSlot<LiveResourceSemantic::Motion> kTemporalMotion{
        LiveSlots::kTemporalMotion, RHIFormat::RG16Float, LiveResourceExtent::Render, true};
    inline constexpr LiveResourceSlot<LiveResourceSemantic::Mask> kTemporalReactive{
        LiveSlots::kTemporalReactive, RHIFormat::R16Float, LiveResourceExtent::Render, true};
    inline constexpr LiveResourceSlot<LiveResourceSemantic::Mask> kTemporalTransparency{
        LiveSlots::kTemporalTransparency, RHIFormat::R16Float, LiveResourceExtent::Render, true};
    inline constexpr LiveResourceSlot<LiveResourceSemantic::Mask> kTemporalResponsive{
        LiveSlots::kTemporalResponsive, RHIFormat::R16Float, LiveResourceExtent::Render, true};
    inline constexpr LiveResourceSlot<LiveResourceSemantic::Depth> kTemporalDepth{
        LiveSlots::kTemporalDepth, RHIFormat::D32Float, LiveResourceExtent::Render, true};
    inline constexpr LiveResourceSlot<LiveResourceSemantic::DisplayColor> kHudlessLdr{
        LiveSlots::kHudlessLdr, RHIFormat::RGBA8Unorm, LiveResourceExtent::Display, true};
    inline constexpr LiveResourceSlot<LiveResourceSemantic::DisplayColor> kUiLayer{
        LiveSlots::kUiLayer, RHIFormat::RGBA8Unorm, LiveResourceExtent::Display, true};
    inline constexpr LiveResourceSlot<LiveResourceSemantic::Depth> kShadowMap{
        LiveSlots::kShadowMap, RHIFormat::D32Float, LiveResourceExtent::Independent, true};
    inline constexpr LiveResourceSlot<LiveResourceSemantic::AmbientOcclusion> kAmbientOcclusion{
        LiveSlots::kAmbientOcclusion, RHIFormat::RG16Float, LiveResourceExtent::HalfRender, true};
    inline constexpr LiveResourceSlot<LiveResourceSemantic::LinearColor> kLitColor{
        LiveSlots::kLitColor, RHIFormat::RGBA16Float, LiveResourceExtent::RenderOrDisplay};
    inline constexpr LiveResourceSlot<LiveResourceSemantic::DisplayColor> kDisplayLdr{
        LiveSlots::kDisplayLdr, RHIFormat::RGBA8Unorm, LiveResourceExtent::RenderOrDisplay};
} // namespace LiveResources

/// 뷰의 표시 성격(E4-2). viewIndex로 갈음할 수 없다 — 뷰 배정은 회전·교체가
/// 있어 인덱스가 카메라 정체를 말하지 않는다. 이름이 중립인 이유: RenderCore는
/// "에디터"를 모른다 — 씬 오버레이 노드를 기여하는 것은 Host다.
namespace LiveViewFlags
{
    /// 이 뷰에 화면 공간 게임 UI를 합성한다(플레이어가 볼 화면).
    constexpr uint32_t kScreenSpaceUI = 1u << 0;

    /// Host가 기여한 씬 오버레이 노드가 이 뷰에 그린다(저작 보조 뷰).
    constexpr uint32_t kSceneOverlay  = 1u << 1;
    constexpr uint32_t kHideSkyBox = 1u << 2;
}

/// 프레임마다 비우고 다시 채우는 이름 → 핸들 표.
///
/// 값 복사가 싼 RGHandle만 담는다. 사이드밴드(그림자 행렬 · IBL 포인터 ·
/// 데칼 목록)는 담지 않는다 — 일반화하려면 타입 소거가 필요한데 얻는 것은
/// 덤프 한 줄이고 잃는 것은 컴파일 타임 타입 검사다. 사이드밴드는 순서
/// 오류가 포인터 타입으로 잡히므로 이 층의 동기인 '조용한 순서 실패'가
/// 애초에 없다.
class LiveBlackboard
{
  public:
    LiveBlackboard() = default;
    LiveBlackboard(const LiveBlackboard& other)
        : m_slots(other.m_slots), m_generation(other.m_generation), m_writeScope(other.m_writeScope)
    {
    }
    LiveBlackboard& operator=(const LiveBlackboard& other)
    {
        if (this != &other)
        {
            m_slots = other.m_slots;
            m_generation = other.m_generation;
            m_writeScope = other.m_writeScope;
            m_writingFeature = {};
            m_boundSlots.clear();
            m_boundPlan = nullptr;
            m_boundRevision = 0;
        }
        return *this;
    }
    LiveBlackboard(LiveBlackboard&& other) noexcept
        : m_slots(std::move(other.m_slots)), m_generation(other.m_generation),
          m_boundSlots(std::move(other.m_boundSlots)), m_boundPlan(std::exchange(other.m_boundPlan, nullptr)),
          m_boundRevision(std::exchange(other.m_boundRevision, 0)), m_writeScope(other.m_writeScope)
    {
    }
    LiveBlackboard& operator=(LiveBlackboard&& other) noexcept
    {
        if (this != &other)
        {
            m_slots = std::move(other.m_slots);
            m_generation = other.m_generation;
            m_boundSlots = std::move(other.m_boundSlots);
            m_boundPlan = std::exchange(other.m_boundPlan, nullptr);
            m_boundRevision = std::exchange(other.m_boundRevision, 0);
            m_writeScope = other.m_writeScope;
            m_writingFeature = {};
        }
        return *this;
    }

    void Reset()
    {
        // Slots own their keys and buffers across frames. Only the current
        // generation is visible; dynamic diagnostic names have bounded retention.
        if (m_generation == UINT64_MAX || m_slots.size() > 4096)
        {
            m_slots.clear();
            m_boundSlots.clear();
            m_boundPlan = nullptr;
            m_generation = 1;
        }
        else
        {
            ++m_generation;
        }
    }

    /// 발행·수정 공용. 수정이 별도 함수가 아닌 이유는 값이 갈리지 않아서다 —
    /// 무엇이 발행이고 무엇이 수정인지는 노드의 writes/modifies가 말하고,
    /// Validate가 그것으로 판정한다.
    void Set(std::string_view slot, RGHandle handle)
    {
        auto& destination = WritableSlot(slot);
        if (destination.handles.size() != 1)
        {
            destination.handles.resize(1);
        }
        destination.handles[0] = handle;
        destination.generation = m_generation;
    }

    /// 없으면 무효 핸들. 슬롯 이름 오타는 여기서 조용히 무효가 되지만,
    /// Validate가 파이프라인을 세울 때 이미 잡았어야 하는 종류다.
    RGHandle Get(std::string_view slot) const
    {
        const auto found = m_slots.find(slot);
        if (found == m_slots.end() || found->second.generation != m_generation || found->second.handles.empty())
        {
            return {};
        }
        if (found->second.handles.size() != 1)
        {
            throw std::runtime_error("A resource-list slot requires GetList.");
        }
        return found->second.handles.front();
    }

    // One slot may publish several independently versioned command buffers.
    // They remain real resource handles, never a dummy join token or hidden producer.
    void SetList(std::string_view slot, std::span<const RGHandle> handles)
    {
        auto& destination = WritableSlot(slot);
        if (destination.handles.size() != handles.size())
        {
            destination.handles.resize(handles.size());
        }
        std::copy_n(handles.data(), handles.size(), destination.handles.data());
        destination.generation = m_generation;
    }
    void SetList(std::string_view slot, const ce::dynamic_array<RGHandle>& handles)
    {
        SetList(slot, std::span<const RGHandle>{handles});
    }
    void SetList(std::string_view slot, std::initializer_list<RGHandle> handles)
    {
        SetList(slot, std::span<const RGHandle>{handles.begin(), handles.size()});
    }
    ce::dynamic_array<RGHandle> GetList(std::string_view slot) const
    {
        const auto found = m_slots.find(slot);
        return found == m_slots.end() || found->second.generation != m_generation ? ce::dynamic_array<RGHandle>{}
                                                                                  : found->second.handles;
    }

    /// A borrowed list for immediate consumption. Set/Reset invalidates this view;
    /// recording callbacks must continue retaining their own frame-owned payload.
    std::span<const RGHandle> GetListView(std::string_view slot) const
    {
        const auto found = m_slots.find(slot);
        return found == m_slots.end() || found->second.generation != m_generation
                   ? std::span<const RGHandle>{}
                   : std::span<const RGHandle>{found->second.handles};
    }

    template<LiveResourceSemantic Semantic, RGResourceKind Kind>
    RGHandle Get(LiveResourceSlot<Semantic, Kind> slot) const
    {
        return Get(slot.name);
    }
    template<LiveResourceSemantic Semantic, RGResourceKind Kind>
    void Set(LiveResourceSlot<Semantic, Kind> slot, RGHandle handle)
    {
        Set(slot.name, handle);
    }
    template<LiveResourceSemantic Semantic, RGResourceKind Kind>
    std::span<const RGHandle> GetListView(LiveResourceSlot<Semantic, Kind, LiveResourceCardinality::List> slot) const
    {
        return GetListView(slot.name);
    }
    template<LiveResourceSemantic Semantic, RGResourceKind Kind>
    void SetList(LiveResourceSlot<Semantic, Kind, LiveResourceCardinality::List> slot,
                 std::span<const RGHandle> handles)
    {
        SetList(slot.name, handles);
    }

  private:
    friend class LivePipelineDesc;
    struct SlotHash
    {
        using is_transparent = void;

        std::size_t operator()(std::string_view slot) const noexcept { return std::hash<std::string_view>{}(slot); }
    };

    struct Slot
    {
        ce::dynamic_array<RGHandle> handles;
        uint64_t generation{};
        uint64_t writePermission{};
    };
    void BeginFeatureWrites(std::string_view node)
    {
        if (++m_writeScope == 0)
        {
            for (auto& [name, slot] : m_slots)
            {
                slot.writePermission = 0;
            }
            ++m_writeScope;
        }
        m_writingFeature = node;
    }
    void AllowFeatureWrite(uint32_t slot) { m_boundSlots[slot]->writePermission = m_writeScope; }
    void EndFeatureWrites() { m_writingFeature = {}; }
    void BindPlan(const void* plan, uint64_t revision, std::span<const LiveResourceBinding> resources)
    {
        if (m_boundPlan == plan && m_boundRevision == revision)
        {
            return;
        }
        m_boundSlots.resize(resources.size());
        for (size_t index = 0; index < resources.size(); ++index)
        {
            m_boundSlots[index] = &WritableSlot(resources[index].name);
        }
        m_boundPlan = plan;
        m_boundRevision = revision;
    }
    std::span<const RGHandle> BoundHandles(uint32_t index) const
    {
        const Slot& slot = *m_boundSlots[index];
        return slot.generation == m_generation ? std::span<const RGHandle>{slot.handles} : std::span<const RGHandle>{};
    }
    void CopyBoundSlot(uint32_t destination, uint32_t source)
    {
        const std::span<const RGHandle> handles = BoundHandles(source);
        Slot& slot = *m_boundSlots[destination];
        slot.handles.assign(handles.begin(), handles.end());
        slot.generation = m_generation;
    }
    Slot& WritableSlot(std::string_view slot)
    {
        const auto found = m_slots.find(slot);
        if (!m_writingFeature.empty() && (found == m_slots.end() || found->second.writePermission != m_writeScope))
        {
            throw std::runtime_error("Feature node '" + std::string{m_writingFeature} + "' wrote undeclared slot '" +
                                     std::string{slot} + "'.");
        }
        if (found != m_slots.end())
        {
            return found->second;
        }
        // Heterogeneous lookup avoids constructing a temporary owning key on
        // every Set. An actual new key is copied and owned exactly once.
        return m_slots.try_emplace(std::string{slot}).first->second;
    }

    // 174d073에서 고친 빌린 키 저장 뒤 원본 문자열 변경 문제가 재발하지 않도록 저장 키는 std::string으로 소유한다.
    // 조회 문자열과 iterator는 호출 밖에서 보관하지 않는다. GetListView만 현재 세대의 값 범위를 빌린다.
    // 저장 키와 조회 view에 같은 해시·문자 비교를 적용해 주소와 종료 문자에 의존하지 않는다.
    ce::engine_node_map<std::string, Slot, SlotHash, std::equal_to<>> m_slots;
    uint64_t m_generation{1};
    ce::dynamic_array<Slot*> m_boundSlots;
    const void* m_boundPlan{};
    uint64_t m_boundRevision{};
    uint64_t m_writeScope{};
    std::string_view m_writingFeature;
};

/// 프레임마다 달라지는 값. 노드 정의는 정적이어야 하므로 이런 것은 캡처가
/// 아니라 인자로 흐른다.
///
/// sharedTarget이 여기 있는 이유: 표시 슬롯은 프레임마다 회전한다(뷰당 3개).
/// 노드가 그것을 캡처하면 노드 정의가 프레임에 묶여 데이터화의 의미가 없어진다.
struct LiveFrameBinding
{
    uint32_t         viewIndex{ 0 };
    RHITextureHandle sharedTarget{};
    RHIReadback      readbackTarget{};
    ce::fixed_array<RHITextureHandle,4> temporalTargets{};

    // 뷰의 표시 성격(LiveViewFlags 비트 조합). 화면 UI는 kScreenSpaceUI 뷰에만,
    // Host가 기여한 씬 오버레이는 kSceneOverlay 뷰에만 그린다 — 게임 뷰는
    // 플레이어가 볼 화면이라 저작 보조물이 나오면 안 된다(E4-2에서
    // isEditorView를 중립 비트로 교체했다).
    uint32_t        viewFlags{ 0 };
    RHIResourceState sharedInitialState{RHIResourceState::Common};
    RHIResourceState sharedFinalState{RHIResourceState::Common};
};

/// 패스 하나의 어댑터. 데이터 절반(검증·덤프의 근거)과 접착 절반(타입)이 있다.
struct LivePassNode
{
    std::string name;
    uint32_t declarationMarker{};

    // ── 데이터 절반 ──

    /// 앞선 노드가 발행해 두어야 읽을 수 있다.
    ce::dynamic_array<std::string> reads;

    /// 이 노드가 처음 발행한다. 같은 슬롯을 둘이 발행하면 Validate가 잡는다.
    ce::dynamic_array<std::string> writes;

    /// 읽고 → 같은 이름으로 재발행한다.
    ///
    /// 꺼졌을 때 기존 값이 그대로 남는다. 새 required 출력을 발행하는
    /// 조건부 노드는 disabledPassthroughs로 호환 입력을 지정해야 한다.
    ce::dynamic_array<std::string> modifies;

    ce::dynamic_array<LiveResourceBinding> inputContracts, outputContracts, updateContracts;
    struct DisabledPassthrough
    {
        LiveResourceBinding output;
        LiveResourceBinding input;
    };
    ce::dynamic_array<DisabledPassthrough> disabledPassthroughs;
    /// CPU-side ordering that cannot be inferred from a GPU resource (e.g. a
    /// shared lighting preparation result). Names are resolved only at assembly.
    ce::dynamic_array<std::string> dependsOn;
    uint32_t requiredViewFlags{};

    void Inputs(std::initializer_list<LiveResourceBinding> resources)
    {
        inputContracts.assign(resources);
        reads.clear();
        for (const LiveResourceBinding& resource : inputContracts)
        {
            reads.push_back(resource.name);
        }
    }
    void Outputs(std::initializer_list<LiveResourceBinding> resources)
    {
        outputContracts.assign(resources);
        writes.clear();
        for (const LiveResourceBinding& resource : outputContracts)
        {
            writes.push_back(resource.name);
        }
    }
    void Updates(std::initializer_list<LiveResourceBinding> resources)
    {
        updateContracts.assign(resources);
        modifies.clear();
        for (const LiveResourceBinding& resource : updateContracts)
        {
            modifies.push_back(resource.name);
        }
    }

    /// 뷰마다 인스턴스를 따로 두는가(시간축 상태를 가진 패스 — SSGI · Fog).
    ///
    /// InitializeAll이 뷰 수만큼 돌고, PrepareAll/DeclareAll이 viewIndex로
    /// 고른다. 공유 패스는 뷰가 둘이어도 한 번만 선다.
    bool perView{ false };

    // ── 접착 절반 ──

    /// 비활성이면 declare를 건너뛴다. 비면 항상 활성.
    ce::owned_func<bool()> active;

    /// 이 노드가 미는 패스. viewIndex로 뷰당 인스턴스를 고른다(공유면 무시).
    ///
    /// 이것만 있으면 Initialize/PrepareFrame/Shutdown의 기본 구현이 성립한다 —
    /// 그 셋을 따로 적는 노드는 특별한 사정이 있는 것뿐이다(포그의 지연
    /// 초기화, 데칼의 SetDecals 선행). 패스가 없는 노드(live_present)는 비운다.
    ce::owned_func<EnhancedRenderPass*(uint32_t viewIndex)> instance;

    /// 기본(instance->Initialize)을 대신할 초기화. 포그처럼 처음 켜질 때까지
    /// 자원을 잡지 않아야 하는 노드가 빈 람다를 넣어 기본을 끈다.
    ce::owned_func<bool(const EnhancedFrameContext&, std::string&, uint32_t viewIndex)> initialize;

    /// 기본(instance->PrepareFrame)을 대신할 프레임 준비.
    ce::owned_func<bool(const EnhancedFrameContext&, std::string&, uint32_t viewIndex)> prepare;

    /// 기본(instance->Shutdown)을 대신할 해제.
    ce::owned_func<void()> shutdown;

    /// 블랙보드에서 꺼내 타입 있는 Inputs로 옮기고, 패스의 Declare를 부르고,
    /// 결과를 다시 블랙보드에 넣는다. 지금 RenderOnce의 블록 하나와 일대일이다.
    ce::owned_func<void(LiveBlackboard&, EnhancedRenderGraph&,
        const EnhancedFrameContext&, const LiveFrameBinding&)> declare;

    /// 이번 프레임에 reads의 이 슬롯을 실제로 소비하는가. 비면 활성일 때 항상 소비한다.
    /// 선택적 생산자(Temporal.Inputs)가 소비자 이름을 몰라도 되게 하는 근거다.
    ce::owned_func<bool(const std::string& slot, const EnhancedFrameContext&, const LiveFrameBinding&)> consumes;
    /// LitColor 출력이 표시 해상도인가(시간 업스케일). 캡처 단계 크기의 근거.
    ce::owned_func<bool(const LiveFrameBinding&)> displayResolutionOutput;

    bool IsActive() const { return !active || active(); }
    bool IsActive(const LiveFrameBinding& binding) const
    {
        return (binding.viewFlags & requiredViewFlags) == requiredViewFlags && IsActive();
    }
    bool OutputsDisplayResolution(const LiveFrameBinding& binding) const
    {
        return displayResolutionOutput && displayResolutionOutput(binding);
    }

  private:
    friend class LivePipelineDesc;
    std::string m_contributorName;
    RenderFeatureStage m_contributorStage{RenderFeatureStage::BeforePresent};
};

struct LivePassNodeSnapshot
{
    std::string name;
    bool conditional{}, active{}, perView{};
    ce::dynamic_array<std::string> reads, writes, modifies;
};

class LivePipelineDesc
{
public:
    void AddNode(LivePassNode node);
    void Clear()
    {
        if (!m_currentContributor.empty())
        {
            throw std::logic_error(
                "A feature contributor must append nodes; clearing the engine pipeline is forbidden.");
        }
        m_nodes.clear();
        m_planNodes.clear();
        m_planResources.clear();
        m_planConsumers.clear();
        ++m_revision;
        m_compiledRevision = 0;
    }
    bool IsEmpty() const { return m_nodes.empty(); }
    size_t NodeCount() const { return m_nodes.size(); }

    /// 파이프라인을 세울 때 한 번. 노드 순서대로 걸으며 발행 집합을 유지한다.
    ///
    /// 잡는 것 셋:
    ///   · 아직 발행 안 된 슬롯을 reads/modifies가 짚는다 → 순서 실수다.
    ///     지금은 무효 핸들이 조용히 흘러 패스의 "입력이 모자라면 입력을
    ///     흘려보낸다" 폴백에 삼켜지고, 그림만 이상해진다.
    ///   · 꺼질 수 있는 노드(active 보유)가 writes를 가진다 → 꺼진 프레임에
    ///     소비자가 무효 핸들을 받는다.
    ///   · 같은 슬롯을 둘이 writes한다 → 둘째는 수정이므로 modifies로 적어야
    ///     하고, 그래야 '꺼지면 값 유지' 규약이 성립한다.
    bool Validate(std::string& outError) const;

    struct CompiledPlanStats
    {
        uint64_t revision{};
        size_t nodes{}, resources{}, dependencies{};
    };
    CompiledPlanStats GetCompiledPlanStats() const;
    std::span<const uint32_t> GetNodeDependencies(size_t index) const;

    /// 노드 순서대로 Initialize. perView 노드는 뷰 수만큼 돈다.
    ///
    /// ★ 순서를 여기서 유도하는 것이 슬라이스 2의 요점이다. 예전에는
    ///   Initialize · PrepareFrame · Shutdown 역순 · Declare 네 곳에 같은
    ///   순서가 따로 적혀 있었고, 셋은 틀려도 컴파일러가 침묵했다.
    bool InitializeAll(const EnhancedFrameContext& context, uint32_t viewCount,
        std::string& outError) const;

    /// 노드 순서대로 PrepareFrame. 비활성 노드도 준비는 한다 —
    /// 포그처럼 '이번 프레임에 켜질지'를 준비 단계가 정하는 경우가 있다.
    bool PrepareAll(const EnhancedFrameContext& context, uint32_t viewIndex,
        std::string& outError) const;

    /// 역순 Shutdown. 순서를 사람이 뒤집어 적던 자리가 사라진다 —
    /// 그 순서가 틀리면 '가끔 죽는' 종류의 실패였다.
    /// viewCount는 InitializeAll에 준 것과 같아야 한다(대칭).
    void ShutdownAll(uint32_t viewCount) const;

    /// 노드 순서대로 declare를 부른다. 비활성 노드는 건너뛴다 —
    /// 그 노드의 modifies 슬롯은 값이 그대로 남아 뒤 노드가 이어받는다.
    void DeclareAll(LiveBlackboard& blackboard, EnhancedRenderGraph& graph,
        const EnhancedFrameContext& context, const LiveFrameBinding& binding,
        const ce::owned_func<void(const LivePassNode&, const LiveBlackboard&)>& afterDeclare = {}) const;

    // Declaration observers run synchronously and are never retained. Borrow a
    // caller's functor instead of constructing an owning std::function every
    // frame when its capture exceeds the implementation's small buffer.
    template<class Observer>
        requires(std::is_class_v<std::remove_cvref_t<Observer>> &&
                 !std::is_same_v<std::remove_cvref_t<Observer>,
                                 ce::owned_func<void(const LivePassNode&, const LiveBlackboard&)>> &&
                 std::is_invocable_r_v<void, Observer&, const LivePassNode&, const LiveBlackboard&>)
    void DeclareAll(LiveBlackboard& blackboard, EnhancedRenderGraph& graph, const EnhancedFrameContext& context,
                    const LiveFrameBinding& binding, Observer&& afterDeclare) const
    {
        using Callback = std::remove_reference_t<Observer>;
        DeclareAllImpl(blackboard, graph, context, binding, std::addressof(afterDeclare),
                       [](const void* observer, const LivePassNode& node, const LiveBlackboard& values)
                       {
                           // Callback preserves the argument's const qualifier;
                           // mutable lvalue/rvalue functors remain callable too.
                           auto* callback = const_cast<Callback*>(static_cast<const Callback*>(observer));
                           (*callback)(node, values);
                       });
    }

    /// 활성 read 또는 비활성 노드의 passthrough가 이번 프레임에 이 슬롯을 소비하는가.
    /// 선택적 입력을 그대로 흘리는 update만으로는 생산을 강제하지 않는다.
    bool HasConsumer(std::string_view slot, const EnhancedFrameContext& context, const LiveFrameBinding& binding) const;

    /// 렌더 디버그 창용. 노드 이름 · 활성 상태 · 슬롯 연결을 사람이 읽는 형태로.
    ///
    /// 이 덤프가 없으면 "이 패스가 어디 끼는지"를 알려면 RenderOnce를 정독해야
    /// 한다. 기본 폰트가 라틴 전용인 디버그 창에서 읽히도록 출력 어휘는 영문이다.
    std::string Dump() const;
    ce::dynamic_array<LivePassNodeSnapshot> SnapshotNodes() const
    {
        ce::dynamic_array<LivePassNodeSnapshot> result;
        result.reserve(m_nodes.size());
        for (const auto& node : m_nodes)
            result.push_back({node.name, bool(node.active), node.IsActive(), node.perView, node.reads, node.writes, node.modifies});
        return result;
    }

    /// GPU·활성 scene 없이 Validate와 Dump의 계약을 독립 검증한다.
    /// 정상 기술, 이름 없음, 비활성 가능 노드의 새 슬롯 발행, 미발행
    /// read/modify, 중복 발행, declare 누락과 덤프 순서·상태·슬롯을 검사한다.
    /// 블랙보드의 부분 문자열·키 소유·목록 복사·Reset 계약도 GPU 없이 확인한다.
    static bool RunSelfTest(std::string& outLog);

private:
  friend class RenderFeatureContributors;
  struct CompiledResourceUse
  {
      uint32_t slot{};
      LiveResourceContract contract;
      bool typed{};
  };
  struct CompiledPassthrough
  {
      uint32_t output{}, input{};
  };
  struct CompiledConsumer
  {
      uint32_t node{};
      bool whenDisabled{};
  };
  struct CompiledNode
  {
      uint32_t node{};
      ce::dynamic_array<CompiledResourceUse> inputs, outputs, updates;
      ce::dynamic_array<CompiledPassthrough> disabledPassthroughs;
      ce::dynamic_array<uint32_t> dependencies;
  };
  bool CompileFeaturePlan(std::string& outError) const;
  void DeclareAllImpl(LiveBlackboard& blackboard, EnhancedRenderGraph& graph, const EnhancedFrameContext& context,
                      const LiveFrameBinding& binding, const void* observer,
                      void (*afterDeclare)(const void*, const LivePassNode&, const LiveBlackboard&)) const;
  void ValidatePublishedResources(const CompiledNode& node, const LiveBlackboard& blackboard,
                                  const EnhancedRenderGraph& graph, const EnhancedFrameContext& context) const;
  ce::dynamic_array<LivePassNode> m_nodes;
  std::string_view m_currentContributor;
  RenderFeatureStage m_currentContributionStage{RenderFeatureStage::BeforePresent};
  uint64_t m_revision{1};
  mutable uint64_t m_compiledRevision{};
  mutable uint64_t m_planBindingId{};
  mutable ce::dynamic_array<CompiledNode> m_planNodes;
  mutable ce::dynamic_array<LiveResourceBinding> m_planResources;
  mutable ce::dynamic_array<ce::dynamic_array<CompiledConsumer>> m_planConsumers;
};
