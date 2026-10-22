#include "../Utility_Framework/Ownership.h"
// Internal evaluator inputs. Public consumers receive purpose-specific contracts.
struct SceneShadingInputs
{
    SceneLayerWork layers;
    std::array<RGHandle, 2> material;
};

// Each stage owns its prepared resources and the code that declares their graph passes.
struct SceneHost::GeometryStage
{
    own::shared_owner<const SceneFrameGeometry> geometryTable;
    bool Prepare(own::shared_owner<SceneFrameGeometry> table, std::string& error,
                 ResidentBufferCache* cache = nullptr, ResidentBufferCache::Key key = {})
    {
        if (!table->Upload(error, cache, key))
            return false;
        geometryTable = std::move(table);
        return true;
    }
    RGHandle DeclareGeometry(const own::shared_owner<const Frame>& prepared, EnhancedRenderGraph& graph) const;
};

struct SceneHost::FirstCullStage
{
    own::shared_owner<const GpuGeometryVisibility::Frame> cameraDepthMeshlets;
    SceneFirstCullOutputs DeclareFirstCull(const own::shared_owner<const Frame>& prepared, EnhancedRenderGraph& graph,
                                           RGHandle geometry) const;
};

struct SceneHost::OccluderStage
{
    own::shared_owner<const IndirectMaterialBatch> depthCommands;
    own::shared_owner<const SceneHybridVisibility> occluderVisibility;
    bool Prepare(const FrameInput& frame, const EnhancedFrameContext& context,
                 const own::shared_owner<const SceneFrameGeometry>& geometry,
                 const own::shared_owner<const GpuGeometryVisibility::Frame>& list, HybridVisibilityPipelines& pipelines,
                 std::string& error);
    RGHandle DeclareOccluder(const own::shared_owner<const Frame>& prepared, EnhancedRenderGraph& graph,
                             RGHandle geometry, const SceneFirstCullOutputs& first) const;
};

struct SceneHost::FinalCullStage
{
    // Opaque recovery union, and the frustum-culled blended list in source meshlet order.
    own::shared_owner<const GpuGeometryVisibility::Frame> cameraMeshlets, blendedMeshlets;
    // Built from the occluder depth; read by opaque meshlet recovery.
    own::shared_owner<const GpuGeometryOcclusion::Pyramid> currentPyramid;
    SceneFinalCullOutputs DeclareFinalCull(const own::shared_owner<const Frame>& prepared, EnhancedRenderGraph& graph,
                                           RGHandle geometry, RGHandle occluderDepth,
                                           const own::shared_owner<const SceneHybridVisibility>& occluder) const;
};

struct SceneHost::ShadowCacheStage
{
    own::shared_owner<ShadowCache> cache;
    std::array<own::shared_owner<const GpuGeometryVisibility::Frame>, 3> meshlets;
    std::array<own::shared_owner<const IndirectMaterialBatch>, 3> commands;
    bool hit{};
    mutable SceneShadowResources outputs{};
    SceneShadowResources Declare(const own::shared_owner<const Frame>& frame, EnhancedRenderGraph& graph,
                                 RGHandle geometry) const;
};

struct SceneHost::ShadowFirstCullStage
{
    std::array<own::shared_owner<const GpuGeometryVisibility::Frame>, 3> meshlets;
    std::optional<GpuGeometryOcclusion::ViewKey> m_cameraPredictionView;
    mutable SceneShadowResources outputs{};
    SceneShadowResources Declare(const own::shared_owner<const Frame>& frame, EnhancedRenderGraph& graph,
                                 RGHandle geometry, RGHandle cameraOccluderDepth) const;
};

struct SceneHost::ShadowOccluderStage
{
    std::array<own::shared_owner<const IndirectMaterialBatch>, 3> commands;
    mutable SceneShadowResources outputs{};
    SceneShadowResources Declare(const own::shared_owner<const Frame>& frame, EnhancedRenderGraph& graph,
                                 const SceneShadowResources& cache, const SceneShadowResources& first) const;
};

struct SceneHost::ShadowFinalCullStage
{
    std::array<own::shared_owner<const GpuGeometryVisibility::Frame>, 3> meshlets;
    std::array<own::shared_owner<const GpuGeometryOcclusion::Pyramid>, 3> pyramids;
    mutable SceneShadowResources outputs{};
    SceneShadowResources Declare(const own::shared_owner<const Frame>& frame, EnhancedRenderGraph& graph,
                                 const SceneShadowResources& depths) const;
};

struct SceneHost::ShadowRasterStage
{
    std::array<own::shared_owner<const IndirectMaterialBatch>, 3> shadowCommands;
    own::shared_owner<const LX::Runtime::GraphicsGeneration> merge;
    mutable RGHandle output;
    static RGHandle Draw(const own::shared_owner<const Frame>& frame, EnhancedRenderGraph& graph,
                         const own::shared_owner<const IndirectMaterialBatch>& commands, RGHandle depth,
                         unsigned cascade, unsigned slice, bool staticCasters, bool count, const std::string& name);
    RGHandle Declare(const own::shared_owner<const Frame>& frame, EnhancedRenderGraph& graph,
                     const SceneShadowResources& cache, const SceneShadowResources& lists, RGHandle shadowMap) const;
};

