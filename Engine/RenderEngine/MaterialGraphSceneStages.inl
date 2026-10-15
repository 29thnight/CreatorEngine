    // Each stage owns its prepared resources and the code that declares their graph passes.
    struct SceneHost::GeometryStage
    {
        std::shared_ptr<const SceneFrameGeometry> geometryTable;
        bool Prepare(std::shared_ptr<SceneFrameGeometry> table, std::string& error)
        {
            if (!table->Upload(error)) return false;
            geometryTable = std::move(table);
            return true;
        }
        RGHandle DeclareGeometry(const own::shared_owner<const Frame>& prepared, EnhancedRenderGraph& graph) const;
    };

    struct SceneHost::FirstCullStage
    {
        std::shared_ptr<const GpuGeometryVisibility::Frame> cameraDepthMeshlets;
        std::shared_ptr<const IndirectMaterialBatch> depthCommands;
        SceneFirstCullOutputs DeclareFirstCull(const own::shared_owner<const Frame>& prepared, EnhancedRenderGraph& graph, RGHandle geometry) const;
    };

    struct SceneHost::OccluderStage
    {
        std::shared_ptr<const SceneHybridVisibility> occluderVisibility;
        bool Prepare(const Frame& frame, const EnhancedFrameContext& context,
                     HybridVisibilityPipelines& pipelines, std::string& error);
        RGHandle DeclareOccluder(const own::shared_owner<const Frame>& prepared, EnhancedRenderGraph& graph, RGHandle geometry, const SceneFirstCullOutputs& first) const;
    };

    struct SceneHost::FinalCullStage
    {
        // Opaque recovery union, and the frustum-culled blended list in source meshlet order.
        std::shared_ptr<const GpuGeometryVisibility::Frame> cameraMeshlets, blendedMeshlets;
        // Surface: opaque mesh dispatch buckets. Layer: blended mesh dispatch per input range.
        std::shared_ptr<const IndirectMaterialBatch> materialCommands, indexedCommands;
        // Built from the occluder depth; read by opaque meshlet recovery.
        std::shared_ptr<const GpuGeometryOcclusion::Pyramid> currentPyramid;
        SceneFinalCullOutputs DeclareFinalCull(const own::shared_owner<const Frame>& prepared, EnhancedRenderGraph& graph, RGHandle geometry, RGHandle occluderDepth) const;
    };

    struct SceneHost::ShadowStage
    {
        std::array<std::shared_ptr<const IndirectMaterialBatch>, 3> shadowCommands;
        RGHandle DeclareShadow(const own::shared_owner<const Frame>& prepared, EnhancedRenderGraph& graph, RGHandle geometry, RGHandle shadowMap) const;
    };

    struct SceneHost::TemporalStage
    {
        std::shared_ptr<const IndirectMaterialBatch> temporalCommands;
        std::array<RGHandle, 5> DeclareTemporal(const own::shared_owner<const Frame>& prepared, EnhancedRenderGraph& graph, RGHandle geometry,
        std::array<RGHandle, 5> targets) const;
    };

    struct SceneHost::SurfaceStage
    {
        std::shared_ptr<const SceneHybridVisibility> hybridVisibility;
        static bool PrepareVisibility(const Frame& frame, const EnhancedFrameContext& context,
            HybridVisibilityPipelines& pipelines, const char* name, bool depthOnly,
            const std::shared_ptr<const GpuGeometryVisibility::Frame>& list,
            std::shared_ptr<const SceneHybridVisibility>& result, std::string& error);
        bool Prepare(const Frame& frame, const EnhancedFrameContext& context,
                     HybridVisibilityPipelines& pipelines, std::string& error);
        std::shared_ptr<const SceneMaterialWork> materialWork;
        mutable SceneSurfaceTargets::Outputs gbuffer;
        SceneSurfaceTargets::Outputs DeclareGBuffer(const own::shared_owner<const Frame>& prepared, EnhancedRenderGraph& graph, const SceneFinalCullOutputs& cull,
                                                           const SceneSurfaceTargets::Outputs& incoming) const;
    };

    struct SceneHost::HistoryStage
    {
        std::shared_ptr<const GpuGeometryOcclusion::Pyramid> nextCameraHistory;
        void DeclareHistory(const own::shared_owner<const Frame>& prepared, EnhancedRenderGraph& graph, RGHandle depth) const;
    };

    struct SceneHost::EffectStage
    {
        std::shared_ptr<const SceneLookupFrame> lookup, alphaLookup;
        std::shared_ptr<const SceneRuntimeEffectsFrame> runtimeEffects;
        std::shared_ptr<const SceneSubsurfaceFrame> subsurface;
        std::shared_ptr<const SceneRefractionFrame> refraction;
        std::shared_ptr<const SceneVolumeFrame> volume;
        std::vector<SceneVolumeBinding> volumeBindings;
        RHIBufferSlice emptyVolumeConstants, emptyVolumeBuffer;
        RHIBindingTable emptyVolumeBindings, emptyRuntimeInputs, emptyRuntimeBackground;
        mutable SceneSurfaceTargets::Outputs forwardGbuffer;
        mutable RGHandle colorOutput, volumeOutput;
        mutable std::map<std::size_t, RGHandle> blendedOutputs;
        mutable std::array<RGHandle, 3> decalBaseline;
        std::array<RHIBufferSlice, 2> decalConstants;
        struct RuntimeBindings { RHIRenderTargetBinding color; RHIBindingTable common, decal, volume; };
        mutable std::mutex runtimeBindingMutex;
        mutable std::map<std::array<std::uint64_t, 7>, RuntimeBindings> runtimeBindings;
        mutable std::mutex forwardTargetMutex;
        mutable RHIRenderTargetBinding forwardTargets;
        struct BlendedRange { std::vector<size_t> inputs; std::array<std::shared_ptr<const IndirectMaterialBatch>, 2> commands; };
        std::vector<BlendedRange> blendedRanges;
        RHIBufferSlice emptyMediumConstants, emptyMediumTriangles;
        bool PrepareBlended(Frame& frame, const std::shared_ptr<const LX::Runtime::ComputeGeneration>& generation, std::string& error);
        mutable SceneSurfaceTargets::Outputs decalInputs;
        const SceneSurfaceTargets::Outputs& SurfaceInputs(const Frame& frame) const;
        RuntimeBindings RuntimeTables(const Frame& frame, const EnhancedRenderGraph::ExecuteContext& execution,
                                      const SceneSurfaceTargets::Outputs& inputs, RGHandle lighting,
                                      RGHandle ambientOcclusion, RGHandle shadowMap, bool blended,
                                      bool transmissionStage) const;
        void AddDecalUses(const Frame& frame, std::vector<EnhancedRenderGraph::RGPassUsage>& uses, bool transmissionStage) const;
        RHIBindingTable DecalTable(const Frame& frame, const EnhancedRenderGraph::ExecuteContext& execution, bool transmissionStage) const;
        template<class DrawType>
        void BindDecal(RHIEncoder& encoder, const DrawType& draw, RHIBindingTable table, bool transmissionStage) const;
        template<class DrawType>
        void BindForward(RHIEncoder& encoder, const DrawType& draw, const EnhancedForwardLighting& forward) const;
        void DeclareDecalInputs(const own::shared_owner<const Frame>& prepared, EnhancedRenderGraph& graph, const SceneSurfaceTargets::Outputs& inputs,
                                       const std::array<RGHandle, 3>& baseline) const;
        RGHandle DeclareColor(const own::shared_owner<const Frame>& prepared, EnhancedRenderGraph& graph, const SceneSurfaceTargets::Outputs& inputs, const SceneCommandInputs& commands,
                                     RGHandle lighting, RGHandle ambientOcclusion, RGHandle shadowMap) const;
        std::vector<ForwardDraw> BlendedDraws(const own::shared_owner<const Frame>& prepared) const;
        RGHandle DeclareBlended(const own::shared_owner<const Frame>& prepared, EnhancedRenderGraph& graph, const std::vector<std::size_t>& indices, const SceneCommandInputs& commands, RGHandle lighting,
                                       RGHandle shadowMap, const EnhancedForwardLighting& forward) const;
        RGHandle DeclareBlendedStream(const own::shared_owner<const Frame>& prepared, EnhancedRenderGraph& graph, const SceneCommandInputs& commands, RGHandle lighting, RGHandle shadowMap,
                                             const EnhancedForwardLighting& tiles) const;
        RGHandle DeclareVolume(const own::shared_owner<const Frame>& prepared, EnhancedRenderGraph& graph, RGHandle lighting, RGHandle depth,
                                      RGHandle shadowMap) const;
        void DeclareForwardGBuffer(const own::shared_owner<const Frame>& prepared, EnhancedRenderGraph& graph, const SceneSurfaceTargets::Outputs& opaque, const SceneCommandInputs& commands,
                                          std::size_t index) const;
        void DeclareRefractionCapture(const own::shared_owner<const Frame>& prepared, EnhancedRenderGraph& graph, const SceneSurfaceTargets::Outputs& inputs, const SceneCommandInputs& commands,
                                             std::optional<std::size_t> index) const;
        RGHandle DeclareShading(const own::shared_owner<const Frame>& prepared, EnhancedRenderGraph& graph, const SceneSurfaceTargets::Outputs& inputs, const SceneCommandInputs& commands,
                                       RGHandle lighting, RGHandle ambientOcclusion, RGHandle shadowMap,
                                       bool transmissionStage, std::optional<std::size_t> blended = {},
                                       EnhancedForwardLighting forward = {}, std::vector<std::size_t> blendedGroup = {}) const;
        RGHandle DeclareRuntimeEffects(const own::shared_owner<const Frame>& prepared, EnhancedRenderGraph& graph, const SceneSurfaceTargets::Outputs& inputs, const SceneCommandInputs& commands,
                                              RGHandle lighting, RGHandle ambientOcclusion, RGHandle shadowMap,
                                              bool transmissionStage, std::optional<std::size_t> blended,
                                              EnhancedForwardLighting forward) const;
    };

