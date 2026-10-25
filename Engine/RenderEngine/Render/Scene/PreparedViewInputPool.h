#pragma once
#if defined(CE_CONTRACT_STANDALONE)
#include "../../../../Tools/build/ModuleAdapters/Creator.Core.Ownership.h"
#else
import Creator.Core.Ownership;
#endif

#include <array>
#include <cstddef>

// 그래프나 다른 소비자가 소유한 입력을 덮어쓰지 않는다. 준비자가 const
// owner를 게시하고 자신의 mutable owner를 놓은 뒤에만 불변 입력이 된다.
// 회수된 항목의 내부 vector capacity는 다음 준비에서 그대로 쓸 수 있다.
template<typename T, std::size_t SlotCount>
class PreparedViewInputPool
{
  public:
    own::shared_owner<T> Acquire()
    {
        for (auto& slot : m_slots)
        {
            if (!slot)
            {
                slot = own::make_shared<T>();
            }
            if (slot.use_count() == 1)
            {
                return slot;
            }
        }
        return {};
    }

  private:
    std::array<own::shared_owner<T>, SlotCount> m_slots;
};
