// Included inside the native material probe namespace. Exercise the production
// reset/cull/finalize shaders with distinct SRV slices and independently known
// visibility, including the last descriptor and a second dispatch row.
void RunMeshletBatchDispatch(RecordingChangeDevice& device, ProbeRoots& roots, ProbePipelines& pipelines)
{
    std::string error;
    const RHIPipelineLayoutParam params[]{RHILayout::Cbv(0), RHILayout::Srv(0), RHILayout::Srv(1),
        RHILayout::UavBufferTable(2, 0), RHILayout::SrvBufferTable(4096, 18), RHILayout::SrvBufferTable(4096, 4114)};
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
    for (const uint32_t binCount : {1u, 2u, 4096u}) {
        Check(device.BeginFrame(error), "Batch meshlet begin");
        const auto upload = [&](const void* data, size_t bytes) {
            const auto slice = device.AllocateUpload({bytes, RHIUploadUsage::Raw, 256});
            Check(slice.IsWritable(), "Batch meshlet upload");
            std::memcpy(slice.cpuAddress, data, bytes); return slice;
        };
        std::vector<GpuGeometryVisibility::Candidate> ranges;
        std::vector<GpuGeometryVisibility::Bin> bins;
        std::vector<RHIBindingDesc> vertices, topologies;
        uint32_t groupCount = 0, outputCount = 0;
        // Raw topology slices deliberately start at nonzero upload offsets.
        // Each descriptor owns one vertex; even meshlet IDs see vertex 0.
        // Variants: half visible, all visible, all culled.
        std::array<RHIBufferSlice, 3> points, topology;
        const std::array<uint32_t, 3> counts{1025, 1089, 1153};
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
        const auto outOfBounds = RHIBindingDesc::SrvBuffer(pointBuffers[0], 3, sizeof(SurfacePoint));
        Check(!device.CreateBindings({&outOfBounds, 1}).IsValid(), "SRV range outside its allocation is rejected before submission");
#endif
        for (uint32_t bin = 0; bin < binCount; ++bin) {
            const auto variant = bin % 3;
            const auto count = counts[variant];
            outputCount = (outputCount + 63) / 64 * 64;
            ranges.push_back({{}, bin, groupCount, outputCount, 0});
            bins.push_back({count, outputCount, 0, 0});
            groupCount += (count + 63) / 64;
            outputCount += count + 1;
            vertices.push_back(RHIBindingDesc::SrvBuffer(pointBuffers[variant], 2, sizeof(SurfacePoint)));
            const auto t = topology[variant];
            topologies.push_back(RHIBindingDesc::SrvBuffer(t.buffer, static_cast<uint32_t>(t.size / 4), 0,
                static_cast<uint32_t>(t.offset / 4)));
        }
        const auto rangeUpload = upload(ranges.data(), ranges.size() * sizeof(ranges[0]));
        const auto binUpload = upload(bins.data(), bins.size() * sizeof(bins[0]));
        std::array<uint32_t, 48> constants{};
        constants[24] = binCount; constants[25] = binCount; constants[27] = groupCount;
        const float one = 1.f;
        for (unsigned c = 0; c < 4; ++c) std::memcpy(&constants[28 + c * 5], &one, sizeof(one));
        constants[47] = 1;
        const auto uniform = device.UploadConstants(constants.data(), sizeof(constants));
        RHIBufferHandle ids, args;
        RHIBufferDesc desc; desc.allowUnorderedAccess = true; desc.bytes = uint64_t(outputCount) * 4;
        Check(device.CreateBuffer(desc, ids, error), "Batch ID buffer");
        desc.bytes = uint64_t(binCount) * sizeof(RHIDrawIndexedIndirectArguments);
        Check(device.CreateBuffer(desc, args, error), "Batch argument buffer");
        const RHIBindingDesc out[]{RHIBindingDesc::UavBuffer(ids, outputCount, 4),
            RHIBindingDesc::UavBuffer(args, binCount, sizeof(RHIDrawIndexedIndirectArguments))};
        const auto outputTable = device.CreateBindings(out);
        const auto vertexTable = device.CreateBindings(vertices), topologyTable = device.CreateBindings(topologies);
        Check(uniform.IsValid() && outputTable.IsValid() && vertexTable.IsValid() && topologyTable.IsValid(),
            "Batch tables stay in the active recording");
        const RHIBufferTransition before[]{{ids, RHIResourceState::Common, RHIResourceState::UnorderedAccess},
            {args, RHIResourceState::Common, RHIResourceState::UnorderedAccess}};
        encoder.ResourceBarriers({{}, before});
        for (unsigned stage = 0; stage < kernels.size(); ++stage) {
            encoder.SetPipeline(RHIBindPoint::Compute, kernels[stage].GetGeneration()->GetHandle());
            encoder.SetConstantBuffer(RHIBindPoint::Compute, 0, uniform);
            encoder.SetRootBuffer(RHIBindPoint::Compute, 1, rangeUpload);
            encoder.SetRootBuffer(RHIBindPoint::Compute, 2, binUpload);
            encoder.SetBindings(RHIBindPoint::Compute, 3, outputTable);
            encoder.SetBindings(RHIBindPoint::Compute, 4, vertexTable);
            encoder.SetBindings(RHIBindPoint::Compute, 5, topologyTable);
            const auto groups = stage == 1 ? groupCount : (binCount + 63) / 64;
            encoder.Dispatch((std::min)(groups, 65535u), (groups + 65534) / 65535, 1);
            const RHIBufferHandle barriers[]{ids, args}; encoder.ResourceBarriers({{}, {}, {}, barriers});
        }
        RHIReadback idReadback, argReadback;
        Check(device.CreateBufferReadback(uint64_t(outputCount) * 4, idReadback, error) &&
            device.CreateBufferReadback(desc.bytes, argReadback, error), "Batch result readbacks");
        const RHIBufferTransition after[]{{ids, RHIResourceState::UnorderedAccess, RHIResourceState::CopySource},
            {args, RHIResourceState::UnorderedAccess, RHIResourceState::CopySource}};
        encoder.ResourceBarriers({{}, after});
        encoder.CopyBufferToReadback(idReadback, ids); encoder.CopyBufferToReadback(argReadback, args);
        Check(device.EndFrame(error), "Batch submit"); device.WaitForGpu();
        RHIReadbackImage idImage, argImage;
        Check(device.MapReadback(idReadback, idImage, error) && device.MapReadback(argReadback, argImage, error),
            "Batch map");
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
                seen[id] = true;
            }
            RHIDrawIndexedIndirectArguments result;
            std::memcpy(&result, argImage.data.data() + bin * sizeof(result), sizeof(result));
            Check(result.indexCount == expected && result.instanceCount == 1 && result.firstIndex == 1,
                "Batch finalized arguments match the GPU list");
        }
        device.ReleaseReadback(idReadback); device.ReleaseReadback(argReadback);
        device.ReleaseBuffer(ids); device.ReleaseBuffer(args);
        for (auto buffer : pointBuffers) device.ReleaseBuffer(buffer);
        std::string validation;
        Check(device.DrainDebugMessages(validation) == 0, "Batch GPU validation " + validation);
        std::cout << "METEOR_MESHLET_BATCH_DISPATCH_OK bins=" << binCount << " groups=" << groupCount
            << " cullDispatches=1 validation=0\n";
    }
}
