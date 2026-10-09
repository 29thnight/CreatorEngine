#pragma once

#include "LXMaterialPipeline.h"
#include "RHI/IRenderDeviceServices.h"
#include "Render/Graph/EnhancedRenderGraph.h"

#include <mathematics/matrix4x4.hpp>

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct EnhancedFrameContext;

// One current-frame, standard-Z depth pyramid shared by any number of indexed
// and meshlet bins. Its textures belong to the existing graph; no temporal
// resources, history promotion, or hidden submission lifetime are introduced.
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
    };

    static ViewKey CaptureView(const EnhancedFrameContext& context, const math::matrix4x4& viewProjection);
    static std::uint32_t LevelCount(std::uint32_t width, std::uint32_t height);
    bool Prepare(const EnhancedFrameContext& context, const math::matrix4x4& viewProjection,
        std::shared_ptr<const Pyramid>& result, std::string& error);
    void ShutdownAfterIdle();

private:
    IRenderDeviceServices* m_device{};
    LX::Runtime::ComputePipeline m_build;
};
