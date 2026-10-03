#pragma once

#include "LXMaterialPipeline.h"

#include "MaterialGraphIblBake.h"
#include "Render/Passes/Geometry/EnhancedGBufferPass.h"

#include <map>
#include <mutex>

namespace material_graph
{
    struct SceneLookupResourcePool;
    struct SceneLookupStats
    {
        std::uint32_t baked{}, reused{}, visible{}, rejected{};
    };
    static_assert(sizeof(SceneLookupStats) == 16);

    class SceneLookupFrame
    {
      public:
        ~SceneLookupFrame();
        std::span<const RHITextureHandle> Inputs() const
        {
            return inputs_;
        }
        RHIBufferHandle Samples() const
        {
            return samples_;
        }
        RHIBufferHandle Statistics() const
        {
            return statistics_;
        }
        const std::array<RGHandle, 11>& DeclareInputs(EnhancedRenderGraph& graph) const;
        const std::array<RGHandle, 11>& GraphInputs(const EnhancedRenderGraph& graph) const;
        std::array<RGHandle, 11> DeclareCaptureOutputs(EnhancedRenderGraph& graph, unsigned first,
                                                       unsigned count) const;
        void DeclareBake(EnhancedRenderGraph& graph, RGHandle owners) const;
        void DeclareReady(EnhancedRenderGraph& graph) const;
        RGHandle GraphSamples(const EnhancedRenderGraph& graph) const;
        RGHandle GraphStatistics(const EnhancedRenderGraph& graph) const;

      private:
        friend class SceneLookupCache;
        friend struct SceneLookupResourcePool;
        IRenderDeviceServices* device_{};
        std::shared_ptr<SceneLookupResourcePool> resourcePool_;
        mutable RHICompletionPoint completion_{};
        std::uint32_t width_{}, height_{};
        std::uint64_t frameId_{}, viewId_{}, sceneEpoch_{}, environmentGeneration_{}, recording_{}, descriptors_{};
        RHITextureHandle environment_, irradiance_, prefiltered_, source_;
        std::array<RHITextureHandle, 3> importance_{};
        std::array<RHITextureHandle, 11> inputs_{};
        RHIBufferHandle samples_, statistics_, emptyPrevious_;
        std::shared_ptr<const LX::Runtime::ComputeGeneration> bake_, clear_;
        std::vector<RHIBufferSlice> constants_;
        RHIBindingTable outputs_;
        RHIBufferSlice filmSensitivityConstants_;
        std::weak_ptr<const SceneLookupFrame> previous_;
        bool reuse_{}, standalone_{};
        std::weak_ptr<const SceneLookupFrame> self_;
        mutable const EnhancedRenderGraph* graph_{};
        mutable std::uint64_t graphEpoch_{};
        mutable std::array<RGHandle, 11> graphInputs_{};
        mutable RGHandle graphSamples_, graphStatistics_;
        mutable std::array<RHIResourceState, 11> inputStates_{};
        mutable RHIResourceState sampleState_ = RHIResourceState::Common;
        mutable RHIResourceState statisticState_ = RHIResourceState::Common;
        mutable std::atomic<unsigned> stages_{};
        void CheckCurrent(const EnhancedRenderGraph& graph) const;
    };

    class SceneLookupCache : private IRHIUploadTransactionListener
    {
      public:
        bool Prepare(const EnhancedFrameContext& context, std::uint64_t viewId, RHITextureHandle environment,
                     RHITextureHandle irradiance, RHITextureHandle prefiltered, std::uint64_t environmentGeneration,
                     std::uint64_t memoryBudget, std::shared_ptr<const SceneLookupFrame>& result, std::string& error,
                     std::array<RHITextureHandle, 3> importance = {}, RHITextureHandle source = {},
                     bool standalone = false);
        // Called only after the entire owning graph has been successfully submitted.
        // A recorded callback or an upload prefix is not publication authorization.
        bool PublishSubmitted(const SceneLookupFrame& frame, std::uint64_t frameId, RHICompletionPoint completion,
                              std::string& error);
        void ShutdownAfterIdle();

      private:
        IRenderDeviceServices* device_{};
        LX::Runtime::ComputePipeline bake_, clear_;
        std::vector<std::shared_ptr<const SceneLookupFrame>> published_;
        std::shared_ptr<SceneLookupResourcePool> resourcePool_;
        std::mutex submissionMutex_;
        std::map<std::uint64_t, RHICompletionPoint> submitted_;
        void OnUploadSubmitted(std::uint64_t recordingId, RHICompletionPoint completion) override;
        void OnUploadCompleted(std::uint64_t completed) override;
        void OnUploadAborted(std::uint64_t recordingId) override;
        bool Initialize(const EnhancedFrameContext& context, std::string& error);
    };
} // namespace material_graph
