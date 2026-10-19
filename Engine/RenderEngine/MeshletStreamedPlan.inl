// A descriptor-only partition. Original connectivity remains in QMC2 pages;
// local logical vertex ranges deliberately do not weld across meshlets.
bool MeshSurfacePlan::BuildStreamed(const EnhancedDrawItem& draw, const SurfaceView& view,
                                     const MeshSurfacePlanBudget& budget, std::shared_ptr<const MeshSurfacePlan>& result,
                                     std::string& error)
{
    const auto& geometry = draw.modelMeshView;
    const auto* metadata = geometry.StreamMetadata();
    if (!metadata || !budget.maxChunks || budget.maxChunkPoints < 3 ||
        budget.maxChunkPoints > (1u << 20) || metadata->vertexCount > budget.maxSourceVertices ||
        !view.sceneEpoch || !view.viewRevision || !view.geometryRevision || !Finite(view.eye) || !Affine(draw.worldMatrix) ||
        draw.boneCount > 256 || (draw.boneCount && !draw.bonePalette) ||
        (!assets::Has(geometry.vertexAttributeMask, assets::VertexAttribute::BoneIndices) && draw.boneCount) ||
        (draw.boneCount && !metadata->lods.groups.empty()))
        return Fail(error, "Thin geometry requires bounded descriptors, affine pose and a supported scene hierarchy.");
    uint64_t triangles{}, points{};
    for (const auto& counts : metadata->counts) triangles += counts.indexCount / 3;
    for (const auto& descriptor : *metadata->flatDescriptors) points += descriptor.vertexCount;
    if (triangles > budget.maxTriangles || triangles * 3 > UINT32_MAX || points > UINT32_MAX)
        return Fail(error, "Thin geometry exceeds the logical topology budget.");
    auto candidate = std::shared_ptr<MeshSurfacePlan>(new MeshSurfacePlan);
    auto source = std::shared_ptr<MeshSurfaceInput>(new MeshSurfaceInput);
    source->geometry_ = geometry; source->geometry_.sourceDescriptor = nullptr;
    source->geometry_.sourcePayload = nullptr; source->geometry_.sourceGeneration = nullptr; source->geometry_.sourceMeshIndex = UINT32_MAX;
    source->world_ = draw.worldMatrix; source->view_ = view;
    source->streamedTopology_ = true; source->logicalVertexCount_ = static_cast<uint32_t>(points);
    source->sourceVertexCount_ = metadata->vertexCount; source->sourcePrimitiveCount_ = static_cast<uint32_t>(triangles);
    source->originalIndexCount_ = metadata->counts[0].indexCount;
    source->geometry_.vertexBytes = points * geometry.vertexStride; source->geometry_.indexCount = static_cast<uint32_t>(triangles * 3);
    source->clusterArchive_ = metadata->archive;
    source->clusterStream_ = std::make_shared<assets::cluster::Stream>(metadata->archive, metadata->lods.groups);
    source->hierarchy_.groups = metadata->lods.groups;
    source->hierarchy_.groupCount = static_cast<uint32_t>(metadata->lods.groups.size());
    source->hierarchy_.rootGroup = source->hierarchy_.groupCount ? source->hierarchy_.groupCount - 1 : UINT32_MAX;
    source->meshlets_.settings = metadata->meshlets.settings; source->meshlets_.geometryDigest = metadata->meshlets.geometryDigest;
    source->meshlets_.descriptors = *metadata->flatDescriptors;
    source->meshlets_.lod0 = {0, static_cast<uint32_t>(metadata->flatDescriptors->size())};
    for (uint32_t bone = 0; bone < draw.boneCount; ++bone)
    {
        if (!Affine(draw.bonePalette[bone])) return Fail(error, "Thin geometry has a nonfinite/nonaffine bone matrix.");
        source->bones_.push_back(PackedBoneMatrix::From(draw.bonePalette[bone]));
    }
    source->SealInstanceBounds();
    candidate->source_ = source; candidate->chunkLimit_ = budget.maxChunkPoints;
    auto& cost = candidate->cost_; cost.sourceVertices = metadata->vertexCount; cost.triangles = static_cast<uint32_t>(triangles);
    cost.points = points; cost.sourceReferenceCountsKnown = false; // Exact unique source IDs stay in deferred pages.
    cost.cpuPayloadBytes = sizeof(MeshSurfacePlan) + sizeof(MeshSurfaceInput) +
        source->meshlets_.descriptors.capacity() * sizeof(experiment::MeshletDescriptor) +
        source->hierarchy_.groups.capacity() * sizeof(experiment::MeshClusterGroup) + source->bones_.capacity() * sizeof(PackedBoneMatrix);
    std::shared_ptr<MeshSurfaceInput> chunk;
    uint32_t firstTriangle{}, triangleCursor{}, logicalCursor{};
    const auto start = [&] {
        chunk = std::shared_ptr<MeshSurfaceInput>(new MeshSurfaceInput);
        chunk->geometry_ = source->geometry_; chunk->geometry_.vertexBytes = 0; chunk->geometry_.indexCount = 0;
        chunk->world_ = source->world_; chunk->view_ = source->view_; chunk->bones_ = source->bones_;
        chunk->streamedTopology_ = true; chunk->sourceVertexCount_ = source->sourceVertexCount_;
        chunk->sourcePrimitiveCount_ = source->sourcePrimitiveCount_; chunk->originalIndexCount_ = source->originalIndexCount_;
        chunk->clusterArchive_ = source->clusterArchive_; chunk->clusterStream_ = source->clusterStream_; chunk->hierarchy_ = source->hierarchy_;
        chunk->meshlets_.settings = source->meshlets_.settings; chunk->meshlets_.geometryDigest = source->meshlets_.geometryDigest;
        firstTriangle = triangleCursor;
    };
    const auto finish = [&] {
        if (!chunk || chunk->meshlets_.descriptors.empty()) return;
        chunk->meshlets_.lod0 = {0, static_cast<uint32_t>(chunk->meshlets_.descriptors.size())};
        chunk->geometry_.vertexBytes = uint64_t(chunk->Count()) * geometry.vertexStride;
        cost.cpuPayloadBytes += sizeof(MeshSurfaceInput) + sizeof(MeshSurfaceChunk) +
            chunk->meshlets_.descriptors.capacity() * sizeof(experiment::MeshletDescriptor) +
            chunk->hierarchy_.groups.capacity() * sizeof(experiment::MeshClusterGroup) + chunk->bones_.capacity() * sizeof(PackedBoneMatrix);
        cost.gpuPayloadBytes += sizeof(RHIMeshletBufferHeader) + chunk->meshlets_.descriptors.size() * (sizeof(experiment::MeshletDescriptor) + 8ull) +
            chunk->hierarchy_.GpuBytes() + chunk->hierarchy_.groupCount * 4ull + chunk->Count() * 8ull + 256 + chunk->bones_.size() * sizeof(PackedBoneMatrix);
        chunk->SealInstanceBounds();
        MeshSurfaceChunk item; item.input = chunk; item.firstTriangle = firstTriangle; candidate->chunks_.push_back(std::move(item));
    };
    start();
    for (auto& sourceDescriptor : source->meshlets_.descriptors)
    {
        if (sourceDescriptor.vertexCount > budget.maxChunkPoints) return Fail(error, "Thin chunk cannot contain one meshlet.");
        if (chunk->Count() + sourceDescriptor.vertexCount > budget.maxChunkPoints)
        {
            finish();
            if (candidate->chunks_.size() >= budget.maxChunks) return Fail(error, "Thin geometry exceeds the chunk count budget.");
            start();
        }
        auto descriptor = sourceDescriptor;
        sourceDescriptor.vertexOffset = logicalCursor; logicalCursor += descriptor.vertexCount;
        descriptor.vertexOffset = chunk->Count();
        chunk->logicalVertexCount_ += descriptor.vertexCount; chunk->geometry_.indexCount += descriptor.triangleCount * 3;
        chunk->meshlets_.descriptors.push_back(descriptor); triangleCursor += descriptor.triangleCount;
        cost.gpuPayloadBytes += metadata->archive->PacketBytes(descriptor.clusterId);
    }
    finish();
    if (cost.cpuPayloadBytes > budget.maxCpuPayloadBytes || cost.gpuPayloadBytes > budget.maxGpuPayloadBytes)
        return Fail(error, "Thin geometry exceeds CPU/GPU payload budgets.");
    result = std::move(candidate); error.clear(); return true;
}
