// Internal evaluator inputs. Public consumers receive purpose-specific contracts.
struct SceneShadingInputs
{
    SceneLayerWork layers;
    std::array<RGHandle, 2> material;
};

// Each stage owns its prepared resources and the code that declares their graph passes.
struct SceneHost::GeometryStage
{
    std::shared_ptr<const SceneFrameGeometry> geometryTable;
    bool Prepare(std::shared_ptr<SceneFrameGeometry> table, std::string& error,
                 ResidentBufferCache* cache = nullptr, ResidentBufferCache::Key key = {})
    {
        if (!table->Upload(error, cache, key))
            return false;
        geometryTable = std::move(table);
        return true;
    }
    RGHandle DeclareGeometry(const own::shared_owner<const FrameInput>& prepared, EnhancedRenderGraph& graph) const;
};

struct SceneHost::FirstCullStage
{
    std::shared_ptr<const GpuGeometryVisibility::Frame> cameraDepthMeshlets;
    SceneFirstCullOutputs DeclareFirstCull(const own::shared_owner<const FrameInput>& prepared,
                                           EnhancedRenderGraph& graph, RGHandle geometry) const;
};

struct SceneHost::OccluderStage
{
    std::shared_ptr<const IndirectMaterialBatch> depthCommands;
    std::shared_ptr<const SceneHybridVisibility> occluderVisibility;
    bool Prepare(const FrameInput& frame, const EnhancedFrameContext& context,
                 const std::shared_ptr<const SceneFrameGeometry>& geometry,
                 const std::shared_ptr<const GpuGeometryVisibility::Frame>& list, HybridVisibilityPipelines& pipelines,
                 std::string& error);
    RGHandle DeclareOccluder(const own::shared_owner<const FrameInput>& prepared, EnhancedRenderGraph& graph,
                             RGHandle geometry, const SceneFirstCullOutputs& first) const;
};

struct SceneHost::FinalCullStage
{
    // Opaque recovery union, and the frustum-culled blended list in source meshlet order.
    std::shared_ptr<const GpuGeometryVisibility::Frame> cameraMeshlets, blendedMeshlets;
    // Built from the occluder depth; read by opaque meshlet recovery.
    std::shared_ptr<const GpuGeometryOcclusion::Pyramid> currentPyramid;
    SceneFinalCullOutputs DeclareFinalCull(const own::shared_owner<const FrameInput>& prepared,
                                           EnhancedRenderGraph& graph, RGHandle geometry, RGHandle occluderDepth,
                                           const std::shared_ptr<const SceneHybridVisibility>& occluder) const;
};

struct SceneHost::ShadowCacheStage
{
    std::shared_ptr<ShadowCache> cache;
    std::array<std::shared_ptr<const GpuGeometryVisibility::Frame>, 3> meshlets;
    std::array<std::shared_ptr<const IndirectMaterialBatch>, 3> commands;
    bool hit{};
    mutable SceneShadowResources outputs{};
    SceneShadowResources Declare(const own::shared_owner<const FrameInput>& frame, EnhancedRenderGraph& graph,
                                 RGHandle geometry) const;
};

struct SceneHost::ShadowFirstCullStage
{
    std::array<std::shared_ptr<const GpuGeometryVisibility::Frame>, 3> meshlets;
    std::optional<GpuGeometryOcclusion::ViewKey> m_cameraPredictionView;
    mutable SceneShadowResources outputs{};
    SceneShadowResources Declare(const own::shared_owner<const Frame>& frame, EnhancedRenderGraph& graph,
                                 RGHandle geometry, RGHandle cameraOccluderDepth) const;
};

struct SceneHost::ShadowOccluderStage
{
    std::array<std::shared_ptr<const IndirectMaterialBatch>, 3> commands;
    mutable SceneShadowResources outputs{};
    SceneShadowResources Declare(const own::shared_owner<const FrameInput>& frame, EnhancedRenderGraph& graph,
                                 const SceneShadowResources& cache, const SceneShadowResources& first) const;
};

struct SceneHost::ShadowFinalCullStage
{
    std::array<std::shared_ptr<const GpuGeometryVisibility::Frame>, 3> meshlets;
    std::array<std::shared_ptr<const GpuGeometryOcclusion::Pyramid>, 3> pyramids;
    mutable SceneShadowResources outputs{};
    SceneShadowResources Declare(const own::shared_owner<const FrameInput>& frame, EnhancedRenderGraph& graph,
                                 const SceneShadowResources& depths) const;
};

