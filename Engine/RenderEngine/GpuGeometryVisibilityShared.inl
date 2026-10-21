// Included after BufferPool in GpuGeometryVisibility.cpp.
namespace
{
    ce::RenderArenaLease AcquireVisibilityInputs()
    {
        // Each call owns its storage, including nested declarations assisted by
        // an upload join. The existing pool returns raw blocks through CAS slots.
        static const ce::RenderArenaPool pool;
        return pool.Acquire();
    }

    template<class T>
    T* VisibilityInputArray(const ce::RenderArenaLease& arena, size_t count)
    {
        static_assert(std::is_trivially_destructible_v<T>);
        return static_cast<T*>(arena->Resource(0).allocate(count * sizeof(T), alignof(T)));
    }
} // namespace

struct GpuGeometryVisibility::SharedBatch
{
    struct ViewData
    {
        math::vector4 projection[4], history[4];
        std::uint32_t width{}, height{}, levels{}, valid{};
        std::uint32_t mode{}, bins{}, outputWords{}, reserved{};
    };
    static_assert(sizeof(ViewData) == 160);
    std::shared_ptr<BufferPool> pool;
    IRenderDeviceServices* device{};
    RHIBufferHandle queue;
    RGHandle graphQueue;
    std::uint32_t jobs{}, groups{}, bins{}, views{};
    bool ordered{};
    // Sealed inputs have no back-reference to Frame. Keep diagnostic counts
    // through these owners instead of copying them or retaining upload scratch.
    std::array<std::shared_ptr<const MeshletInputs>, kSharedViewCapacity> inputs;
    RHIBufferSlice constants, candidates, binData, worlds, viewData;
    RHIBindingTable outputs, vertices, topologies, ids, arguments, first;
    std::array<std::shared_ptr<const GpuGeometryOcclusion::Pyramid>, kSharedViewCapacity> pyramids;
    std::shared_ptr<const LX::Runtime::ComputeGeneration> reset, cull, count, finalize, scatter;
    static constexpr std::uint64_t kQueueBytes = (kWorkRingHeaderWords + kWorkRingSlots) * sizeof(std::uint32_t);
    ~SharedBatch()
    {
        // Every contributing frame is retained by its upload transaction until
        // completion/rejection. Graph callbacks retain the whole set as well.
        if (pool)
        {
            pool->Return(queue, kQueueBytes, false, true);
        }
    }
    void Dispatch(const EnhancedRenderGraph::ExecuteContext& execution, MeshletStage stage) const
    {
        auto& encoder = *execution.encoder;
        const auto& pipeline = stage == MeshletStage::Reset      ? reset
                               : stage == MeshletStage::Cull     ? cull
                               : stage == MeshletStage::Count    ? count
                               : stage == MeshletStage::Finalize ? finalize
                                                                 : scatter;
        encoder.SetPipeline(RHIBindPoint::Compute, pipeline->GetHandle());
        encoder.SetConstantBuffer(RHIBindPoint::Compute, 0, constants);
        encoder.SetRootBuffer(RHIBindPoint::Compute, 1, candidates);
        encoder.SetRootBuffer(RHIBindPoint::Compute, 2, binData);
        encoder.SetBindings(RHIBindPoint::Compute, 3, outputs);
        encoder.SetBindings(RHIBindPoint::Compute, 4, vertices);
        encoder.SetBindings(RHIBindPoint::Compute, 5, topologies);
        encoder.SetRootBuffer(RHIBindPoint::Compute, 6, worlds);
        encoder.SetRootBuffer(RHIBindPoint::Compute, 7, viewData);
        encoder.SetBindings(RHIBindPoint::Compute, 8, ids);
        encoder.SetBindings(RHIBindPoint::Compute, 9, arguments);
        encoder.SetBindings(RHIBindPoint::Compute, 10, first);
        if (stage == MeshletStage::Cull)
        {
            const auto fallback = std::ranges::find_if(pyramids, [](const auto& pyramid) { return bool(pyramid); });
            if (fallback != pyramids.end())
            {
                std::array<RHIBindingDesc, kSharedViewCapacity * kOcclusionLevels> descriptions;
                for (unsigned view = 0; view < kSharedViewCapacity; ++view)
                {
                    const auto& pyramid = view < pyramids.size() && pyramids[view] ? pyramids[view] : *fallback;
                    const auto levels = pyramid->BindingInputs(execution);
                    std::copy(levels.begin(), levels.end(), descriptions.begin() + view * kOcclusionLevels);
                }
                const auto textures = device->CreateBindings(descriptions);
                if (!textures.IsValid())
                {
                    throw std::runtime_error("Shared visibility HZB bindings failed.");
                }
                encoder.SetBindings(RHIBindPoint::Compute, 11, textures);
            }
        }
        DispatchGroups(encoder, stage == MeshletStage::Cull
                                    ? (std::min)(bins * kInitialSubdivisions, kPersistentConsumerGroups)
                                : stage == MeshletStage::Count || stage == MeshletStage::Scatter ? groups
                                : stage == MeshletStage::Finalize && !ordered ? VisibilityGroups(bins)
                                                                              : bins);
    }
};

