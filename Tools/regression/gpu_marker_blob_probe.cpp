#if defined(CE_CONTRACT_STANDALONE)
#include "../build/ModuleAdapters/Creator.Core.Ownership.h"
#else
import Creator.Core.Ownership;
#endif
#include "../../Engine/EngineDiagnostics/ProfileMarker.h"
#include "../../Engine/RenderEngine/Assets/ModelAssetGeneration.h"
#include <atomic>
#include <crtdbg.h>
#include <iostream>
#include <thread>
namespace
{
    std::atomic<unsigned> allocationCount{};
    thread_local bool watchAllocations{};
    int Hook(int type, void*, size_t, int, long, const unsigned char*, int)
    {
        if (watchAllocations && type == _HOOK_ALLOC)
        {
            allocationCount.fetch_add(1, std::memory_order_relaxed);
        }
        return 1;
    }
    void Check(bool value)
    {
        if (!value)
        {
            throw std::runtime_error("Marker/blob regression failed");
        }
    }
} // namespace
int main()
{
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _CrtSetAllocHook(Hook);
    std::string transient = "Gpu.Transient.Name.With.Owned.Registry.Storage";
    const auto first = ce::intern_runtime_marker(transient, ce::marker_kind::gpu_span);
    transient.assign("Changed.Source.Name");
    Check(first ==
          ce::intern_runtime_marker("Gpu.Transient.Name.With.Owned.Registry.Storage", ce::marker_kind::gpu_span));
    Check(first !=
          ce::intern_runtime_marker("Gpu.Transient.Name.With.Owned.Registry.Storage", ce::marker_kind::cpu_scope));
    watchAllocations = true;
    for (unsigned i = 0; i < 4096; ++i)
    {
        Check(first ==
              ce::intern_runtime_marker("Gpu.Transient.Name.With.Owned.Registry.Storage", ce::marker_kind::gpu_span));
    }
    watchAllocations = false;
    Check(allocationCount.load() == 0);
    std::array<std::thread, 8> threads;
    for (auto& thread : threads)
    {
        thread = std::thread(
            [first]()
            {
                for (unsigned i = 0; i < 1000; ++i)
                {
                    Check(first == ce::intern_runtime_marker("Gpu.Transient.Name.With.Owned.Registry.Storage",
                                                             ce::marker_kind::gpu_span));
                    const auto name = "Gpu.Concurrent." + std::to_string(i);
                    const auto marker = ce::intern_runtime_marker(name, ce::marker_kind::gpu_span);
                    Check(marker == ce::intern_runtime_marker(name, ce::marker_kind::gpu_span));
                }
            });
    }
    for (auto& thread : threads)
    {
        thread.join();
    }
    for (unsigned i = 0; i < 9000; ++i)
    {
        const auto name = "Gpu.Bounded." + std::to_string(i);
        const auto marker = ce::intern_runtime_marker(name, ce::marker_kind::gpu_span);
        Check(marker == ce::intern_runtime_marker(name, ce::marker_kind::gpu_span));
    }
    assets::ModelMeshHandle source;
    experiment::cooked::AssetBlobRecord blob;
    blob.targetPlatform = "long-platform-identity-preserved-after-origin-retirement";
    blob.targetAbi = "long-abi-identity-preserved-after-origin-retirement";
    blob.artifactPath = "nested/artifacts/long/path/immutable-mesh-blob.artifact";
    source.blob = own::make_shared<const experiment::cooked::AssetBlobRecord>(blob);
    auto pinned = source;
    const auto* address = &source.Blob();
    watchAllocations = true;
    for (unsigned i = 0; i < 4096; ++i)
    {
        const auto copy = source;
        Check(&copy.Blob() == address);
    }
    watchAllocations = false;
    Check(allocationCount.load() == 0);
    source = {};
    Check(pinned.Blob() == blob);
    auto equal = pinned;
    equal.blob = own::make_shared<const experiment::cooked::AssetBlobRecord>(blob);
    Check(equal == pinned);
    blob.artifactPath += ".replacement";
    equal.blob = own::make_shared<const experiment::cooked::AssetBlobRecord>(blob);
    Check(equal != pinned);
    own::weak_owner<const experiment::cooked::AssetBlobRecord> observer = pinned.blob;
    std::atomic<bool> released{};
    std::thread consumer(
        [held = pinned, address, &released]()
        {
            while (!released.load(std::memory_order_acquire))
            {
                std::this_thread::yield();
            }
            Check(&held.Blob() == address && !held.Blob().artifactPath.empty());
        });
    pinned = {};
    released.store(true, std::memory_order_release);
    consumer.join();
    Check(observer.expired());
    std::cout << "GPU_MARKER_BLOB_OK cachedAllocations=0 boundedCache=true concurrentNames=true blobCopyAllocations=0 "
                 "valueIdentity=true asyncPins=true\n";
}
