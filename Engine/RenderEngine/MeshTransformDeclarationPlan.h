#pragma once

#include "../Utility_Framework/Foundation/StandardLibrary.h"

namespace material_graph
{
    // Declare의 입력 범위를 먼저 고정한다. 원본은 호출자가 합류까지 보관하며,
    // 이 계획은 바이트를 복제하거나 GPU 프레임의 수명을 붙잡지 않는다.
    class MeshTransformDeclarationPlan final
    {
      public:
        static constexpr std::size_t kMaxJobs = 4096;
        static constexpr std::size_t kMaxInputs = kMaxJobs * 3;
        static constexpr std::size_t kMaxInputBytes = 256ull << 20;
        static constexpr std::uint64_t kMaxGroups = 65535ull * 65535ull;

        struct Job
        {
            std::array<std::uint32_t, 3> m_inputOffsets{};
            std::uint32_t m_firstGroup{}, m_groupCount{};
        };

        MeshTransformDeclarationPlan() { m_slots.fill(kEmptySlot); }

        void Reset()
        {
            // 작은 선언에서 최대 크기 해시 표 전체를 매번 지우지 않는다.
            // 직전 계획이 실제 차지한 슬롯만 비우고 배열 저장소는 계속 재사용한다.
            for (std::size_t index = 0; index < m_inputCount; ++index)
            {
                m_slots[m_inputs[index].m_slot] = kEmptySlot;
            }
            m_jobCount = 0;
            m_inputCount = 0;
            m_inputBytes = 0;
            m_groupCount = 0;
        }

        bool Append(std::uint32_t points, std::span<const std::byte> vertices, std::span<const std::byte> bones,
                    std::span<const std::byte> lods, std::string& error)
        {
            if (m_jobCount == kMaxJobs)
            {
                error = "METEOR transform declaration exceeds 4096 jobs.";
                return false;
            }
            const std::uint64_t groups = (std::uint64_t(points) + 31u) / 32u;
            if (groups > kMaxGroups - m_groupCount)
            {
                error = "METEOR transform group list exceeds one 2D dispatch.";
                return false;
            }
            Job job;
            if (!AppendInput(vertices, job.m_inputOffsets[0], error) ||
                !AppendInput(bones, job.m_inputOffsets[1], error) || !AppendInput(lods, job.m_inputOffsets[2], error))
            {
                return false;
            }
            job.m_firstGroup = static_cast<std::uint32_t>(m_groupCount);
            job.m_groupCount = static_cast<std::uint32_t>(groups);
            m_jobs[m_jobCount++] = job;
            m_groupCount += groups;
            return true;
        }

        std::size_t JobCount() const { return m_jobCount; }
        std::size_t InputCount() const { return m_inputCount; }
        std::size_t InputBytes() const { return m_inputBytes; }
        std::uint32_t GroupCount() const { return static_cast<std::uint32_t>(m_groupCount); }
        const Job& GetJob(std::size_t index) const { return m_jobs[index]; }

        // 워커마다 최종 업로드의 겹치지 않는 바이트 구간을 쓴다. 큰 메시 하나도
        // 여러 구간에 나뉘며, 중복 입력은 한 번만 복사하고 정렬 틈은 0으로 채운다.
        void CopyInputRange(std::span<std::byte> destination, std::size_t first, std::size_t last) const
        {
            if (destination.size() < m_inputBytes || first > last || last > m_inputBytes)
            {
                throw std::out_of_range("Invalid transform declaration input copy range.");
            }
            std::size_t low = 0;
            std::size_t high = m_inputCount;
            while (low < high)
            {
                const auto middle = low + (high - low) / 2;
                if (m_inputs[middle].m_offset + m_inputs[middle].m_bytes <= first)
                {
                    low = middle + 1;
                }
                else
                {
                    high = middle;
                }
            }
            for (std::size_t index = low; index < m_inputCount && m_inputs[index].m_first < last; ++index)
            {
                const auto& input = m_inputs[index];
                const auto paddingFirst = (std::max)(first, input.m_first);
                const auto paddingLast = (std::min)(last, input.m_offset);
                if (paddingFirst < paddingLast)
                {
                    std::memset(destination.data() + paddingFirst, 0, paddingLast - paddingFirst);
                }
                const auto copyFirst = (std::max)(first, input.m_offset);
                const auto copyLast = (std::min)(last, input.m_offset + input.m_bytes);
                if (copyFirst < copyLast)
                {
                    std::memcpy(destination.data() + copyFirst, input.m_data + (copyFirst - input.m_offset),
                                copyLast - copyFirst);
                }
            }
        }

