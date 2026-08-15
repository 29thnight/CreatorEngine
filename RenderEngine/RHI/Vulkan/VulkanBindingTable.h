#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include "VulkanLoader.h"
#include "../RHIResourceTypes.h"

#include <span>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

// 프레임 수명의 바인딩 요청 표.
//
// DX12의 RHIBindingTable::backend는 GPU 디스크립터 핸들이지만 Vulkan은
// CreateBindings 시점에 셋 레이아웃을 모른다. 따라서 여기서는 뷰 설명을
// 값으로 보관하고 정수 슬롯만 돌려준다. SetBindings가 현재 파이프라인의
// 레이아웃과 이 요청을 합쳐 실제 VkDescriptorSet 쓰기를 만든다.
//
// 포인터를 RHIBindingTable에 싣지 않는 이유는 둘이다. vector 재할당으로
// 주소가 바뀔 수 있고, 더 중요한 것은 호출부가 그 포인터 수명을 프레임/GPU
// 완료와 맞출 방법이 없기 때문이다. 이 표는 BeginFrame에서, 해당 프레임
// 슬롯의 펜스를 기다린 뒤에만 비워진다.
class VulkanBindingTable
{
public:
    void Reset()
    {
        const std::lock_guard lock(m_mutex);
        m_requests.clear();
        ++m_epoch;
        if (0 == m_epoch) ++m_epoch;
    }

    RHIBindingTable Add(std::span<const RHIBindingDesc> descs)
    {
        if (descs.empty() || descs.size() > UINT32_MAX) return {};

        auto request = std::make_unique<Request>();
        request->descs.assign(descs.begin(), descs.end());

        const std::lock_guard lock(m_mutex);
        m_requests.push_back(std::move(request));

        RHIBindingTable table{};
        // 0은 디버깅할 때도 명백한 무효 값으로 남겨 둔다.
        table.backend = static_cast<uint64_t>(m_requests.size());
        table.count = static_cast<uint32_t>(descs.size());
        table.version = m_epoch;
        return table;
    }

    const std::vector<RHIBindingDesc>* Resolve(const RHIBindingTable& table) const
    {
        const std::lock_guard lock(m_mutex);
        if (!table.IsValid() || 0 == table.backend || table.version != m_epoch)
            return nullptr;
        const uint64_t index = table.backend - 1;
        if (index >= m_requests.size()) return nullptr;

        const std::vector<RHIBindingDesc>& descs =
            m_requests[static_cast<size_t>(index)]->descs;
        if (descs.size() != table.count) return nullptr;
        return &descs;
    }

private:
    struct Request
    {
        std::vector<RHIBindingDesc> descs;
    };

    // Request를 따로 할당해 Add의 vector 재할당 중에도 Resolve가 돌려준 descs
    // 주소가 안정적으로 유지되게 한다. Reset은 worker join 뒤에만 호출된다.
    std::vector<std::unique_ptr<Request>> m_requests;
    mutable std::mutex m_mutex;
    uint64_t m_epoch{ 1 };
};