std::uint64_t GpuGeometryVisibility::Frame::WorkQueueByteOffset() const
{
    return m_sharedBatch ? 0 : std::uint64_t(m_outputCount) * sizeof(std::uint32_t);
}
RHIBufferHandle GpuGeometryVisibility::Frame::WorkQueueBuffer() const
{
    return m_sharedBatch ? m_sharedBatch->queue : m_visibleIds;
}
RGHandle GpuGeometryVisibility::Frame::GraphWorkQueue(const EnhancedRenderGraph& graph) const
{
    CheckCurrent(&graph);
    return m_sharedBatch ? m_sharedBatch->graphQueue : m_graphVisibleIds;
}
std::uint32_t GpuGeometryVisibility::Frame::WorkQueueTaskCount() const
{
    return m_sharedBatch ? m_sharedBatch->jobs : m_meshletJobs;
}
std::uint32_t GpuGeometryVisibility::Frame::WorkQueuePartitionCount() const
{
    return (m_sharedBatch ? m_sharedBatch->bins : m_binCount) * kInitialSubdivisions;
}
std::uint32_t GpuGeometryVisibility::Frame::WorkQueueViewCount() const
{
    return m_sharedBatch ? m_sharedBatch->views : 1u;
}
std::uint32_t GpuGeometryVisibility::Frame::WorkQueueNonemptyPartitions(std::uint32_t width) const
{
    if (width == 0)
    {
        throw std::runtime_error("A nonzero active wave width is required.");
    }
    std::uint32_t count{};
    if (m_sharedBatch)
    {
        for (const auto& input : m_sharedBatch->inputs)
        {
            if (input)
            {
                for (const auto& bin : std::span(input->bins.data(), input->bins.size()))
                {
                    count += (std::min)(kInitialSubdivisions, (bin.indexCount + width - 1u) / width);
                }
            }
        }
    }
    else
    {
        for (const auto& geometry : m_meshletGeometries)
        {
            count += (std::min)(kInitialSubdivisions, (geometry->MeshletCount() + width - 1u) / width);
        }
    }
    return count;
}

