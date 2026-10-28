#include "../../Engine/RenderEngine/Interfaces/RenderBackend.h"
#include "../../Engine/Utility_Framework/Runtime/RenderBackend.h"

#include <atomic>
#include <cstdlib>
#include <new>
#include <string_view>

namespace
{
    std::atomic<unsigned> allocations{0};
}

void* operator new(std::size_t size)
{
    allocations.fetch_add(1, std::memory_order_relaxed);
    if (void* pointer = std::malloc(size ? size : 1)) return pointer;
    throw std::bad_alloc{};
}

void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { std::free(pointer); }

int main()
{
    const auto before = allocations.load(std::memory_order_relaxed);
    RenderBackend backend = RenderBackend::Vulkan;
    if (!TryParseRenderBackend("Dx12", backend) || backend != RenderBackend::DX12) return 1;
    if (std::string_view(RenderBackendName(backend)) != "dx12") return 2;
    if (!TryParseRenderBackend("VULKAN", backend) || backend != RenderBackend::Vulkan) return 3;
    if (std::string_view(RenderBackendName(backend)) != "vulkan") return 4;
    if (TryParseRenderBackend("unknown", backend) || backend != RenderBackend::Vulkan) return 5;
    return allocations.load(std::memory_order_relaxed) == before ? 0 : 6;
}
