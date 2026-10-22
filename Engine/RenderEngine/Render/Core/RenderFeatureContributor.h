#pragma once

#include "../../RHI/RHIFormat.h"
#include "../../../Utility_Framework/Ownership.h"

class LivePipelineDesc;
struct EnhancedGizmoSceneData;

enum class RenderFeatureStage : uint8_t
{
    /// Read GBuffer/temporal/scene inputs; only Scene.LitColor is writable.
    AfterOpaque,
    /// Read scene inputs; only Scene.LitColor is writable (render or display size).
    BeforePostProcess,
    /// Read available scene/display inputs; only Display.LDR is writable.
    BeforePresent,
};

inline const char* RenderFeatureStageName(RenderFeatureStage stage)
{
    switch (stage)
    {
    case RenderFeatureStage::AfterOpaque:
        return "AfterOpaque";
    case RenderFeatureStage::BeforePostProcess:
        return "BeforePostProcess";
    case RenderFeatureStage::BeforePresent:
        return "BeforePresent";
    default:
        return "Unknown";
    }
}

// 렌더 기능 기여 경계 — EngineLayerSeparationPlan E4(§4.4)의 기여 지점.
//
// Host(Editor)가 파이프라인 조립 시점에 자기 노드를 끼워 넣는다. RenderCore는
// 구체 Editor 패스를 조립하지 않고, 기여자가 노드와 패스 인스턴스 수명을
// 소유한다. Player는 기여자를 설치하지 않으므로 노드 자체가 서지 않는다.
//
// 파이프라인은 백엔드별로 따로 서고 리사이즈마다 다시 서므로 Contribute도
// 그때마다 다시 불린다. 기여자는 호출마다 새 패스 묶음을 만들어 노드 람다에
// shared_owner로 실어야 한다 — 그래야 파이프라인 둘이 공존해도 인스턴스가
// 섞이지 않고, desc.Clear()가 노드와 함께 묶음을 놓는다(ShutdownAll 뒤).
struct RenderFeatureContext
{
    /// 기여 노드가 그릴 최종 표시 표적의 포맷. 입력 텍스처에 직접 그리는
    /// 구조라 PSO의 RTV 포맷을 여기 맞춰야 한다 — 어긋나면 커맨드 리스트
    /// 무효 → 디바이스 제거다(실측).
    RHIFormat ldrFormat{};

    /// 단일 뷰 호환 입력. 여러 뷰를 사용하는 Host는 gizmoSceneForView로
    /// 각 실행 슬롯이 소유한 준비 데이터를 제공한다.
    const EnhancedGizmoSceneData* gizmoScene{ nullptr };
    uint32_t viewCount{1};
    std::function<const EnhancedGizmoSceneData*(uint32_t)> gizmoSceneForView;
    uint32_t renderWidth{}, renderHeight{}, displayWidth{}, displayHeight{};
};

struct IRenderFeatureContributor
{
    virtual ~IRenderFeatureContributor() = default;

    virtual std::string_view GetName() const noexcept { return "Host"; }
    virtual RenderFeatureStage GetStage() const noexcept { return RenderFeatureStage::BeforePresent; }

    /// GetStage의 조립 지점에서 불린다. AddNode 순서는 CPU 선언 순서이며,
    /// GPU 실행 의존성은 실제 패스의 리소스 사용으로부터 그래프가 만든다.
    /// viewCount만큼 실행 인스턴스를 소유하고 instance(viewIndex)로 고른다.
    virtual void Contribute(LivePipelineDesc& pipeline,
        const RenderFeatureContext& context) = 0;
};

/// Registration changes happen outside an installed pipeline. Each pipeline
/// snapshot retains its contributors until shutdown; no borrowed plugin pointer
/// crosses declaration/recording boundaries. Stage order is fixed and registration
/// order within a stage is stable. Resource and node conflicts are rejected by
/// LivePipelineDesc::Validate before native initialization.
/// Contributors declare every input/output/update with typed contracts. Engine
/// GBuffer, temporal and scene-command slots are read-only; HDR stages can update
/// Scene.LitColor and BeforePresent can update Display.LDR. Private feature slots
/// can be produced/updated at any stage, subject to normal dependency validation.
class RenderFeatureContributors
{
  public:
    bool Add(own::shared_owner<IRenderFeatureContributor> contributor, std::string& outError)
    {
        outError.clear();
        if (!contributor || contributor->GetName().empty())
        {
            outError = "A render feature contributor requires an owner and a nonempty name.";
            return false;
        }
        const std::string_view name = contributor->GetName();
        if (std::any_of(m_entries.begin(), m_entries.end(), [&](const Entry& entry) { return entry.name == name; }))
        {
            outError = "Render feature contributor '" + std::string{name} + "' is already registered.";
            return false;
        }
        const RenderFeatureStage stage = contributor->GetStage();
        if (stage != RenderFeatureStage::AfterOpaque && stage != RenderFeatureStage::BeforePostProcess &&
            stage != RenderFeatureStage::BeforePresent)
        {
            outError = "Render feature contributor '" + std::string{name} + "' has an invalid stage.";
            return false;
        }
        m_entries.push_back({std::string{name}, stage, std::move(contributor)});
        return true;
    }
    bool Remove(std::string_view name)
    {
        const auto entry =
            std::find_if(m_entries.begin(), m_entries.end(), [&](const Entry& value) { return value.name == name; });
        if (entry == m_entries.end())
        {
            return false;
        }
        m_entries.erase(entry);
        return true;
    }
    void Clear() { m_entries.clear(); }
    bool IsEmpty() const { return m_entries.empty(); }
    size_t Size() const { return m_entries.size(); }
    void Contribute(RenderFeatureStage stage, LivePipelineDesc& pipeline, const RenderFeatureContext& context) const;

  private:
    struct Entry
    {
        std::string name;
        RenderFeatureStage stage;
        own::shared_owner<IRenderFeatureContributor> contributor;
    };
    std::vector<Entry> m_entries;
};
