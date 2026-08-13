#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include "../RHIResourceTypes.h"

#include <span>
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
    void Reset() { m_requests.clear(); }

    RHIBindingTable Add(std::span<const RHIBindingDesc> descs)
    {
        if (descs.empty() || descs.size() > UINT32_MAX) return {};

        Request request{};
        request.descs.assign(descs.begin(), descs.end());
        m_requests.push_back(std::move(request));

        RHIBindingTable table{};
        // 0은 디버깅할 때도 명백한 무효 값으로 남겨 둔다.
        table.backend = static_cast<uint64_t>(m_requests.size());
        table.count = static_cast<uint32_t>(descs.size());
        return table;
    }

    const std::vector<RHIBindingDesc>* Resolve(const RHIBindingTable& table) const
    {
        if (!table.IsValid() || 0 == table.backend) return nullptr;
        const uint64_t index = table.backend - 1;
        if (index >= m_requests.size()) return nullptr;

        const std::vector<RHIBindingDesc>& descs = m_requests[static_cast<size_t>(index)].descs;
        if (descs.size() != table.count) return nullptr;
        return &descs;
    }

private:
    struct Request
    {
        std::vector<RHIBindingDesc> descs;
    };

    std::vector<Request> m_requests;
};

#endif
