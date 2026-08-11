#ifndef DYNAMICCPP_EXPORTS
#include "VulkanFrameAllocators.h"

#include <algorithm>

using namespace VulkanApi;

namespace
{
    /// 풀 하나의 종류별 예산. 넘치면 할당이 실패하고 그것이 계수로 남는다.
    ///
    /// ★ 값의 근거는 실측이다 — 패스 17종에서 한 프레임이 만드는 테이블의
    ///   최대가 수십 자리다(`RenderFrameServices.h` 의 빈도표: 뷰 생성 66 ·
    ///   테이블 33). 넉넉히 잡되 무한이 아니게 둔다: 무한이면 새는 것을
    ///   못 잡고, 너무 좁으면 슬라이스 7 에서 이유 모를 실패가 난다.
    constexpr uint32_t kVkMaxSetsPerFrame = 256;

    const VkDescriptorPoolSize kVkPoolBudget[] = {
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 256 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 128 },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,  512 },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,  256 },

        // ★ 샘플러도 예산이 필요하다 — 불변 샘플러여도 셋 안의 **자리**는
        //   차지한다. `VulkanTrianglePass` 가 검증 레이어에게 반증당하고
        //   적어 둔 것이고, DX12 는 정적 샘플러가 힙을 아예 안 써서 대응이 없다.
        { VK_DESCRIPTOR_TYPE_SAMPLER,        128 },
    };
}

// ────────────────────────────────────────────────────────────── 업로드 링

