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
    for (const char* entry :
         {"Initialize", "PrepareClassify", "Classify", "PrepareSoftware", "Rasterize", "ResolveVS", "ResolvePS"})
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
                                    "|lx-scene-host:29");
    SceneShaderSet rejected;
    Check(!LoadSceneShaders(old, RHIShaderBinary::Dxil, rejected, error),
          "Hybrid ABI rejects old HW-only cooked stages");
    unsigned frames{}, hwPixels{}, swPixels{}, occluderPixels{}, historyFrames{};
    // Fixture 7 repeats fixture 0 with object-space vertices: each draw carries a
    // non-uniform scale, rotation and translation, and its stream holds W^-1 * world.
    // HW, SW, culling and resolve must all apply W to reproduce the same oracle.
    math::matrix4x4 objectToWorld = math::matrix4x4::identity();
    {
        const float c = std::cos(.6f), s = std::sin(.6f);
        const float row0[4]{1.25f * c, 1.25f * s, 0, 0}, row1[4]{-.8f * s, .8f * c, 0, 0},
            row2[4]{0, 0, 1.5f, 0}, row3[4]{.2f, -.1f, .05f, 1};
        for (unsigned column = 0; column < 4; ++column)
        {
            objectToWorld.m[0][column] = row0[column];
            objectToWorld.m[1][column] = row1[column];
            objectToWorld.m[2][column] = row2[column];
            objectToWorld.m[3][column] = row3[column];
        }
    }
    const auto worldToObject = math::inverse(objectToWorld);
    for (unsigned fixture = 0; fixture < 8; ++fixture)
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
                    std::array<float, 3> stored{g.points[v].position[0], g.points[v].position[1], g.points[v].position[2]};
                    if (fixture == 7)
                    {
                        const auto world = stored;
                        for (unsigned axis = 0; axis < 3; ++axis)
                            stored[axis] = world[0] * worldToObject.m[0][axis] + world[1] * worldToObject.m[1][axis] +
                                world[2] * worldToObject.m[2][axis] + worldToObject.m[3][axis];
                    }
                    std::memcpy(g.vertices.data() + v * g.draw.modelMeshView.vertexStride, stored.data(), 12);
                }
                if (fixture == 7) g.draw.worldMatrix = objectToWorld;
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
            // Stage contract: a consumer rejects a missing stage or a handle that is
            // not exactly the one its producer published, before declaring anything.
            const auto rejected = [](auto&& declare) {
                try { declare(); } catch (const std::runtime_error&) { return true; }
                return false;
            };
            const auto meteorGeometry = host.DeclareGeometry(*graph);
            Check(rejected([&] { host.DeclareOccluder(*graph, meteorGeometry, {}); }),
                  "Occluder rejects a missing first culling stage");
            const auto meteorFirstList = host.DeclareFirstCull(*graph, meteorGeometry);
            Check(rejected([&] { host.DeclareFinalCull(*graph, meteorGeometry, {}); }),
                  "Final culling rejects a missing occluder stage");
            Check(rejected([&] { host.DeclareOccluder(*graph, meteorGeometry, {}); }),
                  "Occluder rejects an unpublished first list");
            auto wrongFirstList = meteorFirstList;
            wrongFirstList.list = {};
            Check(rejected([&] { host.DeclareOccluder(*graph, meteorGeometry, wrongFirstList); }),
                  "Occluder rejects a missing first list before declaring commands");
            wrongFirstList.list = meteorFirstList.list;
            ++wrongFirstList.list.version;
            Check(rejected([&] { host.DeclareOccluder(*graph, meteorGeometry, wrongFirstList); }),
                  "Occluder rejects a different first-list version");
            const auto meteorOccluderDepth = host.DeclareOccluder(*graph, meteorGeometry, meteorFirstList);
            Check(rejected([&] { host.DeclareFinalCull(*graph, meteorGeometry, meteorFirstList.list); }),
                  "Final culling rejects a handle other than the occluder depth");
            const auto meteorFinalList = host.DeclareFinalCull(*graph, meteorGeometry, meteorOccluderDepth);
            auto wrongLayerList = meteorFinalList.blendedList;
            if (!wrongLayerList.IsValid()) wrongLayerList = meteorFirstList.list;
            ++wrongLayerList.version;
            Check(rejected([&] { host.DeclareLayerWork(*graph, wrongLayerList); }),
                "Layer work rejects a foreign list before producing commands");
            const auto meteorLayerWork = host.DeclareLayerWork(*graph, meteorFinalList.blendedList);
            Check(rejected([&] { host.DeclareVisibility(*graph, {meteorFirstList.list, {}}, targets.GetOutputs()); }),
                  "Visibility rejects a list other than the final culling list");
            auto wrongFinalList = meteorFinalList;
            ++wrongFinalList.list.version;
            Check(rejected([&] { host.DeclareVisibility(*graph, wrongFinalList, targets.GetOutputs()); }),
                  "Visibility rejects a different final-list version");
            wrongFinalList.list = {};
            Check(rejected([&] { host.DeclareVisibility(*graph, wrongFinalList, targets.GetOutputs()); }),
                  "Visibility rejects missing final visibility before declaring passes");
            const auto visibility = host.DeclareVisibility(*graph, meteorFinalList, targets.GetOutputs());
            auto wrongVisibility = visibility.visibility;
            ++wrongVisibility.version;
            Check(rejected([&] { host.DeclareMaterialWork(*graph, meteorGeometry, wrongVisibility); }),
                  "MaterialWork rejects an unpublished visibility version");
            Check(rejected([&] { host.DeclareSurfaceResolve(*graph, meteorGeometry, visibility, {}); }),
                  "SurfaceResolve cannot implicitly declare its missing material producer");
            const auto materialWork = host.DeclareMaterialWork(*graph, meteorGeometry, visibility.visibility);
            auto wrongMaterialWork = materialWork;
            ++wrongMaterialWork[0].version;
            Check(rejected([&] { host.DeclareSurfaceResolve(*graph, meteorGeometry, visibility, wrongMaterialWork); }),
                  "SurfaceResolve rejects unpublished material tile versions");
            auto wrongSurface = visibility;
            wrongSurface.visibility = wrongVisibility;
            Check(rejected([&] { host.DeclareSurfaceResolve(*graph, meteorGeometry, wrongSurface, materialWork); }),
                  "SurfaceResolve rejects an unpublished visibility version");
            wrongSurface = visibility;
            ++wrongSurface.depth.version;
            Check(rejected([&] { host.DeclareSurfaceResolve(*graph, meteorGeometry, wrongSurface, materialWork); }),
                  "SurfaceResolve rejects an unpublished depth version");
            const auto output = host.DeclareSurfaceResolve(*graph, meteorGeometry, visibility, materialWork);
            host.DeclareHistory(*graph, output.depth);
            const auto hybrid = host.HybridVisibility();
            const auto occluder = host.OccluderVisibility();
            const auto occluderDepth = meteorOccluderDepth;
            const auto table = host.FrameGeometry();
            Check(bool(hybrid), "Hybrid prepared owner");
            Check(occluder && occluder->depthOnly && occluderDepth.IsValid() && occluder->visible != hybrid->visible,
                  "Occluder stage owns its first list and depth-only resolve");
            const bool historyFirstStage = host.CameraDepthMeshletWork()->UsesHistory();
            historyFrames += historyFirstStage;
            std::array<RHIReadback, 6> readbacks;
            Check(
                device.CreateReadback(context.width, context.height, RHIFormat::RG32Uint, 1, readbacks[0], error) &&
                    device.CreateReadback(context.width, context.height, RHIFormat::D32Float, 1, readbacks[1], error) &&
                    device.CreateReadback(context.width, context.height, RHIFormat::R32Uint, 1, readbacks[2], error) &&
                    device.CreateBufferReadback(hybrid->KeyBytes(), readbacks[3], error) &&
                    device.CreateBufferReadback(hybrid->meshlets * 4, readbacks[4], error) &&
                    device.CreateReadback(context.width, context.height, RHIFormat::D32Float, 1, readbacks[5], error),
                "Hybrid readbacks " + error);
            graph->AddPass(
                "Probe.Hybrid.Readback",
                {{output.visibility, RHIResourceState::CopySource, RGAccessMode::Read},
                 {output.depth, RHIResourceState::CopySource, RGAccessMode::Read},
                 {output.bitmask, RHIResourceState::CopySource, RGAccessMode::Read},
                 {hybrid->graphKeys, RHIResourceState::CopySource, RGAccessMode::Read},
                 {hybrid->graphRoutes, RHIResourceState::CopySource, RGAccessMode::Read},
                 {occluderDepth, RHIResourceState::CopySource, RGAccessMode::Read}},
                [=](const auto& e) {
                    e.encoder->CopyToReadback(readbacks[0], e.ResolveHandle(output.visibility));
                    e.encoder->CopyToReadback(readbacks[1], e.ResolveHandle(output.depth));
                    e.encoder->CopyToReadback(readbacks[2], e.ResolveHandle(output.bitmask));
                    e.encoder->CopyBufferToReadback(readbacks[3], hybrid->keys);
                    e.encoder->CopyBufferToReadback(readbacks[4], hybrid->routes);
                    e.encoder->CopyToReadback(readbacks[5], e.ResolveHandle(occluderDepth));
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
            // Both stages produce indirect arguments before consuming them.
            // The HW/SW atomic siblings merge before resolve, and the occluder
            // resolve feeds the current HZB.
            const auto stage = [&](const std::string& prefix, const SceneHybridVisibility& owner) {
                using EdgeReason = EnhancedRenderGraph::DiagnosticSnapshot::VersionEdge::Reason;
                const auto& hw = find(prefix + ".HW");
                const auto& sw = find(prefix + ".SW");
                const auto& prepareClassify = find(prefix + ".PrepareClassify");
                const auto& classify = find(prefix + ".Classify");
                const auto& prepareSoftware = find(prefix + ".PrepareSoftware");
                const auto& resolve = find(prefix + ".Resolve");
                const auto producerConsumer = [&](const EnhancedRenderGraph::DiagnosticPass& producer,
                                                  const EnhancedRenderGraph::DiagnosticPass& consumer,
                                                  RGHandle resource, RGAccessMode producerAccess,
                                                  RHIResourceState consumerState) {
                    const auto write = std::ranges::find_if(producer.usages, [&](const auto& usage) {
                        return usage.resource == resource.index && usage.kind == RGResourceKind::Buffer &&
                               usage.access == producerAccess && usage.state == RHIResourceState::UnorderedAccess;
                    });
                    const auto read = std::ranges::find_if(consumer.usages, [&](const auto& usage) {
                        return usage.resource == resource.index && usage.kind == RGResourceKind::Buffer &&
                               usage.access == RGAccessMode::Read && usage.state == consumerState;
                    });
                    Check(write != producer.usages.end() && read != consumer.usages.end(),
                          producer.name + " publishes the declared buffer state consumed by " + consumer.name);
                    Check(!producer.culled && !consumer.culled && producer.compiledIndex >= 0 &&
                              producer.compiledIndex < consumer.compiledIndex && write->version == read->version,
                          producer.name + " publishes the exact version before " + consumer.name);
                    Check(std::ranges::any_of(snap.versionEdges,
                                              [&](const auto& edge) {
                                                  return edge.producer == producer.authoredIndex &&
                                                         edge.consumer == consumer.authoredIndex &&
                                                         edge.resource == resource.index &&
                                                         edge.version == read->version && edge.reason == EdgeReason::RAW;
                                              }),
                          producer.name + " has a resource dependency to " + consumer.name);
                    if (consumerState == RHIResourceState::IndirectArgument)
                    {
                        Check(std::ranges::any_of(consumer.barriers,
                                                  [&](const auto& barrier) {
                                                      return barrier.resource == resource.index && !barrier.afterPass &&
                                                             !barrier.uav &&
                                                             barrier.before == RHIResourceState::UnorderedAccess &&
                                                             barrier.after == RHIResourceState::IndirectArgument;
                                                  }),
                              consumer.name + " transitions generated arguments before indirect dispatch");
                    }
                    return read->version;
                };
                const auto classifyArguments =
                    producerConsumer(prepareClassify, classify, owner.graphDispatchArguments,
                                     RGAccessMode::Write, RHIResourceState::IndirectArgument);
                const auto softwareArguments =
                    producerConsumer(prepareSoftware, sw, owner.graphDispatchArguments,
                                     RGAccessMode::ReadWrite, RHIResourceState::IndirectArgument);
                Check(classifyArguments < softwareArguments && softwareArguments == owner.graphDispatchArguments.version,
                      prefix + " software dispatch consumes the updated argument version");
                producerConsumer(classify, prepareSoftware, owner.graphWork, RGAccessMode::ReadWrite,
                                 RHIResourceState::ShaderResource);
                producerConsumer(classify, sw, owner.graphWork, RGAccessMode::ReadWrite, RHIResourceState::ShaderResource);
                Check(std::ranges::any_of(snap.versionEdges,
                                          [&](const auto& edge) {
                                              return edge.producer == classify.authoredIndex &&
                                                     edge.consumer == prepareSoftware.authoredIndex &&
                                                     edge.resource == owner.graphDispatchArguments.index &&
                                                     edge.version == classifyArguments && edge.reason == EdgeReason::WAR;
                                          }),
                      prefix + " classification consumes arguments before the software producer overwrites them");
                for (const auto* writer : {&hw, &sw})
                {
                    Check(classify.compiledIndex < writer->compiledIndex && writer->compiledIndex < resolve.compiledIndex,
                          prefix + " classification and merge enclose both raster producers");
                    Check(std::ranges::any_of(writer->usages,
                                              [&](const auto& u) {
                                                  return u.resource == owner.graphKeys.index &&
                                                         u.access == RGAccessMode::AtomicMin64;
                                              }),
                          prefix + " producers declare the same atomic reduction");
                    Check(std::ranges::none_of(writer->usages,
                                               [](const auto& u) {
                                                   return u.state == RHIResourceState::RenderTarget ||
                                                          u.state == RHIResourceState::DepthWrite;
                                               }),
                          prefix + " has no raster attachment dependency");
                }
                Check(std::ranges::none_of(snap.versionEdges,
                                           [&](const auto& e) {
                                               return (e.producer == hw.authoredIndex && e.consumer == sw.authoredIndex) ||
                                                      (e.producer == sw.authoredIndex && e.consumer == hw.authoredIndex);
                                           }),
                      prefix + " sibling atomic writers have no false ordering edge");
                return resolve.compiledIndex;
            };
            Check(find("METEOR.Scene.Commands.Depth.Build").authoredIndex < find("METEOR.Scene.Occluder.HW").authoredIndex &&
                find("METEOR.Scene.Commands.Surface.Build").authoredIndex > find("Geometry.Occlusion.0").authoredIndex &&
                find("METEOR.Scene.Commands.Surface.Build").authoredIndex < find("METEOR.Scene.Visibility.HW").authoredIndex,
                "Depth/surface command producers stay with their consumer raster stages");
            const auto occluderResolve = stage("METEOR.Scene.Occluder", *occluder);
            const auto finalResolve = stage("METEOR.Scene.Visibility", *hybrid);
            Check(occluderResolve < find("Geometry.Occlusion.0").compiledIndex &&
                      find("Geometry.Occlusion.0").compiledIndex < find("METEOR.Scene.Visibility.Classify").compiledIndex,
                  "Occluder depth builds the HZB before final culling and classification");
            Check(std::ranges::none_of(snap.passes,
                                       [](const auto& p) { return p.name == "METEOR.Scene.OccluderDepth"; }),
                  "No separate depth-only occluder raster");
            Check(finalResolve < find("METEOR.Scene.Material.CountTiles").compiledIndex,
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
            std::array<RHIReadbackImage, 6> images;
            for (unsigned n = 0; n < 6; ++n)
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
                    uint32_t occluderBits;
                    std::memcpy(&occluderBits, images[5].data.data() + y * images[5].rowPitch + x * 4, 4);
                    // The first list is a subset of the final union: never nearer, and identical
                    // when no history narrows it (frustum-only first stage).
                    Check(std::bit_cast<float>(occluderBits) >= std::bit_cast<float>(depthBits) &&
                              (historyFirstStage || occluderBits == depthBits),
                          "Occluder depth is the first-stage HW/SW resolve pixel=" + std::to_string(x) + "," + std::to_string(y));
                    ++occluderPixels;
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
              << " cases=8 partitions=3 validation=0 hwPixels=" << hwPixels << " swPixels=" << swPixels
              << " occluderPixels=" << occluderPixels << " historyFrames=" << historyFrames << '\n';
}
