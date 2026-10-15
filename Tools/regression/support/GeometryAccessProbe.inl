// Compare real GPU access against fixed original topology, including repartitioning.
void RunGeometryAccess(RecordingChangeDevice& device, ProbeRoots& roots, ProbePipelines& pipelines,
                       const std::filesystem::path& root)
{
    using Key = material_graph::SceneVisibilityContract;
    uint32_t next=1, begin=0, end=0;
    Check(Key::Reserve(2,next,begin,end) && begin==1 && end==257 && next==257,
        "Frame address range reserves exact meshlet slots");
    Check(Key::Address(begin,end,125)==126 && Key::Address(begin,end,128)==129,
        "Frame key preserves meshlet and triangle identity");
    Check(Key::Address(begin,end,126)==0 && Key::Address(begin,end,127)==0 &&
        Key::Address(begin,end,256)==0, "Reject padding triangles and out-of-range addresses");
    next=UINT32_MAX-128;
    Check(Key::Reserve(1,next,begin,end) && end==UINT32_MAX, "Last valid address range does not wrap");
    const auto saved=next;
    Check(!Key::Reserve(1,next,begin,end) && next==saved &&
        !Key::Reserve(UINT32_MAX,next,begin,end), "Frame overflow rejects atomically, never truncates");
    next=1;
    Check(!Key::Reserve(0,next,begin,end) && next==1, "Empty ranges cannot alias the next record");
    Check(Key::Key(-0.f,1)==Key::Key(0.f,1) && Key::Key(0.f,2)<Key::Key(.5f,1) &&
        Key::Key(.5f,1)<Key::Key(.5f,2) && Key::Key(std::bit_cast<float>(0x3f7fffffu),UINT32_MAX-1)<Key::EmptyKey,
        "Standard depth ordering, signed zero and deterministic address ties");
    Check(Key::Key(-.1f,1)==Key::EmptyKey && Key::Key(1.f,1)==Key::EmptyKey && Key::Key(1.1f,1)==Key::EmptyKey &&
        Key::Key(std::numeric_limits<float>::quiet_NaN(),1)==Key::EmptyKey &&
        Key::Key(std::numeric_limits<float>::infinity(),1)==Key::EmptyKey &&
        Key::Key(.5f,0)==Key::EmptyKey && Key::Key(.5f,UINT32_MAX)==Key::EmptyKey,
        "Invalid depth/address cannot win atomic visibility");
    material_graph::SceneFrameGeometry lookup;
    for (uint32_t count : {1u,2u,1u}) {
        material_graph::SceneFrameGeometry::Record record;
        Check(Key::Reserve(count,next,record.identity.reserved[1],record.identity.reserved[2]), "Lookup range");
        lookup.records.push_back(record);
    }
    Check(lookup.FindRecord(0)==0 && lookup.FindRecord(1)==1 && lookup.FindRecord(126)==1 &&
        lookup.FindRecord(127)==0 && lookup.FindRecord(128)==0 && lookup.FindRecord(129)==2 &&
        lookup.FindRecord(257)==2 && lookup.FindRecord(385)==3 && lookup.FindRecord(513)==0 &&
        lookup.FindRecord(UINT32_MAX)==0, "Frame address binary search boundaries and background");
    std::string error;
    const RHIPipelineLayoutParam params[]{RHILayout::Cbv(0), RHILayout::Srv(0), RHILayout::Srv(1), RHILayout::UavBufferTable(1, 0)};
    const auto layout = roots.GetOrCreate({params, {}}, error);
    Check(layout.IsValid(), "Geometry access layout " + error);
    LX::Runtime::CompiledCompute shader;
    RHIShaderCompileOptions options; options.strictMath = true;
    Check(LX::Runtime::CompileCompute((root / "Tools/regression/support/GeometryAccessProbe.slang").string(),
        "GeometryAccessCS", {}, options, shader, error), "Geometry access compile " + error);
    LX::Runtime::ComputePipeline kernel;
    RHIComputePipelineDesc pipeline; pipeline.layout = layout;
    pipeline.csBytecode = shader.stage.bytecode.Data(); pipeline.csSize = shader.stage.bytecode.Size();
    Check(kernel.Create(pipelines, pipeline, std::move(shader.description), error), "Geometry access PSO " + error);
    unsigned cases = 0;
    for (uint32_t lod : {0u, 3u}) for (unsigned partition = 0; partition != 3; ++partition) {
        experiment::MeshletPayload payload;
        const unsigned first = partition == 2 ? 1 : 0;
        const unsigned count = partition == 0 ? 2 : 1;
        for (unsigned i = 0; i < count; ++i) {
            experiment::MeshletDescriptor descriptor;
            descriptor.vertexOffset = i * 3; descriptor.triangleOffset = i * 3; descriptor.primitiveOffset = i;
            descriptor.vertexCount = 3; descriptor.triangleCount = 1; descriptor.clusterId = 73 + first + i;
            payload.descriptors.push_back(descriptor);
            payload.primitiveRemap.push_back(17 + first + i);
            const uint8_t corners[2][3]{{0, 2, 1}, {2, 0, 1}};
            payload.triangleIndices.insert(payload.triangleIndices.end(), corners[first+i], corners[first+i]+3);
        }
        payload.lod0 = {0, count};
        std::vector<uint32_t> sourceVertices;
        if (partition == 0) {
            payload.vertexRemap = {2, 0, 1, 1, 3, 2}; sourceVertices = {11, 7, 19, 23};
        } else {
            payload.vertexRemap = {0, 1, 2};
            sourceVertices = partition == 1 ? std::vector<uint32_t>{19, 11, 7} : std::vector<uint32_t>{7, 23, 19};
        }
        RHIModelMeshletUpload packed;
        Check(PackMeteorMeshletGpuPayload(payload, sourceVertices.size(), sizeof(SurfacePoint), 0,
            lod, 40, 32, sourceVertices, packed, error), "Geometry access serialized payload " + error);
        Check(device.BeginFrame(error), "Geometry access begin");
        const auto upload = [&](const void* source, size_t bytes) {
            const auto slice = device.AllocateUpload({bytes, RHIUploadUsage::Raw, 256});
            Check(slice.IsWritable(), "Geometry access upload"); std::memcpy(slice.cpuAddress, source, bytes); return slice;
        };
        // Force a nonzero byte offset; neither offsets nor allocation identities are IDs.
        const uint32_t padding[64]{}; upload(padding, sizeof(padding));
        const auto topology = upload(packed.bytes.data(), packed.bytes.size());
        std::vector<std::array<uint32_t,2>> requests;
        for (unsigned i=0; i<count; ++i) requests.push_back({i,0});
        requests.push_back({count,0}); requests.push_back({0,1});
        const auto requestBuffer = upload(requests.data(), requests.size()*sizeof(requests[0]));
        const std::array<uint32_t,4> constants{uint32_t(requests.size()),0,0,0};
        const auto uniform = device.UploadConstants(constants.data(), sizeof(constants));
        RHIBufferDesc desc; desc.bytes = requests.size()*64; desc.allowUnorderedAccess = true;
        RHIBufferHandle output;
        Check(device.CreateBuffer(desc, output, error), "Geometry access output");
        const auto binding = RHIBindingDesc::UavBuffer(output, uint32_t(requests.size()*4), 16);
        const auto table = device.CreateBindings({&binding,1}); Check(table.IsValid(), "Geometry access bindings");
        auto& encoder = device.GetImmediateEncoder();
        const RHIBufferTransition before{output,RHIResourceState::Common,RHIResourceState::UnorderedAccess};
        encoder.ResourceBarriers({{}, {&before,1}});
        encoder.SetPipeline(RHIBindPoint::Compute, kernel.GetGeneration()->GetHandle());
        encoder.SetConstantBuffer(RHIBindPoint::Compute,0,uniform);
        encoder.SetRootBuffer(RHIBindPoint::Compute,1,topology);
        encoder.SetRootBuffer(RHIBindPoint::Compute,2,requestBuffer);
        encoder.SetBindings(RHIBindPoint::Compute,3,table); encoder.Dispatch(1,1,1);
        RHIReadback readback; Check(device.CreateBufferReadback(desc.bytes,readback,error), "Geometry access readback");
        const RHIBufferTransition after{output,RHIResourceState::UnorderedAccess,RHIResourceState::CopySource};
        encoder.ResourceBarriers({{}, {&after,1}}); encoder.CopyBufferToReadback(readback,output);
        Check(device.EndFrame(error), "Geometry access submit"); device.WaitForGpu();
        RHIReadbackImage image; Check(device.MapReadback(readback,image,error), "Geometry access map");
        for (unsigned i=0; i<requests.size(); ++i) {
            std::array<uint32_t,16> actual; std::memcpy(actual.data(), image.data.data()+i*64,64);
            if (i>=count) { Check(actual[0]==UINT32_MAX, "Out-of-range geometry request rejected"); continue; }
            const unsigned original=first+i;
            const std::array<uint32_t,3> expectedSource[2]{{19,7,11},{19,7,23}};
            const std::array<uint32_t,3> expectedCorners[2]{{0,2,1},{2,0,1}};
            const std::array<uint32_t,3> unsplitVertices[2]{{2,1,0},{2,1,3}};
            Check(actual[0]==73+original && actual[1]==lod && actual[2]==17+original && actual[3]==1,
                "Cooked cluster/LOD/primitive identity survives partitioning");
            for (unsigned c=0;c<3;++c) {
                Check(actual[4+c]==expectedCorners[original][c], "Cooked triangle winding retained");
                Check(actual[8+c]==(partition==0 ? unsplitVertices[original][c] : expectedCorners[original][c]),
                    "Chunk-local posed vertex address");
                Check(actual[12+c]==expectedSource[original][c], "Source vertex identity survives partitioning");
            }
        }
        device.ReleaseReadback(readback); device.ReleaseBuffer(output);
        std::string validation; Check(device.DrainDebugMessages(validation)==0, "Geometry access validation " + validation);
        ++cases;
    }
    std::cout << "METEOR_GEOMETRY_ACCESS_OK cases=" << cases << " lods=2 partitions=3 validation=0\n";
}
