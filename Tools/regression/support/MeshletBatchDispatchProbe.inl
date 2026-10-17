// Included inside the native material probe namespace. Exercise the production
// reset/cull/finalize shaders with distinct SRV slices and independently known
// visibility, including the last descriptor and a second dispatch row.
void RunMeshletBatchDispatch(RecordingChangeDevice& device, ProbeRoots& roots, ProbePipelines& pipelines)
{
    std::string error;
    const RHIPipelineLayoutParam params[]{RHILayout::Cbv(0), RHILayout::Srv(0), RHILayout::Srv(1),
        RHILayout::UavBufferTable(2, 0), RHILayout::SrvBufferTable(4096, 18), RHILayout::SrvBufferTable(4096, 4114),
        RHILayout::Srv(8211)};
    const auto layout = roots.GetOrCreate({params, {}}, error);
    Check(layout.IsValid(), "Batch meshlet layout " + error);
    std::array<LX::Runtime::ComputePipeline, 3> kernels;
    const char* entries[]{"ShadowMeshletResetCS", "ShadowMeshletCullCS", "MeshletFinalizeCS"};
    for (unsigned i = 0; i < kernels.size(); ++i) {
        LX::Runtime::CompiledCompute shader;
        RHIShaderCompileOptions options; options.strictMath = true;
        Check(LX::Runtime::CompileCompute(RHIShaderSource::Resolve("GeometryVisibility.slang").string(),
            entries[i], {}, options, shader, error), "Batch meshlet shader " + error);
        RHIComputePipelineDesc desc; desc.layout = layout;
        desc.csBytecode = shader.stage.bytecode.Data(); desc.csSize = shader.stage.bytecode.Size();
        Check(kernels[i].Create(pipelines, desc, std::move(shader.description), error), "Batch meshlet PSO " + error);
    }
    LX::Runtime::ComputePipeline partialCull;
    {
        RHIShaderPermutation permutation;
        Check(permutation.Enable("GEOMETRY_VISIBILITY_QUEUE_PARTIAL_WAVE", error), "Partial-wave permutation");
        LX::Runtime::CompiledCompute shader;
        RHIShaderCompileOptions options;
        options.strictMath = true;
        Check(LX::Runtime::CompileCompute(RHIShaderSource::Resolve("GeometryVisibility.slang").string(),
            "ShadowMeshletCullCS", permutation, options, shader, error), "Partial-wave shader " + error);
        RHIComputePipelineDesc desc;
        desc.layout = layout;
        desc.csBytecode = shader.stage.bytecode.Data();
        desc.csSize = shader.stage.bytecode.Size();
        Check(partialCull.Create(pipelines, desc, std::move(shader.description), error), "Partial-wave PSO " + error);
    }
    std::array<LX::Runtime::ComputePipeline, 3> stressCull;
    for (unsigned i = 0; i < stressCull.size(); ++i)
    {
        RHIShaderPermutation permutation;
        Check(permutation.Enable("GEOMETRY_VISIBILITY_RING_STRESS", error), "Ring stress permutation");
        if (i == 1)
        {
            Check(permutation.Enable("GEOMETRY_VISIBILITY_QUEUE_PARTIAL_WAVE", error), "Partial ring stress permutation");
        }
        LX::Runtime::CompiledCompute shader;
        RHIShaderCompileOptions options;
        options.strictMath = true;
        Check(LX::Runtime::CompileCompute(RHIShaderSource::Resolve("GeometryVisibility.slang").string(),
            i == 2 ? "ShadowMeshletResetCS" : "ShadowMeshletCullCS", permutation, options, shader, error), "Ring stress shader " + error);
        RHIComputePipelineDesc desc;
        desc.layout = layout;
        desc.csBytecode = shader.stage.bytecode.Data();
        desc.csSize = shader.stage.bytecode.Size();
        Check(stressCull[i].Create(pipelines, desc, std::move(shader.description), error), "Ring stress PSO " + error);
    }
    std::uint32_t checkedWords{};
    constexpr std::uint64_t exactOutputWords = (32ull << 20) / 4 - 5 - GpuGeometryVisibility::kWorkRingHeaderWords - GpuGeometryVisibility::kWorkRingSlots;
    Check(GpuGeometryVisibility::MeshletQueueStorageWords(exactOutputWords, 1, 1, checkedWords),
        "Queue and arguments exactly fit their byte budget");
    Check(!GpuGeometryVisibility::MeshletQueueStorageWords(exactOutputWords + 1, 1, 1, checkedWords) && checkedWords == 0,
        "One word beyond the queue budget is rejected");
    Check(!GpuGeometryVisibility::MeshletQueueStorageWords(1, UINT64_MAX, 1, checkedWords) &&
        !GpuGeometryVisibility::MeshletQueueStorageWords(UINT64_MAX, 1, 1, checkedWords) &&
        !GpuGeometryVisibility::MeshletQueueStorageWords(1, 1, 4097, checkedWords),
        "Overflow and bin-capacity checks precede queue allocation");
    unsigned queueCases{};
    for (const unsigned route : {0u, 1u, 2u, 3u, 4u, 5u, 6u})
    {
        for (const uint32_t fixtureBins : {0u, 1u, 2u, 3u, 4u, 5u, 6u, 4096u})
        {
            const uint32_t binCount = (std::max)(1u, fixtureBins);
            const bool dynamic = route != 0;
            const bool ordered = route >= 3;
            Check(device.BeginFrame(error), "Batch meshlet begin");
            const auto upload = [&](const void* data, size_t bytes) {
                const auto slice = device.AllocateUpload({bytes, RHIUploadUsage::Raw, 256});
                Check(slice.IsWritable(), "Batch meshlet upload");
                std::memcpy(slice.cpuAddress, data, bytes); return slice;
            };
            std::vector<GpuGeometryVisibility::Candidate> ranges;
            std::vector<GpuGeometryVisibility::Bin> bins;
            std::vector<RHIBindingDesc> vertices, topologies;
            uint32_t groupCount = 0, outputCount = 0, meshletJobs = 0;
            // Raw topology slices deliberately start at nonzero upload offsets.
            // Each descriptor owns one vertex; even meshlet IDs see vertex 0.
            // Variants: half visible, all visible, all culled.
            std::array<RHIBufferSlice, 3> points, topology;
            std::array<uint32_t, 3> counts{fixtureBins ? 1u : 0u, 65u, 4097u};
            if (fixtureBins >= 4 && fixtureBins <= 6)
            {
                const uint32_t boundary = fixtureBins == 4 ? 512u : fixtureBins == 5 ? 1024u : 256u;
                counts = {boundary - 1u, boundary, boundary + 1u};
            }
            if (route >= 5 && fixtureBins == 4096)
            {
                counts = {1u, 2u, 3u}; // Maximum descriptor count with serial one-slot backpressure.
            }
            for (uint32_t variant = 0; variant < 3; ++variant) {
                std::array<SurfacePoint, 2> p{};
                p[0].position = {variant == 2 ? 3.f : 0.f, 0, .5f, 1};
                p[1].position = {variant == 1 ? 0.f : 3.f, 0, .5f, 1};
                points[variant] = upload(p.data(), sizeof(p));
                const auto count = counts[variant];
                std::vector<uint32_t> words(20 + count * 9, 0u);
                words[3] = 80; words[4] = 32; words[5] = (20 + count * 8) * 4;
                for (uint32_t j = 0; j < count; ++j) {
                    words[20 + j * 8] = j;
                    words[20 + j * 8 + 3] = 1;
                    words[20 + count * 8 + j] = j % 2;
                }
                topology[variant] = upload(words.data(), words.size() * 4);
            }
            // Structured view offsets must be multiples of the element stride.
            // Copy the tiny point arrays into dedicated buffers so descriptor
            // offsets do not depend on the upload allocator's 256-byte alignment.
            std::array<RHIBufferHandle, 3> pointBuffers;
            auto& encoder = device.GetImmediateEncoder();
            for (uint32_t i = 0; i < 3; ++i) {
                RHIBufferDesc desc; desc.bytes = 2 * sizeof(SurfacePoint);
                Check(device.CreateBuffer(desc, pointBuffers[i], error), "Batch point buffer");
                const RHIBufferTransition before{pointBuffers[i], RHIResourceState::Common, RHIResourceState::CopyDest};
                encoder.ResourceBarriers({{}, {&before, 1}});
                RHIBufferSlice dest; dest.buffer = pointBuffers[i]; dest.size = desc.bytes;
                Check(encoder.CopyBuffer(dest, points[i]), "Batch points copy");
                const RHIBufferTransition after{pointBuffers[i], RHIResourceState::CopyDest, RHIResourceState::ShaderResource};
                encoder.ResourceBarriers({{}, {&after, 1}});
            }
    #ifndef LX_PROBE_VULKAN
            const auto outOfBounds = RHIBindingDesc::SrvBuffer(pointBuffers[0], 15, MeshSurfaceBatch::GeometryViewStride);
            Check(!device.CreateBindings({&outOfBounds, 1}).IsValid(), "SRV range outside its allocation is rejected before submission");
    #endif
            for (uint32_t bin = 0; bin < binCount; ++bin) {
                const auto variant = bin % 3;
                const auto count = counts[variant];
                outputCount = (outputCount + 63) / 64 * 64;
                ranges.push_back({{}, bin, groupCount, outputCount, 8u | (ordered ? GpuGeometryVisibility::kOrderedMeshlets : 0u)});
                bins.push_back({count, outputCount, 0, 0});
                groupCount += (count + 63) / 64;
                meshletJobs += count;
                outputCount += count * 2 + 1;
                vertices.push_back(RHIBindingDesc::SrvBuffer(pointBuffers[variant], 10, MeshSurfaceBatch::GeometryViewStride));
                const auto t = topology[variant];
                topologies.push_back(RHIBindingDesc::SrvBuffer(t.buffer, static_cast<uint32_t>(t.size / 4), 0,
                    static_cast<uint32_t>(t.offset / 4)));
            }
            const auto rangeUpload = upload(ranges.data(), ranges.size() * sizeof(ranges[0]));
            const auto binUpload = upload(bins.data(), bins.size() * sizeof(bins[0]));
            std::array<uint32_t, 48> constants{};
            constants[24] = binCount; constants[25] = binCount; constants[27] = dynamic ? (route == 6 ? 0u : meshletJobs) : groupCount;
            constants[26] = dynamic ? GpuGeometryVisibility::kWaveWorkQueue : 0u;
            const auto storageWords = outputCount + GpuGeometryVisibility::kWorkRingHeaderWords + GpuGeometryVisibility::kWorkRingSlots;
            const float one = 1.f;
            for (unsigned c = 0; c < 4; ++c) std::memcpy(&constants[28 + c * 5], &one, sizeof(one));
            constants[47] = 1;
            const auto uniform = device.UploadConstants(constants.data(), sizeof(constants));
            RHIBufferHandle ids, args;
            RHIBufferDesc desc; desc.allowUnorderedAccess = true; desc.bytes = uint64_t(storageWords) * 4;
            Check(device.CreateBuffer(desc, ids, error), "Batch ID buffer");
            desc.bytes = uint64_t(binCount) * sizeof(RHIDrawIndexedIndirectArguments);
            Check(device.CreateBuffer(desc, args, error), "Batch argument buffer");
            const RHIBindingDesc out[]{RHIBindingDesc::UavBuffer(ids, storageWords, 4),
                RHIBindingDesc::UavBuffer(args, binCount, sizeof(RHIDrawIndexedIndirectArguments))};
            const auto outputTable = device.CreateBindings(out);
            const auto vertexTable = device.CreateBindings(vertices), topologyTable = device.CreateBindings(topologies);
            // World-space fixture streams: identity object-to-world per bin.
            std::vector<math::vector4> worlds;
            for (uint32_t bin = 0; bin < binCount; ++bin)
                worlds.insert(worlds.end(), {math::vector4(1, 0, 0, 0), math::vector4(0, 1, 0, 0), math::vector4(0, 0, 1, 0)});
            const auto worldUpload = upload(worlds.data(), worlds.size() * sizeof(math::vector4));
            Check(uniform.IsValid() && outputTable.IsValid() && vertexTable.IsValid() && topologyTable.IsValid(),
                "Batch tables stay in the active recording");
            const RHIBufferTransition before[]{{ids, RHIResourceState::Common, RHIResourceState::UnorderedAccess},
                {args, RHIResourceState::Common, RHIResourceState::UnorderedAccess}};
            encoder.ResourceBarriers({{}, before});
            for (unsigned stage = 0; stage < kernels.size(); ++stage) {
                const auto generation = stage == 0 && route >= 5 ? stressCull[2].GetGeneration()
                    : stage == 1 && route >= 5 ? stressCull[route - 5].GetGeneration()
                    : stage == 1 && route == 4 ? partialCull.GetGeneration() : kernels[stage].GetGeneration();
                encoder.SetPipeline(RHIBindPoint::Compute, generation->GetHandle());
                encoder.SetConstantBuffer(RHIBindPoint::Compute, 0, uniform);
                encoder.SetRootBuffer(RHIBindPoint::Compute, 1, rangeUpload);
                encoder.SetRootBuffer(RHIBindPoint::Compute, 2, binUpload);
                encoder.SetBindings(RHIBindPoint::Compute, 3, outputTable);
                encoder.SetBindings(RHIBindPoint::Compute, 4, vertexTable);
                encoder.SetBindings(RHIBindPoint::Compute, 5, topologyTable);
                encoder.SetRootBuffer(RHIBindPoint::Compute, 6, worldUpload);
                const auto groups = stage == 1
                    ? dynamic ? (std::max)(1u, (std::min)(binCount * GpuGeometryVisibility::kInitialSubdivisions, (route == 1 || route == 5) ? 1u : GpuGeometryVisibility::kPersistentConsumerGroups)) : groupCount
                    : (binCount + 63) / 64;
                if (!groups)
                {
                    continue;
                }
                encoder.Dispatch((std::min)(groups, 65535u), (groups + 65534) / 65535, 1);
                const RHIBufferHandle barriers[]{ids, args}; encoder.ResourceBarriers({{}, {}, {}, barriers});
            }
            RHIReadback idReadback, argReadback;
            Check(device.CreateBufferReadback(uint64_t(storageWords) * 4, idReadback, error) &&
                device.CreateBufferReadback(desc.bytes, argReadback, error), "Batch result readbacks");
            const RHIBufferTransition after[]{{ids, RHIResourceState::UnorderedAccess, RHIResourceState::CopySource},
                {args, RHIResourceState::UnorderedAccess, RHIResourceState::CopySource}};
            encoder.ResourceBarriers({{}, after});
            encoder.CopyBufferToReadback(idReadback, ids); encoder.CopyBufferToReadback(argReadback, args);
            Check(device.EndFrame(error), "Batch submit"); device.WaitForGpu();
            RHIReadbackImage idImage, argImage;
            Check(device.MapReadback(idReadback, idImage, error) && device.MapReadback(argReadback, argImage, error),
                "Batch map");
            if (dynamic)
            {
                const auto* queue = reinterpret_cast<const uint32_t*>(idImage.data.data()) + outputCount;
                Check(queue[0] == meshletJobs && queue[1] == meshletJobs && queue[2] == 0,
                    "Queue consumed exactly its capacity without invalid records");
                Check(meshletJobs ? queue[3] > 0 : queue[3] == 0, "Empty/nonempty wave termination");
                Check(queue[12] == 0 && queue[13] == 0 && queue[14] == 0, "JOB_COUNT and activity leases are quiescent");
                const auto slots = route >= 5 ? 1u : GpuGeometryVisibility::kWorkRingSlots;
                Check(queue[6] == binCount * 16u && queue[9] == slots, "Ring source exhausted and capacity sealed");
                for (uint32_t slot = 0; slot < slots; ++slot)
                {
                    Check(queue[GpuGeometryVisibility::kWorkRingHeaderWords + slot] == 0, "Ring drained every reusable slot");
                }
                if (route >= 5 && meshletJobs > 0)
                {
                    Check(queue[8] >= 4, "Stress ring retried empty gathers before publication");
                    if (meshletJobs > 1)
                    {
                        Check(queue[7] > 0 && queue[0] > slots, "Full ring retains pending work and reuses consumed slots");
                    }
                }
                const auto width = queue[11];
                Check(width > 0 && width <= 64, "Active wave width was recorded");
                Check(queue[5] == queue[0] + queue[7] && queue[4] == queue[1] + queue[8] && queue[4] % width == 0,
                    "Lane reservation accounting matches published/failed and gathered/empty jobs");
                uint32_t nonempty = 0;
                for (const auto& bin : bins)
                {
                    // Count nonempty subdivisions independently by assigning
                    // each source meshlet to its expected subdivision.
                    std::array<bool, 16> used{};
                    const auto perSubdivision = bin.indexCount / 16u;
                    for (uint32_t meshlet = 0; meshlet < bin.indexCount; ++meshlet)
                    {
                        const auto subdivision = bin.indexCount < width * 16u
                            ? meshlet / width : (std::min)(15u, meshlet / perSubdivision);
                        Check(subdivision < 16u, "Initial subdivision index is valid");
                        used[subdivision] = true;
                    }
                    nonempty += static_cast<uint32_t>(std::count(used.begin(), used.end(), true));
                }
                Check(queue[10] == nonempty, "GPU sixteen-way subdivision matches the independent CPU partition oracle");
                ++queueCases;
            }
            for (uint32_t bin = 0; bin < binCount; ++bin) {
                const auto capacity = bins[bin].indexCount;
                const auto expected = bin % 3 == 0 ? (capacity + 1) / 2 : bin % 3 == 1 ? capacity : 0;
                const auto* list = reinterpret_cast<const uint32_t*>(idImage.data.data()) + bins[bin].firstIndex;
                Check(list[0] == expected, "Batch independent survivor count bin=" + std::to_string(bin));
                std::vector<bool> seen(capacity, false);
                for (uint32_t i = 0; i < list[0]; ++i) {
                    const auto id = list[i + 1];
                    Check(id < capacity && !seen[id] && (bin % 3 != 0 || id % 2 == 0),
                        "Batch IDs are complete, unique and belong to the selected geometry");
                    if (ordered && i != 0)
                    {
                        Check(list[i] < id, "Dynamic scheduling retains ordered source meshlet IDs");
                    }
                    seen[id] = true;
                }
                RHIDrawIndexedIndirectArguments result;
                std::memcpy(&result, argImage.data.data() + bin * sizeof(result), sizeof(result));
                Check(result.indexCount == expected && result.instanceCount == 1 && result.firstIndex == 1 && result.baseVertex == 0 && result.firstInstance == 0,
                    "Batch finalized arguments match the GPU list");
            }
            device.ReleaseReadback(idReadback); device.ReleaseReadback(argReadback);
            device.ReleaseBuffer(ids); device.ReleaseBuffer(args);
            for (auto buffer : pointBuffers) device.ReleaseBuffer(buffer);
            std::string validation;
            Check(device.DrainDebugMessages(validation) == 0, "Batch GPU validation " + validation);
            std::cout << "METEOR_MESHLET_BATCH_DISPATCH_OK bins=" << binCount << " referenceGroups=" << groupCount << " meshletJobs=" << meshletJobs
                << " initialPartitions=" << binCount * 16u
                << " route=" << route << " cullDispatches=1 validation=0\n";
        }
    }
    Check(queueCases == 48, "Six ring routes cover empty, tail, skewed and maximum-bin queues");
    std::cout << "METEOR_QUEUE_COUNT_OK cases=" << queueCases
        << " referenceCases=8 orderedCases=32 partialWaveCases=16 oneSlotCases=16 subdivisions=16 ignoredTotalHintCases=8 budgetBoundaries=3 validation=0\n";
}
