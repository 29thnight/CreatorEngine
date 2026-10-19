#pragma once
#include <cstdint>
#include <vector>

#include "prsm_abi.h"

namespace prsm::host
{
// 세대 번호 핸들 테이블. 하위 32비트 슬롯, 상위 32비트 세대. 0은 null.
class CE_API HandleTable
{
public:
    CeHandle Create(void* instance);
    void Destroy(CeHandle handle);
    bool IsAlive(CeHandle handle) const;
    void* Resolve(CeHandle handle) const;
    CeHandle HandleFor(const void* instance) const;

    static HandleTable& Instance();

private:
    struct Slot
    {
        void* instance = nullptr;
        std::uint32_t generation = 1;
        bool used = false;
    };
    std::vector<Slot> slots_{Slot{}}; // 슬롯 0은 null 예약
    std::vector<std::uint32_t> free_;
};
} // namespace prsm::host