struct SceneHost::ShadowHistoryStage
{
    std::array<own::shared_owner<const GpuGeometryOcclusion::Pyramid>, 3> next;
    mutable RGHandle declaredInput;
    void Declare(const own::shared_owner<const Frame>& frame, EnhancedRenderGraph& graph, RGHandle expected,
                 RGHandle shadowMap) const;
};

struct SceneHost::TemporalStage
{
    own::shared_owner<const IndirectMaterialBatch> temporalCommands;
    std::array<RGHandle, 5> DeclareTemporal(const own::shared_owner<const Frame>& prepared, EnhancedRenderGraph& graph,
                                            RGHandle expectedVisibility, RGHandle visibility,
                                            const std::array<RGHandle, 2>& material, RGHandle layerList,
                                            std::array<RGHandle, 5> targets) const;
};

struct SceneHost::VisibilityStage
{
    own::shared_owner<const IndirectMaterialBatch> materialCommands;
    own::shared_owner<const SceneHybridVisibility> hybridVisibility;
    static bool PrepareVisibility(const FrameInput& frame, const EnhancedFrameContext& context,
                                  const own::shared_owner<const SceneFrameGeometry>& geometry,
                                  HybridVisibilityPipelines& pipelines, const char* name, bool depthOnly,
                                  const own::shared_owner<const GpuGeometryVisibility::Frame>& list,
                                  own::shared_owner<const SceneHybridVisibility>& result, std::string& error,
                                  const SceneHybridVisibility* sharedPreparation = nullptr);
    bool Prepare(const FrameInput& frame, const EnhancedFrameContext& context,
                 const own::shared_owner<const SceneFrameGeometry>& geometry,
                 const own::shared_owner<const GpuGeometryVisibility::Frame>& list, HybridVisibilityPipelines& pipelines,
                 std::string& error, const SceneHybridVisibility* sharedPreparation = nullptr);
    mutable SceneSurfaceTargets::Outputs targets;
    SceneSurfaceTargets::Outputs Declare(const own::shared_owner<const Frame>& prepared, EnhancedRenderGraph& graph,
                                         const SceneFinalCullOutputs& cull,
                                         const SceneSurfaceTargets::Outputs& incoming) const;
};

struct SceneHost::MaterialWorkStage
{
    own::shared_owner<const SceneMaterialWork> materialWork;
    std::array<RGHandle, 2> Declare(const own::shared_owner<const Frame>& prepared, EnhancedRenderGraph& graph,
                                    RGHandle expectedVisibility, RGHandle visibility) const;
};

struct SceneHost::SurfaceResolveStage
{
    mutable SceneSurfaceTargets::Outputs gbuffer;
    SceneSurfaceTargets::Outputs Declare(const own::shared_owner<const Frame>& prepared, EnhancedRenderGraph& graph,
                                         RGHandle expectedVisibility, const SceneSurfaceTargets::Outputs& incoming,
                                         const std::array<RGHandle, 2>& material) const;
};

struct SceneHost::HistoryStage
{
    own::shared_owner<const GpuGeometryOcclusion::Pyramid> nextCameraHistory;
    void DeclareHistory(const own::shared_owner<const Frame>& prepared, EnhancedRenderGraph& graph,
                        RGHandle expectedDepth, RGHandle depth) const;
};

struct SceneHost::VolumeStage
{
    own::shared_owner<const SceneVolumeFrame> frame;
    std::vector<SceneVolumeBinding> bindings;
    RHIBufferSlice constants, buffer;
    RHIBindingTable table;
    mutable RGHandle output;
    RGHandle Declare(const own::shared_owner<const Frame>& prepared, EnhancedRenderGraph& graph, RGHandle shaded,
                     RGHandle lighting, RGHandle depth, RGHandle shadowMap) const;
    template<class DrawType>
    void BindForward(RHIEncoder& encoder, const DrawType& draw, const EnhancedForwardLighting& forward) const;
};

struct SceneHost::LayerStage
{
    own::shared_owner<const IndirectMaterialBatch> indexedCommands;
    mutable SceneSurfaceTargets::Outputs forwardGbuffer;
    mutable std::map<std::size_t, RGHandle> blendedOutputs;
    mutable std::mutex forwardTargetMutex;
    mutable RHIRenderTargetBinding forwardTargets;
    struct BlendedRange
    {
        std::vector<size_t> inputs;
        std::array<own::shared_owner<const IndirectMaterialBatch>, 2> commands;
    };
    std::vector<BlendedRange> blendedRanges;
    bool PrepareBlended(const FrameInput& frame, const own::shared_owner<const SceneLookupFrame>& alphaLookup,
                        bool hasRuntimeEffects, const own::shared_owner<const LX::Runtime::ComputeGeneration>& generation,
                        IndirectMaterialBatchPool& batches, std::string& error);
    void DeclareForwardGBuffer(const own::shared_owner<const Frame>& prepared, EnhancedRenderGraph& graph,
                               const SceneSurfaceTargets::Outputs& opaque, const SceneShadingInputs& commands,
                               std::size_t index) const;
    SceneLayerWork Declare(const own::shared_owner<const Frame>& frame, EnhancedRenderGraph& graph,
                           RGHandle list) const;
};

