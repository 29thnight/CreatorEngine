// Real SceneHost mixed raster gate. Rectangles supply an independent coverage/depth oracle;
// no alternate renderer or route override is used to produce the expected result.
void RunHybridVisibility(RecordingChangeDevice& device, ProbeRoots& roots, ProbePipelines& pipelines,
                         ProbeTextures& textures, ProbePool& pool, own::shared_owner<const Instance> source)
{
    const auto material = SceneCooked(source);
    auto description = material->description;
    description.parameters.push_back({902, 0.0});
    own::shared_owner<const Instance> transparentMask;
    std::string error;
    Check(BuildInstance(
              material->generation, description,
              [&](const experiment::AssetId&, LXColorSpace, std::string&) { return material->textures.front().owner; },
              transparentMask, error),
          "Hybrid masked instance");
    ProbeMeshes meshes;
    Check(meshes.Initialize(&device, error), "Hybrid meshes");
    SceneHost host(ce::get_job_scheduler());
    EnhancedFrameContext context;
    context.resources = &device;
    context.rootSignatures = &roots;
    context.psoManager = &pipelines;
    context.textureCache = &textures;
    context.meshCache = &meshes;
    context.width = 25;
    context.height = 19;
    FrameCameraSnapshot camera;
    camera.view = camera.projection = math::matrix4x4::identity();
    camera.eyePosition = math::vector3(0, 0, 2);
    context.camera = &camera;
    RHIShaderCompileOptions shaderOptions;
    shaderOptions.strictMath = true;
    shaderOptions.bufferInt64Atomics = true;
    for (const char* entry : {"Initialize", "Classify", "Rasterize", "ResolveVS", "ResolvePS"})
    {
        RHIShaderCompiler::VerifiedShader spirv;
        const auto profile = std::string_view(entry).ends_with("VS")   ? "vs_6_6"
                             : std::string_view(entry).ends_with("PS") ? "ps_6_6"
                                                                       : "cs_6_6";
        const bool verified =
            RHIShaderCompiler::VerifyFile(RHIShaderSource::Resolve("MeteorHybridVisibility.slang").string(), entry,
                                          profile, RHIShaderBinary::SpirV, {}, spirv, error, shaderOptions);
        Check(verified, "Hybrid SPIR-V compile (runtime deferred) " + error);
    }
    WaitSceneProgram(host, context, material->generation);
    auto old = material->generation->cooked.product;
    old.program.semanticKey.replace(old.program.semanticKey.size() - SceneHostIdentity.size(), SceneHostIdentity.size(),
                                    "|lx-scene-host:28");
    SceneShaderSet rejected;
    Check(!LoadSceneShaders(old, RHIShaderBinary::Dxil, rejected, error),
          "Hybrid ABI rejects old HW-only cooked stages");
    unsigned frames{}, hwPixels{}, swPixels{};
    for (unsigned fixture = 0; fixture < 7; ++fixture)
        for (unsigned workers : {0u, 1u, 4u})
        {
            std::cerr << "METEOR_HYBRID_FRAME fixture=" << fixture << " workers=" << workers << '\n';
            struct Rect
            {
                float x0, y0, x1, y1, z, slope;
                bool reverse{}, twoSided{}, masked{};
            };
            std::array<Rect, 4> rectangles{{{2, 2, 20, 17, .6f, 0},
                                            {5, 5, 11, 11, .25f, 0},
                                            {15, 6, 21, 12, .8f, 0},
                                            {12, 2, 16, 6, .125f, 0, false, false, true}}};
            if (fixture == 1)
            {
                rectangles[0].z = rectangles[1].z = rectangles[2].z = .5f;
            }
            if (fixture == 2)
            {
                rectangles[1].reverse = true;
                rectangles[1].twoSided = true;
                rectangles[2].reverse = true;
            }
            if (fixture == 3)
            {
                rectangles[1] = {-2, 5, 4, 11, .25f, 0};
            }
            if (fixture == 4)
            {
                rectangles[1] = {5.5f, 5.5f, 11.5f, 11.5f, .25f, 0};
                rectangles[2] = {15.5f, 6.5f, 21.5f, 12.5f, .8f, 0};
            }
            if (fixture == 5)
            {
                rectangles[1].slope = .015625f;
                rectangles[2].slope = -.015625f;
            }
            if (fixture == 6)
                rectangles[1] = {5, 5, 11, 11, -.25f, .125f};
            std::array<Geometry, 4> geometry;
            std::vector<EnhancedDrawItem> draws;
            context.frameId = 12000 + frames;
            context.sceneEpoch = 70;
            for (unsigned n = 0; n < geometry.size(); ++n)
            {
                auto& g = geometry[n];
                g = MakeGeometry(false, false);
                const auto& r = rectangles[n];
                g.draw.geometryKey = 8000 + n;
                g.draw.modelMeshView.handle.generation = context.frameId;
                g.draw.modelMeshView.indexData = g.indices.data();
                g.draw.modelMeshView.vertexData = g.vertices.data();
                // Clockwise in viewport coordinates, with one shared diagonal.
                const std::array<std::array<float, 2>, 6> positions{
                    {{r.x0, r.y0}, {r.x1, r.y0}, {r.x1, r.y1}, {r.x0, r.y0}, {r.x1, r.y1}, {r.x0, r.y1}}};
                for (unsigned v = 0; v < 6; ++v)
                {
                    g.points[v].position = {positions[v][0] / context.width * 2 - 1,
                                            1 - positions[v][1] / context.height * 2,
                                            r.z + r.slope * (positions[v][0] - r.x0), 0};
                    std::memcpy(g.vertices.data() + v * g.draw.modelMeshView.vertexStride, g.points[v].position.data(),
                                12);
                }
                if (r.reverse)
                {
                    std::swap(g.indices[1], g.indices[2]);
                    std::swap(g.indices[4], g.indices[5]);
                }
                g.draw.coverage.flags = EnhancedMaterialCoverage::Enabled |
                                        (r.twoSided ? EnhancedMaterialCoverage::DoubleSided : 0u) |
                                        (r.masked ? EnhancedMaterialCoverage::Masked : 0u);
                g.draw.materialGraphInstance = r.masked ? transparentMask : material;
                draws.push_back(g.draw);
            }
            context.draws = &draws;
            SceneInputView view{context.frameId, context.sceneEpoch, 81, 1, context.width, context.height, camera};
            own::shared_owner<const SceneViewInput> input;
            Check(SealScene(view, draws, {}, input, error), "Hybrid sealed geometry " + error);
            Check(device.BeginFrame(error), "Hybrid begin");
            textures.BeginFrame(context.frameId);
            meshes.BeginFrame(uint32_t(context.frameId));
            Check(host.PrepareResidency(context, input, error), "Hybrid residency " + error);
            auto graph = std::make_shared<EnhancedRenderGraph>(device, RGSchedulingMode::ExplicitVersioned,
                                                               RGOrderPolicy::DependencyOrder);
            if (workers)
            {
                pool.BeginFrame(uint32_t(context.frameId));
                Check(graph->PrepareParallel(pool, error), "Hybrid parallel preparation");
            }
            const bool prepared = host.Prepare(context, input, {}, {}, {}, {}, {}, error, 1);
            Check(prepared, "Hybrid prepare " + error);
            SceneSurfaceTargets targets;
            targets.Declare(*graph, context);
            host.DeclareGeometry(*graph);
            host.DeclareCulling(*graph);
            const auto output = host.DeclareGBuffer(*graph, targets.GetOutputs());
            host.DeclareHistory(*graph, output.depth);
            const auto hybrid = host.HybridVisibility();
            const auto table = host.FrameGeometry();
            Check(bool(hybrid), "Hybrid prepared owner");
            std::array<RHIReadback, 5> readbacks;
            Check(
                device.CreateReadback(context.width, context.height, RHIFormat::RG32Uint, 1, readbacks[0], error) &&
                    device.CreateReadback(context.width, context.height, RHIFormat::D32Float, 1, readbacks[1], error) &&
                    device.CreateReadback(context.width, context.height, RHIFormat::R32Uint, 1, readbacks[2], error) &&
                    device.CreateBufferReadback(hybrid->KeyBytes(), readbacks[3], error) &&
                    device.CreateBufferReadback(hybrid->meshlets * 4, readbacks[4], error),
                "Hybrid readbacks " + error);
            graph->AddPass(
                "Probe.Hybrid.Readback",
                {{output.visibility, RHIResourceState::CopySource, RGAccessMode::Read},
                 {output.depth, RHIResourceState::CopySource, RGAccessMode::Read},
                 {output.bitmask, RHIResourceState::CopySource, RGAccessMode::Read},
                 {hybrid->graphKeys, RHIResourceState::CopySource, RGAccessMode::Read},
                 {hybrid->graphRoutes, RHIResourceState::CopySource, RGAccessMode::Read}},
                [=](const auto& e) {
                    e.encoder->CopyToReadback(readbacks[0], e.ResolveHandle(output.visibility));
                    e.encoder->CopyToReadback(readbacks[1], e.ResolveHandle(output.depth));
                    e.encoder->CopyToReadback(readbacks[2], e.ResolveHandle(output.bitmask));
                    e.encoder->CopyBufferToReadback(readbacks[3], hybrid->keys);
                    e.encoder->CopyBufferToReadback(readbacks[4], hybrid->routes);
                },
                true);
            const bool compiled = graph->Compile(error);
            Check(compiled, "Hybrid graph " + error);
            EnhancedRenderGraph::DiagnosticSnapshot snap;
            Check(graph->CaptureDiagnosticSnapshot(snap), "Hybrid graph snapshot");
            const auto find = [&](std::string_view name) -> const EnhancedRenderGraph::DiagnosticPass& {
                auto p = std::ranges::find(snap.passes, name, &EnhancedRenderGraph::DiagnosticPass::name);
                Check(p != snap.passes.end(), "Hybrid pass " + std::string(name));
                return *p;
            };
            const auto& hw = find("METEOR.Scene.Visibility.HW");
            const auto& sw = find("METEOR.Scene.Visibility.SW");
            const auto& classify = find("METEOR.Scene.Visibility.Classify");
            const auto& resolve = find("METEOR.Scene.Visibility.Resolve");
            for (const auto* writer : {&hw, &sw})
            {
                Check(classify.compiledIndex < writer->compiledIndex && writer->compiledIndex < resolve.compiledIndex,
                      "Classification and merge enclose both raster producers");
                Check(std::ranges::any_of(writer->usages,
                                          [&](const auto& u) {
                                              return u.resource == hybrid->graphKeys.index &&
                                                     u.access == RGAccessMode::AtomicMin64;
                                          }),
                      "Both producers declare the same atomic reduction");
                Check(std::ranges::none_of(writer->usages,
                                           [](const auto& u) {
                                               return u.state == RHIResourceState::RenderTarget ||
                                                      u.state == RHIResourceState::DepthWrite;
                                           }),
                      "No obsolete raster attachment dependency");
            }
            Check(std::ranges::none_of(snap.versionEdges,
                                       [&](const auto& e) {
                                           return (e.producer == hw.authoredIndex && e.consumer == sw.authoredIndex) ||
                                                  (e.producer == sw.authoredIndex && e.consumer == hw.authoredIndex);
                                       }),
                  "Sibling atomic writers have no false ordering edge");
            Check(resolve.compiledIndex < find("METEOR.Scene.Material.CountTiles").compiledIndex,
                  "Tile assignment reads merged visibility");
            if (workers)
            {
                graph->SetParallelRecordCostThreshold(0);
                RHIRecordedBatchDesc desc;
                desc.frameId = context.frameId;
                desc.backendGeneration = GetRHISubmissionThread().GetOwnerGeneration(&device);
                desc.lifetimeToken = graph;
                RHIRecordedBatch batch;
                RHISubmissionTicket ticket;
                const bool recorded = graph->RecordParallel(pool, workers, desc, batch, error);
                Check(recorded, "Hybrid workers " + error);
                Check(GetRHISubmissionThread().EnqueueRecordedBatch(&device, device, std::move(batch), ticket, error),
                      "Hybrid worker submit " + error);
            }
            else
            {
                const bool executed = graph->Execute(error);
                Check(executed, "Hybrid execute " + error);
            }
            Check(device.EndFrame(error) && GetRHISubmissionThread().DrainSubmissions(&device, error), "Hybrid submit");
            device.WaitForGpu();
            Check(GetRHISubmissionThread().Drain(&device, error), "Hybrid retirement");
            std::array<RHIReadbackImage, 5> images;
            for (unsigned n = 0; n < 5; ++n)
                Check(device.MapReadback(readbacks[n], images[n], error), "Hybrid readback map");
            for (unsigned record = 1; record < table->records.size(); ++record)
            {
                const auto& r = table->records[record];
                const auto n = unsigned(r.identity.geometryKey - 8000);
                const auto& rect = rectangles[n];
                const uint32_t expected = (n == 0 || rect.masked || rect.x0 < 0 || rect.z < 0) ? 1u : 2u;
                for (unsigned m = 0; m < r.meshletCount; ++m)
                {
                    uint32_t route;
                    std::memcpy(&route, images[4].data.data() + ((r.identity.reserved[1] - 1) / 128 + m) * 4, 4);
                    Check(route == expected,
                          "GPU HW/SW classification record=" + std::to_string(n) + " route=" + std::to_string(route));
                }
            }
            for (unsigned y = 0; y < context.height; ++y)
                for (unsigned x = 0; x < context.width; ++x)
                {
                    unsigned expectedOwner = UINT32_MAX;
                    double z = 1;
                    for (unsigned n = 0; n < rectangles.size(); ++n)
                    {
                        const auto& r = rectangles[n];
                        if (r.masked || (r.reverse && !r.twoSided) || x + .5 < r.x0 || x + .5 >= r.x1 ||
                            y + .5 < r.y0 || y + .5 >= r.y1)
                            continue;
                        const double depth = r.z + r.slope * (x + .5 - r.x0);
                        if (depth >= 0 && depth < z)
                        {
                            z = depth;
                            expectedOwner = n;
                        }
                    }
                    uint32_t address, depthBits, owner;
                    uint64_t key, visibilityKey;
                    std::memcpy(&address, images[0].data.data() + y * images[0].rowPitch + x * 8, 4);
                    std::memcpy(&depthBits, images[1].data.data() + y * images[1].rowPitch + x * 4, 4);
                    std::memcpy(&owner, images[2].data.data() + y * images[2].rowPitch + x * 4, 4);
                    std::memcpy(&key, images[3].data.data() + (y * context.width + x) * 8, 8);
                    std::memcpy(&visibilityKey, images[0].data.data() + y * images[0].rowPitch + x * 8, 8);
                    const auto record = table->FindRecord(address);
                    const auto actualOwner =
                        record ? unsigned(table->records[record].identity.geometryKey - 8000) : UINT32_MAX;
                    Check(actualOwner == expectedOwner,
                          "Independent hybrid coverage/winner fixture=" + std::to_string(fixture) +
                              " pixel=" + std::to_string(x) + "," + std::to_string(y) +
                              " expected=" + std::to_string(expectedOwner) + " actual=" + std::to_string(actualOwner));
                    Near(std::bit_cast<float>(depthBits), z, "Independent hybrid depth plane", 2e-6);
                    if (record)
                    {
                        Check(key == visibilityKey && key == ((uint64_t(depthBits) << 32) | address),
                              "Atomic key, visibility address and depth resolve are bit-identical");
                        Check(owner == table->records[record].owner, "Material reconstruction uses the atomic winner");
                        uint32_t route;
                        std::memcpy(&route, images[4].data.data() + ((address - 1) / 128) * 4, 4);
                        if (route == 1)
                            ++hwPixels;
                        else if (route == 2)
                            ++swPixels;
                        else
                            Check(false, "Visible pixel has a raster route");
                    }
                    else
                        Check(key == UINT64_MAX && visibilityKey == 0 && owner == 0,
                              "Empty atomic sentinel produces clear depth and no material");
                }
            std::string validation;
            const auto problems = device.DrainDebugMessages(validation);
            Check(problems == 0, "Hybrid GPU validation " + validation);
            for (auto& r : readbacks)
                device.ReleaseReadback(r);
            ++frames;
        }
    Check(hwPixels > 0 && swPixels > 0, "Both HW and SW independently win real Scene pixels");
    host.ShutdownAfterIdle();
    meshes.Shutdown();
    std::cout << "METEOR_HYBRID_VISIBILITY_OK frames=" << frames
              << " cases=7 partitions=3 validation=0 hwPixels=" << hwPixels << " swPixels=" << swPixels << '\n';
}
