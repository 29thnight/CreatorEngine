#pragma once
#include "../../../Engine/RenderEngine/MaterialGraphProduct.h"
namespace material_graph
{
struct PipelineGeneration
{
    std::uint64_t generation{};
    std::string semanticKey;
    Selection selection;
    BindingLayout layout;
    ResourcePacket resources;
    LX::Runtime::GraphicsPipeline pipeline;
};

// One render-thread owner publishes the PSO, route, uniform layout and texture
// owners together. It must retire only handles exclusively owned by this slot.
class PipelineSlot
{
  public:
    // Bytecode is selected from the verified surface-only generation. Volume
    // must be published with its composition pipeline by the owning renderer.
    // The caller supplies fixed-function state, formats and the installed layout.
    bool Publish(IRenderPipelineCache& cache, const VerifiedProduct& product, RHIShaderBinary backend,
                 const Capabilities& capabilities, const Budget& budget, std::span<const ParameterOverride> parameters,
                 std::span<const TextureBinding> textures, const RHIGraphicsPipelineDesc& pipeline,
                 RHICompletionPoint retireAfter, std::vector<LX::LXMaterialDiagnostic>& diagnostics);
    const std::shared_ptr<const PipelineGeneration>& Active() const { return active_; }
    // Called with the completed graphics timeline before retiring CPU texture
    // owners. Zero fences retain owners until destruction after device idle.
    // The slot must outlive all submitted generations or be drained idle.
    void CollectRetired(RHICompletionPoint completed);
    std::size_t RetiredGenerationCount() const { return retired_.size(); }

  private:
    struct RetiredGeneration
    {
        std::shared_ptr<const PipelineGeneration> owner;
        RHICompletionPoint after;
    };
    std::shared_ptr<const PipelineGeneration> active_;
    std::vector<RetiredGeneration> retired_;
};
} // namespace material_graph