void GpuGeometryVisibility::Frame::UploadMeshletInputs() const
{
    CheckCurrent();
    if (m_constants.IsValid())
        return;
    if (!m_meshletInputs || m_meshletInputs->bins.size() != m_meshletGeometries.size())
        throw std::runtime_error("Meshlet upload requires its sealed input ranges.");
    const auto& input = *m_meshletInputs;
    const RHIUploadRequest requests[]{
        {sizeof(input.constants), RHIUploadUsage::ConstantBuffer, 256},
        {input.candidates.size() * sizeof(Candidate), RHIUploadUsage::Raw, 256},
        {input.bins.size() * sizeof(Bin), RHIUploadUsage::Raw, 256},
        {m_meshletGeometries.size() * kMeshletInstanceRows * sizeof(math::vector4), RHIUploadUsage::Raw, 16}};
    std::array<RHIBufferSlice, 4> uploads;
    std::string error;
    if (!m_device->ReserveUploadBatch(requests, uploads, error) ||
        std::ranges::any_of(uploads, [](const auto& upload) { return !upload.IsWritable(); }))
        throw std::runtime_error("Meshlet input upload failed: " + error);
    std::memcpy(uploads[0].cpuAddress, &input.constants, sizeof(input.constants));
    std::memcpy(uploads[1].cpuAddress, input.candidates.data(), requests[1].bytes);
    std::memcpy(uploads[2].cpuAddress, input.bins.data(), requests[2].bytes);
    auto* worlds = static_cast<math::vector4*>(uploads[3].cpuAddress);
    static thread_local BufferPool::UploadScratch scratch;
    auto& vertices = scratch.m_vertices;
    auto& topologies = scratch.m_topologies;
    vertices.clear();
    topologies.clear();
    const auto descriptorCount = MeshletDescriptorCount(*m_device, m_binCount);
    vertices.reserve(descriptorCount);
    topologies.reserve(descriptorCount);
    for (std::size_t bin = 0; bin < m_meshletGeometries.size(); ++bin)
    {
        const auto& geometry = m_meshletGeometries[bin];
        const auto& world = geometry->ObjectToWorld();
        for (unsigned column = 0; column < 3; ++column)
            worlds[bin * kMeshletInstanceRows + column] = {world.m[0][column], world.m[1][column], world.m[2][column],
                                                           world.m[3][column]};
        const bool instanceBounds = geometry->UsesDirectClusters() && geometry->IsObjectSpace();
        worlds[bin * kMeshletInstanceRows + 3u] =
            instanceBounds ? geometry->Input()->InstanceMinimum() : math::vector4{};
        worlds[bin * kMeshletInstanceRows + 4u] =
            instanceBounds ? geometry->Input()->InstanceMaximum() : math::vector4{};
        const auto topology = geometry->Meshlets();
        vertices.push_back(RHIBindingDesc::SrvBuffer(geometry->Buffer(), geometry->GeometryViewElements(),
                                                      material_graph::MeshSurfaceBatch::GeometryViewStride));
        topologies.push_back(RHIBindingDesc::SrvBuffer(topology.buffer, static_cast<std::uint32_t>(topology.size / 4),
                                                        0, static_cast<std::uint32_t>(topology.offset / 4)));
    }
    // The shader indexes only live bins. Backends without partially-bound SRV
    // arrays still require every descriptor in the fixed declaration to exist.
    vertices.resize(descriptorCount, vertices.front());
    topologies.resize(descriptorCount, topologies.front());
    RHIBufferSlice historyProjection;
    if (m_historyPyramid || m_cameraPredictionView)
    {
        math::vector4 columns[4];
        const auto& bits = m_cameraPredictionView ? m_cameraPredictionView->projectionBits
                                                  : m_historyPyramid->HistoryOwner()->source.projectionBits;
        for (unsigned column = 0; column < 4; ++column)
            columns[column] = {std::bit_cast<float>(bits[column]), std::bit_cast<float>(bits[4 + column]),
                               std::bit_cast<float>(bits[8 + column]), std::bit_cast<float>(bits[12 + column])};
        historyProjection = m_device->UploadConstants(columns, sizeof(columns));
        if (!historyProjection.IsValid())
            throw std::runtime_error("Meshlet history projection upload failed.");
    }
    const auto vertexBindings = m_device->CreateBindings(vertices);
    const auto topologyBindings = m_device->CreateBindings(topologies);
    const RHIBindingDesc outputs[]{
        RHIBindingDesc::UavBuffer(m_visibleIds, static_cast<std::uint32_t>(m_visibleBytes / 4), 4),
        RHIBindingDesc::UavBuffer(m_arguments, m_binCount, sizeof(RHIDrawIndexedIndirectArguments))};
    const auto outputBindings = m_device->CreateBindings(outputs);
    if (!vertexBindings.IsValid() || !topologyBindings.IsValid() || !outputBindings.IsValid())
        throw std::runtime_error("Meshlet input bindings are incomplete.");
    CheckCurrent();
    m_candidates = uploads[1];
    m_bins = uploads[2];
    m_meshletWorlds = uploads[3];
    m_historyProjection = historyProjection;
    m_meshletVertices = vertexBindings;
    m_meshletTopologies = topologyBindings;
    m_outputs = outputBindings;
    m_constants = uploads[0]; // Publish readiness only after the complete upload.
}

