// Production shadow stages, independent rectangle depth oracle and lifecycle faults.
void RunShadowCorrectness(RecordingChangeDevice& device, ProbeRoots& roots, ProbePipelines& pipelines,
    ProbeTextures& textures, ProbePool& pool, const own::shared_owner<const Instance>& sourceMaterial)
{
    const auto material = SceneCooked(sourceMaterial);
    struct Case
    {
        unsigned view{}, extent{32};
        bool staticPresent{true}, reimport{}, reset{}, mutate{}, abort{}, reject{}, noPublish{}, budgetFailure{}, gpuPending{};
        float staticX{}, dynamicX{}, staticScale{2};
        int hit{-1}, history{-1};
        std::uint64_t budget{512ull << 20};
    };
    struct Result
    {
        SceneHost::ShadowCacheStats cache;
        std::array<unsigned, 3> first{}, final{};
        bool history{};
    };
    unsigned frames{}, pixels{}, recovered{}, rejectedMutations{}, aborts{}, rejections{}, budgetRejects{}, withheldChecks{}, gpuPendingChecks{};
    std::uint64_t nextFrame{41000};
    device.relayCompletions = true;
    for (unsigned workers : {0u, 4u})
    {
        SceneHost host;
        EnhancedFrameContext context;
        context.resources = &device;
        context.rootSignatures = &roots;
        context.psoManager = &pipelines;
        context.textureCache = &textures;
        context.width = context.height = 32;
        context.sceneEpoch = 41000 + workers;
        FrameCameraSnapshot camera;
        camera.view = camera.projection = math::matrix4x4::identity();
        camera.eyePosition = {0, 0, -2};
        context.camera = &camera;
        WaitSceneProgram(host, context, material->generation);
        const auto render = [&](const Case& c)
        {
            Drain drain{device};
            context.frameId = nextFrame++;
            std::cout << "METEOR_SHADOW_CASE frame=" << context.frameId << " workers=" << workers
                << " view=" << c.view << " x=" << c.staticX << " abort=" << c.abort
                << " reject=" << c.reject << std::endl;
            auto fixed = MakeGeometry(false, true);
            auto moving = MakeGeometry(false, true);
            ShadowDecalPlane(fixed);
            ShadowDecalPlane(moving);
            fixed.draw.modelMeshView.indexData = fixed.indices.data();
            moving.draw.modelMeshView.indexData = moving.indices.data();
            fixed.draw.geometryKey = 41001;
            fixed.draw.materialGraphSlot = 1;
            moving.draw.materialGraphSlot = 2;
            moving.draw.geometryKey = 41002;
            fixed.draw.coverage.flags |= EnhancedMaterialCoverage::DoubleSided;
            moving.draw.coverage.flags |= EnhancedMaterialCoverage::DoubleSided | EnhancedMaterialCoverage::Masked;
            moving.draw.coverage.cutoff = 0;
            fixed.draw.materialGraphInstance = moving.draw.materialGraphInstance = material;
            fixed.draw.worldMatrix.m[0][0] = c.staticScale;
            fixed.draw.worldMatrix.m[1][1] = 2;
            fixed.draw.worldMatrix.m[3][0] = c.staticX;
            fixed.draw.worldMatrix.m[3][2] = -.2f;
            moving.draw.worldMatrix.m[0][0] = moving.draw.worldMatrix.m[1][1] = .4f;
            moving.draw.worldMatrix.m[3][0] = c.dynamicX;
            moving.draw.worldMatrix.m[3][2] = .2f;
            if (c.reimport)
            {
                // Changed imported payload, not a revision-only cache test.
                for (unsigned vertex = 0; vertex < 6; ++vertex)
                {
                    const float z = .3f;
                    std::memcpy(fixed.vertices.data() + vertex * fixed.draw.modelMeshView.vertexStride + 8, &z, 4);
                }
            }
            std::vector<EnhancedDrawItem> draws;
            if (c.staticPresent)
            {
                draws.push_back(fixed.draw);
            }
            draws.push_back(moving.draw);
            SceneInputView view{context.frameId, context.sceneEpoch, 41001 + c.view, 1, 32, 32, camera};
            view.temporalFrame.realFrameId = context.frameId;
            view.temporalFrame.historyRevision = 1;
            view.temporalFrame.reset = c.reset;
            own::shared_owner<const SceneViewInput> input;
            std::string error;
            Check(SealScene(view, draws, {}, input, error), "Shadow correctness seal " + error);
            Check(device.BeginFrame(error), "Shadow correctness begin " + error);
            textures.BeginFrame(context.frameId);
            host.PollPrograms(context);
            Check(host.PrepareResidency(context, input, error), "Shadow correctness residency " + error);
            auto graph = std::make_shared<EnhancedRenderGraph>(device, RGSchedulingMode::ExplicitVersioned, RGOrderPolicy::DependencyOrder);
            if (workers)
            {
                pool.BeginFrame(unsigned(context.frameId));
                Check(graph->PrepareParallel(pool, error), "Shadow correctness parallel preparation " + error);
                graph->SetParallelRecordCostThreshold(0);
            }
            EnhancedShadowData shadow;
            shadow.enabled = true;
            shadow.mapResolution = c.extent;
            for (unsigned cascade = 0; cascade < 3; ++cascade)
            {
                shadow.lightViewProjection[cascade] = math::matrix4x4::identity();
                shadow.lightViewProjection[cascade].m[3][0] = float(cascade) * .0625f + float(c.view) * .125f;
                shadow.lightViewProjection[cascade].m[3][2] = float(cascade) * .05f;
            }
            SceneHostBudget budget;
            budget.shadowCacheBytes = c.budget;
            const bool prepared = host.Prepare(context, input, {}, {}, {}, shadow, budget, error, 0);
            if (!prepared)
            {
                Check(c.budgetFailure && error.find("Shadow static-cache") != std::string::npos,
                    "Only the injected shadow byte budget rejects preparation: " + error);
                ++budgetRejects;
                device.AbortFrame();
                return Result{};
            }
            Check(!c.budgetFailure, "Injected shadow byte budget must reject");
            Result result;
            result.cache = host.CurrentShadowCacheStats();
            Check(result.cache.staticCasters == unsigned(c.staticPresent) && result.cache.dynamicCasters == 1,
                "Fixture routes rigid opaque to cache and masked caster to dynamic raster");
            if (c.hit >= 0)
            {
                Check(result.cache.hit == bool(c.hit), "Expected cache hit/miss transition");
            }
            const auto geometry = host.DeclareGeometry(*graph);
            const auto cache = host.DeclareShadowStaticCache(*graph, geometry);
            const auto first = host.DeclareShadowFirstCull(*graph, geometry);
            result.history = host.ShadowFirstMeshletWork(0)->UsesHistory();
            if (c.history >= 0)
            {
                Check(result.history == bool(c.history), "Completed same-view history transition");
            }
            const auto occluder = host.DeclareShadowOccluder(*graph, geometry, cache, first);
            const auto final = host.DeclareShadowFinalCull(*graph, geometry, occluder);
            RGTextureDesc desc;
            desc.width = desc.height = c.extent;
            desc.arraySize = 3;
            desc.format = RHIFormat::D32Float;
            desc.allowDepthStencil = true;
            auto map = graph->Write(graph->CreateTexture(desc));
            graph->AddPass("Probe.ShadowCorrectness.Clear", {{map, RHIResourceState::DepthWrite, RGAccessMode::Write}},
                [map, &device](const auto& e)
                {
                    for (unsigned cascade = 0; cascade < 3; ++cascade)
                    {
                        const auto depth = RHIDepthTargetDesc::DepthSlice(e.ResolveHandle(map), RHIFormat::D32Float, cascade);
                        const auto target = device.CreateRenderTargets(std::span<const RHITextureHandle>{}, &depth);
                        e.encoder->BindRenderTargets(target);
                        e.encoder->ClearDepthTarget(target, 1);
                    }
                });
            map = host.DeclareShadowRaster(*graph, geometry, cache, final, map);
            host.DeclareShadowHistory(*graph, map);
            if (c.mutate)
            {
                // Actual GPU output omission in C1; independent CPU oracle remains unchanged.
                map = graph->Modify(map);
                graph->AddPass("Probe.ShadowCorrectness.OmitCascade", {{map, RHIResourceState::DepthWrite, RGAccessMode::ReadWrite}},
                    [map, &device](const auto& e)
                    {
                        const auto depth = RHIDepthTargetDesc::DepthSlice(e.ResolveHandle(map), RHIFormat::D32Float, 1);
                        const auto target = device.CreateRenderTargets(std::span<const RHITextureHandle>{}, &depth);
                        e.encoder->BindRenderTargets(target);
                        e.encoder->ClearDepthTarget(target, 1);
                    });
            }
            // Record the normal camera/lookup consumers too, so publication goes
            // through the product's complete-frame admission contract.
            const auto cameraFirst = host.DeclareFirstCull(*graph, geometry);
            const auto cameraDepth = host.DeclareOccluder(*graph, geometry, cameraFirst);
            const auto cameraFinal = host.DeclareFinalCull(*graph, geometry, cameraDepth);
            SceneSurfaceTargets surface;
            surface.Declare(*graph, context);
            const auto output = host.DeclareGBuffer(*graph, cameraFinal, surface.GetOutputs());
            host.DeclareHistory(*graph, output.depth);
            RGTextureDesc aoDesc;
            aoDesc.width = aoDesc.height = 32;
            aoDesc.format = RHIFormat::RG16Float;
            aoDesc.allowRenderTarget = true;
            const auto ao = graph->Write(graph->CreateTexture(aoDesc));
            graph->AddPass("Probe.ShadowCorrectness.AO", {{ao, RHIResourceState::RenderTarget, RGAccessMode::Write}},
                [ao, &device](const auto& e)
                {
                    const auto color = e.ResolveHandle(ao);
                    const auto target = device.CreateRenderTargets({&color, 1});
                    const float white[4]{1, 1, 1, 1};
                    e.encoder->BindRenderTargets(target);
                    e.encoder->ClearRenderTargets(target, white);
                });
            SceneColorInputs commands{host.DeclareLayerWork(*graph, cameraFinal.blendedList).layer, {}};
            commands.material = host.MaterialWork()->GraphInputs(*graph);
            host.DeclareColor(*graph, output, commands, SceneHost::DeclareHdrTarget(*graph, context), ao, map);
            if (c.abort || c.reject)
            {
                if (c.reject)
                {
                    Check(workers != 0, "Submission rejection uses a recorded batch");
                    Check(graph->Compile(error), "Reject fixture compile " + error);
                    RHIRecordedBatchDesc batchDesc;
                    batchDesc.frameId = context.frameId;
                    batchDesc.backendGeneration = GetRHISubmissionThread().GetOwnerGeneration(&device) + 1;
                    batchDesc.lifetimeToken = graph;
                    RHIRecordedBatch batch;
                    Check(graph->RecordParallel(pool, workers, batchDesc, batch, error), "Reject fixture record " + error);
                    RHISubmissionTicket ticket;
                    Check(!GetRHISubmissionThread().EnqueueRecordedBatch(&device, device, std::move(batch), ticket, error),
                        "Stale backend batch is rejected before native queue admission");
                    ++rejections;
                }
                else
                {
                    ++aborts;
                }
                device.AbortFrame();
                Check(!host.PublishSubmittedCache(context.frameId, {1}, error), "Aborted/rejected cache cannot publish");
                return result;
            }
            RHIReadback depthRead;
            Check(device.CreateReadback(c.extent, c.extent, RHIFormat::D32Float, 3, depthRead, error), "Shadow correctness depth readback");
            graph->AddPass("Probe.ShadowCorrectness.Read", {{map, RHIResourceState::CopySource, RGAccessMode::Read}},
                [map, depthRead](const auto& e)
                {
                    for (unsigned cascade = 0; cascade < 3; ++cascade)
                    {
                        e.encoder->CopyToReadback(depthRead, e.ResolveHandle(map), cascade, cascade);
                    }
                }, true);
            std::array<RHIReadback, 6> lists;
            const auto shadowWork = host.ShadowMeshletOutputs();
            const unsigned dynamicBin = c.staticPresent ? 1 : 0;
            for (unsigned cascade = 0; cascade < 3; ++cascade)
            {
                const auto last = std::ranges::find(shadowWork, cascade, &SceneHost::ShadowMeshletOutput::cascade);
                Check(last != shadowWork.end(), "Final light list exists");
                const std::array owners{host.ShadowFirstMeshletWork(cascade), last->frame};
                for (unsigned stage = 0; stage < 2; ++stage)
                {
                    const auto& work = owners[stage];
                    const auto slot = cascade * 2 + stage;
                    Check(work->MeshletGeometry(dynamicBin)->ObjectToWorld().m[3][2] == .2f, "Dynamic list bin identity");
                    Check(device.CreateBufferReadback(4, lists[slot], error), "Shadow correctness count readback");
                    graph->AddPass("Probe.ShadowCorrectness.List", {{work->GraphVisibleIds(*graph), RHIResourceState::CopySource, RGAccessMode::Read}},
                        [work, r = lists[slot], dynamicBin](const auto& e)
                        {
                            e.encoder->CopyBufferToReadback(r, work->VisibleIdBuffer(), work->MeshletOutputOffset(dynamicBin) * 4ull, 4);
                        }, true);
                }
            }
            std::string notSubmitted;
            Check(!host.PublishSubmittedCache(context.frameId, {1}, notSubmitted), "Unsubmitted cache cannot publish");
#ifndef LX_PROBE_VULKAN
            if (c.gpuPending)
            {
                Check(workers != 0, "GPU pending fixture uses recorded batch ownership");
                Check(graph->Compile(error), "GPU pending graph compile " + error);
                RHIRecordedBatchDesc batchDesc;
                batchDesc.frameId = context.frameId;
                batchDesc.backendGeneration = GetRHISubmissionThread().GetOwnerGeneration(&device);
                batchDesc.lifetimeToken = graph;
                RHIRecordedBatch batch;
                Check(graph->RecordParallel(pool, workers, batchDesc, batch, error), "GPU pending record " + error);
                Microsoft::WRL::ComPtr<ID3D12Fence> gate;
                Check(SUCCEEDED(device.GetDevice()->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&gate))), "GPU pending gate fence");
                struct ReleaseGate
                {
                    ID3D12Fence* fence;
                    ~ReleaseGate() { fence->Signal(1); }
                } release{gate.Get()};
                Check(GetRHISubmissionThread().ExecuteAndWait(&device, "shadow correctness GPU gate",
                    [queue = device.GetCommandQueue(), gate](std::string&) { return SUCCEEDED(queue->Wait(gate.Get(), 1)); }, error), "GPU queue gate admission " + error);
                RHISubmissionTicket ticket;
                Check(GetRHISubmissionThread().EnqueueRecordedBatch(&device, device, std::move(batch), ticket, error), "GPU pending enqueue " + error);
                const auto completion = ticket.GetRecordedBatch()->GetCompletionPoint();
                Check(device.EndFrame(error) && GetRHISubmissionThread().DrainSubmissions(&device, error), "GPU pending CPU submission " + error);
                Check(device.GetCompletedFenceValue() < completion.value, "Native GPU fence is genuinely incomplete");
                Check(host.PublishSubmittedCache(context.frameId, completion, error, ticket), "Accepted pending cache publication " + error);
                // Prepare a newer recording while GPU work still owns the prior
                // graph. The last completed matching cache/history must survive.
                auto probeView = view;
                context.frameId = probeView.frameId = nextFrame++;
                probeView.temporalFrame.realFrameId = context.frameId;
                auto probeDraws = draws;
                probeDraws[0].worldMatrix.m[3][0] = .4f;
                own::shared_owner<const SceneViewInput> probeInput;
                Check(SealScene(probeView, probeDraws, {}, probeInput, error), "GPU overlap seal " + error);
                Check(device.BeginFrame(error), "GPU overlap begin " + error);
                textures.BeginFrame(context.frameId);
                host.PollPrograms(context);
                Check(host.PrepareResidency(context, probeInput, error), "GPU overlap residency " + error);
                Check(host.Prepare(context, probeInput, {}, {}, {}, shadow, budget, error, 0), "GPU overlap prepare " + error);
                const auto matching = host.CurrentShadowCacheStats();
                Check(matching.hit && matching.sourceFrame < result.cache.sourceFrame, "Native pending GPU preserves older completed cache");
                Check(host.ShadowFirstMeshletWork(0)->UsesHistory(), "Native pending GPU preserves completed same-view history");
                Check(device.GetCompletedFenceValue() < completion.value, "Second recording was prepared before first GPU completion");
                device.AbortFrame();
                Check(SUCCEEDED(gate->Signal(1)), "Release pending GPU work");
                device.WaitForGpu();
                Check(GetRHISubmissionThread().Drain(&device, error), "GPU overlap retirement " + error);
                std::string messages;
                Check(device.DrainDebugMessages(messages) == 0, "GPU overlap validation " + messages);
                ++gpuPendingChecks;
            }
            else
#endif
            {
                SubmitShadowDecal(device, pool, graph, host, context.frameId, workers, !c.mutate && !c.noPublish);
            }
            RHIReadbackImage depths;
            Check(device.MapReadback(depthRead, depths, error), "Shadow correctness depth map " + error);
            unsigned mismatches{};
            for (unsigned cascade = 0; cascade < 3; ++cascade)
            {
                for (unsigned stage = 0; stage < 2; ++stage)
                {
                    RHIReadbackImage count;
                    const auto slot = cascade * 2 + stage;
                    Check(device.MapReadback(lists[slot], count, error), "Shadow count map");
                    (stage ? result.final[cascade] : result.first[cascade]) = count.Elements<std::uint32_t>()[0];
                    device.ReleaseReadback(lists[slot]);
                }
                const float lightX = float(cascade) * .0625f + float(c.view) * .125f;
                for (unsigned y = 0; y < c.extent; ++y)
                {
                    for (unsigned x = 0; x < c.extent; ++x)
                    {
                        const float nx = (x + .5f) * 2 / c.extent - 1 - lightX;
                        const float ny = 1 - (y + .5f) * 2 / c.extent;
                        const float halfStatic = .75f * std::abs(c.staticScale);
                        if (std::abs(std::abs(nx - c.staticX) - halfStatic) < .025f ||
                            std::abs(std::abs(nx - c.dynamicX) - .3f) < .025f ||
                            std::abs(std::abs(ny) - 1.5f) < .025f || std::abs(std::abs(ny) - .3f) < .025f)
                        {
                            continue;
                        }
                        float expected = 1;
                        if (c.staticPresent && std::abs(nx - c.staticX) < halfStatic && std::abs(ny) < 1.5f)
                        {
                            expected = (c.reimport ? .1f : .2f) + cascade * .05f;
                        }
                        if (std::abs(nx - c.dynamicX) < .3f && std::abs(ny) < .3f)
                        {
                            expected = (std::min)(expected, .6f + cascade * .05f);
                        }
                        if (std::abs(depths.At(x, y, 0, cascade) - expected) > .00003f)
                        {
                            ++mismatches;
                        }
                        ++pixels;
                    }
                }
            }
            device.ReleaseReadback(depthRead);
            if (c.mutate)
            {
                Check(mismatches > 20, "Independent depth oracle rejects actual GPU cascade omission");
                ++rejectedMutations;
            }
            else
            {
                Check(mismatches == 0, "Independent three-cascade rectangle/min-depth oracle mismatches=" + std::to_string(mismatches));
            }
            ++frames;
            return result;
        };
        // Alternating views must preserve independent cache and history owners.
        for (unsigned step = 0; step < 12; ++step)
        {
            for (unsigned view = 0; view < 2; ++view)
            {
                Case c;
                c.view = view;
                c.staticX = step == 2 || step == 3 ? 3.f : 0;
                c.staticPresent = step != 4;
                c.reimport = step == 6 || step == 7;
                c.reset = step == 8;
                c.staticScale = step == 9 ? -2.f : 2.f;
                c.extent = step >= 10 ? 16 : 32;
                c.hit = step == 1 || step == 3 || step == 7 || step == 11;
                c.history = step != 0 && step != 8 && step != 10;
                const auto result = render(c);
                if (step == 1 || step == 2)
                {
                    Check(std::ranges::all_of(result.first, [](unsigned count) { return count == 0; }), "Previous static depth hides dynamic caster in first stage");
                }
                if (step == 2)
                {
                    Check(std::ranges::all_of(result.final, [](unsigned count) { return count != 0; }), "Current depth recovers newly exposed dynamic caster in all cascades");
                    ++recovered;
                }
            }
        }
        Case c;
        c.staticX = 3.f;
        c.mutate = true;
        render(c);
        c.mutate = false;
        render(c); // Normal control after omission; mutated history was not published.
        c.staticX = .1f;
        c.abort = true;
        c.hit = 0;
        render(c);
        c.abort = false;
        render(c); // Same key was never admitted, therefore it remains cold.
        if (workers)
        {
            c.staticX = .2f;
            c.reject = true;
            render(c);
            c.reject = false;
            render(c);
        }
        // Deterministic completion-notification withholding: real GPU work runs,
        // but future cache/history owners remain unavailable to SceneHost.
        device.completionCeiling = device.GetCompletedFenceValue();
        c.staticX = .4f;
        c.hit = 0;
        const auto pending = render(c);
        c.staticX = workers ? .2f : .1f;
        c.hit = 1;
        const auto old = render(c);
        Check(old.cache.sourceFrame < pending.cache.sourceFrame, "Newer uncompleted cache does not evict completed matching cache");
        c.staticX = .4f;
        c.hit = 0;
        render(c);
        ++withheldChecks;
        device.ReleaseCompletionGate();