struct SceneHost::ShadowRasterStage
{
    std::array<std::shared_ptr<const IndirectMaterialBatch>, 3> shadowCommands;
    std::shared_ptr<const LX::Runtime::GraphicsGeneration> merge;
    mutable RGHandle output;
    static RGHandle Draw(const own::shared_owner<const FrameInput>& frame, EnhancedRenderGraph& graph,
                         const std::shared_ptr<const IndirectMaterialBatch>& commands, RGHandle depth, unsigned cascade,
                         unsigned slice, bool staticCasters, bool count, const std::string& name);
    RGHandle Declare(const own::shared_owner<const FrameInput>& frame, EnhancedRenderGraph& graph,
                     const SceneShadowResources& cache, const SceneShadowResources& lists, RGHandle shadowMap) const;
};

struct SceneHost::ShadowHistoryStage
{
    std::array<std::shared_ptr<const GpuGeometryOcclusion::Pyramid>, 3> next;
    mutable RGHandle declaredInput;
    void Declare(const own::shared_owner<const FrameInput>& frame, EnhancedRenderGraph& graph, RGHandle expected,
                 RGHandle shadowMap) const;
};

struct SceneHost::TemporalStage
{
    std::shared_ptr<const IndirectMaterialBatch> temporalCommands;
    std::array<RGHandle, 5> DeclareTemporal(const own::shared_owner<const FrameInput>& prepared,
                                            EnhancedRenderGraph& graph, RGHandle expectedVisibility,
                                            RGHandle visibility, const std::array<RGHandle, 2>& material,
                                            RGHandle layerList, std::array<RGHandle, 5> targets) const;
};

struct SceneHost::VisibilityStage
{
    std::shared_ptr<const IndirectMaterialBatch> materialCommands;
    std::shared_ptr<const SceneHybridVisibility> hybridVisibility;
    static bool PrepareVisibility(const FrameInput& frame, const EnhancedFrameContext& context,
                                  const std::shared_ptr<const SceneFrameGeometry>& geometry,
                                  HybridVisibilityPipelines& pipelines, const char* name, bool depthOnly,
                                  const std::shared_ptr<const GpuGeometryVisibility::Frame>& list,
                                  std::shared_ptr<const SceneHybridVisibility>& result, std::string& error,
                                  const SceneHybridVisibility* sharedPreparation = nullptr);
    bool Prepare(const FrameInput& frame, const EnhancedFrameContext& context,
                 const std::shared_ptr<const SceneFrameGeometry>& geometry,
                 const std::shared_ptr<const GpuGeometryVisibility::Frame>& list, HybridVisibilityPipelines& pipelines,
                 std::string& error, const SceneHybridVisibility* sharedPreparation = nullptr);
    mutable SceneSurfaceTargets::Outputs targets;
    SceneSurfaceTargets::Outputs Declare(const own::shared_owner<const FrameInput>& prepared,
                                         EnhancedRenderGraph& graph, const SceneFinalCullOutputs& cull,
                                         const SceneSurfaceTargets::Outputs& incoming) const;
};

struct SceneHost::MaterialWorkStage
{
    std::shared_ptr<const SceneMaterialWork> materialWork;
    std::array<RGHandle, 2> Declare(const own::shared_owner<const FrameInput>& prepared, EnhancedRenderGraph& graph,
                                    RGHandle expectedVisibility, RGHandle visibility) const;
};

struct SceneHost::SurfaceResolveStage
{
    mutable SceneSurfaceTargets::Outputs gbuffer;
    SceneSurfaceTargets::Outputs Declare(const own::shared_owner<const FrameInput>& prepared,
                                         EnhancedRenderGraph& graph, RGHandle expectedVisibility,
                                         const SceneSurfaceTargets::Outputs& incoming,
                                         const std::array<RGHandle, 2>& material) const;
};

struct SceneHost::HistoryStage
{
    std::shared_ptr<const GpuGeometryOcclusion::Pyramid> nextCameraHistory;
    void DeclareHistory(const own::shared_owner<const FrameInput>& prepared, EnhancedRenderGraph& graph,
                        RGHandle expectedDepth, RGHandle depth) const;
};

struct SceneHost::VolumeStage
{
    std::shared_ptr<const SceneVolumeFrame> frame;
    std::vector<SceneVolumeBinding> bindings;
    RHIBufferSlice constants, buffer;
    RHIBindingTable table;
    mutable RGHandle output;
    RGHandle Declare(const own::shared_owner<const FrameInput>& prepared, EnhancedRenderGraph& graph, RGHandle shaded,
                     RGHandle lighting, RGHandle depth, RGHandle shadowMap) const;
    template<class DrawType>
    void BindForward(RHIEncoder& encoder, const DrawType& draw, const EnhancedForwardLighting& forward) const;
};

