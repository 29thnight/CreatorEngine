#pragma once
#include "../Utility_Framework/Ownership.h"

#include "LXMaterialPipeline.h"
#include "RHI/IRenderDeviceServices.h"
#include "Render/Graph/EnhancedRenderGraph.h"

#include <mathematics/matrix4x4.hpp>

#include "../Utility_Framework/StandardLibrary.h"

struct EnhancedFrameContext;

// Standard-Z pyramids. Persistent history is immutable and explicitly retained
// by submitted frames; importing it creates a new recording-local view.
class GpuGeometryOcclusion final
{
    struct HistoryPool;
    struct DeclarationLayout;

  public:
    static constexpr std::uint32_t kMaximumLevels = 16;

    struct ViewKey
    {
        IRenderDeviceServices* device{};
        std::uint64_t recording{}, descriptors{}, frameId{}, sceneEpoch{};
        std::uint32_t width{}, height{};
        std::array<std::uint32_t, 16> projectionBits{};
        bool operator==(const ViewKey&) const = default;
    };

    struct History
    {
        ~History();
        IRenderDeviceServices* device{};
        ViewKey source;
        std::uint64_t viewId{}, revision{}, completion{};
        std::vector<RHITextureHandle> textures;
        std::string name{"METEOR.Camera"};
        std::atomic<bool> recorded{};
        own::shared_owner<HistoryPool> pool;
        RHIResourceState initialState{RHIResourceState::Common};
    };

    class Pyramid
    {
      public:
        // depth is an earlier, same-view D32Float RG2 version, cleared to 1.
        // Repeating this exact declaration is harmless; changing its producer
        // depth version or graph is an error. Consumers must not feed it back.
        void Declare(EnhancedRenderGraph& graph, RGHandle depth) const;
        void RequireView(const ViewKey& view) const;
        void RequireCurrentView(const ViewKey& view) const;
        void AddReadUsages(EnhancedRenderGraph& graph, auto& usages) const
        {
            CheckCurrent(&graph);
            if (m_declaredLevels != m_levelCount)
            {
                throw std::runtime_error("Current-depth pyramid is not completely declared.");
            }
            for (const auto level : std::span{m_levels}.first(m_declaredLevels))
            {
                const auto duplicate = std::find_if(
                    std::span{usages}.begin(), std::span{usages}.end(),
                    [&](const auto& usage)
                    {
                        return (level.index == usage.handle.index && level.version == usage.handle.version &&
                                level.kind == usage.handle.kind && level.epoch == usage.handle.epoch);
                    });
                if (duplicate == std::span{usages}.end())
                {
                    usages.push_back({level, RHIResourceState::ShaderResource, RGAccessMode::Read});
                }
                else if (duplicate->state != RHIResourceState::ShaderResource ||
                         duplicate->access != RGAccessMode::Read)
                {
                    throw std::runtime_error("Current-depth pyramid consumer has conflicting resource usage.");
                }
            }
        }
        RHIBindingTable Bindings(const EnhancedRenderGraph::ExecuteContext& execution) const;
        std::array<RHIBindingDesc, kMaximumLevels> BindingInputs(
            const EnhancedRenderGraph::ExecuteContext& execution) const;
        std::uint32_t LevelCount() const { return m_levelCount; }
        const own::shared_owner<History>& HistoryOwner() const { return m_history; }
        void ImportHistory(EnhancedRenderGraph& graph) const;

      private:
        friend class GpuGeometryOcclusion;
        void CheckCurrent(const EnhancedRenderGraph* graph = nullptr) const;

        ViewKey m_view;
        own::shared_owner<const DeclarationLayout> m_declarationLayout;
        std::uint32_t m_levelCount{};
        own::shared_owner<const LX::Runtime::ComputeGeneration> m_build;
        own::weak_owner<const Pyramid> m_self;
        mutable const EnhancedRenderGraph* m_graph{};
        mutable std::uint64_t m_graphEpoch{};
        mutable RGHandle m_depth;
        mutable std::array<RGHandle, kMaximumLevels> m_levels{};
        mutable std::uint32_t m_declaredLevels{};
        own::shared_owner<History> m_history;
        bool m_historyReader{};
        std::string m_name{"Geometry.Occlusion"};
    };

    static ViewKey CaptureView(const EnhancedFrameContext& context, const math::matrix4x4& viewProjection);
    static std::uint32_t LevelCount(std::uint32_t width, std::uint32_t height);
    bool Prepare(const EnhancedFrameContext& context, const math::matrix4x4& viewProjection,
                 own::shared_owner<const Pyramid>& result, std::string& error,
                 const std::string& name = "Geometry.Occlusion");
    bool PrepareHistory(const EnhancedFrameContext& context, const math::matrix4x4& projection, std::uint64_t viewId,
                        std::uint64_t revision, own::shared_owner<const Pyramid>& result, std::string& error,
                        const std::string& name = "Geometry.Occlusion");
    static own::shared_owner<const Pyramid> ReadHistory(const EnhancedFrameContext& context,
                                                      const math::matrix4x4& projection,
                                                      const own::shared_owner<History>& history);
    void ShutdownAfterIdle();

  private:
    IRenderDeviceServices* m_device{};
    LX::Runtime::ComputePipeline m_build;
    own::shared_owner<HistoryPool> m_historyPool;
    // Bounded CPU-only descriptions. Submitted pyramids retain evicted layouts.
    std::array<own::shared_owner<const DeclarationLayout>, 16> m_declarationLayouts;
    std::size_t m_nextDeclarationLayout{};
};
