// Production SceneHost oracle: one skinned stream, independent per-instance worlds,
// two poses, normal-map TBN, world/pose motion, and warm cross-frame reuse.
void RunObjectSpaceInstances(RecordingChangeDevice& device, ProbeRoots& roots, ProbePipelines& pipelines,
                             ProbeTextures& textures, ProbePool& pool, bool cold = false,
                             std::optional<RHIShaderBinary> backend = std::nullopt)
{
    std::string error;
    LXMaterialAsset asset;
    const auto bsdf=asset.CreateNode("ShaderNodeBsdfPrincipled",0,0);
    const auto normal=asset.CreateNode("ShaderNodeNormalMap",-200,0);
    const auto out=asset.CreateNode("ShaderNodeOutputMaterial",200,0);
    asset.activeOutput=out;
    asset.graph.SetSocketValue(Pin(asset.graph,normal,"Color",Direction::Input),std::array<double,4>{.8,.3,.9,1});
    Check(asset.graph.Connect(Pin(asset.graph,normal,"Normal",Direction::Output),Pin(asset.graph,bsdf,"Normal",Direction::Input)).has_value(),"Object TBN link");
    Check(asset.graph.Connect(Pin(asset.graph,bsdf,"BSDF",Direction::Output),Pin(asset.graph,out,"Surface",Direction::Input)).has_value(),"Object surface link");
    const auto program=GenerateMaterialSlang(asset); Check(bool(program),"Object material generation");
    auto product = CookedScene(*program, "object-space-instances", backend);
    Generation value; Check(uuid::try_parse("EEEEEEEE-EEEE-4EEE-8EEE-EEEEEEEEEEEE",value.assetId.value),"Object material id");
    value.generation=1; value.cooked={std::move(product),{}, {}};
    const auto generation=own::make_shared<const Generation>(std::move(value));
    own::shared_owner<const Instance> instance;
    Check(BuildInstance(generation,{generation->assetId,{},{}},[](const auto&,LXColorSpace,std::string&){return own::shared_owner<const Texture>{};},instance,error),"Object material instance "+error);
    SceneHost host;
    EnhancedFrameContext context; context.resources=&device;context.rootSignatures=&roots;context.psoManager=&pipelines;
    context.textureCache=&textures;context.width=context.height=48;context.sceneEpoch=217;
    FrameCameraSnapshot camera; camera.view=camera.projection=math::matrix4x4::identity();camera.eyePosition={0,0,-2};context.camera=&camera;
    WaitSceneProgram(host,context,generation);
    auto geometry=MakeGeometry(false,true); ShadowDecalPlane(geometry);
    auto& mesh=geometry.draw.modelMeshView;
    const auto oldMask=mesh.vertexAttributeMask,oldStride=mesh.vertexStride;
    const auto skinMask=oldMask|assets::kSkinVertexAttributes,skinStride=assets::StrideOf(skinMask);
    std::vector<std::byte> vertices(6*skinStride);
    std::array<std::array<double,3>,6> local;
    for(unsigned v=0;v<6;++v){
        for(const auto& a:assets::kVertexAttributeTable) if(assets::Has(oldMask,a.attribute))
            std::memcpy(vertices.data()+v*skinStride+assets::OffsetOf(skinMask,a.attribute),geometry.vertices.data()+v*oldStride+assets::OffsetOf(oldMask,a.attribute),assets::SizeOf(a.format));
        std::array<float,3> p;std::memcpy(p.data(),vertices.data()+v*skinStride,12);p[0]*=.3f;p[1]*=.3f;
        local[v]={p[0],p[1],p[2]};std::memcpy(vertices.data()+v*skinStride,p.data(),12);
        const std::array<float,3> n{0,0,1};const std::array<float,4> t{1,0,0,1},weights{.25f,.75f,0,0};
        std::memcpy(vertices.data()+v*skinStride+assets::OffsetOf(skinMask,assets::VertexAttribute::Normal),n.data(),12);
        std::memcpy(vertices.data()+v*skinStride+assets::OffsetOf(skinMask,assets::VertexAttribute::Tangent),t.data(),16);
        const std::array<uint8_t,4> joints{0,1,0,0};
        std::memcpy(vertices.data()+v*skinStride+assets::OffsetOf(skinMask,assets::VertexAttribute::BoneIndices),joints.data(),4);
        std::memcpy(vertices.data()+v*skinStride+assets::OffsetOf(skinMask,assets::VertexAttribute::BoneWeights),weights.data(),16);
    }
    geometry.vertices=std::move(vertices);mesh.vertexAttributeMask=skinMask;mesh.vertexStride=skinStride;
    mesh.vertexLayoutHash=assets::VertexLayoutHash(skinMask);mesh.vertexData=geometry.vertices.data();mesh.vertexBytes=geometry.vertices.size();mesh.indexData=geometry.indices.data();mesh.handle.generation=80001;
    std::optional<ColdGeometryFixture> coldGeometry;
    if (cold)
    {
        RHIModelMeshView cooked;
        Check(cookedMeshes.Cook(mesh,cooked,error),"Cold skin fixture cooking " + error);
        experiment::cooked::ModelGeometryArtifact artifact;
        Check(artifact.mesh.vertices.AssignPacked(skinMask,6,geometry.vertices),"Cold skin exact reference");
        artifact.mesh.indices.assign(geometry.indices.begin(),geometry.indices.end()); artifact.mesh.meshlets = cooked.SourceMesh()->meshlets;
        auto minimum = math::vector3{float(local[0][0]),float(local[0][1]),float(local[0][2])}, maximum = minimum;
        for (const auto& p : local) { minimum.x=std::min(minimum.x,float(p[0]));minimum.y=std::min(minimum.y,float(p[1]));minimum.z=std::min(minimum.z,float(p[2]));
            maximum.x=std::max(maximum.x,float(p[0]));maximum.y=std::max(maximum.y,float(p[1]));maximum.z=std::max(maximum.z,float(p[2])); }
        artifact.mesh.bounds = math::aabb::from_min_max(minimum,maximum); artifact.requiredBoneCount=2;
        experiment::Skeleton skeleton; skeleton.rootBone=experiment::BoneIndex{0}; skeleton.bones.resize(2);
        skeleton.bones[0].name="Root";skeleton.bones[1].name="Child";skeleton.bones[1].parent=experiment::BoneIndex{0};
        Check(experiment::cooked::ComputeSkinBindingDigest(skeleton,artifact.requiredSkinBindingSha256,error),"Cold skin bind identity " + error);
        auto descriptor=own::make_shared<assets::ModelMeshDescriptor>(*cooked.sourceDescriptor);
        auto storage=own::make_shared<assets::ModelSkeletonStorage>();storage->skeleton=descriptor->skeleton->skeleton;
        storage->skinBindingSha256=artifact.requiredSkinBindingSha256;
        descriptor->requiredSkinBindingSha256=artifact.requiredSkinBindingSha256;
        descriptor->skeleton=own::make_shared<assets::ModelSkeletonPayload>(storage,experiment::cooked::ResolvedAssetEntry{},0);
        coldGeometry.emplace(MakeColdGeometryFixture(artifact,*descriptor));
        Check(cookedMeshes.AdoptStream(coldGeometry->descriptor,mesh,error),"Cold skin descriptor adoption " + error);
    }
    using V=std::array<double,3>;
    const auto transform=[](const V& p,const math::matrix4x4& m,double w){V r{};for(unsigned j=0;j<3;++j){r[j]=w*m.m[3][j];for(unsigned i=0;i<3;++i)r[j]+=p[i]*m.m[i][j];}return r;};
    const auto dot=[](V a,V b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];};
    const auto unit=[&](V v){const auto len=std::sqrt(dot(v,v));for(auto& c:v)c/=len;return v;};
    const auto cross=[](V a,V b){return V{a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};};
    unsigned frames{},checkedPixels{},sharedFrames{},reuseFrames{};
    for(unsigned workers:{0u,1u,4u}){
        own::shared_owner<const SceneViewInput> previous;
        std::array<std::array<V,6>,4> previousPoints;
        std::array<uint32_t,2> previousBuffers{};
        for(unsigned step=0;step<3;++step){
            Drain drain{device};context.frameId=23000+frames;
            std::array<std::array<math::matrix4x4,2>,4> bones;
            std::array<std::array<V,6>,4> points;
            std::array<V,4> normalOracle;
            auto draws=std::array{geometry.draw,geometry.draw,geometry.draw,geometry.draw};
            for(unsigned i=0;i<4;++i){
                auto bone=math::matrix4x4::identity();const float angle=step==2?.55f:.4f;
                bone.m[0][0]=1.2f;bone.m[1][1]=.8f*std::cos(angle);bone.m[1][2]=.8f*std::sin(angle);
                bone.m[2][1]=-1.1f*std::sin(angle);bone.m[2][2]=1.1f*std::cos(angle);bone.m[3][0]=i==3?.08f:0;bone.m[3][2]=.03f;
                auto secondBone=math::matrix4x4::identity();secondBone.m[0][0]=.9f;secondBone.m[1][1]=1.05f;secondBone.m[2][2]=.8f;secondBone.m[0][2]=.18f;secondBone.m[3][1]=.04f;
                bones[i]={bone,secondBone};
                math::matrix4x4 skin;
                for(unsigned row=0;row<4;++row)for(unsigned column=0;column<4;++column)skin.m[row][column]=.25f*bone.m[row][column]+.75f*secondBone.m[row][column];
                auto w=math::matrix4x4::identity();const float a=.15f*i+.08f*step,c=std::cos(a),s=std::sin(a),sx=i==1?-1.1f:1.1f,sy=.75f+.1f*i;
                w.m[0][0]=sx*c;w.m[0][1]=sx*s;w.m[1][0]=-sy*s;w.m[1][1]=sy*c;w.m[2][2]=.8f+.05f*i;
                w.m[3][0]=(i%2?.46f:-.46f)+.025f*step;w.m[3][1]=(i/2?.46f:-.46f)-.02f*step;
                if(step==2&&i==3) w.m[3][0]=3.f; // Same shared source, independently culled instance.
                draws[i].worldMatrix=w;draws[i].bonePalette=bones[i].data();draws[i].boneCount=2;draws[i].materialGraphInstance=instance;
                draws[i].geometryKey=81000+i;draws[i].temporalObjectId=81000+i;draws[i].temporalIncarnation=draws[i].temporalInstanceId=1;
                draws[i].coverage.flags=EnhancedMaterialCoverage::Enabled|EnhancedMaterialCoverage::DoubleSided;
                for(unsigned v=0;v<6;++v)points[i][v]=transform(transform(local[v],skin,1),w,1);
                const auto combined=skin*w,inv=math::inverse(combined);
                V n=unit(V{inv.m[0][2],inv.m[1][2],inv.m[2][2]});
                auto t=transform(V{1,0,0},combined,0);const auto ndt=dot(n,t);for(unsigned j=0;j<3;++j)t[j]-=n[j]*ndt;t=unit(t);
                auto b=cross(n,t);const double sign=dot(b,transform(V{0,1,0},combined,0))<0?-1:1;
                V mapped;for(unsigned j=0;j<3;++j)mapped[j]=.6*t[j]-.4*b[j]*sign+.8*n[j];normalOracle[i]=unit(mapped);
            }
            SceneInputView view{context.frameId,context.sceneEpoch,218+workers,1,48,48,camera};view.temporalFrame.reset=step==0;
            own::shared_owner<const SceneViewInput> input;Check(SealScene(view,draws,{},input,error,previous?&*previous:nullptr),"Object sealed "+error);
            if (cold) Check(input->Draws()[0].geometry->Source()->StreamedTopology() &&
                !input->Draws()[0].geometry->Source()->Geometry().vertexData && !input->Draws()[0].geometry->Source()->Geometry().indexData &&
                !coldGeometry->backing->rejectedBulk,"Skin rendering must not acquire CPU vertex/index arrays or full body");
            Check(device.BeginFrame(error),"Object begin");textures.BeginFrame(context.frameId);
            Check(host.PrepareResidency(context,input,error),"Object residency "+error);
            auto graph=own::make_shared<EnhancedRenderGraph>(device,RGSchedulingMode::ExplicitVersioned,RGOrderPolicy::DependencyOrder);
            if(workers){pool.BeginFrame(unsigned(context.frameId));Check(graph->PrepareParallel(pool,error),"Object parallel "+error);}
            const auto transformsBefore=host.GeometryStats().transforms;
            Check(host.Prepare(context,input,{},{},{},{},{},error,0),"Object prepare "+error);
            Check(host.GeometryStats().transforms-transformsBefore<=2,"Only unique poses allocate transform outputs");
            const auto table=host.FrameGeometry();Check(table&&table->geometry.size()==5,"Object frame records");
            const auto& g=table->geometry;
            Check(g[1]->Buffer().id==g[2]->Buffer().id&&g[1]->Buffer().id==g[3]->Buffer().id&&g[1]->Buffer().id!=g[4]->Buffer().id,"Three instances share exactly one pose buffer; different pose stays separate");++sharedFrames;
            if(step==1){Check(g[1]->Buffer().id==previousBuffers[0]&&g[4]->Buffer().id==previousBuffers[1],"World-only changes reuse completed object streams");++reuseFrames;}
            previousBuffers={g[1]->Buffer().id,g[4]->Buffer().id};
            const auto geo=host.DeclareGeometry(*graph);
            for(unsigned i=2;i<=3;++i){const auto a=g[1]->GraphOutput(*graph),b=g[i]->GraphOutput(*graph);Check(a.index==b.index&&a.version==b.version&&a.epoch==b.epoch&&a.kind==b.kind,"Shared instances consume the exact producer version");}
            const auto first=host.DeclareFirstCull(*graph,geo);const auto occ=host.DeclareOccluder(*graph,geo,first);const auto final=host.DeclareFinalCull(*graph,geo,occ);
            SceneSurfaceTargets surface;surface.Declare(*graph,context);const auto output=host.DeclareGBuffer(*graph,final,surface.GetOutputs());
            RGTextureDesc aoDesc;aoDesc.width=aoDesc.height=48;aoDesc.format=RHIFormat::RG16Float;aoDesc.allowRenderTarget=true;std::fill(std::begin(aoDesc.clearColor),std::end(aoDesc.clearColor),1.f);
            const auto ao=graph->Write(graph->CreateTexture(aoDesc));
            graph->AddPass("Probe.Object.AO",{{ao,RHIResourceState::RenderTarget,RGAccessMode::Write}},[ao,&device](const auto&e){const auto color=e.ResolveHandle(ao);const auto target=device.CreateRenderTargets({&color,1});e.encoder->BindRenderTargets(target);const float white[4]{1,1,1,1};e.encoder->ClearRenderTargets(target,white);});
            SceneColorInputs commands{host.DeclareLayerWork(*graph,final.blendedList).layer,{}};if(host.MaterialWork())commands.material=host.MaterialWork()->GraphInputs(*graph);
            const auto lit=host.DeclareColor(*graph,output,commands,SceneHost::DeclareHdrTarget(*graph,context),ao,{});
            std::array<RGHandle,5> temporal;const std::array formats{RHIFormat::RG16Float,RHIFormat::R16Float,RHIFormat::R16Float,RHIFormat::R16Float,RHIFormat::D32Float};
            std::vector<EnhancedRenderGraph::RGPassUsage> clear;
            for(unsigned i=0;i<5;++i){RGTextureDesc d;d.width=d.height=48;d.format=formats[i];d.allowRenderTarget=i<4;d.allowDepthStencil=i==4;temporal[i]=graph->Write(graph->CreateTexture(d));clear.push_back({temporal[i],i==4?RHIResourceState::DepthWrite:RHIResourceState::RenderTarget,RGAccessMode::Write});}
            graph->AddPass("Probe.Object.TemporalClear",clear,[temporal,&device](const auto& e){std::array<RHITextureHandle,4> colors;for(unsigned i=0;i<4;++i)colors[i]=e.ResolveHandle(temporal[i]);const auto d=RHIDepthTargetDesc::Depth(e.ResolveHandle(temporal[4]),RHIFormat::D32Float);const auto t=device.CreateRenderTargets(colors,&d);const float zero[4]{};e.encoder->BindRenderTargets(t);e.encoder->ClearRenderTargets(t,zero);e.encoder->ClearDepthTarget(t,1.f);});
            temporal=host.DeclareTemporal(*graph,output.visibility,host.MaterialWork()->GraphInputs(*graph),final.blendedList,temporal);
            std::array<RHIReadback,5> reads;const std::array handles{output.normal,output.depth,temporal[0],temporal[4],lit};const std::array readFormats{RHIFormat::RGBA16Float,RHIFormat::D32Float,RHIFormat::RG16Float,RHIFormat::D32Float,RHIFormat::RGBA16Float};
            for(unsigned i=0;i<5;++i){Check(device.CreateReadback(48,48,readFormats[i],1,reads[i],error),"Object readback");graph->AddPass("Probe.Object.Read",{{handles[i],RHIResourceState::CopySource,RGAccessMode::Read}},[h=handles[i],r=reads[i]](const auto&e){e.encoder->CopyToReadback(r,e.ResolveHandle(h));},true);}
            const auto depthWork=host.CameraDepthMeshletWork();RHIReadback ids;
            Check(device.CreateBufferReadback((depthWork->MeshletOutputOffset(3)+g[4]->MeshletCount()+1)*4,ids,error),"Object first-list readback");
            graph->AddPass("Probe.Object.FirstList",{{first.list,RHIResourceState::CopySource,RGAccessMode::Read}},[ids,depthWork](const auto&e){e.encoder->CopyBufferToReadback(ids,depthWork->VisibleIdBuffer());},true);
            SubmitShadowDecal(device,pool,graph,host,context.frameId,workers,true);
            // Native work is bounded by unique poses, independently of the draw count.
            EnhancedRenderGraph::DiagnosticSnapshot snap;Check(graph->CaptureDiagnosticSnapshot(snap),"Object snapshot");
            Check(std::ranges::count(snap.passes,std::string("METEOR.Scene.Temporal.Resolve"),&EnhancedRenderGraph::DiagnosticPass::name)==1,"Skinned Temporal uses final material packets");
            Check(std::ranges::none_of(snap.passes,[](const auto& pass){return pass.name=="METEOR.Scene.Commands.Temporal.Layers.Build"||pass.name=="METEOR.Scene.Commands.Temporal.Layers.Raster";}),"Opaque worlds produce no replay commands or layer pass");
            const auto transformPasses=std::ranges::count(snap.passes,std::string("METEOR.TransformMeshWorld"),&EnhancedRenderGraph::DiagnosticPass::name);
            Check(transformPasses<=1 && (step!=1||transformPasses==0),"Unique poses have one batched producer; world-only reuse has none");
            std::array<RHIReadbackImage,5> images;for(unsigned i=0;i<5;++i)Check(device.MapReadback(reads[i],images[i],error),"Object map");
            RHIReadbackImage idImage;Check(device.MapReadback(ids,idImage,error),"Object first-list map");
            for(unsigned i=0;i<4;++i){uint32_t count;std::memcpy(&count,idImage.data.data()+depthWork->MeshletOutputOffset(i)*4,4);Check(count==(step==2&&i==3?0u:g[i+1]->MeshletCount()),"Per-bin world determines independent frustum membership");}
            device.ReleaseReadback(ids);
            std::array<unsigned,4> covered{};
            for(unsigned y=0;y<48;++y)for(unsigned x=0;x<48;++x){
                for(unsigned channel=0;channel<4;++channel)Check(std::isfinite(images[4].At(x,y,channel)),"Object published HDR is finite");
                const double px=(x+.5)/24.-1,py=1-(y+.5)/24.;bool edge=false;int winner=-1;double nearest=1;V weights{};unsigned triangle{};double determinant{};
                for(unsigned i=0;i<4;++i)for(unsigned t=0;t<2;++t){const auto ia=geometry.indices[t*3],ib=geometry.indices[t*3+1],ic=geometry.indices[t*3+2];const auto&a=points[i][ia];const auto&b=points[i][ib];const auto&c=points[i][ic];const double det=(b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0]);const double v=((px-a[0])*(c[1]-a[1])-(py-a[1])*(c[0]-a[0]))/det,w=((b[0]-a[0])*(py-a[1])-(b[1]-a[1])*(px-a[0]))/det,u=1-v-w;
                    if(std::min({u,v,w})>-.02&&std::min({u,v,w})<.02)edge=true;
                    const double z=u*a[2]+v*b[2]+w*c[2];if(std::min({u,v,w})>=.02&&z<nearest){winner=int(i);nearest=z;weights={u,v,w};triangle=t;determinant=det;}}
                if(edge)continue;
                Near(images[1].At(x,y,0),nearest,"Object CPU depth",2e-5);Near(images[3].At(x,y,0),nearest,"Object Temporal CPU depth",2e-5);
                if(winner<0){Near(images[2].At(x,y,0),0,"Object empty motion");Near(images[2].At(x,y,1),0,"Object empty vertical motion");continue;}
                ++covered[winner];++checkedPixels;
                for(unsigned j=0;j<3;++j)Near(images[0].At(x,y,j),normalOracle[winner][j]*(determinant<0?1:-1)*.5+.5,"Object skinned normal-map frame",.002);
                V before{},now{};for(unsigned v=0;v<3;++v){const auto index=geometry.indices[triangle*3+v];for(unsigned j=0;j<3;++j){now[j]+=weights[v]*points[winner][index][j];if(step)before[j]+=weights[v]*previousPoints[winner][index][j];}}
                Near(images[2].At(x,y,0),step?(before[0]-now[0])*24:0,"Object previous world/pose horizontal motion",.002);
                Near(images[2].At(x,y,1),step?(now[1]-before[1])*24:0,"Object previous world/pose vertical motion",.002);
            }
            for(unsigned i=0;i<4;++i)Check(step==2&&i==3?covered[i]==0:covered[i]>=12,"Independent worlds, including reflection and offscreen culling, match oracle coverage");
            for(auto r:reads)device.ReleaseReadback(r);
            previous=std::move(input);previousPoints=points;++frames;
        }
    }
    host.ShutdownAfterIdle();
    if (cold) std::cout << "METEOR_COLD_SKIN_OK frames=9 rawPayload=0 cpuConnectivity=0 bulkReads=0 validation=0\n";
    Check(sharedFrames==9&&reuseFrames==3&&checkedPixels>100,"Object sharing/world/skin/motion coverage");
    std::cout<<"METEOR_OBJECT_SPACE_INSTANCES_OK frames="<<frames<<" worlds=4 poses=2 partitions=3 sharedFrames="<<sharedFrames<<" reuseFrames="<<reuseFrames<<" weightedBones=2 normalAndMotionPixels="<<checkedPixels<<" validation=0\n";
}
