// Runs only after the original frame has completed and failed its CPU oracle.
// It adds no usage, resource, barrier or lifetime extension to that graph.
uint32_t CaptureSkinDepthFailure(RecordingChangeDevice& device, const EnhancedRenderGraph& graph, RGHandle visibility,
                                 const auto& table, const auto& hybrid, unsigned x, unsigned y)
{
    EnhancedRenderGraph::DiagnosticSnapshot snapshot;
    Check(graph.CaptureDiagnosticSnapshot(snapshot), "Depth failure graph snapshot");
    const auto& resource = snapshot.resources.at(visibility.index);
    std::cerr << "SKIN_FAILURE_RESOURCE name=" << resource.name << " alias=" << resource.aliasGroup
              << " first=" << resource.firstUse << " last=" << resource.lastUse
              << " state=" << unsigned(resource.finalState) << '\n';
    std::string error;
    RHIReadback visibilityRead;
    const bool standalone = resource.aliasGroup == UINT32_MAX;
    if (standalone)
    {
        Check(device.CreateReadback(32, 32, RHIFormat::RG32Uint, 1, visibilityRead, error),
              "Post-failure visibility readback " + error);
    }
    std::vector<RHIReadback> poses(table->geometry.size());
    for (size_t i = 1; i < table->geometry.size(); ++i)
    {
        Check(device.CreateBufferReadback(table->geometry[i]->BufferBytes(), poses[i], error),
              "Post-failure pose readback " + error);
    }
    struct BufferCapture
    {
        std::string name;
        RHIBufferHandle buffer;
        uint64_t bytes{}, offset{};
        RHIResourceState state{};
        bool upload{};
        RHIReadback readback;
    };
    std::vector<BufferCapture> captures;
    const auto addBuffer =
        [&](std::string name, RHIBufferHandle buffer, uint64_t bytes, uint64_t offset = 0, bool upload = false)
    {
        const auto handle = graph.FindImportedBuffer(buffer);
        const auto state = upload ? RHIResourceState::Common : snapshot.resources.at(handle.index).finalState;
        BufferCapture capture{std::move(name), buffer, bytes, offset, state, upload, {}};
        Check(device.CreateBufferReadback(bytes, capture.readback, error), "Failure buffer readback " + error);
        captures.push_back(std::move(capture));
    };
    addBuffer("Keys", hybrid->keys, hybrid->KeyBytes());
    addBuffer("Routes", hybrid->routes, uint64_t(hybrid->meshlets) * 4);
    addBuffer("FrameGeometry", table->buffer, table->records.size() * sizeof(table->records[0]));
    if (device.skinViewConstants.IsWritable())
    {
        const auto& view = device.skinViewConstants;
        const bool exact = std::memcmp(view.cpuAddress, device.skinViewExpected.data(), 4800) == 0;
        std::cerr << "SKIN_FAILURE_VIEW_CPU exact=" << exact << " offset=" << view.offset << '\n';
        addBuffer("ViewConstants", view.buffer, 4800, view.offset, true);
    }
    Check(device.BeginFrame(error), "Post-failure capture frame " + error);
    auto& encoder = device.GetImmediateEncoder();
    if (standalone)
    {
        EnhancedRenderGraph::ExecuteContext context;
        context.graph = &graph;
        const auto texture = context.ResolveHandle(visibility);
        const RHITransition before{texture, resource.finalState, RHIResourceState::CopySource};
        const RHITransition after{texture, RHIResourceState::CopySource, resource.finalState};
        encoder.ResourceBarriers({{&before, 1}});
        encoder.CopyToReadback(visibilityRead, texture);
        encoder.ResourceBarriers({{&after, 1}});
    }
    for (size_t i = 1; i < table->geometry.size(); ++i)
    {
        const auto& geometry = *table->geometry[i];
        const auto state = snapshot.resources.at(geometry.GraphOutput(graph).index).finalState;
        const RHIBufferTransition before{geometry.Buffer(), state, RHIResourceState::CopySource};
        const RHIBufferTransition after{geometry.Buffer(), RHIResourceState::CopySource, state};
        encoder.ResourceBarriers({{}, {&before, 1}});
        encoder.CopyBufferToReadback(poses[i], geometry.Buffer());
        encoder.ResourceBarriers({{}, {&after, 1}});
    }
    for (auto& capture : captures)
    {
        const RHIBufferTransition before{capture.buffer, capture.state, RHIResourceState::CopySource};
        const RHIBufferTransition after{capture.buffer, RHIResourceState::CopySource, capture.state};
        if (!capture.upload)
        {
            encoder.ResourceBarriers({{}, {&before, 1}});
        }
        encoder.CopyBufferToReadback(capture.readback, capture.buffer, capture.offset, capture.bytes);
        if (!capture.upload)
        {
            encoder.ResourceBarriers({{}, {&after, 1}});
        }
    }
    Check(device.EndFrame(error) && GetRHISubmissionThread().DrainSubmissions(&device, error),
          "Post-failure capture submit " + error);
    device.WaitForGpu();
    Check(GetRHISubmissionThread().Drain(&device, error), "Post-failure capture retire " + error);
    uint32_t address{};
    if (standalone)
    {
        RHIReadbackImage image;
        Check(device.MapReadback(visibilityRead, image, error), "Post-failure visibility map " + error);
        std::memcpy(&address, image.data.data() + y * image.rowPitch + x * 8, 4);
        uint32_t depth{};
        std::memcpy(&depth, image.data.data() + y * image.rowPitch + x * 8 + 4, 4);
        std::cerr << "SKIN_FAILURE_VISIBILITY address=" << address << " depth=" << std::bit_cast<float>(depth) << '\n';
        device.ReleaseReadback(visibilityRead);
    }
    for (size_t i = 1; i < table->geometry.size(); ++i)
    {
        RHIReadbackImage image;
        Check(device.MapReadback(poses[i], image, error), "Post-failure pose map " + error);
        const auto expected = table->geometry[i]->Input()->Bones();
        uint32_t count{};
        std::memcpy(&count, image.data.data(), 4);
        const bool exact = count == expected.size() && image.data.size() >= 32 + expected.size_bytes() &&
                           std::memcmp(image.data.data() + 32, expected.data(), expected.size_bytes()) == 0;
        std::cerr << "SKIN_FAILURE_POSE record=" << i << " count=" << count << " exact=" << exact << '\n';
        device.ReleaseReadback(poses[i]);
    }
    for (auto& capture : captures)
    {
        RHIReadbackImage image;
        Check(device.MapReadback(capture.readback, image, error), "Failure buffer map " + error);
        if (capture.name == "Keys")
        {
            uint64_t key{};
            std::memcpy(&key, image.data.data() + (y * 32 + x) * 8, 8);
            std::cerr << "SKIN_FAILURE_KEY address=" << uint32_t(key)
                      << " depth=" << std::bit_cast<float>(uint32_t(key >> 32)) << '\n';
        }
        if (capture.name == "Routes")
        {
            const auto record = table->FindRecord(address);
            const auto meshlet = (address - table->records[record].identity.reserved[1]) >> 7;
            const auto index = (table->records[record].identity.reserved[1] - 1) / 128 + meshlet;
            uint32_t route{};
            std::memcpy(&route, image.data.data() + index * 4, 4);
            std::cerr << "SKIN_FAILURE_ROUTE value=" << route << '\n';
        }
        if (capture.name == "FrameGeometry")
        {
            const bool exact = std::memcmp(image.data.data(), table->records.data(), capture.bytes) == 0;
            std::cerr << "SKIN_FAILURE_FRAME_GEOMETRY exact=" << exact << '\n';
        }
        if (capture.name == "ViewConstants")
        {
            const bool exact = std::memcmp(image.data.data(), device.skinViewExpected.data(), 4800) == 0;
            const auto* values = reinterpret_cast<const float*>(image.data.data());
            std::cerr << "SKIN_FAILURE_VIEW_GPU exact=" << exact << " projection=";
            for (unsigned component = 0; component < 16; ++component)
            {
                std::cerr << values[component] << ',';
            }
            std::cerr << '\n';
        }
        device.ReleaseReadback(capture.readback);
    }
    return address;
}

