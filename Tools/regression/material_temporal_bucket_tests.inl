void RunTemporalBuckets(RecordingChangeDevice& device, ProbeRoots& roots, ProbePipelines& pipelines,
    ProbeTextures& textures, ProbePool& pool, const std::filesystem::path& root,
    const own::shared_owner<const Texture>& image, bool transmitting = false)
{
    const auto product = transmitting ? RefractionProduct(root, false) : ShadowDecalProduct(root, false);
    GenerationStore store;
    experiment::AssetId id;
    Check(Uuid::TryParse("DDDDDDDD-DDDD-4DDD-8DDD-DDDDDDDDDDDD", id.value), "Temporal graph identity");
    std::string error;
    if(transmitting){
        IndirectMaterialBatch invalid;invalid.device=&device;
        IndirectMaterialBatch::Bucket bucket;bucket.roots={2,1};invalid.buckets.push_back(bucket);
        const std::array<uint32_t,1> words{};const std::array<std::array<uint32_t,4>,1> records{};
        Check(!invalid.Upload(words,records,32,error)&&error.find("strictly increasing")!=std::string::npos&&!invalid.output.IsValid(),"Invalid indirect root order is rejected before allocation/recording");
    }
    const auto generation = store.Load(id, [&](CookedProgram& cooked, std::string&) {
        cooked = {product, WriteMaterialProgramMetadata(product.program), BuildBoundSource(product.program)};
        return true;
    }, true, error);
    Check(bool(generation), "Temporal generation " + error);
    SceneHost host;
    EnhancedFrameContext context;
    context.resources = &device; context.rootSignatures = &roots; context.psoManager = &pipelines;
    context.textureCache = &textures; context.width = context.height = 16; context.sceneEpoch = 198;
    FrameCameraSnapshot camera;
    camera.view = camera.projection = math::matrix4x4::identity();
    camera.eyePosition = math::vector3(0, 0, -2); context.camera = &camera;
    WaitSceneProgram(host, context, generation);
    auto geometry = MakeGeometry(false, true);
    ShadowDecalPlane(geometry, .875f);
    unsigned frames{};
    for (unsigned workers : {0u, 1u, 4u}) {
        own::shared_owner<const SceneViewInput> previous;
        for (unsigned step = 0; step < (transmitting ? 2u : 4u); ++step) {
            Drain drain{device};
            context.frameId = 19000 + frames;
            auto draws = std::array{geometry.draw, geometry.draw};
            for (unsigned i = 0; i < 2; ++i) {
                draws[i].geometryKey += i;
                draws[i].temporalObjectId = 201 + i;
                draws[i].temporalIncarnation = 1;
                draws[i].temporalInstanceId = 1;
                draws[i].worldMatrix.m[3][0] = i ? .25f - step * .125f : step * .125f;
                draws[i].worldMatrix.m[3][2] = i ? -.2f : 0.f;
                draws[i].coverage.flags |= step == 2 && i ? EnhancedMaterialCoverage::Blended : EnhancedMaterialCoverage::Masked;
                draws[i].coverage.cutoff = .5f;
                InstanceDescription description{id, {{1200, i && step >= 2 ? .25 : 1.0}}, {}};
                if(transmitting)description.parameters.clear();
                Check(BuildInstance(generation, description,
                    [&](const experiment::AssetId&, LXColorSpace, std::string&) { return image; },
                    draws[i].materialGraphInstance, error), "Temporal instance " + error);
            }
            SceneInputView view{context.frameId, context.sceneEpoch, 199, 1, 16, 16, camera};
            view.temporalFrame.reset = step == 0 || step == 3;
            own::shared_owner<const SceneViewInput> input;
            Check(SealScene(view, draws, {}, input, error, previous ? &*previous : nullptr), "Temporal seal " + error);
            Check(device.BeginFrame(error), "Temporal begin"); textures.BeginFrame(context.frameId);
            Check(host.PrepareResidency(context, input, error), "Temporal residency " + error);
            auto graph = std::make_shared<EnhancedRenderGraph>(device, RGSchedulingMode::ExplicitVersioned);
            if (workers) { pool.BeginFrame(unsigned(context.frameId)); Check(graph->PrepareParallel(pool, error), "Temporal parallel prepare"); }
            Check(host.Prepare(context, input, {}, {}, {}, {}, {}, error, 0), "Temporal prepare " + error);
            const auto meteorGeometry = host.DeclareGeometry(*graph);
            const auto first=host.DeclareFirstCull(*graph,meteorGeometry);
            const auto occluder=host.DeclareOccluder(*graph,meteorGeometry,first);
            const auto final=host.DeclareFinalCull(*graph,meteorGeometry,occluder);
            SceneSurfaceTargets surface;surface.Declare(*graph,context);
            const auto visible=host.DeclareGBuffer(*graph,final,surface.GetOutputs());

            std::array<RGHandle, 5> targets;
            std::array<RHIReadback, 5> readbacks;
            const std::array formats{RHIFormat::RG16Float, RHIFormat::R16Float, RHIFormat::R16Float, RHIFormat::R16Float, RHIFormat::D32Float};
            std::vector<EnhancedRenderGraph::RGPassUsage> clearUses;
            for (unsigned i = 0; i < 5; ++i) {
                RGTextureDesc desc; desc.width = desc.height = 16; desc.format = formats[i];
                desc.allowRenderTarget = i < 4; desc.allowDepthStencil = i == 4; desc.name = "Probe.TemporalBucket";
                targets[i] = graph->Write(graph->CreateTexture(desc));
                clearUses.push_back({targets[i], i == 4 ? RHIResourceState::DepthWrite : RHIResourceState::RenderTarget, RGAccessMode::Write});
                Check(device.CreateReadback(16,16,formats[i],1,readbacks[i],error), "Temporal readback");
            }
            graph->AddPass("Probe.Temporal.Clear", clearUses, [targets,&device](const auto& execution) {
                std::array<RHITextureHandle,4> colors;
                for (unsigned i=0;i<4;++i) colors[i]=execution.ResolveHandle(targets[i]);
                const auto depth=RHIDepthTargetDesc::Depth(execution.ResolveHandle(targets[4]),RHIFormat::D32Float);
                const auto binding=device.CreateRenderTargets(colors,&depth);
                const float zero[4]{}; execution.encoder->BindRenderTargets(binding);
                execution.encoder->ClearRenderTargets(binding,zero); execution.encoder->ClearDepthTarget(binding,1.f);
            });
            const auto packets=host.MaterialWork();
            Check(bool(packets)!=transmitting,"Transmitting Temporal uses camera meshlets; opaque uses material packets");
            targets=host.DeclareTemporal(*graph,visible.visibility,packets?packets->GraphInputs(*graph):std::array<RGHandle,2>{},final.blendedList,targets);
            for(unsigned i=0;i<5;++i) graph->AddPass("Probe.Temporal.Read",{{targets[i],RHIResourceState::CopySource,RGAccessMode::Read}},
                [target=targets[i],readback=readbacks[i]](const auto& execution){execution.encoder->CopyToReadback(readback,execution.ResolveHandle(target));},true);
            RecordingChangeDevice::VisibilityBindingAudit visibilityBindings{device, *graph, visible.visibility};
            SubmitShadowDecal(device,pool,graph,host,context.frameId,workers,false);
            visibilityBindings.Verify();
            EnhancedRenderGraph::DiagnosticSnapshot snapshot;
            Check(graph->CaptureDiagnosticSnapshot(snapshot),"Temporal packet snapshot");
            const auto countPass=[&](const char* name){return std::ranges::count(snapshot.passes,std::string(name),&EnhancedRenderGraph::DiagnosticPass::name);};
            Check(countPass("METEOR.Scene.Temporal.Resolve")==unsigned(!transmitting),"Opaque Temporal reads final material packets");
            Check(countPass("METEOR.Scene.Commands.Temporal.Layers.Raster")==unsigned(transmitting||step==2),"Only ordered layers replay");
            Check(countPass("METEOR.Scene.Commands.Temporal.Layers.Build")==unsigned(transmitting||step==2),"No opaque replay commands are produced");
            Check(host.TemporalBucketSubmissionCount()==(step==2?2u:1u),"Opaque packets plus ordered layers use their own native submissions");
            std::array<RHIReadbackImage,5> mapped;
            for(unsigned i=0;i<5;++i) Check(device.MapReadback(readbacks[i],mapped[i],error),"Temporal map");
            for(unsigned y=0;y<16;++y) for(unsigned x=0;x<16;++x) {
                const bool first=x>=2+step && x<14+step && y>=2 && y<14;
                const bool second=step!=3 && x>=4-step && x<16-step && y>=2 && y<14;
                const bool covered=first||second, valid=step==1||step==2;
                Near(mapped[0].At(x,y,0),covered&&valid?(second?1.:-1.):0.,"Temporal previous/current vertex correspondence",1.0/1024.0);
                Near(mapped[0].At(x,y,1),0.,"Temporal vertical motion",1.0/1024.0);
                Near(mapped[1].At(x,y,0),transmitting&&covered?1.:second&&step==2?1.:0.,"Temporal reactive mask");
                Near(mapped[2].At(x,y,0),transmitting&&covered?1.:second&&step==2?.25:0.,"Temporal instance alpha");
                Near(mapped[3].At(x,y,0),covered&&!valid?1.:0.,"Temporal reset responsive mask");
                Near(mapped[4].At(x,y,0),second?.2:first?.4:1.,"Temporal final visibility/layer depth");
            }
            for(auto readback:readbacks) device.ReleaseReadback(readback);
            graph.reset(); previous=std::move(input); ++frames;
        }
    }
    host.ShutdownAfterIdle();
    if(transmitting){std::cout<<"METEOR_TEMPORAL_TRANSMISSION_OK frames="<<frames<<" instances=2 validation=0\n";return;}
    std::cout << "LX_TEMPORAL_BUCKET_OK frames=" << frames << " instances=2 opaquePackets=12 layerReplays=3 validation=0\n";
}
