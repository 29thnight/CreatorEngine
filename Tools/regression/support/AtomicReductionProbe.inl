// C1/C5: shuffled multi-producer DAGs plus actual contended unsigned 64-bit min.
void RunAtomicReduction(RecordingChangeDevice& device, ProbeRoots& roots, ProbePipelines& pipelines,
    const std::filesystem::path& root, ProbePool& pool)
{
    std::string error;
    Check(device.GetAtomicCapabilities().bufferInt64Min, "Atomic64 capability is mandatory; no fallback");
    const auto noop=[](const EnhancedRenderGraph::ExecuteContext&) {};
    std::array<unsigned,4> order{0,1,2,3};
    unsigned permutations=0;
    do {
        EnhancedRenderGraph graph(device,RGSchedulingMode::ExplicitVersioned);
        RGBufferDesc d; d.bytes=17*8; d.allowUnorderedAccess=true; d.name="atomic.fixture";
        auto init=graph.Write(graph.CreateBuffer(d)); auto reduced=graph.AtomicMin64(init);
        std::array<RGPassId,4> ids;
        for (auto p : order) {
            const auto h=p==0 ? init : reduced;
            const auto access=p==0 ? RGAccessMode::Write : p==3 ? RGAccessMode::Read : RGAccessMode::AtomicMin64;
            ids[p]=graph.AddPass(std::to_string(p),{{h,p==3 ? RHIResourceState::CopySource : RHIResourceState::UnorderedAccess,access}},noop,p==3);
        }
        Check(graph.Compile(error), "Atomic shuffled DAG " + error);
        EnhancedRenderGraph::DiagnosticSnapshot snap; graph.CaptureDiagnosticSnapshot(snap);
        const auto pos=[&](unsigned p){return snap.passes[ids[p].index].compiledIndex;};
        Check(pos(0)<pos(1) && pos(0)<pos(2) && pos(1)<pos(3) && pos(2)<pos(3),"All producers bracketed by initialization and consumer");
        for(const auto& e:snap.versionEdges)
            Check(!((e.producer==ids[1].index && e.consumer==ids[2].index)||(e.producer==ids[2].index && e.consumer==ids[1].index)),"No sibling producer dependency");
        unsigned barriers=0;
        for(unsigned p : {1u,2u}) for(const auto& b:snap.passes[ids[p].index].barriers) if(b.uav) ++barriers;
        Check(barriers==1,"Only initialization UAV barrier; no sibling barrier");
        std::vector<EnhancedRenderGraph::QueueHint> hints;
        for(auto id:ids) hints.push_back({id,true,1000,true}); // synthetic eligibility, not a measurement
        EnhancedRenderGraph::QueueSchedule queues;
        Check(graph.BuildQueueSchedule({true,true,false,true},hints,0,queues,error) && !queues.usesCompute,
            "Reduction storage must not split across queue ownership");
        if(permutations==0) {
            device.disableAtomic64=true;const bool reused=graph.Compile(error);device.disableAtomic64=false;
            Check(!reused,"Cached structural schedule cannot bypass capability validation");
        }
        ++permutations;
    } while(std::next_permutation(order.begin(),order.end()));
    for(unsigned invalid=0;invalid<7;++invalid) {
        EnhancedRenderGraph graph(device,RGSchedulingMode::ExplicitVersioned);
        RGBufferDesc d;d.bytes=invalid==4 ? 12 : 16;d.allowUnorderedAccess=true;d.name="atomic.reject";
        auto uninitialized=graph.CreateBuffer(d);
        Check(!graph.AtomicMin64(uninitialized).IsValid(),"Reject uninitialized reduction handle");
        auto init=graph.Write(uninitialized); auto reduced=graph.AtomicMin64(init);
        graph.AddPass("init",{{init,RHIResourceState::UnorderedAccess,RGAccessMode::Write}},noop);
        if(invalid!=0) graph.AddPass("writer",{{reduced,invalid==3 ? RHIResourceState::CopyDest : RHIResourceState::UnorderedAccess,
            invalid==1 ? RGAccessMode::ReadWrite : RGAccessMode::AtomicMin64}},noop);
        if(invalid==2) graph.AddPass("mixed",{{reduced,RHIResourceState::UnorderedAccess,RGAccessMode::Write}},noop);
        if(invalid==5) { auto stale=graph.Modify(init); graph.AddPass("fork",{{stale,RHIResourceState::UnorderedAccess,RGAccessMode::ReadWrite}},noop); }
        graph.AddPass("read",{{reduced,RHIResourceState::CopySource,RGAccessMode::Read}},noop,true);
        device.disableAtomic64=invalid==6;
        const bool accepted=graph.Compile(error);
        device.disableAtomic64=false;
        Check(!accepted,"Reject malformed atomic contract " + std::to_string(invalid));
    }
    for(unsigned invalid=0;invalid<3;++invalid) {
        RHIBufferDesc d;d.bytes=invalid==1 ? 12 : 16;d.allowUnorderedAccess=invalid!=0;
        RHIBufferHandle buffer;Check(device.CreateBuffer(d,buffer,error),"Atomic invalid-import fixture");
        const auto stale=buffer;
        if(invalid==2) device.ReleaseBuffer(buffer);
        {
            EnhancedRenderGraph graph(device,RGSchedulingMode::ExplicitVersioned);
            auto reduced=graph.AtomicMin64(graph.ImportBuffer(stale,RHIResourceState::Common,"atomic.invalid-import"));
            graph.AddPass("atomic",{{reduced,RHIResourceState::UnorderedAccess,RGAccessMode::AtomicMin64}},noop,true);
            Check(!graph.Compile(error),"Reject non-UAV, misaligned or stale imported storage");
        }
        if(invalid!=2) device.ReleaseBuffer(buffer);
    }
    // Overwritten transient reductions are dead; a Modify successor retains every contributor.
    for(bool overwrite : {false,true}) {
        EnhancedRenderGraph graph(device,RGSchedulingMode::ExplicitVersioned);
        RGBufferDesc d;d.bytes=16;d.allowUnorderedAccess=true;d.name="atomic.lifetime";
        auto init=graph.Write(graph.CreateBuffer(d));auto reduced=graph.AtomicMin64(init);
        auto next=overwrite ? graph.Write(reduced) : graph.Modify(reduced);
        auto first=graph.AddPass("init",{{init,RHIResourceState::UnorderedAccess,RGAccessMode::Write}},noop);
        auto a=graph.AddPass("a",{{reduced,RHIResourceState::UnorderedAccess,RGAccessMode::AtomicMin64}},noop);
        auto b=graph.AddPass("b",{{reduced,RHIResourceState::UnorderedAccess,RGAccessMode::AtomicMin64}},noop);
        graph.AddPass("next",{{next,RHIResourceState::UnorderedAccess,overwrite ? RGAccessMode::Write : RGAccessMode::ReadWrite}},noop);
        graph.AddPass("read",{{next,RHIResourceState::CopySource,RGAccessMode::Read}},noop,true);
        Check(graph.Compile(error),"Atomic lifetime " + error);
        EnhancedRenderGraph::DiagnosticSnapshot snap;graph.CaptureDiagnosticSnapshot(snap);
        for(auto id:{first,a,b}) Check(snap.passes[id.index].culled==overwrite,"Reduction group retention matches consumed version");
    }
    const RHIPipelineLayoutParam params[]{RHILayout::Uav(0),RHILayout::Cbv(0)};
    const auto layout=roots.GetOrCreate({params,{}},error);
    Check(layout.IsValid(),"Atomic root layout " + error);
    std::array<LX::Runtime::ComputePipeline,2> kernels;
    for(unsigned i=0;i<2;++i) {
        LX::Runtime::CompiledCompute shader; RHIShaderCompileOptions options;options.bufferInt64Atomics=true;
        Check(LX::Runtime::CompileCompute((root/"Tools/regression/support/AtomicReductionProbe.slang").string(),
            i==0 ? "ResetCS" : "ReduceCS",{},options,shader,error),"Atomic SM6.6 shader " + error);
        RHIShaderCompiler::VerifiedShader spirv;
        Check(RHIShaderCompiler::VerifyFile((root/"Tools/regression/support/AtomicReductionProbe.slang").string(),
            i==0 ? "ResetCS" : "ReduceCS","cs_6_6",RHIShaderBinary::SpirV,{},spirv,error,options),
            "Atomic SPIR-V compilation (runtime deferred) " + error);
        RHIComputePipelineDesc desc;desc.layout=layout;desc.csBytecode=shader.stage.bytecode.Data();desc.csSize=shader.stage.bytecode.Size();
        Check(kernels[i].Create(pipelines,desc,std::move(shader.description),error),"Atomic PSO " + error);
    }
    for(unsigned workers : {0u,1u,4u}) for(unsigned reverse=0;reverse<2;++reverse) {
        Check(device.BeginFrame(error),"Atomic frame begin");
        RHIBufferDesc d;d.bytes=17*8;d.allowUnorderedAccess=true;RHIBufferHandle buffer;
        Check(device.CreateBuffer(d,buffer,error),"Atomic buffer");
        RHIReadback readback;Check(device.CreateBufferReadback(d.bytes,readback,error),"Atomic readback");
        auto graphOwner=std::make_shared<EnhancedRenderGraph>(device,RGSchedulingMode::ExplicitVersioned);
        auto& graph=*graphOwner;
        if(workers) {pool.BeginFrame(workers*2+reverse);Check(graph.PrepareParallel(pool,error),"Atomic parallel preparation");}
        std::array<RHIBufferSlice,2> constants;
        for(unsigned p=0;p<2;++p){uint32_t values[4]{p,0,0,0};constants[p]=device.UploadConstants(values,sizeof(values));}
        {
            auto init=graph.Write(graph.ImportBuffer(buffer,RHIResourceState::Common,"atomic.gpu"));
            auto reduced=graph.AtomicMin64(init);
            const auto dispatch=[&](const EnhancedRenderGraph::ExecuteContext& ctx,unsigned kernel,unsigned producer){
                auto& enc=*ctx.encoder;enc.SetPipeline(RHIBindPoint::Compute,kernels[kernel].GetGeneration()->GetHandle());
                RHIBufferSlice slice;slice.buffer=buffer;slice.size=d.bytes;
                Check(enc.SetRootUnorderedBuffer(RHIBindPoint::Compute,0,slice),"Atomic root UAV binding");enc.SetConstantBuffer(RHIBindPoint::Compute,1,constants[producer]);
                enc.Dispatch(kernel==0 ? 1 : 16,1,1);
            };
            graph.AddPass("consume",{{reduced,RHIResourceState::CopySource,RGAccessMode::Read}},
                [&](const auto& ctx){ctx.encoder->CopyBufferToReadback(readback,buffer);},true);
            for(unsigned n=0;n<2;++n){const auto p=n^reverse;
                graph.AddPass("reduce"+std::to_string(p),{{reduced,RHIResourceState::UnorderedAccess,RGAccessMode::AtomicMin64}},
                    [&,p](const auto& ctx){dispatch(ctx,1,p);});}
            graph.AddPass("init",{{init,RHIResourceState::UnorderedAccess,RGAccessMode::Write}},
                [&](const auto& ctx){dispatch(ctx,0,0);});
            Check(graph.Compile(error),"Atomic GPU graph " + error);
            if(workers) {
                graph.SetParallelRecordCostThreshold(0);
                RHIRecordedBatchDesc batchDesc;batchDesc.frameId=9000+workers*2+reverse;
                batchDesc.backendGeneration=GetRHISubmissionThread().GetOwnerGeneration(&device);batchDesc.lifetimeToken=graphOwner;
                RHIRecordedBatch batch;RHISubmissionTicket ticket;
                Check(graph.RecordParallel(pool,workers,batchDesc,batch,error),"Atomic worker recording " + error);
                Check(GetRHISubmissionThread().EnqueueRecordedBatch(&device,device,std::move(batch),ticket,error),"Atomic worker submit " + error);
            } else Check(graph.Execute(error),"Atomic sequential recording " + error);
            Check(device.EndFrame(error) && GetRHISubmissionThread().DrainSubmissions(&device,error),"Atomic submit");device.WaitForGpu();
            Check(GetRHISubmissionThread().Drain(&device,error),"Atomic retirement");
        }
        RHIReadbackImage image;Check(device.MapReadback(readback,image,error),"Atomic map");
        std::array<uint64_t,17> expected;expected.fill(UINT64_MAX);
        for(unsigned p=0;p<2;++p) for(unsigned i=0;i<1024;++i){
            const uint64_t value=(uint64_t(0x7ffffffdu+i%7)<<32) | uint64_t((p==(i%16)%2 ? 2048u : 4096u)-i);
            expected[i%16]=(std::min)(expected[i%16],value);
        }
        Check(image.data.size()>=sizeof(expected) && std::memcmp(image.data.data(),expected.data(),sizeof(expected))==0,
            "Exact uint64 contention, high-bit unsigned ordering, low-bit tie break and empty sentinel");
        device.ReleaseReadback(readback);device.ReleaseBuffer(buffer);
    }
    std::string validation;const auto problems=device.DrainDebugMessages(validation);Check(problems==0,"Atomic GPU validation " + validation);
    std::cout<<"METEOR_ATOMIC_REDUCTION_OK permutations="<<permutations<<" frames=6 validation=0\n";
}
