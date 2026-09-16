#include "VoiceTable.h"

namespace wave
{
    namespace
    {
        // 세대는 0 을 건너뛴다 — 값 초기화한 핸들(generation 0)이 유효해 보이면 안 된다.
        [[nodiscard]] std::uint32_t NextGeneration(std::uint32_t current) noexcept
        {
            const std::uint32_t next = current + 1u;
            return (0u == next) ? 1u : next;
        }
    }

    VoiceTable::VoiceTable(std::size_t capacity)
    {
        m_slots.resize(capacity);
        for (std::size_t index = 0; index < capacity; ++index)
        {
            m_freeSlots.push_back(static_cast<std::uint32_t>(index));
        }
    }

    VoiceHandle VoiceTable::Acquire(const PlayRequest& request, std::uint64_t frame)
    {
        if (m_freeSlots.empty()) return VoiceHandle{};

        const std::uint32_t index = m_freeSlots.front();
        m_freeSlots.pop_front();

        Slot& slot = m_slots[index];
        slot.generation = NextGeneration(slot.generation);

        slot.record = VoiceRecord{};
        slot.record.clip = request.clip;
        slot.record.bus = request.bus;
        slot.record.ownerId = request.ownerId;
        slot.record.priority = request.priority;
        slot.record.loop = request.loop;
        slot.record.baseGain = request.volume;
        slot.record.createdFrame = frame;
        slot.record.state = VoiceState::Pending;

        ++m_aliveCount;
        return VoiceHandle{ index, slot.generation };
    }

    bool VoiceTable::Release(VoiceHandle handle)
    {
        VoiceRecord* const record = Find(handle);
        if (nullptr == record) return false;

        record->state = VoiceState::Free;
        record->backendVoice = BackendVoiceId{};
        record->ownerId = 0;
        record->clip = ClipKey{};

        // ★ 세대는 여기서 올리지 않는다. Acquire 가 올린다 — 슬롯이 실제로 다시
        //   쓰이는 순간이 세대가 바뀌어야 할 순간이다. 상태가 Free 인 것만으로
        //   낡은 핸들은 이미 거부된다.
        m_freeSlots.push_back(handle.index);
        if (m_aliveCount > 0) --m_aliveCount;
        return true;
    }

    const VoiceTable::Slot* VoiceTable::ResolveSlot(VoiceHandle handle) const noexcept
    {
        if (!handle.IsValid()) return nullptr;
        if (handle.index >= m_slots.size()) return nullptr;

        const Slot& slot = m_slots[handle.index];
        if (slot.generation != handle.generation) return nullptr;
        if (VoiceState::Free == slot.record.state) return nullptr;
        return &slot;
    }

    bool VoiceTable::IsAlive(VoiceHandle handle) const noexcept
    {
        return nullptr != ResolveSlot(handle);
    }

    VoiceRecord* VoiceTable::Find(VoiceHandle handle) noexcept
    {
        const VoiceRecord* const record = static_cast<const VoiceTable*>(this)->Find(handle);
        return const_cast<VoiceRecord*>(record);
    }

    const VoiceRecord* VoiceTable::Find(VoiceHandle handle) const noexcept
    {
        const Slot* const slot = ResolveSlot(handle);
        if (nullptr == slot) return nullptr;
        return &slot->record;
    }
}
