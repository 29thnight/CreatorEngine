// Exercise the production HZB builder and meshlet kernels with independently
// specified visibility. No CPU implementation of the culling formula is used.
void RunMeshletHzb(RecordingChangeDevice& device, ProbeRoots& roots, ProbePipelines& pipelines,
    const std::filesystem::path& root, bool temporal = false)
{
    std::string error;
    const RHIPipelineLayoutParam params[]{RHILayout::Cbv(0), RHILayout::Srv(0), RHILayout::Srv(1),
        RHILayout::UavBufferTable(2, 0), RHILayout::SrvBufferTable(4096, 18),
        RHILayout::SrvBufferTable(4096, 4114), RHILayout::Srv(8211), RHILayout::SrvTable(16, 2)};
    const auto baseLayout = roots.GetOrCreate({std::span(params, 7), {}}, error);
    const auto hzbLayout = roots.GetOrCreate({params, {}}, error);
    Check(baseLayout.IsValid() && hzbLayout.IsValid(), "HZB meshlet layouts " + error);
    std::array<LX::Runtime::ComputePipeline, 3> kernels;
    const char* entries[]{"ShadowMeshletResetCS", "ShadowMeshletCullCS", "MeshletFinalizeCS"};
    for (unsigned i = 0; i < kernels.size(); ++i) {
        LX::Runtime::CompiledCompute shader;
        RHIShaderCompileOptions options; options.strictMath = true;
        RHIShaderPermutation permutation;
        if (i == 1) Check(permutation.Enable("GEOMETRY_VISIBILITY_OCCLUSION", error), "HZB permutation");
        Check(LX::Runtime::CompileCompute(RHIShaderSource::Resolve("GeometryVisibility.slang").string(),
            entries[i], permutation, options, shader, error), "HZB meshlet shader " + error);
        RHIComputePipelineDesc desc; desc.layout = i == 1 ? hzbLayout : baseLayout;
        desc.csBytecode = shader.stage.bytecode.Data(); desc.csSize = shader.stage.bytecode.Size();
        Check(kernels[i].Create(pipelines, desc, std::move(shader.description), error), "HZB meshlet PSO " + error);
    }
    std::array<LX::Runtime::ComputePipeline, 2> temporalKernels;
    if (temporal) for (unsigned i = 0; i < 2; ++i) {
        std::vector<RHIPipelineLayoutParam> layoutParams(std::begin(params), std::end(params));
        layoutParams.push_back(i == 0 ? RHILayout::Cbv(1) : RHILayout::Srv(8210));
        RHIShaderPermutation permutation;
        Check(permutation.Enable("GEOMETRY_VISIBILITY_OCCLUSION", error) && permutation.Enable(i == 0 ?
            "GEOMETRY_VISIBILITY_HISTORY" : "GEOMETRY_VISIBILITY_RECOVERY", error), "Temporal meshlet permutation");
        LX::Runtime::CompiledCompute shader; RHIShaderCompileOptions options; options.strictMath = true;
        const bool compiled = LX::Runtime::CompileCompute(RHIShaderSource::Resolve("GeometryVisibility.slang").string(),
            "ShadowMeshletCullCS", permutation, options, shader, error);
        Check(compiled, "Temporal meshlet shader " + error);
        RHIComputePipelineDesc desc; desc.layout = roots.GetOrCreate({layoutParams, {}}, error);
        desc.csBytecode = shader.stage.bytecode.Data(); desc.csSize = shader.stage.bytecode.Size();
        Check(temporalKernels[i].Create(pipelines, desc, std::move(shader.description), error), "Temporal meshlet pipeline " + error);
    }
    const RHIPipelineLayoutParam depthParams[]{RHILayout::Cbv(0)};
    RHIGraphicsPipelineDesc depthDesc;
    depthDesc.layout = roots.GetOrCreate({depthParams, {}}, error);
    RHIShaderCompiler::VerifiedShader vs, ps;
    const auto source = (root / "Tools/regression/support/MeshletHzbDepth.slang").string();
    Check(RHIShaderCompiler::VerifyFile(source, "DepthVS", "vs_6_0", RHIShaderCompiler::GetOutput(), {}, vs, error) &&
        RHIShaderCompiler::VerifyFile(source, "DepthPS", "ps_6_0", RHIShaderCompiler::GetOutput(), {}, ps, error),
        "HZB depth fixture shaders " + error);
    depthDesc.vsBytecode = vs.bytecode.Data(); depthDesc.vsSize = vs.bytecode.Size();
    depthDesc.psBytecode = ps.bytecode.Data(); depthDesc.psSize = ps.bytecode.Size();
    depthDesc.depthEnable = true; depthDesc.depthFunc = RHICompareOp::Always;
    depthDesc.depthWriteMask = RHIDepthWrite::All; depthDesc.numRenderTargets = 0;
    depthDesc.dsvFormat = RHIFormat::D32Float; depthDesc.cullMode = RHICullMode::None;
    const auto depthPso = pipelines.GetOrCreate(depthDesc, error);
    Check(depthPso.IsValid(), "HZB depth fixture pipeline " + error);
    GpuGeometryOcclusion occlusion;
    std::shared_ptr<const GpuGeometryOcclusion::Pyramid> previous;
    const std::array<std::set<uint32_t>, 8> expected{{{1,2,3,5}, {0,1,2,3,5}, {0,1,2,3,5,6},
        {3,5}, {0,1,2,3,4,5,6}, {1,2,3,5}, {1,2,3,5,6}, {1,2,3,5}}};
    const std::set<uint32_t> all{0,1,2,3,5,6}, occluded{1,2,3,5};
    const std::array<std::set<uint32_t>, 8> firstExpected{{all, occluded, all, occluded, {5}, all, {0,1,2,3,5}, occluded}};
    const std::array<std::set<uint32_t>, 8> temporalExpected{{all, all, all, occluded, {5}, all, {0,1,2,3,5}, all}};
    for (uint32_t test = 0; test < expected.size(); ++test) {
        Check(device.BeginFrame(error), "HZB begin");
        EnhancedFrameContext context{};
        context.resources = &device; context.rootSignatures = &roots; context.psoManager = &pipelines;
        context.width = test == 1 ? 33 : test == 2 || test == 6 ? 17 : 32;
        context.height = test == 1 ? 19 : test == 2 || test == 6 ? 13 : 24;
        if (temporal) { context.width = 32; context.height = 24; }
        context.frameId = 1000 + test;
        math::matrix4x4 matrix{};
        for (unsigned c = 0; c < 4; ++c) matrix.m[c][c] = 1;
        if (test == 3) { matrix.m[2][3] = 1; matrix.m[3][3] = 0; }
        if (test == 5) matrix.m[3][0] = .1f;
        if (temporal) { matrix = {}; for (unsigned c = 0; c < 4; ++c) matrix.m[c][c] = 1;
            if (test == 4) matrix.m[3][0] = 4; }
        const auto view = GpuGeometryOcclusion::CaptureView(context, matrix);
        if (previous) {
            bool rejected = false;
            try { previous->RequireView(view); } catch (const std::runtime_error&) { rejected = true; }
            Check(rejected, "HZB rejects previous recording");
        }
        std::shared_ptr<const GpuGeometryOcclusion::Pyramid> pyramid;
        Check(temporal ? occlusion.PrepareHistory(context, matrix, 1, 1, pyramid, error) :
            occlusion.Prepare(context, matrix, pyramid, error), "HZB prepare " + error);
        std::shared_ptr<const GpuGeometryOcclusion::Pyramid> history;
        if (temporal && previous) {
            const auto accepted = previous->HistoryOwner()->completion;
            previous->HistoryOwner()->completion = 0;
            bool rejectedUnsubmitted = false;
            try { GpuGeometryOcclusion::ReadHistory(context, matrix, previous->HistoryOwner()); }
            catch (const std::runtime_error&) { rejectedUnsubmitted = true; }
            Check(rejectedUnsubmitted, "Temporal history rejects an unsubmitted recording");
            previous->HistoryOwner()->completion = accepted;
            history = GpuGeometryOcclusion::ReadHistory(context, matrix, previous->HistoryOwner());
            for (unsigned mutation = 0; mutation < 3; ++mutation) {
                auto wrong = context;
                if (mutation == 0) ++wrong.width;
                if (mutation == 1) ++wrong.sceneEpoch;
                if (mutation == 2) wrong.frameId = previous->HistoryOwner()->source.frameId;
                bool rejected = false;
                try { GpuGeometryOcclusion::ReadHistory(wrong, matrix, previous->HistoryOwner()); }
                catch (const std::runtime_error&) { rejected = true; }
                Check(rejected, "Temporal history rejects changed extent, scene and frame order");
            }
        }
        for (unsigned mutation = 0; mutation < 3; ++mutation) {
            auto invalid = view;
            if (mutation == 0) ++invalid.width;
            if (mutation == 1) ++invalid.frameId;
            if (mutation == 2) invalid.projectionBits[0] ^= 1;
            bool rejected = false;
            try { pyramid->RequireView(invalid); } catch (const std::runtime_error&) { rejected = true; }
            Check(rejected, "HZB rejects a mismatched sealed view");
        }
        const auto upload = [&](const void* data, size_t bytes) {
            const auto slice = device.AllocateUpload({bytes, RHIUploadUsage::Raw, 256});
            Check(slice.IsWritable(), "HZB upload"); std::memcpy(slice.cpuAddress, data, bytes); return slice;
        };
        std::array<SurfacePoint, 21> points{};
        for (unsigned id = 0; id < 7; ++id) for (unsigned v = 0; v < 3; ++v) {
            const float x = id == 4 ? 3.f : id == 6 ? .95f : 0.f;
            const float y = id == 6 ? -.95f : 0.f;
            const float z = id == 1 ? .2f : id == 2 ? .3f : id == 3 ? (v ? .1f : 0.f) : .8f;
            points[id * 3 + v].position = {x + (v == 1 ? .03f : -.03f), y + (v == 2 ? .03f : -.03f), z, 1};
        }
        points[15].position[0] = std::numeric_limits<float>::quiet_NaN();
        if (temporal && test == 6) for (unsigned v = 0; v < 3; ++v) points[v].position[2] = .2f;
        std::vector<uint32_t> words(20 + 7 * 11, 0);
        words[3] = 80; words[4] = 32; words[5] = (20 + 7 * 8) * 4;
        for (unsigned id = 0; id < 7; ++id) {
            words[20 + id * 8] = id * 3; words[23 + id * 8] = 3;
            for (unsigned v = 0; v < 3; ++v) words[20 + 7 * 8 + id * 3 + v] = id * 3 + v;
        }
        const auto pointUpload = upload(points.data(), sizeof(points));
        const auto topology = upload(words.data(), words.size() * 4);
        RHIBufferHandle pointBuffer, ids, args;
        RHIBufferDesc bufferDesc; bufferDesc.bytes = sizeof(points);
        Check(device.CreateBuffer(bufferDesc, pointBuffer, error), "HZB point buffer");
        bufferDesc.allowUnorderedAccess = true; bufferDesc.bytes = 512 * 4;
        Check(device.CreateBuffer(bufferDesc, ids, error), "HZB ID buffer");
        bufferDesc.bytes = sizeof(RHIDrawIndexedIndirectArguments);
        Check(device.CreateBuffer(bufferDesc, args, error), "HZB argument buffer");
        const GpuGeometryVisibility::Candidate range{{}, 0, 0, 0, temporal ? 8u : 0u};
        const GpuGeometryVisibility::Bin bin{7, 0, 0, 0};
        const auto rangeUpload = upload(&range, sizeof(range)), binUpload = upload(&bin, sizeof(bin));
        // World-space fixture stream: identity object-to-world for the single bin.
        const std::array<math::vector4, 3> world{math::vector4(1, 0, 0, 0), math::vector4(0, 1, 0, 0), math::vector4(0, 0, 1, 0)};
        const auto worldUpload = upload(world.data(), sizeof(world));
        std::array<uint32_t, 48> constants{};
        constants[24] = constants[25] = 1;
        constants[27] = bin.indexCount;
        constants[26] = GpuGeometryVisibility::kWaveWorkQueue;
        // Shader constants store columns, while matrix.m is row-major.
        for (unsigned c = 0; c < 4; ++c) for (unsigned r = 0; r < 4; ++r)
            std::memcpy(&constants[28 + c * 4 + r], &matrix.m[r][c], 4);
        constants[44] = context.width; constants[45] = context.height;
        constants[46] = pyramid->LevelCount(); constants[47] = test == 4 ? 0 : 1;
        if (temporal) constants[47] = 1;
        const auto uniform = device.UploadConstants(constants.data(), sizeof(constants));
        const RHIBindingDesc outputs[]{RHIBindingDesc::UavBuffer(ids, 512, 4),
            RHIBindingDesc::UavBuffer(args, 1, sizeof(RHIDrawIndexedIndirectArguments))};
        const auto outputTable = device.CreateBindings(outputs);
        const auto vertex = RHIBindingDesc::SrvBuffer(pointBuffer, 105, MeshSurfaceBatch::GeometryViewStride);
        const auto topo = RHIBindingDesc::SrvBuffer(topology.buffer, uint32_t(topology.size / 4), 0, uint32_t(topology.offset / 4));
        const auto vertexTable = device.CreateBindings({&vertex, 1}), topologyTable = device.CreateBindings({&topo, 1});
        EnhancedRenderGraph graph(device, RGSchedulingMode::ExplicitVersioned);
        const auto pointCopy = graph.Write(graph.ImportBuffer(pointBuffer, RHIResourceState::Common, "HZB.Points"));
        graph.AddPass("HZB.Upload", {{pointCopy, RHIResourceState::CopyDest, RGAccessMode::Write}},
            [=](const auto& execution) { RHIBufferSlice destination; destination.buffer = pointBuffer; destination.size = sizeof(points);
                Check(execution.encoder->CopyBuffer(destination, pointUpload), "HZB points copy"); });
        RGTextureDesc texture; texture.width = context.width; texture.height = context.height;
        texture.format = RHIFormat::D32Float; texture.allowDepthStencil = true; texture.name = "HZB.Depth";
        const auto depth = graph.Write(graph.CreateTexture(texture));
        struct DepthConstants { uint32_t width, height, mode; float depth; };
        const DepthConstants fixture{context.width, context.height, temporal ? 0u : test == 1 ? 1u : test == 6 ? 2u : 0u,
            temporal ? (test == 1 || test == 7 ? 1.f : .3f) : test == 2 ? 1.f : .3f};
        const auto depthConstants = device.UploadConstants(&fixture, sizeof(fixture));
        graph.AddPass("HZB.DepthFixture", {{depth, RHIResourceState::DepthWrite, RGAccessMode::Write}},
            [=, &device](const auto& execution) {
                const auto target = RHIDepthTargetDesc::Depth(execution.ResolveHandle(depth), RHIFormat::D32Float);
                const auto binding = device.CreateRenderTargets(std::span<const RHITextureHandle>{}, &target);
                auto& encoder = *execution.encoder;
                encoder.BindRenderTargets(binding); encoder.ClearDepthTarget(binding, 1.f);
                encoder.SetViewportAndScissor(fixture.width, fixture.height);
                encoder.SetPrimitiveTopology(RHIPrimitiveTopology::TriangleList);
                encoder.SetPipeline(RHIBindPoint::Graphics, depthPso);
                encoder.SetConstantBuffer(RHIBindPoint::Graphics, 0, depthConstants); encoder.Draw(3, 1);
            });
        pyramid->Declare(graph, depth);
        RHIBufferHandle firstIds, firstArgs;
        RGHandle firstWrite, firstArgWrite;
        RHIReadback firstReadback;
        if (temporal) {
            bufferDesc.bytes = 512 * 4;
            Check(device.CreateBuffer(bufferDesc, firstIds, error), "Temporal first IDs");
            bufferDesc.bytes = sizeof(RHIDrawIndexedIndirectArguments);
            Check(device.CreateBuffer(bufferDesc, firstArgs, error), "Temporal first args");
            Check(device.CreateBufferReadback(64 * 4, firstReadback, error), "Temporal first readback");
            firstWrite = graph.Write(graph.ImportBuffer(firstIds, RHIResourceState::Common, "Temporal.FirstIDs"));
            firstArgWrite = graph.Write(graph.ImportBuffer(firstArgs, RHIResourceState::Common, "Temporal.FirstArgs"));
            const RHIBindingDesc firstOutputs[]{RHIBindingDesc::UavBuffer(firstIds, 512, 4),
                RHIBindingDesc::UavBuffer(firstArgs, 1, sizeof(RHIDrawIndexedIndirectArguments))};
            const auto firstTable = device.CreateBindings(firstOutputs);
            // Cold start uses the production frustum kernel and source classification.
            std::vector<EnhancedRenderGraph::RGPassUsage> firstUses{{pointCopy, RHIResourceState::ShaderResource, RGAccessMode::Read},
                {firstWrite, RHIResourceState::UnorderedAccess, RGAccessMode::Write},
                {firstArgWrite, RHIResourceState::UnorderedAccess, RGAccessMode::Write}};
            RHIBufferSlice historyUniform;
            if (history) {
                history->ImportHistory(graph); history->AddReadUsages(graph, firstUses);
                std::array<uint32_t, 16> columns;
                const auto& bits = previous->HistoryOwner()->source.projectionBits;
                for (unsigned c = 0; c < 4; ++c) for (unsigned r = 0; r < 4; ++r) columns[c * 4 + r] = bits[r * 4 + c];
                historyUniform = device.UploadConstants(columns.data(), sizeof(columns));
            }
            // Compile the production cold-start frustum entry, not a mocked classification.
            LX::Runtime::CompiledCompute coldShader; RHIShaderCompileOptions options; options.strictMath = true;
            Check(LX::Runtime::CompileCompute(RHIShaderSource::Resolve("GeometryVisibility.slang").string(),
                "ShadowMeshletCullCS", {}, options, coldShader, error), "Temporal cold kernel");
            auto cold = std::make_shared<LX::Runtime::ComputePipeline>();
            RHIComputePipelineDesc coldDesc; coldDesc.layout = baseLayout;
            coldDesc.csBytecode = coldShader.stage.bytecode.Data(); coldDesc.csSize = coldShader.stage.bytecode.Size();
            Check(cold->Create(pipelines, coldDesc, std::move(coldShader.description), error), "Temporal cold pipeline");
            graph.AddPass("Temporal.First", firstUses, [=, &kernels, &temporalKernels](const auto& execution) {
                auto& encoder = *execution.encoder;
                for (unsigned stage = 0; stage < 3; ++stage) {
                    encoder.SetPipeline(RHIBindPoint::Compute, stage == 1 ? (history ? temporalKernels[0].GetGeneration()->GetHandle() :
                        cold->GetGeneration()->GetHandle()) : kernels[stage].GetGeneration()->GetHandle());
                    encoder.SetConstantBuffer(RHIBindPoint::Compute, 0, uniform);
                    encoder.SetRootBuffer(RHIBindPoint::Compute, 1, rangeUpload); encoder.SetRootBuffer(RHIBindPoint::Compute, 2, binUpload);
                    encoder.SetBindings(RHIBindPoint::Compute, 3, firstTable); encoder.SetBindings(RHIBindPoint::Compute, 4, vertexTable);
                    encoder.SetBindings(RHIBindPoint::Compute, 5, topologyTable);
                    encoder.SetRootBuffer(RHIBindPoint::Compute, 6, worldUpload);
                    if (stage == 1 && history) { encoder.SetBindings(RHIBindPoint::Compute, 7, history->Bindings(execution));
                        encoder.SetConstantBuffer(RHIBindPoint::Compute, 8, historyUniform); }
                    encoder.Dispatch(1,1,1);
                    const RHIBufferHandle barriers[]{firstIds, firstArgs}; encoder.ResourceBarriers({{}, {}, {}, barriers});
                }
            });
        }
        const auto idWrite = graph.Write(graph.ImportBuffer(ids, RHIResourceState::Common, "HZB.IDs"));
        const auto argWrite = graph.Write(graph.ImportBuffer(args, RHIResourceState::Common, "HZB.Args"));
        std::vector<EnhancedRenderGraph::RGPassUsage> uses{{pointCopy, RHIResourceState::ShaderResource, RGAccessMode::Read},
            {idWrite, RHIResourceState::UnorderedAccess, RGAccessMode::Write},
            {argWrite, RHIResourceState::UnorderedAccess, RGAccessMode::Write}};
        pyramid->AddReadUsages(graph, uses);
        if (temporal) uses.push_back({firstWrite, RHIResourceState::ShaderResource, RGAccessMode::Read});
        graph.AddPass("HZB.MeshletKernels", uses, [=, &kernels, &temporalKernels](const auto& execution) {
            auto& encoder = *execution.encoder;
            const auto depthTable = pyramid->Bindings(execution);
            for (unsigned stage = 0; stage < 3; ++stage) {
                encoder.SetPipeline(RHIBindPoint::Compute, temporal && stage == 1 ? temporalKernels[1].GetGeneration()->GetHandle() : kernels[stage].GetGeneration()->GetHandle());
                encoder.SetConstantBuffer(RHIBindPoint::Compute, 0, uniform);
                encoder.SetRootBuffer(RHIBindPoint::Compute, 1, rangeUpload);
                encoder.SetRootBuffer(RHIBindPoint::Compute, 2, binUpload);
                encoder.SetBindings(RHIBindPoint::Compute, 3, outputTable);
                encoder.SetBindings(RHIBindPoint::Compute, 4, vertexTable);
                encoder.SetBindings(RHIBindPoint::Compute, 5, topologyTable);
                encoder.SetRootBuffer(RHIBindPoint::Compute, 6, worldUpload);
                if (stage == 1) encoder.SetBindings(RHIBindPoint::Compute, 7, depthTable);
                if (stage == 1 && temporal) { RHIBufferSlice slice; slice.buffer = firstIds; slice.size = 64 * 4;
                    encoder.SetRootBuffer(RHIBindPoint::Compute, 8, slice); }
                encoder.Dispatch(1, 1, 1);
                const RHIBufferHandle barriers[]{ids, args}; encoder.ResourceBarriers({{}, {}, {}, barriers});
            }
        });
        RHIReadback idReadback, argReadback;
        Check(device.CreateBufferReadback(64 * 4, idReadback, error) &&
            device.CreateBufferReadback(sizeof(RHIDrawIndexedIndirectArguments), argReadback, error), "HZB readbacks");
        graph.AddPass("HZB.Readback", {{idWrite, RHIResourceState::CopySource, RGAccessMode::Read},
            {argWrite, RHIResourceState::CopySource, RGAccessMode::Read}}, [=](const auto& execution) {
                execution.encoder->CopyBufferToReadback(idReadback, ids); execution.encoder->CopyBufferToReadback(argReadback, args);
            }, true);
        if (temporal) graph.AddPass("Temporal.FirstReadback", {{firstWrite, RHIResourceState::CopySource, RGAccessMode::Read}},
            [=](const auto& execution) { execution.encoder->CopyBufferToReadback(firstReadback, firstIds); }, true);
        Check(graph.Compile(error), "HZB graph compile " + error);
        Check(graph.Execute(error), "HZB graph execute " + error);
        Check(device.EndFrame(error), "HZB submit"); device.WaitForGpu();
        RHIReadbackImage idImage, argImage;
        Check(device.MapReadback(idReadback, idImage, error) && device.MapReadback(argReadback, argImage, error), "HZB map");
        const auto* list = reinterpret_cast<const uint32_t*>(idImage.data.data());
        Check(list[0] <= 7, "HZB bounded count");
        std::set<uint32_t> actual;
        for (unsigned i = 0; i < list[0]; ++i) Check(actual.insert(list[i + 1]).second, "HZB unique IDs");
        Check(actual == (temporal ? temporalExpected[test] : expected[test]), "HZB independent expected IDs temporal=" +
            std::to_string(temporal) + " case=" + std::to_string(test));
        if (temporal) {
            RHIReadbackImage image;
            Check(device.MapReadback(firstReadback, image, error), "Temporal first map");
            const auto* data = reinterpret_cast<const uint32_t*>(image.data.data());
            Check(data[0] <= 7, "Temporal first bounded count");
            std::set<uint32_t> first;
            for (unsigned i = 0; i < data[0]; ++i) Check(first.insert(data[i+1]).second, "Temporal first unique IDs");
            Check(first == firstExpected[test], "Temporal independent first-stage IDs case=" + std::to_string(test));
            for (unsigned i = 0; i < 7; ++i) Check(data[8+i] == (first.contains(i) ? 1u : 0u), "Temporal source classification");
            Check(pyramid->HistoryOwner()->recorded.load(), "Temporal final history recorded");
            pyramid->HistoryOwner()->completion = device.GetLastSignaledFenceValue();
            device.ReleaseReadback(firstReadback); device.ReleaseBuffer(firstIds); device.ReleaseBuffer(firstArgs);
        }
        const auto* result = reinterpret_cast<const uint32_t*>(argImage.data.data());
        Check(result[0] == actual.size() && result[1] == 1 && result[2] == 1, "HZB finalized args");
        device.ReleaseReadback(idReadback); device.ReleaseReadback(argReadback);
        device.ReleaseBuffer(ids); device.ReleaseBuffer(args); device.ReleaseBuffer(pointBuffer);
        std::string validation;
        Check(device.DrainDebugMessages(validation) == 0, "HZB GPU validation " + validation);
        previous = pyramid;
    }
    previous.reset(); occlusion.ShutdownAfterIdle();
    std::cout << (temporal ? "METEOR_MESHLET_TEMPORAL_OK cases=8 validation=0\n" : "METEOR_MESHLET_HZB_OK cases=8 validation=0\n");
    if (!temporal) RunMeshletHzb(device, roots, pipelines, root, true);
}
