// Included after BufferPool in GpuGeometryVisibility.cpp.
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
    std::uint32_t jobs{}, bins{}, views{};
    std::vector<std::uint32_t> counts;
    RHIBufferSlice constants, candidates, binData, worlds, viewData;
    RHIBindingTable outputs, vertices, topologies, ids, arguments, first;
    std::vector<std::shared_ptr<const GpuGeometryOcclusion::Pyramid>> pyramids;
    std::shared_ptr<const LX::Runtime::ComputeGeneration> reset, cull, finalize;
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
    void Dispatch(const EnhancedRenderGraph::ExecuteContext& execution, unsigned stage) const
    {
        auto& encoder = *execution.encoder;
        const auto& pipeline = stage == 0 ? reset : stage == 1 ? cull : finalize;
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
        if (stage == 1)
        {
            const auto fallback = std::ranges::find_if(pyramids, [](const auto& pyramid) { return bool(pyramid); });
            if (fallback != pyramids.end())
            {
                std::vector<RHIBindingDesc> descriptions;
                for (unsigned view = 0; view < kSharedViewCapacity; ++view)
                {
                    const auto& pyramid = view < pyramids.size() && pyramids[view] ? pyramids[view] : *fallback;
                    const auto levels = pyramid->BindingInputs(execution);
                    descriptions.insert(descriptions.end(), levels.begin(), levels.end());
                }
                const auto textures = device->CreateBindings(descriptions);
                if (!textures.IsValid())
                {
                    throw std::runtime_error("Shared visibility HZB bindings failed.");
                }
                encoder.SetBindings(RHIBindPoint::Compute, 11, textures);
            }
        }
        DispatchGroups(encoder, stage == 1 ? (std::min)(bins * kInitialSubdivisions, kPersistentConsumerGroups)
                                           : VisibilityGroups(bins));
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
        for (const auto meshlets : m_sharedBatch->counts)
        {
            count += (std::min)(kInitialSubdivisions, (meshlets + width - 1u) / width);
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

std::shared_ptr<GpuGeometryVisibility::SharedBatch> GpuGeometryVisibility::Frame::BuildShared(
    std::span<const std::shared_ptr<const Frame>> frames)
{
    ce::profile_scope profile{ce::marker<"GpuVisibility.SharedBuild">()};
    auto batch = std::make_shared<SharedBatch>();
    const auto& firstFrame = *frames.front();
    batch->device = firstFrame.m_device;
    batch->pool = firstFrame.m_bufferPool;
    batch->views = static_cast<std::uint32_t>(frames.size());
    batch->reset = firstFrame.m_sharedReset;
    batch->finalize = firstFrame.m_sharedFinalize;
    std::vector<Candidate> candidates;
    std::vector<Bin> bins;
    std::vector<math::vector4> worlds;
    std::vector<SharedBatch::ViewData> views;
    std::vector<RHIBindingDesc> vertices, topologies, ids, arguments, first;
    std::size_t totalBins{};
    for (const auto& frame : frames)
    {
        totalBins += frame->m_binCount;
    }
    candidates.reserve(totalBins);
    bins.reserve(totalBins);
    worlds.reserve(totalBins * 3u);
    vertices.reserve(totalBins);
    topologies.reserve(totalBins);
    batch->counts.reserve(totalBins);
    views.reserve(frames.size());
    batch->pyramids.reserve(frames.size());
    ids.reserve(kSharedViewCapacity);
    arguments.reserve(kSharedViewCapacity);
    first.reserve(kSharedViewCapacity);
    std::uint64_t jobs{};
    for (const auto& frame : frames)
    {
        if (frame->m_device != firstFrame.m_device || frame->m_recording != firstFrame.m_recording ||
            frame->m_descriptors != firstFrame.m_descriptors || frames.size() > kSharedViewCapacity)
        {
            throw std::runtime_error("Shared visibility requires the same device/upload/descriptor recording.");
        }
        const auto* source = static_cast<const GeometryVisibilityConstants*>(frame->m_constants.cpuAddress);
        SharedBatch::ViewData view{};
        std::copy(std::begin(source->projectionColumns), std::end(source->projectionColumns), view.projection);
        std::copy(std::begin(source->projectionColumns), std::end(source->projectionColumns), view.history);
        view.width = source->width;
        view.height = source->height;
        view.levels = source->occlusionLevels;
        view.valid = source->projectionValid;
        view.bins = frame->m_binCount;
        view.outputWords = frame->m_outputCount;
        view.mode = frame->m_firstStage ? 3u : frame->m_historyPyramid ? 2u : frame->m_pyramid ? 1u : 0u;
        if (frame->m_historyPyramid)
        {
            const auto& bits = frame->m_historyPyramid->HistoryOwner()->source.projectionBits;
            for (unsigned column = 0; column < 4; ++column)
            {
                view.history[column] = {std::bit_cast<float>(bits[column]), std::bit_cast<float>(bits[4 + column]),
                                        std::bit_cast<float>(bits[8 + column]),
                                        std::bit_cast<float>(bits[12 + column])};
            }
        }
        const auto viewIndex = static_cast<std::uint32_t>(views.size());
        views.push_back(view);
        batch->pyramids.push_back(frame->m_pyramid);
        ids.push_back(
            RHIBindingDesc::UavBuffer(frame->m_visibleIds, static_cast<std::uint32_t>(frame->m_visibleBytes / 4), 4));
        arguments.push_back(
            RHIBindingDesc::UavBuffer(frame->m_arguments, frame->m_binCount, sizeof(RHIDrawIndexedIndirectArguments)));
        const auto* original = static_cast<const Candidate*>(frame->m_candidates.cpuAddress);
        const auto* originalBins = static_cast<const Bin*>(frame->m_bins.cpuAddress);
        const auto* originalWorlds = static_cast<const math::vector4*>(frame->m_meshletWorlds.cpuAddress);
        for (unsigned bin = 0; bin < frame->m_binCount; ++bin)
        {
            if (bins.size() >= kMeshletBinCapacity || jobs + originalBins[bin].indexCount >= UINT32_MAX)
            {
                throw std::runtime_error("Shared visibility exceeds its bounded bin/job capacity.");
            }
            auto candidate = original[bin];
            candidate.bin = viewIndex;
            candidate.sourceIndex = static_cast<std::uint32_t>(jobs);
            candidate.outputOffset = bin;
            candidates.push_back(candidate);
            bins.push_back(originalBins[bin]);
            jobs += originalBins[bin].indexCount;
            batch->counts.push_back(originalBins[bin].indexCount);
            worlds.insert(worlds.end(), originalWorlds + bin * 3u, originalWorlds + bin * 3u + 3u);
            const auto& geometry = frame->m_meshletGeometries[bin];
            vertices.push_back(RHIBindingDesc::SrvBuffer(geometry->Buffer(), geometry->GeometryViewElements(),
                                                         material_graph::MeshSurfaceBatch::GeometryViewStride));
            const auto topology = geometry->Meshlets();
            topologies.push_back(RHIBindingDesc::SrvBuffer(topology.buffer,
                                                           static_cast<std::uint32_t>(topology.size / 4), 0,
                                                           static_cast<std::uint32_t>(topology.offset / 4)));
        }
    }
    batch->jobs = static_cast<std::uint32_t>(jobs);
    batch->bins = static_cast<std::uint32_t>(bins.size());
    const auto upload = [&](const void* data, std::size_t bytes, RHIUploadUsage usage = RHIUploadUsage::Raw) {
        const auto slice = batch->device->AllocateUpload({bytes, usage, 256});
        if (!slice.IsWritable())
        {
            throw std::runtime_error("Shared visibility upload failed.");
        }
        std::memcpy(slice.cpuAddress, data, bytes);
        return slice;
    };
    GeometryVisibilityConstants constants{};
    constants.candidateCount = constants.binCount = batch->bins;
    constants.planeMask = kWaveWorkQueue;
    constants.padding = batch->jobs; // Diagnostic only; JOB_COUNT controls completion.
    batch->constants = upload(&constants, sizeof(constants), RHIUploadUsage::ConstantBuffer);
    batch->candidates = upload(candidates.data(), candidates.size() * sizeof(Candidate));
    batch->binData = upload(bins.data(), bins.size() * sizeof(Bin));
    batch->worlds = upload(worlds.data(), worlds.size() * sizeof(math::vector4));
    batch->viewData = upload(views.data(), views.size() * sizeof(SharedBatch::ViewData));
    const std::uint32_t zero{};
    const auto dummy = upload(&zero, sizeof(zero));
    for (const auto& frame : frames)
    {
        if (frame->m_firstStage)
        {
            first.push_back(
                RHIBindingDesc::SrvBuffer(frame->m_firstStage->m_visibleIds, frame->m_firstStage->m_outputCount, 4));
        }
        else
        {
            first.push_back(
                RHIBindingDesc::SrvBuffer(dummy.buffer, 1, 4, static_cast<std::uint32_t>(dummy.offset / 4)));
        }
    }
    while (ids.size() < kSharedViewCapacity)
    {
        ids.push_back(ids.front());
        arguments.push_back(arguments.front());
        first.push_back(first.front());
    }
    std::string error;
    if (!batch->pool->Acquire(SharedBatch::kQueueBytes, false, batch->queue, error))
    {
        throw std::runtime_error(error);
    }
    const RHIBindingDesc outputs[]{RHIBindingDesc::UavBuffer(batch->queue, kWorkRingHeaderWords + kWorkRingSlots, 4),
                                   arguments.front()};
    batch->outputs = batch->device->CreateBindings(outputs);
    batch->vertices = batch->device->CreateBindings(vertices);
    batch->topologies = batch->device->CreateBindings(topologies);
    batch->ids = batch->device->CreateBindings(ids);
    batch->arguments = batch->device->CreateBindings(arguments);
    batch->first = batch->device->CreateBindings(first);
    if (!batch->outputs.IsValid() || !batch->vertices.IsValid() || !batch->topologies.IsValid() ||
        !batch->ids.IsValid() || !batch->arguments.IsValid() || !batch->first.IsValid())
    {
        throw std::runtime_error("Shared visibility descriptor table allocation failed.");
    }
    const bool occlusion = std::ranges::any_of(batch->pyramids, [](const auto& pyramid) { return bool(pyramid); });
    batch->cull = occlusion ? firstFrame.m_sharedOcclusion : firstFrame.m_sharedCull;
    return batch;
}