std::shared_ptr<GpuGeometryVisibility::SharedBatch> GpuGeometryVisibility::Frame::BuildShared(
    std::span<const std::shared_ptr<const Frame>> frames)
{
    ce::profile_scope profile{ce::marker<"GpuVisibility.SharedBuild">()};
    if (frames.empty() || frames.size() > kSharedViewCapacity)
    {
        throw std::runtime_error("Shared visibility requires one to eight sealed views.");
    }
    auto batch = ce::make_render_shared<SharedBatch>();
    const auto arena = AcquireVisibilityInputs();
    const auto& firstFrame = *frames.front();
    batch->device = firstFrame.m_device;
    batch->pool = firstFrame.m_bufferPool;
    batch->views = static_cast<std::uint32_t>(frames.size());
    batch->reset = firstFrame.m_sharedReset;
    batch->count = firstFrame.m_sharedCount;
    batch->finalize = firstFrame.m_sharedFinalize;
    batch->scatter = firstFrame.m_sharedScatter;
    std::array<RHIBindingDesc, kSharedViewCapacity> ids, arguments, first;
    std::size_t totalBins{};
    for (const auto& frame : frames)
    {
        frame->CheckCurrent();
        if (!frame->m_meshletInputs || frame->m_device != firstFrame.m_device ||
            frame->m_recording != firstFrame.m_recording || frame->m_descriptors != firstFrame.m_descriptors ||
            frames.size() > kSharedViewCapacity)
        {
            throw std::runtime_error("Shared visibility requires one sealed device/upload/descriptor recording.");
        }
        totalBins += frame->m_binCount;
        batch->ordered = batch->ordered || frame->m_ordered;
    }
    if (totalBins == 0 || totalBins > kMeshletBinCapacity)
    {
        throw std::runtime_error("Shared visibility exceeds its bounded bin capacity.");
    }
    const RHIUploadRequest requests[]{
        {sizeof(GeometryVisibilityConstants), RHIUploadUsage::ConstantBuffer, 256},
        {totalBins * sizeof(Candidate), RHIUploadUsage::Raw, 256},
        {totalBins * sizeof(Bin), RHIUploadUsage::Raw, 256},
        {totalBins * kMeshletInstanceRows * sizeof(math::vector4), RHIUploadUsage::Raw, 16},
        {frames.size() * sizeof(SharedBatch::ViewData), RHIUploadUsage::Raw, 16},
        {sizeof(std::uint32_t), RHIUploadUsage::Raw, 16}};
    std::array<RHIBufferSlice, 6> uploads;
    std::string error;
    if (!batch->device->ReserveUploadBatch(requests, uploads, error) ||
        std::ranges::any_of(uploads, [](const auto& upload) { return !upload.IsWritable(); }))
    {
        throw std::runtime_error("Shared visibility upload failed: " + error);
    }
    batch->constants = uploads[0];
    batch->candidates = uploads[1];
    batch->binData = uploads[2];
    batch->worlds = uploads[3];
    batch->viewData = uploads[4];
    const auto dummy = uploads[5];
    *static_cast<std::uint32_t*>(dummy.cpuAddress) = 0;
    auto* candidates = static_cast<Candidate*>(batch->candidates.cpuAddress);
    auto* bins = static_cast<Bin*>(batch->binData.cpuAddress);
    auto* worlds = static_cast<math::vector4*>(batch->worlds.cpuAddress);
    auto* views = static_cast<SharedBatch::ViewData*>(batch->viewData.cpuAddress);
    const auto descriptorCount = MeshletDescriptorCount(*batch->device, static_cast<std::uint32_t>(totalBins));
    auto* vertices = VisibilityInputArray<RHIBindingDesc>(arena, descriptorCount);
    auto* topologies = VisibilityInputArray<RHIBindingDesc>(arena, descriptorCount);
    std::uint64_t jobs{};
    std::uint32_t viewIndex{}, binIndex{};
    for (const auto& frame : frames)
    {
        const auto* source = &frame->m_meshletInputs->constants;
        SharedBatch::ViewData view{};
        std::copy(std::begin(source->projectionColumns), std::end(source->projectionColumns), view.projection);
        std::copy(std::begin(source->projectionColumns), std::end(source->projectionColumns), view.history);
        view.width = source->width;
        view.height = source->height;
        view.levels = source->occlusionLevels;
        view.valid = source->projectionValid;
        view.bins = frame->m_binCount;
        view.outputWords = frame->m_outputCount;
        view.mode = frame->m_firstStage             ? 3u
                    : frame->m_cameraPredictionView ? 4u
                    : frame->m_historyPyramid       ? 2u
                    : frame->m_pyramid              ? 1u
                                                    : 0u;
        if (frame->m_historyPyramid || frame->m_cameraPredictionView)
        {
            const auto& bits = frame->m_cameraPredictionView
                                   ? frame->m_cameraPredictionView->projectionBits
                                   : frame->m_historyPyramid->HistoryOwner()->source.projectionBits;
            for (unsigned column = 0; column < 4; ++column)
            {
                view.history[column] = {std::bit_cast<float>(bits[column]), std::bit_cast<float>(bits[4 + column]),
                                        std::bit_cast<float>(bits[8 + column]),
                                        std::bit_cast<float>(bits[12 + column])};
            }
        }
        views[viewIndex] = view;
        batch->inputs[viewIndex] = frame->m_meshletInputs;
        batch->pyramids[viewIndex] = frame->m_pyramid;
        ids[viewIndex] =
            RHIBindingDesc::UavBuffer(frame->m_visibleIds, static_cast<std::uint32_t>(frame->m_visibleBytes / 4), 4);
        arguments[viewIndex] =
            RHIBindingDesc::UavBuffer(frame->m_arguments, frame->m_binCount, sizeof(RHIDrawIndexedIndirectArguments));
        const auto* original = frame->m_meshletInputs->candidates.data();
        const auto* originalBins = frame->m_meshletInputs->bins.data();
        const auto firstBin = binIndex;
        for (unsigned bin = 0; bin < frame->m_binCount; ++bin)
        {
            if (binIndex >= kMeshletBinCapacity || jobs + originalBins[bin].indexCount >= UINT32_MAX)
            {
                throw std::runtime_error("Shared visibility exceeds its bounded bin/job capacity.");
            }
            auto candidate = original[bin];
            candidate.sphere.z += static_cast<float>(firstBin);
            candidate.bin = viewIndex;
            candidate.sourceIndex = static_cast<std::uint32_t>(jobs);
            candidate.outputOffset = bin;
            candidates[binIndex] = candidate;
            bins[binIndex] = originalBins[bin];
            bins[binIndex].preservedInstanceCount = batch->groups;
            batch->groups += VisibilityGroups(originalBins[bin].indexCount);
            jobs += originalBins[bin].indexCount;
            const auto& geometry = frame->m_meshletGeometries[bin];
            const auto& world = geometry->ObjectToWorld();
            for (unsigned column = 0; column < 3; ++column)
            {
                worlds[binIndex * kMeshletInstanceRows + column] = {world.m[0][column], world.m[1][column],
                                                                    world.m[2][column], world.m[3][column]};
            }
            const bool instanceBounds = geometry->UsesDirectClusters() && geometry->IsObjectSpace();
            worlds[binIndex * kMeshletInstanceRows + 3u] =
                instanceBounds ? geometry->Input()->InstanceMinimum() : math::vector4{};
            worlds[binIndex * kMeshletInstanceRows + 4u] =
                instanceBounds ? geometry->Input()->InstanceMaximum() : math::vector4{};
            std::construct_at(vertices + binIndex,
                              RHIBindingDesc::SrvBuffer(geometry->Buffer(), geometry->GeometryViewElements(),
                                                        material_graph::MeshSurfaceBatch::GeometryViewStride));
            const auto topology = geometry->Meshlets();
            std::construct_at(topologies + binIndex,
                              RHIBindingDesc::SrvBuffer(topology.buffer, static_cast<std::uint32_t>(topology.size / 4),
                                                        0, static_cast<std::uint32_t>(topology.offset / 4)));
            ++binIndex;
        }
        ++viewIndex;
    }
    batch->jobs = static_cast<std::uint32_t>(jobs);
    batch->bins = binIndex;
    for (size_t bin = binIndex; bin < descriptorCount; ++bin)
    {
        std::construct_at(vertices + bin, vertices[0]);
        std::construct_at(topologies + bin, topologies[0]);
    }
    GeometryVisibilityConstants constants{};
    constants.candidateCount = constants.binCount = batch->bins;
    constants.planeMask = kWaveWorkQueue;
    if (batch->ordered)
    {
        constants.planeMask |= kOrderedScanDispatch;
    }
    constants.padding = batch->jobs; // Diagnostic only; JOB_COUNT controls completion.
    std::memcpy(batch->constants.cpuAddress, &constants, sizeof(constants));
    for (std::size_t index = 0; index < frames.size(); ++index)
    {
        const auto& frame = frames[index];
        if (frame->m_firstStage)
        {
            first[index] =
                RHIBindingDesc::SrvBuffer(frame->m_firstStage->m_visibleIds, frame->m_firstStage->m_outputCount, 4);
        }
        else
        {
            first[index] = RHIBindingDesc::SrvBuffer(dummy.buffer, 1, 4, static_cast<std::uint32_t>(dummy.offset / 4));
        }
    }
    for (std::size_t index = frames.size(); index < kSharedViewCapacity; ++index)
    {
        ids[index] = ids.front();
        arguments[index] = arguments.front();
        first[index] = first.front();
    }
    if (!batch->pool->Acquire(SharedBatch::kQueueBytes, false, batch->queue, error))
    {
        throw std::runtime_error(error);
    }
    const RHIBindingDesc outputs[]{RHIBindingDesc::UavBuffer(batch->queue, kWorkRingHeaderWords + kWorkRingSlots, 4),
                                   arguments.front()};
    {
        // CPU 배열 조립과 descriptor 테이블 생성은 같은 Declare 안에서도 별도 비용이다.
        ce::profile_scope bindingsProfile{ce::marker<"GpuVisibility.SharedBindings">()};
        batch->outputs = batch->device->CreateBindings(outputs);
        batch->vertices = batch->device->CreateBindings({vertices, descriptorCount});
        batch->topologies = batch->device->CreateBindings({topologies, descriptorCount});
        batch->ids = batch->device->CreateBindings(ids);
        batch->arguments = batch->device->CreateBindings(arguments);
        batch->first = batch->device->CreateBindings(first);
        if (!batch->outputs.IsValid() || !batch->vertices.IsValid() || !batch->topologies.IsValid() ||
            !batch->ids.IsValid() || !batch->arguments.IsValid() || !batch->first.IsValid())
        {
            throw std::runtime_error("Shared visibility descriptor table allocation failed.");
        }
    }
    const bool occlusion = std::ranges::any_of(batch->pyramids, [](const auto& pyramid) { return bool(pyramid); });
    batch->cull = occlusion ? firstFrame.m_sharedOcclusion : firstFrame.m_sharedCull;
    for (const auto& frame : frames)
    {
        frame->CheckCurrent();
    }
    return batch;
}
