#pragma once

#include "LXMaterialPipeline.h"

#include "MaterialGraphIblBake.h"
#include "Render/Passes/Geometry/EnhancedGBufferPass.h"

#include <map>
#include <mutex>
#include <set>

namespace material_graph
{
    struct SceneLookupResourcePool;
    // baked counts reference integrations, approximated the split-sum ones.
    // visible = baked + approximated + reused + rejected.
    struct SceneLookupStats
    {
        std::uint32_t baked{}, reused{}, visible{}, rejected{};
        // reserved0 == 1은 픽셀 cache/readback 대신 fragment 평가를 사용한 프레임이다.
        std::uint32_t approximated{}, reserved0{}, reserved1{}, reserved2{};
    };
    static_assert(sizeof(SceneLookupStats) == 32);

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
            // UsesRuntimeEvaluation()이면 legacy root용 단일 sentinel이다.
            return samples_;
        }
        RHIBufferHandle Statistics() const
        {
            return statistics_;
        }
        bool UsesRuntimeEvaluation() const
        {
            return runtimeEvaluation_;
        }
        bool RequiresCapture() const
        {
            return capture_;
        }
        void DeclareShadingInputs(EnhancedRenderGraph& graph,
                                  std::vector<EnhancedRenderGraph::RGPassUsage>& uses) const;
        void BindRuntime(RHIEncoder& encoder, unsigned firstRootSlot) const;
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
        mutable bool submissionConfirmed_{};
        std::uint32_t width_{}, height_{};
        std::uint64_t frameId_{}, viewId_{}, sceneEpoch_{}, environmentGeneration_{}, recording_{}, descriptors_{};
        std::uint64_t resourceBytes_{}, exclusiveBytes_{};
        RHITextureHandle environment_, irradiance_, prefiltered_, source_;
        std::array<RHITextureHandle, 3> importance_{};
        std::array<RHITextureHandle, 11> inputs_{};
        RHIBufferHandle samples_, statistics_, emptyPrevious_;
        std::shared_ptr<const LX::Runtime::ComputeGeneration> bake_, clear_;
        std::vector<RHIBufferSlice> constants_;
        RHIBindingTable outputs_;
        RHIBindingTable runtimeInputs_;
        RHIBufferSlice runtimeConstants_;
        RHIBufferSlice filmSensitivityConstants_;
        std::weak_ptr<const SceneLookupFrame> previous_;
        bool reuse_{}, standalone_{}, runtimeEvaluation_{}, capture_ = true;
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
                     bool standalone = false, bool approximate = false, bool capture = true,
                     bool runtimeEvaluation = false);
        void ResetPreparationStatus()
        {
            preparationDeferred_ = false;
        }
        bool PreparationDeferred() const
        {
            return preparationDeferred_;
        }
        // Called only after the entire owning graph has been successfully submitted.
        // A recorded callback or an upload prefix is not publication authorization.
        bool PublishSubmitted(const SceneLookupFrame& frame, std::uint64_t frameId, RHICompletionPoint completion,
                              std::string& error);
        // 큐 접수의 수명 증거를 먼저 보관한다. native 성공과 cache 게시는 별도다.
        bool TrackAcceptedSubmission(const SceneLookupFrame& frame, RHICompletionPoint completion, std::string& error);
        void ShutdownAfterIdle();

      private:
        IRenderDeviceServices* device_{};
        LX::Runtime::ComputePipeline bake_, clear_;
        std::vector<std::shared_ptr<const SceneLookupFrame>> published_;
        std::shared_ptr<SceneLookupResourcePool> resourcePool_;
        std::mutex submissionMutex_;
        std::map<std::uint64_t, RHICompletionPoint> submitted_;
        std::set<std::uint64_t> acceptedRecordings_;
        bool preparationDeferred_{};
        void OnUploadSubmitted(std::uint64_t recordingId, RHICompletionPoint completion) override;
        void OnUploadCompleted(std::uint64_t completed) override;
        void OnUploadAborted(std::uint64_t recordingId) override;
        void OnUploadSubmissionRejected(std::uint64_t recordingId, RHICompletionPoint reservedCompletion) override;
        bool Initialize(const EnhancedFrameContext& context, std::string& error);
    };
} // namespace material_graph