        template<class Group>
        void FillGroupRange(std::span<Group> destination, std::size_t first, std::size_t last) const
        {
            if (destination.size() < m_groupCount || first > last || last > m_groupCount)
            {
                throw std::out_of_range("Invalid transform declaration group range.");
            }
            std::size_t low = 0;
            std::size_t high = m_jobCount;
            while (low < high)
            {
                const auto middle = low + (high - low) / 2;
                const auto& job = m_jobs[middle];
                if (std::uint64_t(job.m_firstGroup) + job.m_groupCount <= first)
                {
                    low = middle + 1;
                }
                else
                {
                    high = middle;
                }
            }
            for (std::size_t index = low; index < m_jobCount && first < last; ++index)
            {
                const auto& job = m_jobs[index];
                const auto end = (std::min)(last, std::size_t(job.m_firstGroup) + job.m_groupCount);
                for (; first < end; ++first)
                {
                    std::construct_at(destination.data() + first,
                                      Group{static_cast<std::uint32_t>(index),
                                            static_cast<std::uint32_t>((first - job.m_firstGroup) * 32u)});
                }
            }
        }

      private:
        struct Input
        {
            const std::byte* m_data{};
            std::size_t m_bytes{}, m_first{}, m_offset{};
            std::uint32_t m_slot{};
        };

        bool AppendInput(std::span<const std::byte> source, std::uint32_t& offset, std::string& error)
        {
            if ((!source.data() && !source.empty()) || source.size() > kMaxInputBytes)
            {
                error = "METEOR transform input is unavailable or exceeds 256 MiB.";
                return false;
            }
            auto hash = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(source.data()));
            hash ^= std::rotl(static_cast<std::uint64_t>(source.size()), 23);
            hash ^= hash >> 30;
            hash *= 0xbf58476d1ce4e5b9ull;
            hash ^= hash >> 27;
            hash *= 0x94d049bb133111ebull;
            hash ^= hash >> 31;
            auto slot = static_cast<std::size_t>(hash) & (kSlotCount - 1);
            while (m_slots[slot] != kEmptySlot)
            {
                const auto& input = m_inputs[m_slots[slot]];
                if (input.m_data == source.data() && input.m_bytes == source.size())
                {
                    offset = static_cast<std::uint32_t>(input.m_offset);
                    return true;
                }
                slot = (slot + 1) & (kSlotCount - 1);
            }
            const auto aligned = (m_inputBytes + 15u) & ~std::size_t{15u};
            if (m_inputCount == kMaxInputs || source.size() > kMaxInputBytes - aligned)
            {
                error = "METEOR transform packed input exceeds 256 MiB; no fallback.";
                return false;
            }
            m_inputs[m_inputCount] = {source.data(), source.size(), m_inputBytes, aligned,
                                      static_cast<std::uint32_t>(slot)};
            m_slots[slot] = static_cast<std::uint32_t>(m_inputCount++);
            m_inputBytes = aligned + source.size();
            offset = static_cast<std::uint32_t>(aligned);
            return true;
        }

        static constexpr std::uint32_t kEmptySlot = UINT32_MAX;
        static constexpr std::size_t kSlotCount = 32768;
        std::array<Job, kMaxJobs> m_jobs;
        std::array<Input, kMaxInputs> m_inputs;
        std::array<std::uint32_t, kSlotCount> m_slots;
        std::size_t m_jobCount{}, m_inputCount{}, m_inputBytes{};
        std::uint64_t m_groupCount{};
    };
} // namespace material_graph
