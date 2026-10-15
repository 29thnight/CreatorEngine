// Cooked hierarchy -> production GPU lists -> HW/SW visibility -> material and
// Temporal recovery. Expected pixels use authored triangle topology, not shader code.
void RunClusterLod(RecordingChangeDevice& device, ProbeRoots& roots, ProbePipelines& pipelines,
    ProbeTextures& textures, ProbePool& pool, const own::shared_owner<const Instance>& sourceMaterial)
{
    const auto material=SceneCooked(sourceMaterial);
    std::string error;
    auto geometry=MakeGeometry(false,false);
    auto& mesh=geometry.draw.modelMeshView;
    const auto stride=mesh.vertexStride;
    const auto prototype=geometry.vertices;
    constexpr uint32_t side=17;
    geometry.vertices.resize(side*side*stride);
    std::vector<math::vector3> positions;
    for(uint32_t y=0;y<side;++y) for(uint32_t x=0;x<side;++x) {
        const auto vertex=y*side+x;
        std::memcpy(geometry.vertices.data()+vertex*stride,prototype.data(),stride);
        const math::vector3 p{-.6f+x*.075f,-.6f+y*.075f,.04f*std::sin(float(x)) *std::cos(float(y))};
        positions.push_back(p);
        std::memcpy(geometry.vertices.data()+vertex*stride+assets::OffsetOf(mesh.vertexAttributeMask,assets::VertexAttribute::Position),&p,12);
    }
    std::vector<uint32_t> indices;
    for(uint32_t y=0;y+1<side;++y)for(uint32_t x=0;x+1<side;++x) {
        const uint32_t a=y*side+x,b=a+1,c=a+side,d=c+1;
        indices.insert(indices.end(),{a,c,b,b,c,d});
    }
    mesh.vertexData=geometry.vertices.data();mesh.vertexBytes=geometry.vertices.size();
    mesh.indexData=indices.data();mesh.indexCount=uint32_t(indices.size());mesh.handle.generation=91001;
    experiment::MeshLodBuildSettings settings;settings.levelCount=3;settings.targetRelativeError=.5f;
    RHIModelMeshView cooked;
    Check(cookedMeshes.Cook(mesh,cooked,error,{},&settings),"Cluster fixture cooking "+error);
    const auto* source=cooked.SourceMesh();Check(source&&source->coarseLods.levels.size()>=2,"Fixture has multiple actual simplified levels");
    const auto& chain=source->coarseLods;
    Check(chain.groups.size()>chain.levels.size()+1,"Cooker emits spatially independent replacement groups");
    // Test semantic hierarchy validation even when the attacker recomputes its digest.
    experiment::Mesh authored;Check(authored.vertices.AssignPacked(mesh.vertexAttributeMask,positions.size(),geometry.vertices),"Hierarchy authored vertices");
    auto minimum=positions.front(),maximum=positions.front();
    for(const auto& p:positions){minimum.x=std::min(minimum.x,p.x);minimum.y=std::min(minimum.y,p.y);minimum.z=std::min(minimum.z,p.z);maximum.x=std::max(maximum.x,p.x);maximum.y=std::max(maximum.y,p.y);maximum.z=std::max(maximum.z,p.z);}
    authored.bounds=math::aabb::from_min_max(minimum,maximum);
    authored.indices=indices;Check(experiment::importer::BuildMeshlets(authored,authored.meshlets,error),"Hierarchy base cooking");
    authored.coarseLods=chain;
    // Coincident seam wedges have distinct indices, but one geometric boundary.
    {
        auto seam=authored;seam.vertices={};auto bytes=geometry.vertices;
        for(size_t t=0;t<seam.indices.size();t+=3){const auto original=seam.indices[t];const auto offset=bytes.size();
            bytes.insert(bytes.end(),geometry.vertices.begin()+original*stride,geometry.vertices.begin()+(original+1)*stride);
            seam.indices[t]=uint32_t(offset/stride);}
        Check(seam.vertices.AssignPacked(mesh.vertexAttributeMask,bytes.size()/stride,bytes),"Seam packed vertices");
        Check(experiment::importer::BuildMeshlets(seam,seam.meshlets,error),"Seam base meshlets");
        Check(experiment::importer::BuildMeshLods(seam,seam.coarseLods,error,settings),"Coincident seam DAG cooking "+error);
        Check(!seam.coarseLods.IsEmpty()&&experiment::importer::ValidateMeshLods(seam,seam.coarseLods,error),"Seam DAG preserves geometric boundaries");
    }
    for(unsigned mutation=0;mutation<8;++mutation) {
        auto bad=chain;
        if(mutation==0)bad.groups.back().childGroup=uint32_t(bad.groups.size()-1);
        if(mutation==1)++bad.groups[1].firstMeshlet;
        if(mutation==2)bad.groups.back().geometricError=-1;
        if(mutation==3)bad.groups.front().parentGroup=UINT32_MAX;
        if(mutation==4)bad.groups.back().secondChild=bad.groups.back().childGroup;
        if(mutation==5)bad.groups.front().sphere[3]=0;
        if(mutation==6)bad.groups.back().lodIndex=0;
        if(mutation==7)bad.groups.back().primitiveBase=0;
        bad.geometryDigest=experiment::importer::ComputeMeshLodGeometryDigest(authored,bad);
        Check(!experiment::importer::ValidateMeshLods(authored,bad,error),"Reject corrupt hierarchy even with matching digest");
    }
    experiment::cooked::ModelGeometryArtifact artifact;artifact.mesh=authored;
    std::vector<std::byte> encoded;
    const bool wrote=experiment::cooked::WriteModelGeometryArtifact(artifact,encoded,error);
    Check(wrote,"Hierarchy artifact write "+error);
    experiment::cooked::ModelGeometryArtifact decoded;
    Check(experiment::cooked::ReadModelGeometryArtifact(encoded,decoded,error)&&decoded.mesh.coarseLods.groups==chain.groups,"Hierarchy artifact roundtrip "+error);
    auto old=encoded;const uint32_t oldVersion=3;std::memcpy(old.data()+4,&oldVersion,4);
    Check(!experiment::cooked::ReadModelGeometryArtifact(old,decoded,error),"Old geometry schema is rejected");
    // Exact serialized DAG/header costs are per chunk, never per cluster.
    {
        auto draw=geometry.draw;draw.modelMeshView=cooked;
        SurfaceView v{{0,0,-2,0},1,1,1};MeshSurfacePlanBudget b;b.maxChunkPoints=1u<<20;
        std::shared_ptr<const MeshSurfacePlan> plan;
        Check(MeshSurfacePlan::BuildForScene(draw,v,b,plan,error),"DAG budget fixture "+error);
        Check(plan->Chunks().size()==1,"DAG budget fixture is a single chunk");
        uint64_t topology{};for(const auto& d:plan->Source()->Meshlets().descriptors)
            topology+=sizeof(experiment::MeshletDescriptor)+d.vertexCount*8ull+d.triangleCount*7ull;
        const uint64_t expected=topology+sizeof(RHIMeshletBufferHeader)+plan->Source()->Hierarchy().GpuBytes()+sizeof(PackedBoneMatrix)+
            plan->Cost().points*(stride+sizeof(float)+sizeof(SurfacePoint)+sizeof(IblBakePoint)+sizeof(IblBakeSample))+plan->Cost().triangles*12ull;
        Check(plan->Cost().gpuPayloadBytes==expected,"DAG payload charged once per chunk");
        b.maxGpuPayloadBytes=expected;
        Check(MeshSurfacePlan::BuildForScene(draw,v,b,plan,error),"Exact DAG GPU budget accepted");
        --b.maxGpuPayloadBytes;
        Check(!MeshSurfacePlan::BuildForScene(draw,v,b,plan,error),"One byte below DAG GPU budget rejected");
    }
    SceneHost host;
    EnhancedFrameContext context;context.resources=&device;context.rootSignatures=&roots;context.psoManager=&pipelines;
    context.textureCache=&textures;context.width=context.height=32;context.sceneEpoch=1001;
    FrameCameraSnapshot camera;camera.view=camera.projection=math::matrix4x4::identity();camera.projection.m[2][2]=.05f;
    camera.eyePosition={0,0,-2};context.camera=&camera;
    WaitSceneProgram(host,context,material->generation);
    unsigned frames{},pixelChecks{},transitionChecks{},mixedFrames{};
    std::vector<float> thresholds;for(const auto& g:chain.groups)if(g.lodIndex&&g.geometricError>0)thresholds.push_back(g.geometricError);
    std::sort(thresholds.begin(),thresholds.end());
    Check(thresholds.size()>2 && thresholds.front()<thresholds.back(),"Fixture has different spatial errors");
    // Pick between actual group error values, away from floating projection margins.
    float splitError{};for(size_t i=1;i<thresholds.size();++i)if(thresholds[i]>thresholds[i-1]*1.05f){splitError=(thresholds[i]+thresholds[i-1])*.5f;break;}
    Check(splitError>0,"Fixture admits a mixed spatial cut");
    for(uint32_t chunkSize:{64u,1u<<20}) {
        own::shared_owner<const SceneViewInput> previous;
        math::matrix4x4 oldWorld=math::matrix4x4::identity(),oldProjection=camera.projection;
        for(unsigned step=0;step<8;++step) {
            Drain drain{device};context.frameId=28000+frames;
            camera.projection=math::matrix4x4::identity();camera.projection.m[2][2]=.05f;
            if(step>=6){camera.projection.m[0][0]=camera.projection.m[1][1]=5;camera.projection.m[2][2]=1.001f;camera.projection.m[2][3]=1;camera.projection.m[3][2]=-.1001f;camera.projection.m[3][3]=0;}
            auto draw=geometry.draw;draw.modelMeshView=cooked;draw.materialGraphInstance=material;draw.materialGraphSlot=1;draw.geometryKey=998;
            draw.coverage.flags=EnhancedMaterialCoverage::Enabled|EnhancedMaterialCoverage::DoubleSided;
            draw.temporalObjectId=998;draw.temporalIncarnation=1;draw.temporalInstanceId=1;
            draw.worldMatrix=math::matrix4x4::identity();draw.worldMatrix.m[0][0]=step==3?-.8f:1.f;
            draw.worldMatrix.m[1][1]=step==3?1.2f:1.f;
            if(step==5)for(unsigned i=0;i<3;++i)draw.worldMatrix.m[i][i]=.5f;draw.worldMatrix.m[3][0]=step*.015f;draw.worldMatrix.m[3][2]=5.f;
            SceneInputView view{context.frameId,context.sceneEpoch,998+chunkSize,1,32,32,camera};view.temporalFrame.reset=step==0;
            auto& tc=view.temporalFrame.camera;tc.valid=true;tc.orthographicProjection=step<6;tc.aspectRatio=1;
            tc.viewMatrix=std::bit_cast<std::array<float,16>>(camera.view);
            tc.projectionMatrix=tc.cameraViewToClip=std::bit_cast<std::array<float,16>>(camera.projection);
            const auto inverse=*math::try_inverse(camera.projection);const auto previousProjection=step?oldProjection:camera.projection;
            tc.clipToCameraView=std::bit_cast<std::array<float,16>>(inverse);
            tc.clipToPreviousClip=std::bit_cast<std::array<float,16>>(inverse*previousProjection);
            tc.previousClipToClip=std::bit_cast<std::array<float,16>>(*math::try_inverse(previousProjection)*camera.projection);
            tc.position={0,0,-2};tc.up={0,1,0};tc.right={1,0,0};tc.forward={0,0,1};
            view.temporalFrame.realFrameId=context.frameId;view.temporalFrame.historyRevision=1;
            SceneInputBudget budget;budget.mesh.maxChunkPoints=chunkSize;budget.geometryMaxPixelError=(step%2)?1e6f:0.f;
            if(step>=4)budget.geometryMaxPixelError=splitError*float(std::sqrt(512.*3.));
            own::shared_owner<const SceneViewInput> input;
            if(step==0){auto bad=budget;bad.geometryMaxPixelError=std::numeric_limits<float>::quiet_NaN();own::shared_owner<const SceneViewInput> rejected;Check(!SealScene(view,{&draw,1},bad,rejected,error),"Nonfinite LOD error budget is rejected before GPU preparation");}
            const bool sealed=SealScene(view,{&draw,1},budget,input,error,previous?&*previous:nullptr);
            Check(sealed,"Cluster scene seal "+error);
            Check(device.BeginFrame(error),"Cluster begin");textures.BeginFrame(context.frameId);
            Check(host.PrepareResidency(context,input,error),"Cluster residency "+error);
            auto graph=std::make_shared<EnhancedRenderGraph>(device,RGSchedulingMode::ExplicitVersioned,RGOrderPolicy::DependencyOrder);
            const unsigned workers=chunkSize==64?4:0;
            if(workers){pool.BeginFrame(unsigned(context.frameId));Check(graph->PrepareParallel(pool,error),"Cluster parallel "+error);}
            EnhancedShadowData shadow;shadow.enabled=true;shadow.mapResolution=32;
            for(auto& projection:shadow.lightViewProjection)projection=camera.projection;
            shadow.lightViewProjection[0]=math::matrix4x4::identity();shadow.lightViewProjection[0].m[2][2]=.05f;shadow.lightViewProjection[0].m[3][2]=-.2499f; // Guard crosses the near plane; finest group required.
            shadow.lightViewProjection[2].m[3][0]=step>=6?30:3; // Independently culled light frustum.
            const bool prepared=host.Prepare(context,input,{},{},{},shadow,{},error,0);
            Check(prepared,"Cluster prepare "+error);
            const auto table=host.FrameGeometry();
            const auto geo=host.DeclareGeometry(*graph);
            RGTextureDesc shadowDesc;shadowDesc.width=shadowDesc.height=32;shadowDesc.arraySize=3;shadowDesc.format=RHIFormat::D32Float;shadowDesc.allowDepthStencil=true;
            const auto shadowMap=graph->Write(graph->CreateTexture(shadowDesc));
            graph->AddPass("Probe.Cluster.ShadowClear",{{shadowMap,RHIResourceState::DepthWrite,RGAccessMode::Write}},[&device,shadowMap](const auto& e){for(uint32_t c=0;c<3;++c){const auto d=RHIDepthTargetDesc::DepthSlice(e.ResolveHandle(shadowMap),RHIFormat::D32Float,c);const auto t=device.CreateRenderTargets(std::span<const RHITextureHandle>{},&d);e.encoder->BindRenderTargets(t);e.encoder->ClearDepthTarget(t,1.f);}});
            const auto shadowOutput=host.DeclareShadow(*graph,geo,shadowMap);
            const auto first=host.DeclareFirstCull(*graph,geo);
            const auto occ=host.DeclareOccluder(*graph,geo,first);const auto final=host.DeclareFinalCull(*graph,geo,occ);
            SceneSurfaceTargets surface;surface.Declare(*graph,context);const auto output=host.DeclareGBuffer(*graph,final,surface.GetOutputs());
            host.DeclareHistory(*graph,output.depth);
            RGTextureDesc aoDesc;aoDesc.width=aoDesc.height=32;aoDesc.format=RHIFormat::RG16Float;aoDesc.allowRenderTarget=true;
            const auto ao=graph->Write(graph->CreateTexture(aoDesc));
            graph->AddPass("Probe.Cluster.AO",{{ao,RHIResourceState::RenderTarget,RGAccessMode::Write}},[ao,&device](const auto&e){const auto color=e.ResolveHandle(ao);const auto target=device.CreateRenderTargets({&color,1});e.encoder->BindRenderTargets(target);const float white[4]{1,1,1,1};e.encoder->ClearRenderTargets(target,white);});
            SceneColorInputs commands{host.DeclareLayerWork(*graph,final.blendedList).layer,{}};commands.material=host.MaterialWork()->GraphInputs(*graph);
            host.DeclareColor(*graph,output,commands,SceneHost::DeclareHdrTarget(*graph,context),ao,shadowOutput);
            std::array<RGHandle,5> temporal;std::vector<EnhancedRenderGraph::RGPassUsage> clear;
            const std::array formats{RHIFormat::RG16Float,RHIFormat::R16Float,RHIFormat::R16Float,RHIFormat::R16Float,RHIFormat::D32Float};
            for(unsigned i=0;i<5;++i){RGTextureDesc d;d.width=d.height=32;d.format=formats[i];d.allowRenderTarget=i<4;d.allowDepthStencil=i==4;temporal[i]=graph->Write(graph->CreateTexture(d));clear.push_back({temporal[i],i==4?RHIResourceState::DepthWrite:RHIResourceState::RenderTarget,RGAccessMode::Write});}
            graph->AddPass("Probe.Cluster.TemporalClear",clear,[temporal,&device](const auto& e){std::array<RHITextureHandle,4> colors;for(unsigned i=0;i<4;++i)colors[i]=e.ResolveHandle(temporal[i]);const auto d=RHIDepthTargetDesc::Depth(e.ResolveHandle(temporal[4]),RHIFormat::D32Float);const auto t=device.CreateRenderTargets(colors,&d);const float zero[4]{};e.encoder->BindRenderTargets(t);e.encoder->ClearRenderTargets(t,zero);e.encoder->ClearDepthTarget(t,1.f);});
            if(step==0){auto wrong=final.list;++wrong.version;bool rejected=false;try{host.DeclareTemporal(*graph,output.visibility,host.MaterialWork()->GraphInputs(*graph),wrong,temporal);}catch(const std::runtime_error&){rejected=true;}Check(rejected,"Temporal rejects a non-published LOD list version");}
            temporal=host.DeclareTemporal(*graph,output.visibility,host.MaterialWork()->GraphInputs(*graph),final.blendedList,temporal);
            std::array<RHIReadback,3> reads;const std::array handles{output.depth,temporal[0],temporal[4]};
            for(unsigned i=0;i<3;++i){Check(device.CreateReadback(32,32,i==1?RHIFormat::RG16Float:RHIFormat::D32Float,1,reads[i],error),"Cluster pixel readback");graph->AddPass("Probe.Cluster.Read",{{handles[i],RHIResourceState::CopySource,RGAccessMode::Read}},[h=handles[i],r=reads[i]](const auto&e){e.encoder->CopyToReadback(r,e.ResolveHandle(h));},true);}
            const auto work=host.CameraMeshletWork();const auto last=uint32_t(table->geometry.size()-2);
            RHIReadback ids;Check(device.CreateBufferReadback((work->MeshletOutputOffset(last)+table->geometry.back()->MeshletCount()*2+1)*4,ids,error),"Cluster list readback");
            graph->AddPass("Probe.Cluster.List",{{final.list,RHIResourceState::CopySource,RGAccessMode::Read}},[ids,work](const auto&e){e.encoder->CopyBufferToReadback(ids,work->VisibleIdBuffer());},true);
            std::array<RHIReadback,3> shadowLists;
            const auto shadowWork=host.ShadowMeshletOutputs();
            Check(!shadowWork.empty(),"Independent light LOD work exists");
            for(unsigned cascade=0;cascade<3;++cascade) {
                const auto f=std::ranges::find(shadowWork,cascade,&SceneHost::ShadowMeshletOutput::cascade)->frame;
                Check(device.CreateBufferReadback((f->MeshletOutputOffset(last)+table->geometry.back()->MeshletCount()*2+1)*4,shadowLists[cascade],error),"Shadow LOD readback");
                graph->AddPass("Probe.Cluster.ShadowList",{{f->GraphVisibleIds(*graph),RHIResourceState::CopySource,RGAccessMode::Read}},[f,r=shadowLists[cascade]](const auto&e){e.encoder->CopyBufferToReadback(r,f->VisibleIdBuffer());},true);
            }
            SubmitShadowDecal(device,pool,graph,host,context.frameId,workers,true);
            RHIReadbackImage lists;Check(device.MapReadback(ids,lists,error),"Cluster list map");
            const double scale=std::sqrt(double(draw.worldMatrix.m[0][0])*draw.worldMatrix.m[0][0]+double(draw.worldMatrix.m[1][1])*draw.worldMatrix.m[1][1]+double(draw.worldMatrix.m[2][2])*draw.worldMatrix.m[2][2]);
            const auto project=[](const math::vector3& p,const math::matrix4x4& world,const math::matrix4x4& projection) {
                std::array<double,4> w{},clip{};const double point[4]{p.x,p.y,p.z,1};
                for(unsigned c=0;c<4;++c)for(unsigned k=0;k<4;++k)w[c]+=point[k]*world.m[k][c];
                for(unsigned c=0;c<4;++c)for(unsigned k=0;k<4;++k)clip[c]+=w[k]*projection.m[k][c];return clip;
            };
            const auto cut=[&](double budget,const math::matrix4x4& projection) {
                std::set<uint32_t> selected;
                const auto visit=[&](auto&& self,uint32_t n)->void {
                    const auto& g=chain.groups[n];
                    const auto center=project({g.sphere[0],g.sphere[1],g.sphere[2]},draw.worldMatrix,projection);
                    double norms[4]{};for(unsigned c=0;c<4;++c)for(unsigned k=0;k<3;++k)norms[c]+=double(projection.m[k][c])*projection.m[k][c];
                    for(auto& v:norms)v=std::sqrt(v);
                    const double radius=g.sphere[3]*scale,lowW=center[3]-radius*norms[3];
                    double derivativeSquared{};
                    if(lowW>0 && center[2]-radius*norms[2]>0)for(unsigned c=0;c<2;++c){const double d=(norms[c]*(std::abs(center[3])+radius*norms[3])+norms[3]*(std::abs(center[c])+radius*norms[c]))/(lowW*lowW)*16;derivativeSquared+=d*d;}
                    const bool accepts=lowW>0&&center[2]-radius*norms[2]>0&&budget>0&&g.geometricError*std::sqrt(derivativeSquared)*scale<=budget;
                    if(g.childGroup==UINT32_MAX || accepts)selected.insert(n);
                    else {self(self,g.childGroup);self(self,g.secondChild);}
                };visit(visit,uint32_t(chain.groups.size()-1));return selected;
            };
            const auto expected=cut(budget.geometryMaxPixelError,camera.projection);
            std::set<uint32_t> selectedClusters,selectedGroups,selectedLevels;
            uint32_t expectedCount{};for(auto n:expected)expectedCount+=chain.groups[n].meshletCount;
            for(uint32_t bin=0;bin<table->geometry.size()-1;++bin){const auto* list=reinterpret_cast<const uint32_t*>(lists.data.data())+work->MeshletOutputOffset(bin);const auto& descriptors=table->geometry[bin+1]->Input()->Meshlets().descriptors;
                for(uint32_t i=0;i<list[0];++i){Check(list[i+1]<descriptors.size(),"GPU selected ID in range");const auto& d=descriptors[list[i+1]];
                    Check(selectedClusters.insert(d.clusterId).second,"GPU selected IDs unique across chunks");Check(expected.contains(d.reserved),"GPU selects the authored spatial cut");
                    selectedGroups.insert(d.reserved);selectedLevels.insert(chain.groups[d.reserved].lodIndex);}}
            Check(selectedClusters.size()==expectedCount&&selectedGroups==expected,"Complete nonoverlapping spatial cut across chunks");
            if(selectedLevels.size()>1)++mixedFrames;
            for(unsigned cascade=0;cascade<3;++cascade) {
                RHIReadbackImage image;Check(device.MapReadback(shadowLists[cascade],image,error),"Shadow LOD list map");
                const auto expectedLight=cut(cascade==0?0:budget.geometryMaxPixelError,shadow.lightViewProjection[cascade]);
                uint32_t count{},expectedLightCount{};for(auto n:expectedLight)expectedLightCount+=chain.groups[n].meshletCount;
                for(const auto& item:shadowWork)if(item.cascade==cascade){const auto* list=reinterpret_cast<const uint32_t*>(image.data.data())+item.frame->MeshletOutputOffset(item.bin);const auto& descriptors=item.frame->MeshletGeometry(item.bin)->Input()->Meshlets().descriptors;
                    for(uint32_t i=0;i<list[0];++i){Check(list[i+1]<descriptors.size()&&expectedLight.contains(descriptors[list[i+1]].reserved),"Light selects independently from camera");++count;}}
                Check(count==(cascade==2?0u:expectedLightCount),"Per-light spatial cut/near-plane/frustum oracle");
                device.ReleaseReadback(shadowLists[cascade]);
            }
            std::array<RHIReadbackImage,3> images;for(unsigned i=0;i<3;++i)Check(device.MapReadback(reads[i],images[i],error),"Cluster pixels map");
            std::vector<uint32_t> topology;
            for(auto n:expected) {
                const auto& g=chain.groups[n];const auto& payload=g.lodIndex?chain.levels[g.lodIndex-1].meshlets:authored.meshlets;
                const auto& sourceIndices=g.lodIndex?chain.levels[g.lodIndex-1].indices:indices;
                for(uint32_t m=0;m<g.meshletCount;++m){const auto& d=payload.descriptors[g.firstMeshlet-g.levelFirstMeshlet+m];
                    for(uint32_t t=0;t<d.triangleCount;++t){const auto p=payload.primitiveRemap[d.primitiveOffset+t];topology.insert(topology.end(),sourceIndices.begin()+p*3,sourceIndices.begin()+p*3+3);}}
            }
            for(unsigned y=8;y<24;++y)for(unsigned x=8;x<24;++x){
                const double nx=(x+.5)/16.-1,ny=1-(y+.5)/16.;
                bool covered=false;double depth{};math::vector3 object;
                for(size_t t=0;t<topology.size();t+=3){const auto a=positions[topology[t]],b=positions[topology[t+1]],c=positions[topology[t+2]];
                    const auto ac=project(a,draw.worldMatrix,camera.projection),bc=project(b,draw.worldMatrix,camera.projection),cc=project(c,draw.worldMatrix,camera.projection);
                    const double ax=ac[0]/ac[3],ay=ac[1]/ac[3],bx=bc[0]/bc[3],by=bc[1]/bc[3],cx=cc[0]/cc[3],cy=cc[1]/cc[3];
                    const double den=(by-cy)*(ax-cx)+(cx-bx)*(ay-cy);if(std::abs(den)<1e-12)continue;
                    const double u=((by-cy)*(nx-cx)+(cx-bx)*(ny-cy))/den,v=((cy-ay)*(nx-cx)+(ax-cx)*(ny-cy))/den,w=1-u-v;
                    if(std::min({u,v,w})<.002)continue; // Avoid edge quantization/ties in the analytic pixel oracle.
                    const double inverseW=u/ac[3]+v/bc[3]+w/cc[3];
                    object={float((u*a.x/ac[3]+v*b.x/bc[3]+w*c.x/cc[3])/inverseW),float((u*a.y/ac[3]+v*b.y/bc[3]+w*c.y/cc[3])/inverseW),float((u*a.z/ac[3]+v*b.z/bc[3]+w*c.z/cc[3])/inverseW)};
                    depth=u*ac[2]/ac[3]+v*bc[2]/bc[3]+w*cc[2]/cc[3];covered=true;break;}
                // The fixed original rectangle covers this conservative interior,
                // including mixed-patch seams; missing depth is always an error.
                const double interiorX=(nx-draw.worldMatrix.m[3][0])/draw.worldMatrix.m[0][0],interiorY=ny/draw.worldMatrix.m[1][1];
                if(std::abs(interiorX)<.45&&std::abs(interiorY)<.45)Check(images[0].At(x,y,0)<.9999,"Mixed patch seam has no interior coverage hole");
                if(!covered)continue;
                Near(images[0].At(x,y,0),depth,"Spatial cut analytic depth",.00003);
                Near(images[2].At(x,y,0),images[0].At(x,y,0),"Temporal depth follows selected patch",.00003);
                const auto previousClip=project(object,oldWorld,oldProjection);
                Near(images[1].At(x,y,0),step?(previousClip[0]/previousClip[3]-nx)*16:0,"Spatial LOD/projection transition horizontal motion",.004);
                Near(images[1].At(x,y,1),step?(ny-previousClip[1]/previousClip[3])*16:0,"Spatial LOD/projection transition vertical motion",.004);
                ++pixelChecks;if(step)++transitionChecks;
            }
            for(auto r:reads)device.ReleaseReadback(r);device.ReleaseReadback(ids);
            previous=std::move(input);oldWorld=draw.worldMatrix;oldProjection=camera.projection;++frames;
        }
    }
    host.ShutdownAfterIdle();
    Check(mixedFrames>=2,"GPU selects different spatial LODs in one draw across both partitions");
    Check(pixelChecks>100&&transitionChecks>100,"Cluster topology and transition oracle coverage");
    std::cout<<"METEOR_CLUSTER_LOD_OK frames="<<frames<<" partitions=2 malformed=9 pixels="<<pixelChecks<<" transitions="<<transitionChecks<<" mixedFrames="<<mixedFrames<<" validation=0\n";
}
