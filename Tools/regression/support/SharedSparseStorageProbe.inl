namespace shared_storage_probe
{
using namespace material_graph;
#include "../../../Engine/RenderEngine/MeshletStreamingTopology.inl"
#include "../../../Engine/RenderEngine/MeshletSparseStorage.inl"
} // namespace shared_storage_probe

void RunSharedSparseStorage(RecordingChangeDevice& device)
{
    using namespace shared_storage_probe;
    std::string error;
    const auto budgetBefore = device.GetPersistentMemoryBudgetStats();
    auto pool = own::make_shared<RHISharedSparsePagePool>(device, 3);
    auto a = pool->Create(4 * RHISparseTileBytes, error);
    auto b = pool->Create(4 * RHISparseTileBytes, error);
    auto c = pool->Create(4 * RHISparseTileBytes, error);
    Check(a && b && c, "Three asset virtual buffers share a physical pool " + error);
    Check(device.GetPersistentMemoryBudgetStats().registeredOwners == budgetBefore.registeredOwners + 1,
          "Shared assets register exactly one physical heap VRAM owner");
    Check(pool->Prepare(a, {{0, 2}, {1, 0}}, error) && pool->Prepare(b, {{0, 2}}, error),
          "Three-page global residency admission " + error);
    const auto address = device.Resolve(a->buffer)->GetGPUVirtualAddress();
    const auto originalA = a->tiles;
    const auto revision = pool->Revision(a);
    Check(!pool->Prepare(c, {{0, 2}}, error) && a->tiles == originalA && pool->Revision(a) == revision,
          "Active-page exhaustion rejects before mutating any asset");
    pool->SetEvictable(a, true);
    Check(pool->Prepare(c, {{0, 2}}, error), "Completed asset optional page is evicted " + error);
    Check(a->tiles.contains(0) && !a->tiles.contains(1) && b->tiles.contains(0) && pool->Revision(a) > revision &&
              pool->GetStats().evictions == 1 && pool->GetStats().reuses == 1,
          "Global page priority preserves metadata and reassigns an optional page");
    Check(device.Resolve(a->buffer)->GetGPUVirtualAddress() == address, "Cross-asset eviction retains virtual address");
    // Native readback proves that distinct assets sharing one heap do not alias
    // their mapped pages, including a tile reassigned from A to C.
    Check(device.BeginFrame(error), "Shared sparse correctness recording");
    const std::array allocations{a, b, c};
    std::array<RHIReadback, 3> reads;
    for (size_t asset = 0; asset < allocations.size(); ++asset)
    {
        const auto buffer = allocations[asset]->buffer;
        const auto source = device.AllocateUpload({256, RHIUploadUsage::BufferCopy, 16});
        Check(source.IsWritable(), "Shared sparse staging");
        for (uint32_t word = 0; word < 64; ++word)
        {
            static_cast<uint32_t*>(source.cpuAddress)[word] = uint32_t(0xabc00000u + asset * 256 + word);
        }
        RHIBarrierBatch activation{};
        activation.aliasBuffers = {&buffer, 1};
        auto& encoder = device.GetImmediateEncoder();
        encoder.ResourceBarriers(activation);
        device.TransitionBuffers(
            std::array{RHIBufferTransition{buffer, RHIResourceState::Common, RHIResourceState::CopyDest}});
        auto destination = RHIBufferSlice::Whole(buffer);
        destination.size = 256;
        Check(encoder.CopyBuffer(destination, source), "Shared asset page upload");
        device.TransitionBuffers(
            std::array{RHIBufferTransition{buffer, RHIResourceState::CopyDest, RHIResourceState::CopySource}});
        Check(device.CreateBufferReadback(256, reads[asset], error), "Shared sparse readback creation");
        encoder.CopyBufferToReadback(reads[asset], buffer);
    }
    Check(device.EndFrame(error) && device.DrainForLifecycle(RHILifecycleCommand::OfflineReadbackCapture, error),
          "Shared sparse GPU completion " + error);
    for (size_t asset = 0; asset < allocations.size(); ++asset)
    {
        RHIReadbackImage image;
        Check(device.MapReadback(reads[asset], image, error), "Shared sparse readback map");
        for (uint32_t word = 0; word < 64; ++word)
        {
            Check(image.Elements<uint32_t>()[word] == uint32_t(0xabc00000u + asset * 256 + word),
                  "Shared pool native GPU words remain isolated");
        }
        device.ReleaseReadback(reads[asset]);
    }
    pool->SetEvictable(a, true);
    pool->SetEvictable(b, true);
    pool->SetEvictable(a, false);
    pool->SetEvictable(a, true);
    auto d = pool->Create(RHISparseTileBytes, error);
    Check(pool->Prepare(d, {{0, 2}}, error) && a->tiles.contains(0) && b->tiles.empty(),
          "Equal-priority global eviction respects recent cache-hit use");
    const auto stableC = c->tiles;
    Check(!pool->Prepare(c, {{4, 2}}, error) && c->tiles == stableC,
          "Malformed virtual-page demand leaves prior residency unchanged");
    // Real storage upload proofs: adjacent packed packets share one tile,
    // unchanged packets survive refinement, and an uncompleted upload is retried.
    MeshletSparseStorage storage(device);
    storage.pool = own::make_shared<RHISharedSparsePagePool>(device, 3);
    SparseClusterPayload payload;
    payload.virtualBytes = 3 * RHISparseTileBytes;
    const auto packet = own::make_shared<const std::vector<std::byte>>(32, std::byte{0x42});
    payload.packets = {{uint32_t(RHISparseTileBytes), 1, packet}, {uint32_t(RHISparseTileBytes + 32), 0, packet}};
    Check(storage.Prepare(payload, 32, 1024, error), "Packed storage prepare");
    Check(storage.allocation->tiles.size() == 2 && storage.NeedsPacket(payload.packets[0]),
          "Two packed packets use one data tile");
    storage.Commit(storage.serial, storage.Revision(), payload, std::vector<std::byte>(32));
    payload.packets.push_back({uint32_t(2 * RHISparseTileBytes - 16), 0, packet});
    Check(storage.Prepare(payload, 32, 1024, error) && !storage.NeedsPacket(payload.packets[0]) &&
              !storage.NeedsPacket(payload.packets[1]) && storage.NeedsPacket(payload.packets[2]),
          "Incremental refinement transfers only newly admitted packets");
    const auto neverSubmitted = storage.serial;
    Check(storage.Prepare(payload, 32, 1024, error), "Retry preparation after abandoned recording");
    storage.Commit(neverSubmitted, storage.Revision(), payload, std::vector<std::byte>(32));
    Check(storage.NeedsPacket(payload.packets[2]), "Aborted or stale upload cannot publish a committed packet");
    storage.Commit(storage.serial, storage.Revision(), payload, std::vector<std::byte>(32));
    Check(!storage.NeedsPacket(payload.packets[2]), "Completed crossing packet publishes its upload proof");
    auto other = storage.pool->Create(3 * RHISparseTileBytes, error);
    storage.SetEvictable(true);
    Check(storage.pool->Prepare(other, {{0, 2}}, error) && !storage.allocation->tiles.contains(2),
          "Global eviction reclaims low priority data before root and metadata");
    Check(storage.Prepare(payload, 32, 1024, error) == false, "Active other asset prevents an unsafe restore");
    storage.pool->SetEvictable(other, true);
    Check(storage.Prepare(payload, 32, 1024, error) && storage.NeedsPacket(payload.packets[2]) &&
              !storage.NeedsPacket(payload.packets[0]),
          "Remapped packet is reuploaded while retained packet is reused");
    storage.Commit(storage.serial, storage.Revision(), payload, std::vector<std::byte>(32));
    storage.SetEvictable(true);
    Check(storage.pool->Prepare(other, {{0, 2}, {1, 2}, {2, 2}}, error) && storage.allocation->tiles.empty(),
          "Idle root and metadata pages are reclaimed after optional pages");
    Check(!storage.Prepare(payload, 32, 1024, error) && storage.committedMetadata.empty() &&
              storage.NeedsPacket(payload.packets[0]),
          "Evicted metadata and root lose every upload proof");
    storage.pool->SetEvictable(other, true);
    Check(storage.Prepare(payload, 32, 1024, error) && storage.NeedsPacket(payload.packets[0]) &&
              storage.NeedsPacket(payload.packets[2]),
          "Complete asset restoration requires root and data reuploads");
    std::cout
        << "METEOR_SHARED_SPARSE_POOL_OK assets=3 physicalPages=3 gpuWords=192 activePin=1 priority=1 lru=1 reuse=1 "
           "delta=1 abort=1 validation=0\n";
}