struct SceneHost::EffectStage
{
    LayerStage layers;
    struct LookupState
    {
        own::shared_owner<const SceneLookupFrame> opaque, layered;
    } lookup;

    own::shared_owner<const SceneSubsurfaceFrame> subsurface;
    own::shared_owner<const SceneRefractionFrame> refraction;

    mutable RGHandle colorOutput;
    VolumeStage volume;
    struct DecalState
    {
        std::array<RHIBufferSlice, 2> constants;
        void AddUses(const SceneSurfaceTargets::Outputs& inputs, auto& uses,
                     bool transmission) const;
        RHIBindingTable Table(const FrameInput& frame, const EnhancedRenderGraph::ExecuteContext& execution,
                              const SceneSurfaceTargets::Outputs& inputs, bool transmission) const;
        template<class DrawType>
        void Bind(RHIEncoder& encoder, const DrawType& draw, RHIBindingTable table, bool enabled) const;
    } decal;
    struct RuntimeBindings
    {
        RHIRenderTargetBinding color;
        RHIBindingTable common, decal, volume;
    };
    struct RuntimeState
    {
        own::shared_owner<const SceneRuntimeEffectsFrame> frame;
        RHIBindingTable emptyInputs, emptyBackground;
        mutable std::mutex mutex;
        mutable std::map<std::array<std::uint64_t, 13>, RuntimeBindings> bindings;
        RuntimeBindings Tables(const FrameInput& frame, const EnhancedRenderGraph::ExecuteContext& execution,
                               const SceneSurfaceTargets::Outputs& inputs, RGHandle lighting, RGHandle ambientOcclusion,
                               RGHandle shadowMap, bool blended, bool transmissionStage, const VolumeStage& volume,
                               const DecalState& decal) const;
    } runtime;
    template<class DrawType>
    void BindForward(RHIEncoder& encoder, const DrawType& draw, const EnhancedForwardLighting& forward) const;

    RGHandle DeclareColor(const own::shared_owner<const Frame>& prepared, EnhancedRenderGraph& graph,
                          const SceneSurfaceTargets::Outputs& expected, const SceneSurfaceTargets::Outputs& inputs,
                          const SceneColorInputs& inputWork, RGHandle lighting, RGHandle ambientOcclusion,
                          RGHandle shadowMap) const;
    std::vector<ForwardDraw> BlendedDraws(const own::shared_owner<const Frame>& prepared) const;
    RGHandle DeclareBlended(const own::shared_owner<const Frame>& prepared, EnhancedRenderGraph& graph,
                            const SceneSurfaceTargets::Outputs& opaque, const std::vector<std::size_t>& indices,
                            const SceneLayerWork& commands, RGHandle lighting, RGHandle shadowMap,
                            const EnhancedForwardLighting& forward) const;
    RGHandle DeclareBlendedStream(const own::shared_owner<const Frame>& prepared, EnhancedRenderGraph& graph,
                                  const SceneSurfaceTargets::Outputs& opaque, const SceneLayerWork& commands,
                                  RGHandle lighting, RGHandle shadowMap, const EnhancedForwardLighting& tiles) const;

    void DeclareRefractionCapture(const own::shared_owner<const Frame>& prepared, EnhancedRenderGraph& graph,
                                  const SceneSurfaceTargets::Outputs& inputs, const SceneShadingInputs& commands,
                                  std::optional<std::size_t> index) const;
    RGHandle DeclareShading(const own::shared_owner<const Frame>& prepared, EnhancedRenderGraph& graph,
                            const SceneSurfaceTargets::Outputs& inputs, const SceneShadingInputs& commands,
                            RGHandle lighting, RGHandle ambientOcclusion, RGHandle shadowMap, bool transmissionStage,
                            std::optional<std::size_t> blended = {}, EnhancedForwardLighting forward = {},
                            std::vector<std::size_t> blendedGroup = {}) const;
    RGHandle DeclareRuntimeEffects(const own::shared_owner<const Frame>& prepared, EnhancedRenderGraph& graph,
                                   const SceneSurfaceTargets::Outputs& inputs, const SceneShadingInputs& commands,
                                   RGHandle lighting, RGHandle ambientOcclusion, RGHandle shadowMap,
                                   bool transmissionStage, std::optional<std::size_t> blended,
                                   EnhancedForwardLighting forward) const;
};
