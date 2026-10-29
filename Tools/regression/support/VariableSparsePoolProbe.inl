// Native multi-heap mapping, pressure retirement and global growth arbitration.
void RunVariableSparsePool(RecordingChangeDevice& device)
{
    auto& coordinator = device.GetPersistentMemoryBudgetCoordinator();
    const auto previousBudget = coordinator.GetDecision(kRHIDeviceLocalMemoryBudgetDomain).budget;
    struct RestoreBudget
    {
        RHIDeviceMemoryBudgetCoordinator& coordinator;
        RHIPersistentHeapBudget budget;
        ~RestoreBudget() { coordinator.UpdateBudget(kRHIDeviceLocalMemoryBudgetDomain, budget); }
    } restore{coordinator, previousBudget};
    coordinator.UpdateBudget(kRHIDeviceLocalMemoryBudgetDomain, {0, 64ull << 20, false});
    const auto before = coordinator.GetStats();
    auto pool = own::make_shared<RHISharedSparsePagePool>(device, RHISharedSparsePagePool::Policy{2, 2, 6, 2});
    std::string error;
    auto allocation = pool->Create(8 * RHISparseTileBytes, error);
    Check(bool(allocation), "Variable slab virtual allocation " + error);
    const auto address = device.Resolve(allocation->buffer)->GetGPUVirtualAddress();
    const ce::engine_ordered_map<uint32_t, uint32_t> four{{0, 2}, {1, 1}, {2, 0}, {3, 0}};
    Check(pool->Prepare(allocation, four, error) && pool->GetStats().slabs == 2 &&
              pool->GetStats().physicalBytes == 4 * RHISparseTileBytes,
          "Demand grows a second physical slab without replacing the reserved resource " + error);
    Check(coordinator.GetStats().registeredOwners == before.registeredOwners + 2 &&
              coordinator.GetStats().growthCommits == before.growthCommits + 2,
          "Each physical slab owns one globally coordinated VRAM lease");
    const auto originalTiles = allocation->tiles;
    Check(!pool->Prepare(allocation, {{0, 2}, {1, 1}, {2, 0}, {3, 0}, {4, 0}, {5, 0}, {6, 0}}, error) &&
              allocation->tiles == originalTiles && pool->GetStats().physicalBytes == 6 * RHISparseTileBytes,
          "Maximum pool capacity rejects an oversized cut without changing its prior mappings");
    auto secondary = pool->Create(8 * RHISparseTileBytes, error);
    Check(secondary && pool->Prepare(secondary, {{0, 1}}, error) && secondary->tiles.at(0) >= 4,
          "A second asset maps the additional physical slab through its own stable reserved buffer");
    Check(pool->Tick(1, error), "Initial variable pool tick");
    Check(device.BeginFrame(error), "Multi-slab native frame");
    RHIBufferHandle ordinary;
    RHIBufferDesc ordinaryDesc;
    ordinaryDesc.bytes = 256;
    Check(device.CreateBuffer(ordinaryDesc, ordinary, error), "Malformed slab source fixture");
    coordinator.UpdateBudget(kRHIDeviceLocalMemoryBudgetDomain, {60ull << 20, 64ull << 20, false});
    Check(device.GetStreamingMemoryBudget().memoryPressure && !pool->CanGrow(),
          "Real device coordinator pressure disables physical growth");
    Check(pool->Tick(2, error) && pool->GetStats().physicalBytes == 6 * RHISparseTileBytes &&
              allocation->tiles == originalTiles && pool->GetStats().deferredShrinks > 0,
          "Pressure retains slabs used by either active asset");
    const auto buffer = allocation->buffer;
    const RHISparseTileMapping invalid[]{{0, 1, true}, {1, 0, true, ordinary}};
    Check(!device.UpdateSparseBufferMappings(buffer, invalid, error),
          "A mixed batch with a non-sparse source rejects before any native map");
    device.ReleaseBuffer(ordinary);
    auto& encoder = device.GetImmediateEncoder();
    RHIBarrierBatch activation{};
    activation.aliasBuffers = {&buffer, 1};
    encoder.ResourceBarriers(activation);
    device.TransitionBuffers(
        std::array{RHIBufferTransition{buffer, RHIResourceState::Common, RHIResourceState::CopyDest}});
    std::array<RHIReadback, 4> reads;
    for (uint32_t tile = 0; tile < 4; ++tile)
    {
        const auto upload = device.AllocateUpload({64, RHIUploadUsage::BufferCopy, 16});
        Check(upload.IsWritable(), "Multi-slab staging");
        for (uint32_t word = 0; word < 16; ++word)
        {
            static_cast<uint32_t*>(upload.cpuAddress)[word] = 0x72000000u + tile * 256 + word;
        }
        auto destination = RHIBufferSlice::Whole(buffer);
        destination.offset = uint64_t(tile) * RHISparseTileBytes;
        destination.size = 64;
        Check(encoder.CopyBuffer(destination, upload), "Multi-slab native upload");
    }
    device.TransitionBuffers(
        std::array{RHIBufferTransition{buffer, RHIResourceState::CopyDest, RHIResourceState::CopySource}});
    for (uint32_t tile = 0; tile < reads.size(); ++tile)
    {
        Check(device.CreateBufferReadback(64, reads[tile], error), "Multi-slab readback creation");
        encoder.CopyBufferToReadback(reads[tile], buffer, uint64_t(tile) * RHISparseTileBytes);
    }
    const auto secondaryBuffer = secondary->buffer;
    RHIBarrierBatch secondaryActivation{};
    secondaryActivation.aliasBuffers = {&secondaryBuffer, 1};
    encoder.ResourceBarriers(secondaryActivation);
    device.TransitionBuffers(
        std::array{RHIBufferTransition{secondaryBuffer, RHIResourceState::Common, RHIResourceState::CopyDest}});
    const auto secondaryUpload = device.AllocateUpload({64, RHIUploadUsage::BufferCopy, 16});
    Check(secondaryUpload.IsWritable(), "Second asset slab staging");
    for (uint32_t word = 0; word < 16; ++word)
    {
        static_cast<uint32_t*>(secondaryUpload.cpuAddress)[word] = 0x73000000u + word;
    }
    auto secondaryDestination = RHIBufferSlice::Whole(secondaryBuffer);
    secondaryDestination.size = 64;
    Check(encoder.CopyBuffer(secondaryDestination, secondaryUpload), "Second asset slab upload");
    device.TransitionBuffers(
        std::array{RHIBufferTransition{secondaryBuffer, RHIResourceState::CopyDest, RHIResourceState::CopySource}});
    RHIReadback secondaryRead;
    Check(device.CreateBufferReadback(64, secondaryRead, error), "Second asset slab readback creation");
    encoder.CopyBufferToReadback(secondaryRead, secondaryBuffer);
    Check(device.EndFrame(error) && device.DrainForLifecycle(RHILifecycleCommand::OfflineReadbackCapture, error),
          "Multi-slab native completion " + error);
    for (uint32_t tile = 0; tile < reads.size(); ++tile)
    {
        RHIReadbackImage image;
        Check(device.MapReadback(reads[tile], image, error), "Multi-slab readback mapping");
        for (uint32_t word = 0; word < 16; ++word)
        {
            Check(image.Elements<uint32_t>()[word] == 0x72000000u + tile * 256 + word,
                  "Native tiles in distinct physical heaps retain independent contents");
        }
        device.ReleaseReadback(reads[tile]);
    }
    RHIReadbackImage secondaryImage;
    Check(device.MapReadback(secondaryRead, secondaryImage, error), "Second asset slab readback mapping");
    for (uint32_t word = 0; word < 16; ++word)
    {
        Check(secondaryImage.Elements<uint32_t>()[word] == 0x73000000u + word,
              "Additional-slab pages remain isolated between assets");
    }
    device.ReleaseReadback(secondaryRead);
    pool->SetEvictable(secondary, true);
    pool->SetEvictable(allocation, true);
    Check(pool->Tick(3, error) && pool->GetStats().physicalBytes == 2 * RHISparseTileBytes &&
              allocation->tiles.size() == 2 && allocation->tiles.contains(0) && allocation->tiles.contains(1),
          "Completed idle tail is unmapped and returned while the floor slab stays resident");
    Check(device.DrainForLifecycle(RHILifecycleCommand::OfflineReadbackCapture, error), "Slab budget retirement");
    Check(coordinator.GetStats().registeredOwners == before.registeredOwners + 1,
          "The returned slab releases its global VRAM owner after its mapping fence");
    coordinator.UpdateBudget(kRHIDeviceLocalMemoryBudgetDomain, {0, 16 * RHISparseTileBytes, false});
    const auto foreign = coordinator.RegisterOwner();
    auto ticket = coordinator.TryReserveGrowth(foreign, kRHIDeviceLocalMemoryBudgetDomain, 2 * RHISparseTileBytes);
    Check(ticket.IsValid() && !device.GetStreamingMemoryBudget().memoryPressure &&
              device.GetStreamingMemoryBudget().availableGrowthBytes == 0,
          "Another allocator reserves all shared growth headroom without triggering pressure");
    pool->SetEvictable(allocation, false);
    const auto retained = allocation->tiles;
    Check(!pool->Prepare(allocation, four, error) && allocation->tiles == retained &&
              pool->GetStats().growthDenials > 0,
          "Foreign global reservation blocks growth without damaging retained root pages");
    Check(coordinator.CancelGrowth(ticket), "Foreign reservation cancellation");
    coordinator.UnregisterOwner(foreign);
    Check(pool->Prepare(allocation, four, error) && pool->GetStats().slabs == 2,
          "Released global headroom allows the same reserved buffer to grow again " + error);
    Check(device.Resolve(buffer)->GetGPUVirtualAddress() == address, "Grow and shrink retain the GPU virtual address");
    pool->SetEvictable(allocation, true);
    Check(pool->Tick(4, error) && pool->GetStats().slabs == 2 && pool->Tick(4, error) && pool->GetStats().slabs == 2,
          "Idle hysteresis counts recordings rather than repeated same-frame cache acquisitions");
    Check(pool->Tick(5, error) && pool->GetStats().slabs == 1,
          "Idle extra slab returns after the configured two-recording grace period");
    Check(device.DrainForLifecycle(RHILifecycleCommand::OfflineReadbackCapture, error), "Idle slab retirement");
    Check(coordinator.GetStats().registeredOwners == before.registeredOwners + 1,
          "Idle shrink also retires its physical budget lease");
    std::cout << "METEOR_VARIABLE_SPARSE_POOL_OK floorPages=2 peakPages=6 gpuWords=80 activePin=1 pressure=1 "
                 "globalReservation=1 regrow=1 hysteresis=1 stableAddress=1 validation=0\n";
}
