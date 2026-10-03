void RunCsmLegacyBatches(RecordingChangeDevice& device, ProbeRoots& roots, ProbePipelines& pipelines,
                         ProbeTextures& textures, ProbePool& pool)
{
    std::string error;
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
            draw.baseColor = instance % 2 ? alphaB.get() : alphaA.get();
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
    std::array<float, 3> reference{};
    for (unsigned workers : {0u, 4u})
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
        for (unsigned layer = 0; layer < 3; ++layer)
        {
            const float value = mapped.At(1024, 1024, 0, layer);
            Check(value < 1.f, "CSM opaque alpha batch preserved caster depth");
            if (workers == 0) reference[layer] = value;
            else Near(value, reference[layer], "CSM serial/parallel depth equivalence");
        }
        device.ReleaseReadback(readback);
    }
    shadow.Shutdown();
    meshes.Shutdown();
    std::cout << "CSM_LEGACY_BATCH_OK instances=384 batches=192 modes=2\n";
}