bool VulkanUploadRing::Initialize(VkDevice device, VkPhysicalDevice physicalDevice,
    VulkanResourceTable& table, uint32_t frameCount, uint64_t bytesPerFrame,
    std::string& outError)
{
    if (VK_NULL_HANDLE == device || 0 == frameCount || 0 == bytesPerFrame)
    {
        outError = "업로드 링 인자가 잘못됐다";
        return false;
    }

    // 디바이스가 요구하는 최소 정렬 (헤더 ★).
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(physicalDevice, &properties);
    // ★ 괄호는 실수가 아니다. `Windows.h` 의 `max` 매크로가 유니티 빌드에서
    //   묻어 들어와 `std::max(` 를 삼킨다 — 괄호가 매크로 전개를 막는다.
    m_minAlignment = std::max<uint64_t>(1,
        (std::max)(properties.limits.minUniformBufferOffsetAlignment,
            properties.limits.minStorageBufferOffsetAlignment));

    m_bytesPerFrame = bytesPerFrame;
    m_blocks.resize(frameCount);

    VkPhysicalDeviceMemoryProperties memory{};
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memory);

    for (Block& block : m_blocks)
    {
        VkBufferCreateInfo info{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        info.size = bytesPerFrame;
        info.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT
            | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
            | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
            | VK_BUFFER_USAGE_INDEX_BUFFER_BIT
            | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VkResult made = vkCreateBuffer(device, &info, nullptr, &block.buffer);
        if (VK_SUCCESS != made)
        {
            outError = "업로드 버퍼 생성 실패 — " + ResultToString(made);
            return false;
        }

        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements(device, block.buffer, &requirements);

        // ★ HOST_COHERENT 를 요구한다. 없으면 쓸 때마다
        //   `vkFlushMappedMemoryRanges` 를 불러야 하고, 그러면 "링은 계속
        //   매핑돼 있고 쓰는 쪽은 memcpy 만 한다"는 계약(`RHIBufferSlice::
        //   cpuAddress`)이 백엔드마다 달라진다.
        uint32_t type = UINT32_MAX;
        const VkMemoryPropertyFlags wanted =
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        for (uint32_t i = 0; i < memory.memoryTypeCount; ++i)
        {
            const bool allowed = 0 != (requirements.memoryTypeBits & (1u << i));
            if (allowed && wanted == (memory.memoryTypes[i].propertyFlags & wanted))
            {
                type = i;
                break;
            }
        }
        if (UINT32_MAX == type)
        {
            outError = "호스트 가시·일관 메모리 타입을 찾지 못했다";
            return false;
        }

        VkMemoryAllocateInfo allocate{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        allocate.allocationSize = requirements.size;
        allocate.memoryTypeIndex = type;

        made = vkAllocateMemory(device, &allocate, nullptr, &block.memory);
        if (VK_SUCCESS != made)
        {
            outError = "업로드 메모리 할당 실패 — " + ResultToString(made);
            return false;
        }

        made = vkBindBufferMemory(device, block.buffer, block.memory, 0);
        if (VK_SUCCESS != made)
        {
            outError = "업로드 메모리 바인드 실패 — " + ResultToString(made);
            return false;
        }

        made = vkMapMemory(device, block.memory, 0, VK_WHOLE_SIZE, 0, &block.mapped);
        if (VK_SUCCESS != made)
        {
            outError = "업로드 메모리 매핑 실패 — " + ResultToString(made);
            return false;
        }

        // ★ 표에는 **외부**로 등록한다. 링이 만들고 링이 놓으므로 표가 죽이면
        //   안 된다 — 표가 "자기 칸만 비우고 남의 리소스를 죽이지 않는다"고
        //   적어 둔 그 계약의 첫 실제 소비자다.
        VulkanBufferEntry entry{};
        entry.buffer = block.buffer;
        entry.memory = block.memory;
        entry.bytes = bytesPerFrame;
        entry.mapped = block.mapped;

        block.handle = table.AddExternalBuffer(entry);
        if (!block.handle.IsValid())
        {
            outError = "업로드 버퍼를 표에 올리지 못했다";
            return false;
        }
    }

    return true;
}

void VulkanUploadRing::Shutdown(VkDevice device)
{
    if (VK_NULL_HANDLE != device)
    {
        for (Block& block : m_blocks)
        {
            if (nullptr != block.mapped && VK_NULL_HANDLE != block.memory)
            {
                vkUnmapMemory(device, block.memory);
            }
            if (VK_NULL_HANDLE != block.buffer) vkDestroyBuffer(device, block.buffer, nullptr);
            if (VK_NULL_HANDLE != block.memory) vkFreeMemory(device, block.memory, nullptr);
        }
    }

    m_blocks.clear();
    m_offset = 0;
    m_bytesPerFrame = 0;
}

RHIBufferSlice VulkanUploadRing::Allocate(uint64_t bytes, uint64_t alignment)
{
    RHIBufferSlice slice{};
    if (0 == bytes || m_frameIndex >= m_blocks.size()) return slice;

    // 호출부의 값을 **넓힌다** (헤더 ★). 좁히지 않는다.
    const uint64_t align = std::max<uint64_t>(1, (std::max)(alignment, m_minAlignment));
    const uint64_t start = (m_offset + align - 1) / align * align;
    if (start + bytes > m_bytesPerFrame) return slice;   // 조용히 겹치지 않는다

    const Block& block = m_blocks[m_frameIndex];
    m_offset = start + bytes;

    slice.buffer = block.handle;
    slice.offset = start;
    slice.size = bytes;
    slice.cpuAddress = static_cast<uint8_t*>(block.mapped) + start;
    return slice;
}

// ────────────────────────────────────────────────────────────── 디스크립터 풀

bool VulkanDescriptorPool::Initialize(VkDevice device, uint32_t frameCount,
    std::string& outError)
{
    if (VK_NULL_HANDLE == device || 0 == frameCount)
    {
        outError = "디스크립터 풀 인자가 잘못됐다";
        return false;
    }

    m_pools.resize(frameCount, VK_NULL_HANDLE);
    for (VkDescriptorPool& pool : m_pools)
    {
        VkDescriptorPoolCreateInfo info{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
        info.maxSets = kVkMaxSetsPerFrame;
        info.poolSizeCount = static_cast<uint32_t>(std::size(kVkPoolBudget));
        info.pPoolSizes = kVkPoolBudget;

        const VkResult made = vkCreateDescriptorPool(device, &info, nullptr, &pool);
        if (VK_SUCCESS != made)
        {
            outError = "디스크립터 풀 생성 실패 — " + ResultToString(made);
            return false;
        }
    }

    return true;
}

void VulkanDescriptorPool::Shutdown(VkDevice device)
{
    if (VK_NULL_HANDLE != device)
    {
        for (VkDescriptorPool pool : m_pools)
        {
            if (VK_NULL_HANDLE != pool) vkDestroyDescriptorPool(device, pool, nullptr);
        }
    }
    m_pools.clear();
}

bool VulkanDescriptorPool::Reset(VkDevice device, uint32_t frameIndex)
{
    m_frameIndex = frameIndex;
    if (VK_NULL_HANDLE == device || frameIndex >= m_pools.size()) return false;
    return VK_SUCCESS == vkResetDescriptorPool(device, m_pools[frameIndex], 0);
}

VkDescriptorSet VulkanDescriptorPool::Allocate(VkDevice device, VkDescriptorSetLayout setLayout)
{
    if (VK_NULL_HANDLE == device || VK_NULL_HANDLE == setLayout) return VK_NULL_HANDLE;
    if (m_frameIndex >= m_pools.size()) return VK_NULL_HANDLE;

    VkDescriptorSetAllocateInfo info{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    info.descriptorPool = m_pools[m_frameIndex];
    info.descriptorSetCount = 1;
    info.pSetLayouts = &setLayout;

    VkDescriptorSet set = VK_NULL_HANDLE;
    if (VK_SUCCESS != vkAllocateDescriptorSets(device, &info, &set)) return VK_NULL_HANDLE;
    return set;
}

#endif