#ifndef LX_PROBE_VULKAN
        if (workers)
        {
            c.staticX = .5f;
            c.hit = 0;
            c.gpuPending = true;
            render(c);
            c.gpuPending = false;
            c.hit = 1;
            render(c); // Newly completed pending cache is now eligible.
        }
#endif
        // Exact per-cache budget is tested on a fresh host, avoiding hidden
        // resident entries; retained/in-flight sum is checked below separately.
        host.ShutdownAfterIdle();
        WaitSceneProgram(host, context, material->generation);
        c = {};
        c.budget = 12ull * 32 * 32 - 1;
        c.budgetFailure = true;
        render(c);
        c.budget += 1;
        c.budgetFailure = false;
        c.hit = 0;
        render(c);
        c.staticX = .2f;
        c.budget = 2 * 12ull * 32 * 32 - 1;
        c.budgetFailure = true;
        render(c);
        ++c.budget;
        c.budgetFailure = false;
        render(c); // Exact old-owner + replacement budget succeeds.
        host.ShutdownAfterIdle();
    }
    device.ReleaseCompletionGate();
    device.relayCompletions = false;
    Check(device.completionRelays.empty(), "Every shadow relay listener unregistered after shutdown");
    Check(recovered == 4 && rejectedMutations == 2 && aborts == 2 && rejections == 1 && budgetRejects == 4 && withheldChecks == 2 && gpuPendingChecks == 1,
        "Shadow correctness acceptance case coverage");
    std::cout << "METEOR_SHADOW_CORRECTNESS_OK frames=" << frames << " views=2 recovered=" << recovered
        << " gpuOmissionRejected=" << rejectedMutations << " aborts=" << aborts << " submissionRejections=" << rejections
        << " budgetRejections=" << budgetRejects << " withheldCompletionCases=" << withheldChecks << " nativeGpuPendingCases=" << gpuPendingChecks << " pixels=" << pixels << " validation=0\n";
}
