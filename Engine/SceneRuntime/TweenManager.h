#pragma once

#include "EntityHandle.h"

#include <mathematics/tween.hpp>

#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

// Mathematics의 tween<T>는 값·시간·재생 정책만 소유한다. 엔진 target과 완료
// 통지는 Scene 수명에 묶인 이 manager가 별도 slot에 보관한다. target은 raw
// pointer가 아니라 EntityHandle이며, apply/completion은 capture가 불가능한 함수
// 포인터다. Scene&도 매 Update 호출에서만 빌리고 slot에는 저장하지 않는다.

enum class TweenApplyResult : uint8_t
{
    Applied,
    TargetLost,
    BindingLost,
};

enum class TweenEndReason : uint8_t
{
    Completed,
    Cancelled,
    TargetLost,
    BindingLost,
};

template<typename Value>
concept TweenManagedValue =
    std::same_as<Value, float> ||
    std::same_as<Value, math::vector2> ||
    std::same_as<Value, math::vector3> ||
    std::same_as<Value, math::vector4> ||
    std::same_as<Value, math::quaternion> ||
    std::same_as<Value, math::color> ||
    std::same_as<Value, math::rect>;

template<TweenManagedValue Value>
struct TweenHandle
{
    uint32_t slot{ 0 };
    uint32_t generation{ 0 };

    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return generation != 0;
    }

    constexpr bool operator==(const TweenHandle&) const noexcept = default;
};

static_assert(std::is_trivially_copyable_v<TweenHandle<float>>);
static_assert(sizeof(TweenHandle<float>) == sizeof(uint32_t) * 2);

template<typename Context, TweenManagedValue Value>
using TweenApplyFunction = TweenApplyResult (*)(
    Context&, EntityHandle, const Value&, uint64_t userData) noexcept;

template<typename Context, TweenManagedValue Value>
using TweenCompletionFunction = void (*)(
    Context&, TweenHandle<Value>, TweenEndReason, uint64_t userData) noexcept;

// Context를 template parameter로 둔 이유는 저장소의 manager 구현을 Scene에
// 결합하지 않은 채 contract probe에서 가짜 context로 동일한 수명 규약을 태우기
// 위해서다. 제품 코드는 아래 TweenManager = BasicTweenManager<Scene>만 사용한다.
template<typename Context>
class BasicTweenManager final
{
public:
    BasicTweenManager() = default;
    ~BasicTweenManager() = default;

    BasicTweenManager(const BasicTweenManager&) = delete;
    BasicTweenManager& operator=(const BasicTweenManager&) = delete;
    BasicTweenManager(BasicTweenManager&&) = delete;
    BasicTweenManager& operator=(BasicTweenManager&&) = delete;

    template<TweenManagedValue Value>
    [[nodiscard]] TweenHandle<Value> Play(
        EntityHandle target,
        math::tween<Value> tween,
        TweenApplyFunction<Context, Value> apply,
        TweenCompletionFunction<Context, Value> completion = nullptr,
        uint64_t userData = 0)
    {
        if (!target.IsValid() || nullptr == apply || m_clearRequested)
        {
            return {};
        }

        auto& pool = PoolOf<Value>();
        uint32_t slotIndex = 0;
        if (!pool.freeSlots.empty())
        {
            slotIndex = pool.freeSlots.back();
            pool.freeSlots.pop_back();
        }
        else
        {
            if (pool.slots.size() >=
                static_cast<size_t>((std::numeric_limits<uint32_t>::max)()))
            {
                return {};
            }
            slotIndex = static_cast<uint32_t>(pool.slots.size());
            pool.slots.emplace_back();
        }

        auto& slot = pool.slots[slotIndex];
        if (0 == slot.generation) slot.generation = 1;
        slot.entry.emplace(Entry<Value>{
            std::move(tween), target, apply, completion, userData,
            m_updating, std::nullopt });
        return TweenHandle<Value>{ slotIndex, slot.generation };
    }