// 디바이스 수명의 동적 샘플러 표.
//
// RHISamplerTable은 DX12에서 GPU sampler-heap handle이지만 Vulkan에서는
// descriptor set에 쓸 VkSampler 목록을 가리키는 안정적인 정수 슬롯이다.
// CreateSamplers는 패스 Initialize에서 호출되어 프레임을 넘어 보관되므로
// VulkanBindingTable처럼 BeginFrame에 비우면 안 된다. VkSampler도 그것을
// 참조한 descriptor set보다 오래 살아야 하므로 디바이스 Shutdown까지 둔다.
class VulkanSamplerTable
{
public:
    RHISamplerTable Add(VkDevice device, std::span<const RHISamplerDesc> descs)
    {
        if (VK_NULL_HANDLE == device || descs.empty() || descs.size() > UINT32_MAX)
            return {};

        for (size_t i = 0; i < m_requests.size(); ++i)
        {
            if (Same(m_requests[i].descs, descs))
                return RHISamplerTable{ static_cast<uint64_t>(i + 1) };
        }

        Request request{};
        request.descs.assign(descs.begin(), descs.end());
        request.samplers.reserve(descs.size());
        for (const RHISamplerDesc& desc : descs)
        {
            VkSamplerCreateInfo info{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
            info.magFilter = Filter(desc.minMag);
            info.minFilter = Filter(desc.minMag);
            info.mipmapMode = Mipmap(desc.mip);
            info.addressModeU = Address(desc.addressU);
            info.addressModeV = Address(desc.addressV);
            info.addressModeW = Address(desc.addressW);
            info.compareEnable = (RHICompareOp::None != desc.compare) ? VK_TRUE : VK_FALSE;
            info.compareOp = Compare(desc.compare);
            info.borderColor = (RHIBorderColor::OpaqueWhite == desc.border)
                ? VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE
                : VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
            info.maxLod = desc.maxLod;

            VkSampler sampler = VK_NULL_HANDLE;
            if (VK_SUCCESS != VulkanApi::vkCreateSampler(
                device, &info, nullptr, &sampler))
            {
                for (VkSampler made : request.samplers)
                    VulkanApi::vkDestroySampler(device, made, nullptr);
                return {};
            }
            request.samplers.push_back(sampler);
        }

        m_requests.push_back(std::move(request));
        return RHISamplerTable{ static_cast<uint64_t>(m_requests.size()) };
    }

    const std::vector<VkSampler>* Resolve(const RHISamplerTable& table) const
    {
        if (!table.IsValid()) return nullptr;
        const uint64_t index = table.backend - 1;
        if (index >= m_requests.size()) return nullptr;
        return &m_requests[static_cast<size_t>(index)].samplers;
    }

    void Shutdown(VkDevice device)
    {
        if (VK_NULL_HANDLE != device)
        {
            for (const Request& request : m_requests)
                for (VkSampler sampler : request.samplers)
                    if (VK_NULL_HANDLE != sampler)
                        VulkanApi::vkDestroySampler(device, sampler, nullptr);
        }
        m_requests.clear();
    }

private:
    struct Request
    {
        std::vector<RHISamplerDesc> descs;
        std::vector<VkSampler> samplers;
    };

    static bool Same(const RHISamplerDesc& a, const RHISamplerDesc& b)
    {
        return a.minMag == b.minMag && a.mip == b.mip &&
            a.addressU == b.addressU && a.addressV == b.addressV &&
            a.addressW == b.addressW && a.compare == b.compare &&
            a.border == b.border && a.maxLod == b.maxLod;
    }

    static bool Same(const std::vector<RHISamplerDesc>& a,
        std::span<const RHISamplerDesc> b)
    {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i)
            if (!Same(a[i], b[i])) return false;
        return true;
    }

    static VkFilter Filter(RHIFilterMode mode)
    {
        return (RHIFilterMode::Linear == mode) ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    }

    static VkSamplerMipmapMode Mipmap(RHIFilterMode mode)
    {
        return (RHIFilterMode::Linear == mode)
            ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
    }

    static VkSamplerAddressMode Address(RHIAddressMode mode)
    {
        switch (mode)
        {
        case RHIAddressMode::Wrap:   return VK_SAMPLER_ADDRESS_MODE_REPEAT;
        case RHIAddressMode::Border: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        default:                     return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        }
    }

    static VkCompareOp Compare(RHICompareOp op)
    {
        switch (op)
        {
        case RHICompareOp::Less:      return VK_COMPARE_OP_LESS;
        case RHICompareOp::LessEqual: return VK_COMPARE_OP_LESS_OR_EQUAL;
        default:                      return VK_COMPARE_OP_NEVER;
        }
    }

    std::vector<Request> m_requests;
};

#endif
