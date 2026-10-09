void RunCsmLegacyBatches(RecordingChangeDevice& device, ProbeRoots& roots, ProbePipelines& pipelines,
                         ProbeTextures& textures)
{
    std::string error;
    thread_pool recordingThreads;
    job_scheduler recordingJobs(recordingThreads);
    recordingJobs.start(8);
    ProbePool pool(recordingJobs);
    Check(pool.Initialize(device, 8, ProbeDevice::kFrameCount, error), "CSM eight-worker pool");
    const auto makeAlpha = [](const char* name, uint8_t alpha) {
        auto pixels = TextureImage::Allocate(RHIFormat::RGBA8Unorm, 1, 1, 1, 1, false);
        const uint8_t value[4]{255, 255, 255, alpha};
        std::memcpy(pixels.MutablePixelsAt(*pixels.Find(0, 0)), value, sizeof(value));
        return Texture::CreateSharedFromImage(name, std::move(pixels));
    };
    const auto alphaA = makeAlpha("CSM.Batch.Transparent", 0);
    const auto alphaB = makeAlpha("CSM.Batch.Opaque", 255);
    auto geometry = MakeGeometry(false, true);
    ShadowDecalPlane(geometry);
    geometry.draw.modelMeshView.indexData = geometry.indices.data();
    std::vector<EnhancedDrawItem> draws;
    for (unsigned group = 0; group < 32; ++group)
        for (unsigned instance = 0; instance < 4; ++instance)
        {
            auto draw = geometry.draw;
            draw.geometryKey = 21000 + group;
            draw.modelMeshView.handle.generation = 21000 + group;
            // Spatially distinct groups make a missing partition visible in readback.
            draw.worldMatrix.m[3][0] = (float(group % 8) - 3.5f) * 2.f;
            draw.worldMatrix.m[3][1] = (float(group / 8) - 1.5f) * 2.f;
            draw.baseColor = instance % 2 ? &*alphaB.borrow() : &*alphaA.borrow();
            draw.coverage.flags = EnhancedMaterialCoverage::Enabled | EnhancedMaterialCoverage::Masked
                | EnhancedMaterialCoverage::DoubleSided;
            draws.push_back(draw); // A/B/A/B must become exactly two complete groups.
        }
    FrameCameraSnapshot camera;
    camera.isOrthographic = true; camera.nearPlane = 0; camera.farPlane = 20;
    camera.projection = math::orthographic_off_center_lh(-10, 10, -10, 10, 0, 20);
    camera.inverseView = math::matrix4x4::identity(); camera.forward = {0, 0, 1};
    std::vector<EnhancedLight> lights(1);
    lights[0].direction = {0, 0, 1, 0}; lights[0].color.a = 1;
    ProbeMeshes meshes;
    Check(meshes.Initialize(&device, error), "CSM batch mesh cache");
    EnhancedFrameContext context;
    context.resources = &device; context.rootSignatures = &roots; context.psoManager = &pipelines;
    context.textureCache = &textures; context.meshCache = &meshes;
    context.camera = &camera; context.lights = &lights; context.shadowDraws = &draws;
    EnhancedShadowPass shadow;
    Check(shadow.Initialize(context, error), "CSM batch pipeline " + error);
    RHIReadbackImage reference;
    for (unsigned workers : {0u, 2u, 4u, 5u, 6u, 8u})
    {
        Drain drain{device};
        context.frameId = 12000 + workers;
        Check(device.BeginFrame(error), "CSM batch begin");
        textures.BeginFrame(context.frameId);
        meshes.BeginFrame(context.frameId);
        Check(shadow.PrepareFrame(context, error), "CSM batch prepare " + error);
        auto graph = std::make_shared<EnhancedRenderGraph>(device);
        if (workers)
        {
            pool.BeginFrame(static_cast<unsigned>(context.frameId));
            Check(graph->PrepareParallel(pool, error), "CSM batch parallel prefix");
        }
        shadow.Declare(*graph, context);
        const auto map = shadow.GetShadowMap();
        RHIReadback readback;
        Check(device.CreateReadback(2048, 2048, RHIFormat::D32Float, 3, readback, error), "CSM batch readback");
        graph->AddPass("CSM.Batch.Read", {{map, RHIResourceState::CopySource}}, [map, readback](const auto& execution) {
            for (unsigned layer = 0; layer < 3; ++layer)
                execution.encoder->CopyToReadback(readback, execution.ResolveHandle(map), layer, layer);
        }, true);
        SceneHost unused;
        SubmitShadowDecal(device, pool, graph, unused, context.frameId, workers, false);
        Check(shadow.GetLastDrawCount() == 384 && shadow.GetLastBatchCount() == 192,
              "CSM alpha groups submitted once per cascade without split duplication");
        RHIReadbackImage mapped;
        Check(device.MapReadback(readback, mapped, error), "CSM batch map");
        const auto units = graph->GetStats().recordUnits;
        if (workers)
            Check(units == (std::min)(workers, 6u) + 1,
                  "Actual shadow slice count plus one readback unit");
        if (workers >= 6)
            Check(units == 7, "Six actual shadow slices exercise two partitions per cascade");
        for (unsigned layer = 0; layer < 3; ++layer)
        {
            bool covered = false;
            for (unsigned y = 0; y < mapped.height; ++y)
            {
                const auto* row = mapped.data.data() + layer * mapped.sliceBytes + y * mapped.rowPitch;
                for (unsigned x = 0; x < mapped.width; ++x)
                    covered |= reinterpret_cast<const float*>(row)[x] < 1.f;
                if (workers)
                    Check(std::memcmp(row, reference.data.data() + layer * reference.sliceBytes
                        + y * reference.rowPitch, mapped.width * sizeof(float)) == 0,
                        "All serial/parallel shadow pixels are identical");
            }
            Check(covered, "CSM opaque alpha groups preserve caster depth");
        }
        if (workers == 0) reference = std::move(mapped);
        std::cout << "CSM_LEGACY_PARTITION_OK workers=" << workers
                  << " shadowSlices=" << (workers ? units - 1 : 1)
                  << " instances=384 batches=192\n";
        device.ReleaseReadback(readback);
    }
    pool.Shutdown();
    recordingJobs.shutdown();
    shadow.Shutdown();
    meshes.Shutdown();
    std::cout << "CSM_LEGACY_BATCH_OK instances=384 batches=192 modes=6\n";
}
