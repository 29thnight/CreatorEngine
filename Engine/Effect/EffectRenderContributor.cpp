#include "EffectRenderContributor.h"
#include "../RenderEngine/Render/Core/RenderFeatureContributor.h"
#include "../RenderEngine/Render/Core/EnhancedLivePipelineDesc.h"

namespace ce::effects
{
class EffectContributor final : public IRenderFeatureContributor
{
public:
    EffectContributor(own::shared_owner<EffectBridgeCounters> counters,
        own::shared_owner<const std::atomic<std::uint64_t>> sceneEpoch)
        : counters_(std::move(counters)), sceneEpoch_(std::move(sceneEpoch)) {}
    std::string_view GetName() const noexcept override { return "Effect"; }
    RenderFeatureStage GetStage() const noexcept override { return RenderFeatureStage::BeforePostProcess; }
    void Contribute(LivePipelineDesc& pipeline, const RenderFeatureContext&) override
    {
        LivePassNode node;
        node.name = "Effect.HDRBoundary";
        node.Inputs({LiveResources::kLitColor,LiveResources::kGBufferDepth});
        node.declare = [counters = counters_, sceneEpoch = sceneEpoch_](LiveBlackboard& board, EnhancedRenderGraph& graph,
            const EnhancedFrameContext& context, const LiveFrameBinding&)
        {
            if (!context.effects || context.effects->sceneEpoch != sceneEpoch->load(std::memory_order_acquire)) return;
            const auto frame = context.effects;
            ++counters->declarations;
            // Read-only contract probe: no draw, integration or HDR modification
            // is claimed. The execute callback owns this exact frame even after
            // GT publication/reload or the next view overwrites its context.
            graph.AddPass("Effect.HDRBoundary", {
                {board.Get(LiveSlots::kLitColor),RHIResourceState::PixelShaderResource,RGAccessMode::Read},
                {board.Get(LiveSlots::kGBufferDepth),RHIResourceState::PixelShaderResource,RGAccessMode::Read}},
                [frame,counters,sceneEpoch](const EnhancedRenderGraph::ExecuteContext&) {
                    if (!frame->sceneEpoch) throw std::logic_error("Unscoped effect frame reached recording");
                    if (frame->sceneEpoch != sceneEpoch->load(std::memory_order_acquire)) return;
                    ++counters->records;
                }, true);
        };
        pipeline.AddNode(std::move(node));
    }
private:
    own::shared_owner<EffectBridgeCounters> counters_;
    own::shared_owner<const std::atomic<std::uint64_t>> sceneEpoch_;
};
own::shared_owner<IRenderFeatureContributor> MakeEffectRenderContributor(own::shared_owner<EffectBridgeCounters> counters,
    own::shared_owner<const std::atomic<std::uint64_t>> sceneEpoch)
{
    return own::make_shared<EffectContributor>(std::move(counters),std::move(sceneEpoch));
}
}
