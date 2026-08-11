#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include "VulkanLoader.h"
#include "VulkanResourceTable.h"
#include "../RHIResourceTypes.h"

#include <string>
#include <vector>

// 프레임마다 되감는 것 둘 — 업로드 링과 디스크립터 풀 (5c-4d).
//
// ── 왜 한 파일인가 ──
//
// 성질이 같다: **슬롯마다 하나씩 있고, `BeginFrame` 이 그 슬롯의 펜스를
// 기다린 뒤 되감으며, 프레임 안에서는 앞으로만 간다.** 되감는 시점이 어긋나면
// 둘 다 GPU 가 읽는 중인 것을 덮어쓴다 — 그 시점이 한곳에 있어야 한다.
//
// DX12 쪽은 `DX12UploadRing` 과 `DX12DescriptorRing` 으로 갈려 있는데, 그것은
// 저쪽에서 둘이 **다른 종류의 메모리**(업로드 힙 / 디스크립터 힙)라서다.
// Vulkan 에서는 하나가 `VkBuffer` 이고 하나가 `VkDescriptorPool` 이라 더
// 다르지만, **되감기 규약이 같다**는 것이 묶는 근거다.

/// 프레임 업로드 링 — `RHIBufferSlice` 의 생산자.
///
/// ★ **정렬을 호출부가 정하는 계약이 여기서 시험된다.** `AllocateUpload` 는
///   정렬을 인자로 받고, `RHIResourceTypes.h` 는 그 이유를 "용도가 정한다 —
///   상수는 256, 텍스처 복사원은 512" 라고 적었다. 그 숫자들은 **DX12 의
///   고정 상수**다.
///
///   Vulkan 은 다르다: `minUniformBufferOffsetAlignment` 는 **디바이스 속성**
///   이고 기계마다 갈린다(16 ~ 256). 그래서 백엔드가 호출부의 값을 **넓힌다**
///   — 좁히지 않는다. 계약은 안 바뀌고("이만큼은 맞춰 달라"), 실제 값은
///   디바이스가 요구하는 것과의 최댓값이 된다.
///
///   ★ 넓히지 않고 그대로 쓰면 조용히 틀린다. 검증 레이어가 잡아 주기는
///     하지만 그것은 이 기계 이야기이고, 정렬이 더 큰 기계에서 처음 터진다.
class VulkanUploadRing
{
public:
    bool Initialize(VkDevice device, VkPhysicalDevice physicalDevice,
        VulkanResourceTable& table, uint32_t frameCount, uint64_t bytesPerFrame,
        std::string& outError);

    void Shutdown(VkDevice device);

    /// 슬롯을 갈아 끼우고 되감는다. 부르는 쪽이 펜스를 이미 기다린 뒤여야 한다.
    void Reset(uint32_t frameIndex)
    {
        m_frameIndex = frameIndex;
        m_offset = 0;
    }

    /// 자른다. 구간이 모자라면 무효 슬라이스다 — 호출부는 `IsValid()` 하나만
    /// 검사한다(계약이 그렇게 적혀 있다).
    RHIBufferSlice Allocate(uint64_t bytes, uint64_t alignment);

    /// 이번 프레임에 쓴 바이트. 자가 검증이 "정말 잘렸나"를 보는 데 쓴다.
    uint64_t UsedBytes() const { return m_offset; }
    uint64_t CapacityBytes() const { return m_bytesPerFrame; }

private:
    struct Block
    {
        VkBuffer        buffer{ VK_NULL_HANDLE };
        VkDeviceMemory  memory{ VK_NULL_HANDLE };
        void*           mapped{ nullptr };
        RHIBufferHandle handle;
    };

    std::vector<Block> m_blocks;
    uint32_t m_frameIndex{ 0 };
    uint64_t m_offset{ 0 };
    uint64_t m_bytesPerFrame{ 0 };

    /// 디바이스가 요구하는 최소 정렬. 호출부의 값과 최댓값을 쓴다 (위 ★).
    uint64_t m_minAlignment{ 1 };
};

/// 프레임 디스크립터 풀.
///
/// ★ **DX12 의 링과 모델이 다르다.** 저쪽은 힙 하나에서 연속 구간을 잘라
///   `D3D12_GPU_DESCRIPTOR_HANDLE` 하나로 가리킨다 — 자를 때 "몇 개"만 알면
///   되고 종류를 몰라도 된다.
///
///   Vulkan 은 풀을 **만들 때** 종류별 예산을 요구하고, 잘라 오는 단위가
///   `VkDescriptorSet` 이며 그 셋은 **어떤 셋 레이아웃의 것인지**를 알아야
///   할당된다. `VulkanTrianglePass` 가 그 비대칭을 이미 적어 두었다:
///   "몇 개를 어느 종류로 쓸 것인가를 Vulkan 은 풀을 만들 때, DX12 는 자를 때
///   안다."
///
///   그래서 예산이 여기 상수로 박힌다. 넘치면 조용히 넘어가지 않고 무효
///   핸들을 주며, 인코더가 그것을 계수로 남긴다.
class VulkanDescriptorPool
{
public:
    bool Initialize(VkDevice device, uint32_t frameCount, std::string& outError);
    void Shutdown(VkDevice device);

    /// 슬롯을 갈아 끼우고 되감는다.
    ///
    /// ★ `vkResetDescriptorPool` 은 그 풀에서 나간 **모든 셋을 한 번에**
    ///   무효로 만든다. DX12 링이 오프셋을 0 으로 되돌리는 것과 같은 일이고,
    ///   같은 전제를 갖는다 — GPU 가 그 셋들을 다 쓴 뒤여야 한다.
    bool Reset(VkDevice device, uint32_t frameIndex);

    /// 셋 하나를 잘라 온다. 예산이 다하면 `VK_NULL_HANDLE`.
    VkDescriptorSet Allocate(VkDevice device, VkDescriptorSetLayout setLayout);

private:
    std::vector<VkDescriptorPool> m_pools;
    uint32_t m_frameIndex{ 0 };
};

#endif