struct SceneHost::LayerStage
{
    std::shared_ptr<const IndirectMaterialBatch> indexedCommands;
    mutable SceneSurfaceTargets::Outputs forwardGbuffer;
    mutable std::map<std::size_t, RGHandle> blendedOutputs;
    mutable std::mutex forwardTargetMutex;
    mutable RHIRenderTargetBinding forwardTargets;
    struct BlendedRange
    {
        std::vector<size_t> inputs;
        std::array<std::shared_ptr<const IndirectMaterialBatch>, 2> commands;
    };
    std::vector<BlendedRange> blendedRanges;
    bool PrepareBlended(const FrameInput& frame, const std::shared_ptr<const SceneLookupFrame>& alphaLookup,
                        bool hasRuntimeEffects, const std::shared_ptr<const LX::Runtime::ComputeGeneration>& generation,
                        std::string& error);
    void DeclareForwardGBuffer(const own::shared_owner<const FrameInput>& prepared, EnhancedRenderGraph& graph,
                               const SceneSurfaceTargets::Outputs& opaque, const SceneShadingInputs& commands,
                               std::size_t index) const;
    SceneLayerWork Declare(const own::shared_owner<const FrameInput>& frame, EnhancedRenderGraph& graph,
                           RGHandle list) const;
};

struct SceneHost::EffectStage
{
    LayerStage layers;
    struct LookupState
    {
        std::shared_ptr<const SceneLookupFrame> opaque, layered;
    } lookup;

    std::shared_ptr<const SceneSubsurfaceFrame> subsurface;
    std::shared_ptr<const SceneRefractionFrame> refraction;

    mutable RGHandle colorOutput;
    VolumeStage volume;
    struct DecalState
    {
        std::array<RHIBufferSlice, 2> constants;
        void AddUses(const SceneSurfaceTargets::Outputs& inputs, std::vector<EnhancedRenderGraph::RGPassUsage>& uses,
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
        std::shared_ptr<const SceneRuntimeEffectsFrame> frame;
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

    RGHandle DeclareColor(const own::shared_owner<const FrameInput>& prepared, EnhancedRenderGraph& graph,
                          const SceneSurfaceTargets::Outputs& expected, const SceneSurfaceTargets::Outputs& inputs,
                          const SceneColorInputs& inputWork, RGHandle lighting, RGHandle ambientOcclusion,
                          RGHandle shadowMap) const;
    std::vector<ForwardDraw> BlendedDraws(const own::shared_owner<const FrameInput>& prepared) const;
    RGHandle DeclareBlended(const own::shared_owner<const FrameInput>& prepared, EnhancedRenderGraph& graph,
                            const SceneSurfaceTargets::Outputs& opaque, const std::vector<std::size_t>& indices,
                            const SceneLayerWork& commands, RGHandle lighting, RGHandle shadowMap,
                            const EnhancedForwardLighting& forward) const;
    RGHandle DeclareBlendedStream(const own::shared_owner<const FrameInput>& prepared, EnhancedRenderGraph& graph,
                                  const SceneSurfaceTargets::Outputs& opaque, const SceneLayerWork& commands,
                                  RGHandle lighting, RGHandle shadowMap, const EnhancedForwardLighting& tiles) const;

    void DeclareRefractionCapture(const own::shared_owner<const FrameInput>& prepared, EnhancedRenderGraph& graph,
                                  const SceneSurfaceTargets::Outputs& inputs, const SceneShadingInputs& commands,
                                  std::optional<std::size_t> index) const;
    RGHandle DeclareShading(const own::shared_owner<const FrameInput>& prepared, EnhancedRenderGraph& graph,
                            const SceneSurfaceTargets::Outputs& inputs, const SceneShadingInputs& commands,
                            RGHandle lighting, RGHandle ambientOcclusion, RGHandle shadowMap, bool transmissionStage,
                            std::optional<std::size_t> blended = {}, EnhancedForwardLighting forward = {},
                            std::vector<std::size_t> blendedGroup = {}) const;
    RGHandle DeclareRuntimeEffects(const own::shared_owner<const FrameInput>& prepared, EnhancedRenderGraph& graph,
                                   const SceneSurfaceTargets::Outputs& inputs, const SceneShadingInputs& commands,
                                   RGHandle lighting, RGHandle ambientOcclusion, RGHandle shadowMap,
                                   bool transmissionStage, std::optional<std::size_t> blended,
                                   EnhancedForwardLighting forward) const;
};
