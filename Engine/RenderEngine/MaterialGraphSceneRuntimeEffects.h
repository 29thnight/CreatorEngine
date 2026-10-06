#pragma once

#include "MaterialGraphSceneLookup.h"

namespace material_graph
{
    inline constexpr std::uint32_t SceneRuntimeTileSize = 256;
    inline constexpr std::uint32_t SceneRuntimeSubsurfaceRadius = 4;
    inline constexpr std::uint32_t SceneRuntimeTileExtent =
        SceneRuntimeTileSize + 2 * SceneRuntimeSubsurfaceRadius;
    inline constexpr std::uint32_t SceneRuntimeEffectStride = 224;

    struct SceneRuntimeEffectPool;
    struct SceneRuntimeEffectBundle;

    struct SceneRuntimeEffectTile
    {
        std::uint32_t x{}, y{}, width{}, height{};
        std::uint32_t sourceX{}, sourceY{}, sourceWidth{}, sourceHeight{};
        RHIBufferSlice constants;
    };

    // 런타임 효과의 scratch는 표시용 HDR과 수명이 다르다. 전체 그래프가
    // GPU에서 완료된 뒤에는 이전 표시 프레임이 남아 있어도 다음 lease가 재사용한다.
    class SceneRuntimeEffectsFrame
    {
      public:
        ~SceneRuntimeEffectsFrame();
        std::span<const SceneRuntimeEffectTile> Tiles() const
        {
            return tiles_;
        }
        void DeclareInputs(EnhancedRenderGraph& graph) const;
        void DeclareBackground(EnhancedRenderGraph& graph, RGHandle color, RGHandle depth) const;
        RGHandle DeclareDepthOutput(EnhancedRenderGraph& graph) const;
        void RecordDepth(const EnhancedRenderGraph::ExecuteContext& execution, RGHandle source, std::size_t tile) const;
        std::array<RGHandle, 14> DeclareCaptureOutputs(EnhancedRenderGraph& graph, unsigned part) const;
        std::span<const RHITextureHandle> Inputs() const;
        RHITextureHandle Depth() const;
        RHIRenderTargetBinding CaptureTargets(unsigned part) const;
        void DeclareShadingInputs(EnhancedRenderGraph& graph,
                                  std::vector<EnhancedRenderGraph::RGPassUsage>& uses,
                                  bool samples = true, bool background = false) const;
        void Bind(RHIEncoder& encoder, unsigned firstRootSlot, std::size_t tile) const;
        void MarkSubmitted(RHICompletionPoint completion) const;
        void ConfirmSubmitted(RHICompletionPoint completion) const;
        void CheckCurrent(const EnhancedRenderGraph& graph) const;

      private:
        friend class SceneRuntimeEffectsResources;
        IRenderDeviceServices* device_{};
        std::shared_ptr<SceneRuntimeEffectPool> pool_;
        std::shared_ptr<SceneRuntimeEffectBundle> bundle_;
        std::uint64_t recording_{}, descriptors_{}, lease_{};
        bool transmission_{};
        std::vector<SceneRuntimeEffectTile> tiles_;
        std::shared_ptr<const LX::Runtime::GraphicsGeneration> depthCopy_;
        RHIBindingTable inputs_, background_;
        RHIRenderTargetBinding depthTarget_;
        std::array<RHIRenderTargetBinding, 2> captureTargets_{};
        mutable std::mutex depthBindingMutex_;
        mutable std::map<std::uint64_t, RHIBindingTable> depthBindings_;
        std::weak_ptr<const SceneRuntimeEffectsFrame> self_;
        mutable const EnhancedRenderGraph* graph_{};
        mutable std::uint64_t graphEpoch_{};
        mutable std::array<RGHandle, 14> graphInputs_{};
        mutable RGHandle graphTileDepth_, graphColor_, graphDepth_;
    };

    class SceneRuntimeEffectsResources : private IRHIUploadTransactionListener
    {
      public:
        bool Prepare(const EnhancedFrameContext& context, const math::matrix4x4& viewProjection,
                     bool transmission, bool backgroundHasMedium, std::uint64_t effectBudget,
                     std::uint64_t refractionBudget, std::shared_ptr<const SceneRuntimeEffectsFrame>& result,
                     std::string& error);
        bool PreparationDeferred() const
        {
            return preparationDeferred_;
        }
        void ResetPreparationStatus()
        {
            preparationDeferred_ = false;
        }
        void ShutdownAfterIdle();

      private:
        IRenderDeviceServices* device_{};
        LX::Runtime::GraphicsPipeline depthCopy_;
        std::shared_ptr<SceneRuntimeEffectPool> pool_;
        bool preparationDeferred_{};
        bool Initialize(const EnhancedFrameContext& context, std::string& error);
        void OnUploadSubmitted(std::uint64_t recording, RHICompletionPoint completion) override;
        void OnUploadCompleted(std::uint64_t completed) override;
        void OnUploadAborted(std::uint64_t recording) override;
        void OnUploadSubmissionRejected(std::uint64_t recording, RHICompletionPoint reservedCompletion) override;
    };
} // namespace material_graph