    template<TweenManagedValue Value>
    bool Cancel(TweenHandle<Value> handle) noexcept
    {
        Entry<Value>* entry = FindActive(handle);
        if (nullptr == entry) return false;
        entry->endReason = TweenEndReason::Cancelled;
        return true;
    }

    template<TweenManagedValue Value>
    bool Pause(TweenHandle<Value> handle) noexcept
    {
        Entry<Value>* entry = FindActive(handle);
        if (nullptr == entry) return false;
        entry->tween.pause();
        return true;
    }

    template<TweenManagedValue Value>
    bool Resume(TweenHandle<Value> handle) noexcept
    {
        Entry<Value>* entry = FindActive(handle);
        if (nullptr == entry) return false;
        entry->tween.resume();
        return true;
    }

    template<TweenManagedValue Value>
    bool Restart(TweenHandle<Value> handle) noexcept
    {
        Entry<Value>* entry = FindActive(handle);
        if (nullptr == entry) return false;
        entry->tween.restart();
        return true;
    }

    template<TweenManagedValue Value>
    bool Seek(TweenHandle<Value> handle, float elapsedSeconds) noexcept
    {
        if (!(elapsedSeconds >= 0.0f) || !std::isfinite(elapsedSeconds))
        {
            return false;
        }

        Entry<Value>* entry = FindActive(handle);
        if (nullptr == entry) return false;
        entry->tween.seek(elapsedSeconds);
        return true;
    }

    template<TweenManagedValue Value>
    [[nodiscard]] bool Contains(TweenHandle<Value> handle) const noexcept
    {
        return nullptr != FindActive(handle);
    }

    template<TweenManagedValue Value>
    [[nodiscard]] std::optional<Value> Sample(
        TweenHandle<Value> handle) const noexcept
    {
        const Entry<Value>* entry = FindActive(handle);
        if (nullptr == entry) return std::nullopt;
        return entry->tween.sample();
    }

    template<TweenManagedValue Value>
    [[nodiscard]] std::optional<math::tween_state> State(
        TweenHandle<Value> handle) const noexcept
    {
        const Entry<Value>* entry = FindActive(handle);
        if (nullptr == entry) return std::nullopt;
        return entry->tween.state();
    }

    template<TweenManagedValue Value>
    [[nodiscard]] size_t ActiveCount() const noexcept
    {
        return CountActive(PoolOf<Value>());
    }

    [[nodiscard]] size_t ActiveCount() const noexcept
    {
        size_t total = 0;
        std::apply([&total](const auto&... pools)
        {
            ((total += CountActive(pools)), ...);
        }, m_pools);
        return total;
    }

    // apply가 manager를 다시 호출해도 slot 제거와 callback 실행은 순회 밖에서
    // 일어난다. 그 apply 안에서 만든 tween은 pendingActivation으로 표시되어 다음
    // Update부터 재생된다. completion callback은 모든 typed pool의 sweep이 끝난 뒤
    // 호출되므로 callback에서 Play/Cancel을 호출해도 현재 순회를 무효화하지 않는다.
    void Update(float deltaSeconds, Context& context)
    {
        if (m_updating || m_dispatching) return;
        if (!(deltaSeconds >= 0.0f) || !std::isfinite(deltaSeconds)) return;

        {
            FlagScope updating{ m_updating };
            std::apply([&](auto&... pools)
            {
                (AdvancePool(pools, deltaSeconds, context), ...);
            }, m_pools);

            if (m_clearRequested)
            {
                ClearNow();
                return;
            }

            std::apply([&](auto&... pools)
            {
                (SweepPool(pools), ...);
            }, m_pools);
            std::apply([](auto&... pools)
            {
                (ActivatePending(pools), ...);
            }, m_pools);
        }

        {
            FlagScope dispatching{ m_dispatching };
            std::apply([&](auto&... pools)
            {
                (DispatchPool(pools, context), ...);
            }, m_pools);
        }

        if (m_clearRequested) ClearNow();
    }

