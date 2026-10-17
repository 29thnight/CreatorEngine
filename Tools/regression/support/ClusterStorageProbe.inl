#include "SharedSparseStorageProbe.inl"
void RunClusterResidencyState(const std::shared_ptr<const assets::cluster::Archive>& source)
{
    using namespace assets::cluster;
    auto archive = std::make_shared<Archive>(*source);
    archive->packets = {source->packets[0],source->packets[1],source->packets[0]};
    archive->packetSizes.clear(); archive->fileOffsets.clear(); archive->pageReader = {};
    const auto pageBytes = archive->packets[0].size();
    Check(pageBytes == archive->packets[1].size(), "Symmetric page fixture sizes");
    std::vector<experiment::MeshClusterGroup> groups(3);
    for (uint32_t group = 0; group < 3; ++group)
    {
        groups[group].firstMeshlet = group; groups[group].meshletCount = 1;
        groups[group].parentGroup = group == 2 ? UINT32_MAX : 2;
    }
    groups[2].childGroup = 0; groups[2].secondChild = 1;
    Stream stream(archive,groups);
    const std::array<uint32_t,1> left{0},right{1};
    const StreamBudget budget{pageBytes*2,pageBytes};
    const auto cold = stream.Prepare(1,left,budget);
    Check(cold->groups[2] && !cold->groups[0] && cold->bytes == pageBytes, "Cold root is pinned before refinement");
    const auto refined = stream.Prepare(2,left,budget);
    Check(refined->groups[0] && refined->groups[2] && !refined->groups[1], "Requested refinement arrives next recording");
    const auto switched = stream.Prepare(3,right,budget);
    Check(switched->groups[1] && !switched->groups[0] && switched->evictions == 1 && refined->groups[0] && refined->packets[0],
        "Eviction preserves the preceding submitted snapshot's immutable page owner");
    Check(stream.Prepare(3,left,budget) == switched, "Second view shares the sealed same-recording residency cut");
    stream.SubmitGpuRequests(left);
    const auto gpuPreferred = stream.Prepare(4,right,budget);
    Check(gpuPreferred->groups[0] && !gpuPreferred->groups[1] && stream.GpuFeedbackGroups() == 1,
        "Completed GPU demand takes priority over CPU prefetch under the same physical budget");
    bool rejected = false;
    try
    {
        stream.Prepare(5,left,{pageBytes-1,pageBytes});
    }
    catch (const std::exception&)
    {
        rejected = true;
    }
    Check(rejected && switched->groups[2], "One byte below pinned root budget rejects without damaging the old snapshot");
    auto split = std::make_shared<Archive>(*archive);
    split->packets = {source->packets[0],source->packets[1],source->packets[0],source->packets[0]};
    auto splitGroups = groups; splitGroups[0].meshletCount = 2; splitGroups[1].firstMeshlet = 2; splitGroups[2].firstMeshlet = 3;
    Stream partial(split,splitGroups);
    const StreamBudget partialBudget{pageBytes*4,pageBytes};
    partial.Prepare(1,left,partialBudget);
    const auto incomplete = partial.Prepare(2,left,partialBudget);
    Check(incomplete->packets[0] && !incomplete->packets[1] && !incomplete->groups[0] && incomplete->groups[2],
        "Partial replacement upload never publishes an incomplete child group");
    const auto complete = partial.Prepare(3,left,partialBudget);
    Check(complete->groups[0] && complete->packets[0] == incomplete->packets[0] && complete->revision > incomplete->revision,
        "Final page atomically makes the complete child group selectable");
    auto asynchronous = std::make_shared<Archive>(*archive);
    auto backing = std::make_shared<const std::vector<std::vector<std::byte>>>(archive->packets);
    auto failure = std::make_shared<std::atomic<bool>>(true);
    std::promise<void> release;
    const auto gate = release.get_future().share();
    struct ReleaseGate
    {
        std::promise<void>& gate;
        ~ReleaseGate()
        {
            try { gate.set_value(); } catch (const std::future_error&) {}
        }
    } finally{release};
    asynchronous->pageReader = [backing,gate,failure](uint32_t page) {
        if (page == 0)
        {
            gate.wait();
        }
        if (page == 1 && failure->load())
        {
            throw std::runtime_error("Injected page I/O/hash failure");
        }
        return backing->at(page);
    };
    asynchronous->packetSizes = {uint32_t(pageBytes),uint32_t(pageBytes),uint32_t(pageBytes)};
    for (auto& packet : asynchronous->packets)
    {
        packet.clear();
    }
    Stream io(asynchronous,groups);
    const std::array<uint32_t,2> requests{0,1};
    const StreamBudget ioBudget{pageBytes*3,pageBytes*3};
    const auto pending = io.Prepare(1,requests,ioBudget);
    Check(pending->groups[2] && !pending->groups[0] && !pending->groups[1], "Pending asynchronous pages retain the root fallback");
    uint64_t recording = 2;
    bool sawFailure = false;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (!sawFailure && std::chrono::steady_clock::now() < deadline)
    {
        sawFailure = io.Prepare(recording++,requests,ioBudget)->readFailures != 0;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    Check(sawFailure, "Asynchronous read failure is observed while coverage stays at the pinned parent");
    failure->store(false); release.set_value();
    std::shared_ptr<const ResidentSet> ready;
    deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    do
    {
        ready = io.Prepare(recording++,requests,ioBudget);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while ((!ready->groups[0] || !ready->groups[1]) && std::chrono::steady_clock::now() < deadline);
    Check(ready->groups[0] && ready->groups[1] && ready->groups[2] && !pending->groups[0],
        "I/O recovery refines without mutating the old frame's cut");
    std::cout << "METEOR_CLUSTER_RESIDENCY_OK root=1 refinement=1 eviction=1 partial=1 asyncFailure=1 asyncRecovery=1 sharedView=1 validation=0\n";
}

void RunSparseBufferStorage(RecordingChangeDevice& device)
{
    std::string error;
    RHIBufferHandle buffer;
    const auto beforeBudget = device.GetPersistentMemoryBudgetStats();
    const RHISparseBufferDesc desc{RHISparseTileBytes * 4, 2, false, L"Probe.SparseClusters"};
    Check(device.CreateSparseBuffer(desc, buffer, error), "Reserved buffer creation " + error);
    const auto admittedBudget = device.GetPersistentMemoryBudgetStats();
    Check(admittedBudget.growthCommits == beforeBudget.growthCommits + 1 &&
        admittedBudget.registeredOwners == beforeBudget.registeredOwners + 1,
        "Sparse physical heap reserves and commits the shared device VRAM budget");
    const auto initialAddress = device.Resolve(buffer)->GetGPUVirtualAddress();
    unsigned rejected{};
    for (unsigned mutation = 0; mutation < 4; ++mutation)
    {
        std::vector<RHISparseTileMapping> invalid{{0, 0, true}};
        if (mutation == 0) invalid.push_back({4, 0, true});
        if (mutation == 1) invalid.push_back({1, 2, true});
        if (mutation == 2) invalid.push_back({0, 1, true});
        if (mutation == 3) invalid.push_back({UINT32_MAX, 0, false});
        Check(!device.UpdateSparseBufferMappings(buffer, invalid, error), "Invalid sparse batch rejected atomically");
        ++rejected;
    }
    RHIBufferDesc ordinaryDesc;
    ordinaryDesc.bytes = RHISparseTileBytes;
    RHIBufferHandle ordinary;
    Check(device.CreateBuffer(ordinaryDesc, ordinary, error), "Ordinary buffer creation");
    const RHISparseTileMapping mapping{0, 0, true};
    Check(!device.UpdateSparseBufferMappings(ordinary, {&mapping, 1}, error), "Ordinary buffer cannot be tiled");
    device.ReleaseBuffer(ordinary);
    RHIReadback readback;
    Check(device.CreateBufferReadback(256, readback, error), "Sparse readback creation");
    uint64_t checkedWords{};
    for (unsigned frame = 0; frame < 6; ++frame)
    {
        Check(device.BeginFrame(error), "Sparse recording begins");
        const uint32_t virtualTile = frame % 2 ? 3 : 0;
        const RHISparseTileMapping changes[]{{frame % 2 ? 0u : 3u, 0, false}, {virtualTile, frame % 2, true}};
        Check(device.UpdateSparseBufferMappings(buffer, changes, error), "Sparse map/unmap " + error);
        Check(device.Resolve(buffer)->GetGPUVirtualAddress() == initialAddress, "Reserved GPU address remains stable");
        const auto upload = device.AllocateUpload({256, RHIUploadUsage::BufferCopy, 16});
        Check(upload.IsWritable(), "Sparse packet staging");
        std::array<uint32_t, 64> expected{};
        for (unsigned word = 0; word < expected.size(); ++word) expected[word] = 0x5a000000u ^ (frame << 16) ^ word;
        std::memcpy(upload.cpuAddress, expected.data(), sizeof(expected));
        auto& encoder = device.GetImmediateEncoder();
        RHIBarrierBatch activation{};
        activation.aliasBuffers = {&buffer, 1};
        encoder.ResourceBarriers(activation);
        device.TransitionBuffers(std::array{RHIBufferTransition{buffer, RHIResourceState::Common, RHIResourceState::CopyDest}});
        RHIBufferSlice destination = RHIBufferSlice::Whole(buffer);
        destination.offset = uint64_t(virtualTile) * RHISparseTileBytes;
        destination.size = sizeof(expected);
        Check(encoder.CopyBuffer(destination, upload), "Sparse packet upload");
        device.TransitionBuffers(std::array{RHIBufferTransition{buffer, RHIResourceState::CopyDest, RHIResourceState::CopySource}});
        encoder.CopyBufferToReadback(readback, buffer, destination.offset);
        Check(device.EndFrame(error) && device.DrainForLifecycle(RHILifecycleCommand::OfflineReadbackCapture, error),
            "Sparse packet completion " + error);
        RHIReadbackImage image;
        Check(device.MapReadback(readback, image, error), "Sparse packet map " + error);
        Check(image.ElementCount<uint32_t>() == expected.size(), "Sparse readback extent");
        for (unsigned word = 0; word < expected.size(); ++word)
        {
            Check(image.Elements<uint32_t>()[word] == expected[word], "Sparse packet GPU bytes equal upload after remap");
            ++checkedWords;
        }
    }
    device.ReleaseReadback(readback);
    device.ReleaseBuffer(buffer);
    Check(!device.UpdateSparseBufferMappings(buffer, {&mapping, 1}, error), "Stale sparse handle is rejected");
    Check(device.GetPersistentMemoryBudgetStats().registeredOwners == beforeBudget.registeredOwners,
        "Completed sparse heap releases its shared VRAM budget owner");
    auto& coordinator = device.GetPersistentMemoryBudgetCoordinator();
    const auto previousBudget = coordinator.GetDecision(kRHIDeviceLocalMemoryBudgetDomain).budget;
    coordinator.UpdateBudget(kRHIDeviceLocalMemoryBudgetDomain, {65536, 65536, false});
    RHIBufferHandle denied;
    const bool rejectedPressure = !device.CreateSparseBuffer(desc, denied, error) && !denied.IsValid();
    const bool pressurePublished = device.StreamingMemoryPressure();
    coordinator.UpdateBudget(kRHIDeviceLocalMemoryBudgetDomain, previousBudget);
    Check(rejectedPressure && pressurePublished && device.GetPersistentMemoryBudgetStats().registeredOwners == beforeBudget.registeredOwners,
        "Global VRAM pressure rejects sparse growth without leaking its reservation");
    std::cout << "METEOR_SPARSE_VRAM_BUDGET_OK commit=1 release=1 pressure=1 validation=0\n";
    std::cout << "METEOR_SPARSE_BUFFER_OK frames=6 words=" << checkedWords << " malformed=" << rejected + 2
        << " stableAddress=1 validation=0\n";
}

void RunClusterStorage(RecordingChangeDevice& device, ProbeRoots& roots, ProbePipelines& pipelines,
    const std::filesystem::path& root)
{
    RunSparseBufferStorage(device);
    RunSharedSparseStorage(device);
    using namespace assets::cluster;
    using assets::quantized::Word;
    using assets::quantized::Put;
    std::string error;
    const RHIPipelineLayoutParam params[]{RHILayout::Cbv(0), RHILayout::Srv(0), RHILayout::Srv(1), RHILayout::UavBufferTable(2, 0)};
    const auto layout = roots.GetOrCreate({params, {}}, error);
    Check(layout.IsValid(), "Cluster decoder layout " + error);
    LX::Runtime::CompiledCompute shader;
    RHIShaderCompileOptions options; options.strictMath = true;
    Check(LX::Runtime::CompileCompute((root / "Tools/regression/support/ClusterStorageProbe.slang").string(),
        "ClusterStorageCS", {}, options, shader, error), "Direct cluster shader " + error);
    LX::Runtime::ComputePipeline kernel;
    RHIComputePipelineDesc desc; desc.layout = layout;
    desc.csBytecode = shader.stage.bytecode.Data(); desc.csSize = shader.stage.bytecode.Size();
    Check(kernel.Create(pipelines, desc, std::move(shader.description), error), "Cluster decoder pipeline " + error);
    uint64_t words{}, seamChecks{};
    unsigned cases{}, rejected{};
    std::shared_ptr<const Archive> reference;
    for (const auto mask : assets::kModelVertexMasks)
    {
        constexpr uint32_t count = 64;
        const auto stride = assets::StrideOf(mask);
        std::vector<std::byte> raw(count * stride);
        for (uint32_t vertex = 0; vertex < count; ++vertex)
        {
            const auto set = [&](assets::VertexAttribute attribute, std::initializer_list<float> values) {
                if (!assets::Has(mask, attribute))
                {
                    return;
                }
                auto offset = vertex * stride + assets::OffsetOf(mask, attribute);
                for (auto value : values)
                {
                    Put(raw, offset, std::bit_cast<uint32_t>(value)); offset += 4;
                }
            };
            const float angle = vertex * .01f, weight = .25f + (vertex % 3) * .25f;
            set(assets::VertexAttribute::Position, {vertex / 64.f - .5f, std::sin(vertex * .2f) * .3f, 1.f + std::cos(vertex * .3f) * .1f});
            set(assets::VertexAttribute::Normal, {0, std::sin(angle), std::cos(angle)});
            set(assets::VertexAttribute::Tangent, {1, 0, 0, vertex % 2 ? -1.f : 1.f});
            set(assets::VertexAttribute::Uv0, {vertex / 63.f, std::sin(float(vertex)) * 2});
            set(assets::VertexAttribute::Uv1, {-2.f + vertex / 50.f, 500.f + vertex * .1f});
            set(assets::VertexAttribute::Color, {vertex / 63.f, .5f, 0, 1});
            set(assets::VertexAttribute::BoneWeights, {weight, 1.f - weight, 0, 0});
            if (assets::Has(mask, assets::VertexAttribute::BoneIndices))
            {
                Put(raw, vertex * stride + assets::OffsetOf(mask, assets::VertexAttribute::BoneIndices), 0xffff0100u);
            }
        }
        experiment::MeshletPayload meshlets;
        for (uint32_t cluster = 0; cluster < 2; ++cluster)
        {
            experiment::MeshletDescriptor descriptor;
            descriptor.vertexOffset = cluster * count; descriptor.vertexCount = count;
            meshlets.descriptors.push_back(descriptor);
            for (uint32_t vertex = 0; vertex < count; ++vertex)
            {
                meshlets.vertexRemap.push_back(cluster ? count - 1 - vertex : vertex);
            }
        }
        const auto archive = Build(mask, count, raw, meshlets);
        reference = archive;
        std::vector<std::byte> expected, reversed;
        Check(Decode(archive->packets[0], expected, error) && Decode(archive->packets[1], reversed, error), "Cluster CPU decode " + error);
        for (uint32_t vertex = 0; vertex < count; ++vertex)
        {
            Check(std::memcmp(expected.data() + vertex * stride, reversed.data() + (count - 1 - vertex) * stride, stride) == 0,
                "Shared seam vertices decode to identical bits across independently stored meshlets");
            ++seamChecks;
            for (const auto& attribute : assets::kVertexAttributeTable)
            {
                if (!assets::Has(mask, attribute.attribute))
                {
                    continue;
                }
                const auto offset = vertex * stride + assets::OffsetOf(mask, attribute.attribute);
                for (unsigned scalar = 0; scalar < assets::SizeOf(attribute.format) / 4; ++scalar)
                {
                    const auto a = Word(raw, offset + scalar * 4), b = Word(expected, offset + scalar * 4);
                    if (attribute.attribute == assets::VertexAttribute::BoneIndices)
                    {
                        Check(a == b, "Packed bone identity stays exact");
                    }
                    else
                    {
                        Near(std::bit_cast<float>(b), std::bit_cast<float>(a), "Authored attribute error policy",
                            Policy{}.absoluteError[static_cast<unsigned>(attribute.attribute)] + .0000002);
                    }
                }
            }
        }
        for (unsigned mutation = 0; mutation < 5; ++mutation)
        {
            auto bad = archive->packets[0];
            if (mutation == 0)
            {
                bad.pop_back();
            }
            else
            {
                Put(bad, mutation == 1 ? 0 : mutation == 2 ? 12 : mutation == 3 ? 16 : 36, 0xffffffffu);
            }
            std::vector<std::byte> output;
            Check(!Decode(bad, output, error) && output.empty(), "Invalid cluster packet rejected transactionally");
            ++rejected;
        }
        std::vector<std::byte> topology(sizeof(RHIMeshletBufferHeader) + count * 8);
        Put(topology, 0, 5); Put(topology, 80, sizeof(RHIMeshletBufferHeader));
        const auto packetOffset = static_cast<uint32_t>(topology.size());
        topology.insert(topology.end(), archive->packets[0].begin(), archive->packets[0].end());
        for (uint32_t vertex = 0; vertex < count; ++vertex)
        {
            Put(topology, sizeof(RHIMeshletBufferHeader) + vertex * 8, packetOffset);
            Put(topology, sizeof(RHIMeshletBufferHeader) + vertex * 8 + 4, vertex);
        }
        const bool skin = assets::Has(mask, assets::VertexAttribute::BoneIndices);
        const uint32_t boneCount = skin ? 2 : 0;
        std::vector<std::byte> pose(160);
        Put(pose, 0, boneCount);
        std::array<math::matrix4x4, 2> bones{math::matrix4x4::identity(), math::matrix4x4::identity()};
        bones[0].m[3][0] = .3f; bones[1].m[3][1] = -.25f;
        for (uint32_t bone = 0; bone < boneCount; ++bone)
        {
            const auto packed = PackedBoneMatrix::From(bones[bone]);
            std::memcpy(pose.data() + 32 + bone * sizeof(packed), &packed, sizeof(packed));
        }
        Check(device.BeginFrame(error), "Direct cluster begin");
        const auto upload = [&](const auto& data) {
            auto slice = device.AllocateUpload({data.size(), RHIUploadUsage::Raw, 256});
            Check(slice.IsWritable(), "Cluster input upload"); std::memcpy(slice.cpuAddress, data.data(), data.size()); return slice;
        };
        const auto gpuPose = upload(pose), gpuTopology = upload(topology);
        const std::array<uint32_t, 4> constants{count, stride / 4, 0, 0};
        const auto uniform = device.UploadConstants(constants.data(), sizeof(constants));
        std::array<RHIBufferHandle, 2> outputs;
        const std::array<uint64_t, 2> sizes{count * sizeof(SurfacePoint), expected.size()};
        for (unsigned i = 0; i < 2; ++i)
        {
            RHIBufferDesc buffer; buffer.bytes = sizes[i]; buffer.allowUnorderedAccess = true;
            Check(device.CreateBuffer(buffer, outputs[i], error), "Cluster output buffer");
        }
        const RHIBindingDesc bindings[]{RHIBindingDesc::UavBuffer(outputs[0], count, sizeof(SurfacePoint)),
            RHIBindingDesc::UavBuffer(outputs[1], static_cast<uint32_t>(expected.size() / 4), 4)};
        const auto table = device.CreateBindings(bindings);
        auto& encoder = device.GetImmediateEncoder();
        const RHIBufferTransition before[]{ {outputs[0],RHIResourceState::Common,RHIResourceState::UnorderedAccess},
            {outputs[1],RHIResourceState::Common,RHIResourceState::UnorderedAccess} };
        encoder.ResourceBarriers({{}, before});
        encoder.SetPipeline(RHIBindPoint::Compute, kernel.GetGeneration()->GetHandle());
        encoder.SetConstantBuffer(RHIBindPoint::Compute, 0, uniform);
        encoder.SetRootBuffer(RHIBindPoint::Compute, 1, gpuPose); encoder.SetRootBuffer(RHIBindPoint::Compute, 2, gpuTopology);
        encoder.SetBindings(RHIBindPoint::Compute, 3, table); encoder.Dispatch(1,1,1);
        std::array<RHIReadback, 2> reads;
        const RHIBufferTransition after[]{ {outputs[0],RHIResourceState::UnorderedAccess,RHIResourceState::CopySource},
            {outputs[1],RHIResourceState::UnorderedAccess,RHIResourceState::CopySource} };
        encoder.ResourceBarriers({{}, after});
        for (unsigned i = 0; i < 2; ++i)
        {
            Check(device.CreateBufferReadback(sizes[i], reads[i], error), "Direct cluster readback");
            encoder.CopyBufferToReadback(reads[i], outputs[i]);
        }
        Check(device.EndFrame(error), "Direct cluster submit"); device.WaitForGpu();
        std::array<RHIReadbackImage, 2> images;
        for (unsigned i = 0; i < 2; ++i)
        {
            Check(device.MapReadback(reads[i], images[i], error), "Direct cluster map");
        }
        Check(images[1].data.size() >= expected.size() && std::memcmp(images[1].data.data(), expected.data(), expected.size()) == 0,
            "Actual GPU attribute decoder matches CPU quantized bits for every semantic");
        for (uint32_t vertex = 0; vertex < count; ++vertex)
        {
            SurfacePoint point; std::memcpy(&point, images[0].data.data() + vertex * sizeof(point), sizeof(point));
            for (unsigned axis = 0; axis < 3; ++axis)
            {
                double position = std::bit_cast<float>(Word(raw, vertex * stride + axis * 4));
                const float weight = .25f + (vertex % 3) * .25f;
                position += skin ? axis == 0 ? weight * .3 : axis == 1 ? (1 - weight) * -.25 : 0 : 0;
                Near(point.position[axis], position, "Direct dequantization and weighted skinning", .00012);
            }
            const double angle = vertex * .01;
            Check(point.normal[1] * std::sin(angle) + point.normal[2] * std::cos(angle) > .99999,
                "Quantized normal angular agreement");
            Near(point.uvLod[0], vertex / 63., "Direct UV decode", .00011);
        }
        for (unsigned i = 0; i < 2; ++i)
        {
            device.ReleaseReadback(reads[i]); device.ReleaseBuffer(outputs[i]);
        }
        words += expected.size() / 4; ++cases;
    }
    Check(reference != nullptr, "Streaming reference archive");
    RunClusterResidencyState(reference);
    std::string messages;
    Check(device.DrainDebugMessages(messages) == 0, "Direct cluster GPU validation " + messages);
    std::cout << "METEOR_CLUSTER_STORAGE_OK cases=" << cases << " rejected=" << rejected << " words=" << words
        << " seamChecks=" << seamChecks << " validation=0\n";
}
