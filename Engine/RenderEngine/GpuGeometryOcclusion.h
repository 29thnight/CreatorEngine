#pragma once

#include "LXMaterialPipeline.h"
#include "RHI/IRenderDeviceServices.h"
#include "Render/Graph/EnhancedRenderGraph.h"

#include <mathematics/matrix4x4.hpp>

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct EnhancedFrameContext;

// Standard-Z pyramids. Persistent history is immutable and explicitly retained
// by submitted frames; importing it creates a new recording-local view.
class GpuGeometryOcclusion final
{
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
        std::atomic<bool> recorded{};
    };

    class Pyramid
    {
    public:
        // depth is an earlier, same-view D32Float RG2 version, cleared to 1.
        // Repeating this exact declaration is harmless; changing its producer
        // depth version or graph is an error. Consumers must not feed it back.
        void Declare(EnhancedRenderGraph& graph, RGHandle depth) const;
        void RequireView(const ViewKey& view) const;
        void AddReadUsages(EnhancedRenderGraph& graph,
            std::vector<EnhancedRenderGraph::RGPassUsage>& usages) const;
        RHIBindingTable Bindings(const EnhancedRenderGraph::ExecuteContext& execution) const;
        std::uint32_t LevelCount() const { return m_levelCount; }
        const std::shared_ptr<History>& HistoryOwner() const { return m_history; }
        void ImportHistory(EnhancedRenderGraph& graph) const;

    private:
        friend class GpuGeometryOcclusion;
        void CheckCurrent(const EnhancedRenderGraph* graph = nullptr) const;

        ViewKey m_view;
        std::uint32_t m_levelCount{};
        std::shared_ptr<const LX::Runtime::ComputeGeneration> m_build;
        std::weak_ptr<const Pyramid> m_self;
        mutable const EnhancedRenderGraph* m_graph{};
        mutable std::uint64_t m_graphEpoch{};
        mutable RGHandle m_depth;
        mutable std::vector<RGHandle> m_levels;
        std::shared_ptr<History> m_history;
        bool m_historyReader{};
    };

    static ViewKey CaptureView(const EnhancedFrameContext& context, const math::matrix4x4& viewProjection);
    static std::uint32_t LevelCount(std::uint32_t width, std::uint32_t height);
    bool Prepare(const EnhancedFrameContext& context, const math::matrix4x4& viewProjection,
        std::shared_ptr<const Pyramid>& result, std::string& error);
    bool PrepareHistory(const EnhancedFrameContext& context, const math::matrix4x4& projection,
        std::uint64_t viewId, std::uint64_t revision,
        std::shared_ptr<const Pyramid>& result, std::string& error);
    static std::shared_ptr<const Pyramid> ReadHistory(const EnhancedFrameContext& context,
        const math::matrix4x4& projection, const std::shared_ptr<History>& history);
    void ShutdownAfterIdle();

private:
    IRenderDeviceServices* m_device{};
    LX::Runtime::ComputePipeline m_build;
};