    // Scene teardown/hot reload에서는 user callback을 실행하지 않는다. 모든 slot의
    // generation만 올려 기존 handle을 즉시 무효화한다. callback 중 Clear가 오면
    // 남은 callback을 중단하고 dispatch가 끝난 직후 같은 처리를 한다.
    void Clear()
    {
        if (m_updating || m_dispatching)
        {
            m_clearRequested = true;
            return;
        }
        ClearNow();
    }

private:
    template<TweenManagedValue Value>
    struct Entry
    {
        math::tween<Value> tween;
        EntityHandle target;
        TweenApplyFunction<Context, Value> apply;
        TweenCompletionFunction<Context, Value> completion;
        uint64_t userData;
        bool pendingActivation;
        std::optional<TweenEndReason> endReason;
    };

    template<TweenManagedValue Value>
    struct Slot
    {
        uint32_t generation{ 1 };
        std::optional<Entry<Value>> entry;
    };

    template<TweenManagedValue Value>
    struct CompletionRecord
    {
        TweenCompletionFunction<Context, Value> callback;
        TweenHandle<Value> handle;
        TweenEndReason reason;
        uint64_t userData;
    };

    template<TweenManagedValue Value>
    struct Pool
    {
        // push_back 중 기존 slot reference가 유지되어야 apply 재진입 Play가 안전하다.
        std::deque<Slot<Value>> slots;
        std::vector<uint32_t> freeSlots;
        std::vector<CompletionRecord<Value>> completions;
    };

    class FlagScope final
    {
    public:
        explicit FlagScope(bool& flag) noexcept : m_flag(flag) { m_flag = true; }
        ~FlagScope() { m_flag = false; }
        FlagScope(const FlagScope&) = delete;
        FlagScope& operator=(const FlagScope&) = delete;

    private:
        bool& m_flag;
    };

    using Pools = std::tuple<
        Pool<float>,
        Pool<math::vector2>,
        Pool<math::vector3>,
        Pool<math::vector4>,
        Pool<math::quaternion>,
        Pool<math::color>,
        Pool<math::rect>>;

    template<TweenManagedValue Value>
    Pool<Value>& PoolOf() noexcept
    {
        return std::get<Pool<Value>>(m_pools);
    }

    template<TweenManagedValue Value>
    const Pool<Value>& PoolOf() const noexcept
    {
        return std::get<Pool<Value>>(m_pools);
    }

    template<TweenManagedValue Value>
    Entry<Value>* FindActive(TweenHandle<Value> handle) noexcept
    {
        if (!handle.IsValid() || m_clearRequested) return nullptr;
        auto& pool = PoolOf<Value>();
        if (handle.slot >= pool.slots.size()) return nullptr;
        auto& slot = pool.slots[handle.slot];
        if (slot.generation != handle.generation || !slot.entry ||
            slot.entry->endReason.has_value())
        {
            return nullptr;
        }
        return &*slot.entry;
    }

    template<TweenManagedValue Value>
    const Entry<Value>* FindActive(TweenHandle<Value> handle) const noexcept
    {
        if (!handle.IsValid() || m_clearRequested) return nullptr;
        const auto& pool = PoolOf<Value>();
        if (handle.slot >= pool.slots.size()) return nullptr;
        const auto& slot = pool.slots[handle.slot];
        if (slot.generation != handle.generation || !slot.entry ||
            slot.entry->endReason.has_value())
        {
            return nullptr;
        }
        return &*slot.entry;
    }

