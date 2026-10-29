// Fixed visible load, variable CPU working set. Runs the production SceneHost,
// graph, software recording and asynchronous RHI submission with pixel readback.
void RunGeometryPreparationScale(RecordingChangeDevice& device, ProbeRoots& roots, ProbePipelines& pipelines,
                                 ProbeTextures& textures, ProbePool& pool, job_scheduler& recordingJobs,
                                 const own::shared_owner<const Instance>& sourceMaterial)
{
    const ce::render::scheduling_scope scheduling{ce::render::thread_role::render};
    std::cout << "GEOMETRY_SCALE_SCHEDULING active=" << scheduling.active() << " error=" << scheduling.error() << '\n';
    const auto material = SceneCooked(sourceMaterial);
    const auto readOption = [](const char* name, unsigned fallback)
    {
        const auto* text = std::getenv(name);
        return text ? unsigned(std::stoul(text)) : fallback;
    };
    const auto count = readOption("CREATOR_GEOMETRY_SCALE_DRAWS", 1024);
    const auto unique = readOption("CREATOR_GEOMETRY_SCALE_UNIQUE", 1);
    const auto frames = readOption("CREATOR_GEOMETRY_SCALE_FRAMES", 32);
    const auto recording = readOption("CREATOR_GEOMETRY_SCALE_RECORD", 1) != 0;
    const auto* gatePath = std::getenv("CREATOR_GEOMETRY_SCALE_GATE");
    const auto* capturePath = std::getenv("CREATOR_GEOMETRY_SCALE_CAPTURE");
    Check(count > 0 && count <= 4096 && unique > 0 && unique <= count && frames >= 4 && frames <= 256,
          "Bounded geometry scale fixture");
    std::string error;
    std::vector<Geometry> meshes;
    meshes.reserve(unique);
    for (unsigned index = 0; index < unique; ++index)
    {
        meshes.push_back(MakeGeometry(false, true));
        auto& geometry = meshes.back();
        ShadowDecalPlane(geometry);
        geometry.draw.modelMeshView.indexData = geometry.indices.data();
        // Distinct immutable mesh identities, identical authored visible shape.
        char meshId[64]{};
        std::snprintf(meshId, sizeof(meshId), "22222222-2222-8222-8222-%012x", index + 1);
        Check(uuid::try_parse(meshId, geometry.draw.modelMeshView.handle.meshId), "Scale mesh identity");
        geometry.draw.modelMeshView.handle.generation = 900000 + index;
        Check(cookedMeshes.Resolve(geometry.draw.modelMeshView, geometry.draw.modelMeshView, error),
              "Scale fixture cook " + error);
    }
    ce::dynamic_array<EnhancedDrawItem> draws(count);
    for (unsigned index = 0; index < count; ++index)
    {
        draws[index] = meshes[index % unique].draw;
        auto& draw = draws[index];
        draw.materialGraphInstance = material;
        draw.materialGraphSlot = 1;
        draw.geometryKey = 900000 + index % unique;
        draw.temporalObjectId = 1000000 + index;
        draw.temporalIncarnation = 1;
        draw.coverage.flags = EnhancedMaterialCoverage::Enabled | EnhancedMaterialCoverage::DoubleSided;
        // Keep exactly one visible instance; GPU raster load stays fixed.
        draw.worldMatrix = math::matrix4x4::identity();
        draw.worldMatrix.m[3][0] = index ? 10.f + index : 0.f;
    }
    auto& profiler = ce::profiler();
    profiler.initialize();
    profiler.register_thread("[RenderThread]", ce::track_kind::command_thread);
    {
        SceneHost host;
        EnhancedFrameContext context;
        context.resources = &device;
        context.rootSignatures = &roots;
        context.psoManager = &pipelines;
        context.textureCache = &textures;
        context.width = context.height = 32;
        context.sceneEpoch = 940;
        FrameCameraSnapshot camera;
        camera.view = camera.projection = math::matrix4x4::identity();
        camera.eyePosition = {0, 0, -2};
        context.camera = &camera;
        WaitSceneProgram(host, context, material->generation);
        own::shared_owner<const SceneViewInput> previous;
        const auto qpc = []
        {
            LARGE_INTEGER value;
            QueryPerformanceCounter(&value);
            return value.QuadPart;
        };
        const auto tick = [] { return std::chrono::steady_clock::now(); };
        const auto milliseconds = [](auto begin, auto end)
        { return std::chrono::duration<double, std::milli>(end - begin).count(); };
        for (unsigned step = 0; step < frames + 8; ++step)
        {
            if (step == 8 && gatePath)
            {
                std::ofstream(std::string(gatePath) + ".ready") << GetCurrentProcessId();
                const auto deadline = tick() + std::chrono::seconds(120);
                while (!std::filesystem::exists(std::string(gatePath) + ".go") && tick() < deadline)
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
                Check(std::filesystem::exists(std::string(gatePath) + ".go"), "Scale collection gate");
            }
            if (step == 8 && recording)
            {
                profiler.record(10000 + step);
                const auto deadline = tick() + std::chrono::seconds(10);
                while (profiler.state() != ce::recorder_state::recording && tick() < deadline)
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                Check(profiler.state() == ce::recorder_state::recording, "Scale recording starts");
            }
            const auto startQpc = qpc();
            const auto start = tick();
            context.frameId = 10000 + step;
            {
                ce::profile_scope frameScope{ce::marker<"GeometryScaleFrame">()};
                SceneInputView view{context.frameId, context.sceneEpoch, 941, 1, 32, 32, camera};
                view.temporalFrame.reset = step == 0;
                // Alternate one world to exercise both current and previous inputs.
                draws.back().worldMatrix.m[3][1] = step % 2 ? .01f : 0.f;
                own::shared_owner<const SceneViewInput> input;
                Check(SealScene(view, draws, {}, input, error, previous ? &*previous : nullptr), "Scale seal " + error);
                const auto sealed = tick();
                const auto sealedQpc = qpc();
                Check(device.BeginFrame(error), "Scale begin " + error);
                textures.BeginFrame(context.frameId);
                Check(host.PrepareResidency(context, input, error), "Scale residency " + error);
                auto graph = own::make_shared<EnhancedRenderGraph>(device, RGSchedulingMode::ExplicitVersioned,
                                                                   RGOrderPolicy::DependencyOrder);
                pool.BeginFrame(unsigned(context.frameId));
                Check(graph->PrepareParallel(pool, error), "Scale parallel " + error);
                const auto prepareStartQpc = qpc();
                const auto prepareStart = tick();
                Check(host.Prepare(context, input, {}, {}, {}, {}, {}, error, 0), "Scale prepare " + error);
                const auto prepared = tick();
                const auto preparedQpc = qpc();
                const auto table = host.FrameGeometry();
                Check(table && table->geometry.size() == count + 1, "Every scale draw has a geometry record");
                if (unique == 1)
                {
                    for (unsigned index = 2; index <= count; ++index)
                    {
                        Check(table->geometry[index]->Buffer().id == table->geometry[1]->Buffer().id,
                              "Repeated rigid mesh shares the immutable object-space output");
                    }
                }
                const auto geometry = host.DeclareGeometry(*graph);
                const auto first = host.DeclareFirstCull(*graph, geometry);
                const auto occluder = host.DeclareOccluder(*graph, geometry, first);
                const auto final = host.DeclareFinalCull(*graph, geometry, occluder);
                SceneSurfaceTargets surface;
                surface.Declare(*graph, context);
                const auto output = host.DeclareGBuffer(*graph, final, surface.GetOutputs());
                RGTextureDesc aoDescription;
                aoDescription.width = aoDescription.height = 32;
                aoDescription.format = RHIFormat::RG16Float;
                aoDescription.allowRenderTarget = true;
                std::fill(std::begin(aoDescription.clearColor), std::end(aoDescription.clearColor), 1.f);
                const auto ao = graph->Write(graph->CreateTexture(aoDescription));
                graph->AddPass("Probe.Scale.AO", {{ao, RHIResourceState::RenderTarget, RGAccessMode::Write}},
                               [ao, &device](const auto& execution)
                               {
                                   const auto color = execution.ResolveHandle(ao);
                                   const auto target = device.CreateRenderTargets({&color, 1});
                                   const float white[4]{1, 1, 1, 1};
                                   execution.encoder->BindRenderTargets(target);
                                   execution.encoder->ClearRenderTargets(target, white);
                               });
                SceneColorInputs commands{host.DeclareLayerWork(*graph, final.blendedList).layer, {}};
                commands.material = host.MaterialWork()->GraphInputs(*graph);
                const auto lit =
                    host.DeclareColor(*graph, output, commands, SceneHost::DeclareHdrTarget(*graph, context), ao, {});
                RHIReadback colorReadback;
                Check(device.CreateReadback(32, 32, RHIFormat::RGBA16Float, 1, colorReadback, error),
                      "Scale color readback");
                graph->AddPass(
                    "Probe.Scale.Color", {{lit, RHIResourceState::CopySource, RGAccessMode::Read}},
                    [lit, colorReadback](const auto& execution)
                    { execution.encoder->CopyToReadback(colorReadback, execution.ResolveHandle(lit)); }, true);
                RHIReadback readback;
                Check(device.CreateReadback(32, 32, RHIFormat::D32Float, 1, readback, error), "Scale depth readback");
                graph->AddPass(
                    "Probe.Scale.Depth", {{output.depth, RHIResourceState::CopySource, RGAccessMode::Read}},
                    [handle = output.depth, readback](const auto& execution)
                    { execution.encoder->CopyToReadback(readback, execution.ResolveHandle(handle)); }, true);
                const auto declared = tick();
                SubmitShadowDecal(device, pool, graph, host, context.frameId, 4, true);
                const auto submitted = tick();
                RHIReadbackImage image;
                Check(device.MapReadback(readback, image, error), "Scale map " + error);
                for (unsigned y = 0; y < 32; ++y)
                {
                    for (unsigned x = 0; x < 32; ++x)
                    {
                        Near(image.At(x, y, 0), x >= 4 && x < 28 && y >= 4 && y < 28 ? .4 : 1.,
                             "Scale authored plane depth", 3e-5);
                    }
                }
                RHIReadbackImage colorImage;
                Check(device.MapReadback(colorReadback, colorImage, error), "Scale color map " + error);
                for (unsigned channel = 0; channel < 4; ++channel)
                {
                    Check(std::isfinite(colorImage.At(16, 16, channel)), "Scale finite visible HDR");
                }
                device.ReleaseReadback(colorReadback);
                device.ReleaseReadback(readback);
                previous = std::move(input);
                if (step >= 8)
                {
                    std::cout << "GEOMETRY_SCALE_FRAME {\"step\":" << step - 8 << ",\"draws\":" << count
                              << ",\"unique\":" << unique << ",\"recording\":" << recording
                              << ",\"beginQpc\":" << startQpc << ",\"endQpc\":" << qpc()
                              << ",\"sealEndQpc\":" << sealedQpc
                              << ",\"prepareBeginQpc\":" << prepareStartQpc << ",\"prepareEndQpc\":" << preparedQpc
                              << ",\"sealMs\":" << milliseconds(start, sealed)
                              << ",\"prepareMs\":" << milliseconds(prepareStart, prepared)
                              << ",\"declareMs\":" << milliseconds(prepared, declared)
                              << ",\"submitWaitMs\":" << milliseconds(declared, submitted)
                              << ",\"frameMs\":" << milliseconds(start, tick()) << "}\n";
                }
            }
            profiler.publish_frame(unsigned(context.frameId));
        }
    }
    if (recording)
    {
        profiler.pause();
    }
    // Match the Editor's worker lifecycle hooks and drain every producer tail.
    recordingJobs.shutdown();
    ce::get_job_scheduler().shutdown();
    RHISubmissionTicket tail;
    Check(GetRHISubmissionThread().Enqueue(
              &device, "ScaleCaptureTail",
              [](std::string&)
              {
                  ce::profiler().publish_thread();
                  ce::profiler().unregister_thread();
                  return true;
              },
              tail, error),
          "Scale RHI tail");
    Check(GetRHISubmissionThread().Drain(&device, error), "Scale RHI tail drain " + error);
    if (recording)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (profiler.state() != ce::recorder_state::frozen && std::chrono::steady_clock::now() < deadline)
        {
            profiler.publish_thread();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        const auto capture = profiler.capture();
        Check(bool(capture), "Scale native capture exists");
        const auto stats = profiler.summary();
        std::cout << "GEOMETRY_SCALE_CAPTURE complete=" << capture->complete()
                  << " unacked=" << stats.pause_unacked_streams << " dropped=" << stats.dropped_events
                  << " late=" << stats.late_events_dropped << " lateSpans=" << stats.late_spans_dropped
                  << " unbalanced=" << stats.unbalanced_scopes << " open=" << stats.pause_open_scopes << "\n";
        if (capturePath)
        {
            Check(bool(ce::save_capture(*capture, std::filesystem::path(capturePath))), "Scale save native capture");
        }
        Check(capture->complete(), "Scale complete native capture");
    }
    profiler.unregister_thread();
    profiler.shutdown();
    std::cout << "GEOMETRY_SCALE_OK draws=" << count << " unique=" << unique << " frames=" << frames
              << " pixels=" << (frames + 8) * 1024 << " validation=0\n";
}