// This decoder executes only after a failed frame and its diagnostic copy retired.
void CaptureSkinDepthVertices(RecordingChangeDevice& device, ProbeRoots& roots, ProbePipelines& pipelines,
                              const auto& table, uint32_t address)
{
    const auto record = table->FindRecord(address);
    Check(record != 0, "Failure primitive record");
    const auto& geometry = *table->geometry[record];
    const uint32_t offset = address - table->records[record].identity.reserved[1];
    const uint32_t query[4]{offset >> 7, offset & 127u, 0, 0};
    std::string error;
    const RHIPipelineLayoutParam params[]{RHILayout::Srv(0), RHILayout::Srv(1), RHILayout::Srv(2),
                                          RHILayout::UavBufferTable(1, 0)};
    const auto layout = roots.GetOrCreate({params, {}}, error);
    Check(layout.IsValid(), "Failure decoder layout " + error);
    LX::Runtime::CompiledCompute shader;
    RHIShaderCompileOptions options;
    options.strictMath = true;
    Check(LX::Runtime::CompileCompute(
              (std::filesystem::current_path() / "Tools/regression/support/SkinDepthFailureProbe.slang").string(),
              "SkinDepthFailureCS", {}, options, shader, error),
          "Failure decoder shader " + error);
    LX::Runtime::ComputePipeline kernel;
    RHIComputePipelineDesc desc;
    desc.layout = layout;
    desc.csBytecode = shader.stage.bytecode.Data();
    desc.csSize = shader.stage.bytecode.Size();
    Check(kernel.Create(pipelines, desc, std::move(shader.description), error), "Failure decoder PSO " + error);
    Check(device.BeginFrame(error), "Failure decoder frame " + error);
    const auto upload = device.AllocateUpload({sizeof(query), RHIUploadUsage::BufferCopy, 16});
    Check(upload.IsWritable(), "Failure decoder upload");
    std::memcpy(upload.cpuAddress, query, sizeof(query));
    RHIBufferHandle output;
    RHIBufferDesc buffer;
    buffer.bytes = 48;
    buffer.allowUnorderedAccess = true;
    Check(device.CreateBuffer(buffer, output, error), "Failure decoder output " + error);
    const RHIBindingDesc binding = RHIBindingDesc::UavBuffer(output, 3, 16);
    const auto bindings = device.CreateBindings({&binding, 1});
    auto& encoder = device.GetImmediateEncoder();
    const RHIBufferTransition before{output, RHIResourceState::Common, RHIResourceState::UnorderedAccess};
    encoder.ResourceBarriers({{}, {&before, 1}});
    encoder.SetPipeline(RHIBindPoint::Compute, kernel.GetGeneration()->GetHandle());
    encoder.SetRootBuffer(RHIBindPoint::Compute, 0, RHIBufferSlice::Whole(geometry.Buffer()));
    encoder.SetRootBuffer(RHIBindPoint::Compute, 1, geometry.Meshlets());
    encoder.SetRootBuffer(RHIBindPoint::Compute, 2, upload);
    encoder.SetBindings(RHIBindPoint::Compute, 3, bindings);
    encoder.Dispatch(1, 1, 1);
    const RHIBufferTransition after{output, RHIResourceState::UnorderedAccess, RHIResourceState::CopySource};
    encoder.ResourceBarriers({{}, {&after, 1}});
    RHIReadback readback;
    Check(device.CreateBufferReadback(48, readback, error), "Failure decoder readback " + error);
    encoder.CopyBufferToReadback(readback, output);
    Check(device.EndFrame(error) && device.DrainForLifecycle(RHILifecycleCommand::OfflineReadbackCapture, error),
          "Failure decoder completion " + error);
    RHIReadbackImage image;
    Check(device.MapReadback(readback, image, error), "Failure decoder map " + error);
    for (unsigned corner = 0; corner < 3; ++corner)
    {
        float value[4];
        std::memcpy(value, image.data.data() + corner * 16, 16);
        std::cerr << "SKIN_GPU_VERTEX corner=" << corner << " vertex=" << std::bit_cast<uint32_t>(value[3])
                  << " position=" << value[0] << ',' << value[1] << ',' << value[2] << '\n';
    }
    device.ReleaseReadback(readback);
    device.ReleaseBuffer(output);
}
