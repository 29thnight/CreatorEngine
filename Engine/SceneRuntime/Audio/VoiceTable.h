#pragma once
#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

#include "PlayRequest.h"

namespace wave
{
    // 논리 보이스 하나의 엔진 쪽 기록.
    //
    // ★ 이 표가 있는 것이 새 배선의 전부다. 옛 배선은 이 정보를 하나도 들고 있지
    //   않아서 "지금 몇 개가 울리는가 · 누가 낸 것인가 · 어느 것을 뺏을 것인가" 를
    //   전부 백엔드에 물었다. 표가 있으면 그 질문들이 메모리 순회로 끝나고,
    //   정책(cap · steal · 가상화)이 백엔드 구현과 무관해진다.
    struct VoiceRecord final
    {
        ClipKey clip;
        BusId bus{};
        std::uint64_t ownerId{ 0 };

        // 백엔드가 자기 자원을 되찾는 데 쓰는 불투명 값. 상위 계층은 역참조하지 않는다.
        BackendVoiceId backendVoice{};

        int priority{ 128 };
        bool loop{ false };
        VoiceState state{ VoiceState::Free };

        // ★ 나이는 벽시계가 아니라 프레임으로 센다. 옛 배선의 Oldest 정책은
        //   `getPosition`(재생 위치)으로 나이를 재서, 길이가 다른 클립이 섞이면
        //   "긴 클립의 후반부" 가 항상 먼저 죽었고 루프 채널은 위치가 되감겨
        //   판정이 요동쳤다.
        std::uint64_t createdFrame{ 0 };

        // 정책이 비교에 쓰는 이득. 백엔드의 가청도(getAudibility)에 의존하지 않는다.
        float baseGain{ 1.0f };
        float attenuationGain{ 1.0f };

        [[nodiscard]] float EffectiveGain() const noexcept { return baseGain * attenuationGain; }
    };

    // 고정 용량 슬롯 표. 세대가 붙은 핸들을 발급하고, 낡은 핸들을 거부한다.
    //
    // ★ 용량이 고정인 이유. 보이스 수는 정책이 정하는 값이지 입력에 따라 자라는
    //   값이 아니다. 자라게 두면 cap 이 의미를 잃고, 최악의 프레임에서 할당이 끼어든다.
    class VoiceTable final
    {
    public:
        explicit VoiceTable(std::size_t capacity, std::uint32_t generationNamespace = 0u);

        // 빈 슬롯을 하나 잡아 기록을 채우고 핸들을 준다. 자리가 없으면 무효 핸들.
        //
        // ★ 자리 없음을 예외나 로그가 아니라 **무효 핸들**로 돌려준다 — 보이스가
        //   모자라는 것은 정상 상태이지 오류가 아니다. 부르는 쪽이 값으로 처리한다.
        [[nodiscard]] VoiceHandle Acquire(const PlayRequest& request, std::uint64_t frame);

        // 슬롯을 반납한다. 이미 낡은 핸들이면 아무 일도 하지 않고 false.
        bool Release(VoiceHandle handle);

        [[nodiscard]] bool IsAlive(VoiceHandle handle) const noexcept;

        // 살아 있으면 기록을, 낡았으면 nullptr. 부르는 쪽이 항상 널 검사를 하게 만든다.
        [[nodiscard]] VoiceRecord* Find(VoiceHandle handle) noexcept;
        [[nodiscard]] const VoiceRecord* Find(VoiceHandle handle) const noexcept;

        [[nodiscard]] std::size_t AliveCount() const noexcept { return m_aliveCount; }
        [[nodiscard]] std::size_t Capacity() const noexcept { return m_slots.size(); }

        // 살아 있는 기록 전부를 순회한다. 소유자별 정지·버스별 계수가 이것으로 끝난다.
        template <typename Visitor>
        void ForEachAlive(Visitor&& visitor)
        {
            for (std::size_t index = 0; index < m_slots.size(); ++index)
            {
                Slot& slot = m_slots[index];
                if (VoiceState::Free == slot.record.state) continue;

                const VoiceHandle handle{ static_cast<std::uint32_t>(index), slot.generation };
                visitor(handle, slot.record);
            }
        }

    private:
        struct Slot final
        {
            VoiceRecord record{};
            std::uint32_t generation{ 0 };
        };

        [[nodiscard]] const Slot* ResolveSlot(VoiceHandle handle) const noexcept;

        std::vector<Slot> m_slots;

        // ★ 반납 순서대로 다시 쓴다(FIFO). LIFO 로 하면 방금 놓은 슬롯이 곧바로
        //   재사용돼, 낡은 핸들이 살아 있는 다른 보이스를 가리킬 확률이 높아진다.
        //   세대가 그걸 거부해 주긴 하지만, 잘못된 접근이 **늦게 돌아오는 쪽**이
        //   디버깅에 유리하다.
        //
        //   vector + 읽기 커서로 짰다가 deque 로 바꿨다 — 소비된 앞부분을 언제
        //   걷을지 결정하는 분기가 실수의 자리였고, 그 분기가 없어야 순서 규약이
        //   한눈에 읽힌다.
        std::deque<std::uint32_t> m_freeSlots;
        std::size_t m_aliveCount{ 0 };
        std::uint32_t m_generationNamespace{ 0 };
    };
}