    template<TweenManagedValue Value>
    void AdvancePool(Pool<Value>& pool, float deltaSeconds, Context& context)
    {
        const size_t countAtEntry = pool.slots.size();
        for (size_t index = 0; index < countAtEntry; ++index)
        {
            if (m_clearRequested) return;
            auto& slot = pool.slots[index];
            if (!slot.entry) continue;
            auto& entry = *slot.entry;
            if (entry.pendingActivation || entry.endReason.has_value()) continue;
            if (entry.tween.state() == math::tween_state::paused) continue;

            const math::tween_step<Value> step = entry.tween.advance(deltaSeconds);
            const TweenApplyResult result =
                entry.apply(context, entry.target, step.value, entry.userData);
            if (m_clearRequested) return;

            // apply 안에서 현재 handle을 Cancel할 수 있다. 그 경우 Cancelled가
            // completion/target 결과보다 우선하며 여기서 덮어쓰지 않는다.
            if (entry.endReason.has_value()) continue;

            switch (result)
            {
            case TweenApplyResult::Applied:
                if (step.completed()) entry.endReason = TweenEndReason::Completed;
                break;
            case TweenApplyResult::TargetLost:
                entry.endReason = TweenEndReason::TargetLost;
                break;
            case TweenApplyResult::BindingLost:
            default:
                entry.endReason = TweenEndReason::BindingLost;
                break;
            }
        }
    }

    template<TweenManagedValue Value>
    static void SweepPool(Pool<Value>& pool)
    {
        pool.completions.clear();
        pool.completions.reserve(pool.slots.size());
        pool.freeSlots.reserve(pool.slots.size());

        for (size_t index = 0; index < pool.slots.size(); ++index)
        {
            auto& slot = pool.slots[index];
            if (!slot.entry || !slot.entry->endReason.has_value()) continue;

            const TweenHandle<Value> oldHandle{
                static_cast<uint32_t>(index), slot.generation };
            if (nullptr != slot.entry->completion)
            {
                pool.completions.push_back(CompletionRecord<Value>{
                    slot.entry->completion, oldHandle,
                    *slot.entry->endReason, slot.entry->userData });
            }

            slot.entry.reset();
            slot.generation = NextGeneration(slot.generation);
            pool.freeSlots.push_back(static_cast<uint32_t>(index));
        }
    }

    template<TweenManagedValue Value>
    static void ActivatePending(Pool<Value>& pool) noexcept
    {
        for (auto& slot : pool.slots)
        {
            if (slot.entry) slot.entry->pendingActivation = false;
        }
    }

    template<TweenManagedValue Value>
    void DispatchPool(Pool<Value>& pool, Context& context) noexcept
    {
        std::vector<CompletionRecord<Value>> completions =
            std::move(pool.completions);
        pool.completions.clear();
        for (const CompletionRecord<Value>& completion : completions)
        {
            if (m_clearRequested) return;
            completion.callback(context, completion.handle,
                completion.reason, completion.userData);
        }
    }

    template<TweenManagedValue Value>
    static size_t CountActive(const Pool<Value>& pool) noexcept
    {
        size_t count = 0;
        for (const auto& slot : pool.slots)
        {
            if (slot.entry && !slot.entry->endReason.has_value()) ++count;
        }
        return count;
    }

    template<TweenManagedValue Value>
    static void ClearPool(Pool<Value>& pool)
    {
        pool.freeSlots.clear();
        pool.freeSlots.reserve(pool.slots.size());
        pool.completions.clear();
        for (size_t index = 0; index < pool.slots.size(); ++index)
        {
            auto& slot = pool.slots[index];
            slot.entry.reset();
            slot.generation = NextGeneration(slot.generation);
            pool.freeSlots.push_back(static_cast<uint32_t>(index));
        }
    }

    void ClearNow()
    {
        std::apply([](auto&... pools)
        {
            (ClearPool(pools), ...);
        }, m_pools);
        m_clearRequested = false;
    }

    static constexpr uint32_t NextGeneration(uint32_t generation) noexcept
    {
        ++generation;
        return 0 == generation ? 1 : generation;
    }

    Pools m_pools;
    bool m_updating{ false };
    bool m_dispatching{ false };
    bool m_clearRequested{ false };
};

class Scene;
using TweenManager = BasicTweenManager<Scene>;
