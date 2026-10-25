#pragma once
// Generated from Engine/Contracts/Core/Creator.Core.Delegate.ixx; do not edit.
// Regenerate with: py Tools/build/generate-module-adapters.py
#if !defined(CE_CONTRACT_STANDALONE)
#error "Native consumers must import the named module directly."
#endif

extern "C" void creator_report_delegate_exception(const char*, const char*) noexcept;
#include "../../../Engine/Utility_Framework/Foundation/StandardLibrary.h"
#include "Creator.Core.SpinLock.h"
namespace ce
{
    class delegate_handle
    {
      public:
        delegate_handle() : id_(0) {}
        explicit delegate_handle(std::uint32_t id) : id_(id) {}
        bool is_valid() const { return id_ != 0; }
        std::uint32_t id() const { return id_; }
        bool operator==(const delegate_handle& other) const { return id_ == other.id_; }

        void reset() { id_ = 0; }

      private:
        std::uint32_t id_;
    };

    template<typename T, typename Ret, typename... Args>
    concept callable_with_signature = requires(T t, Args... args) {
        { std::invoke(t, args...) } -> std::convertible_to<Ret>;
    };

    template<typename T, typename Ret, typename... Args>
    struct raw_callback
    {
        T* instance;
        Ret (T::*member)(Args...);
        int priority = 0;
    };

    template<typename T, typename Ret, typename... Args>
    struct shared_callback
    {
        std::shared_ptr<T> instance;
        Ret (T::*member)(Args...);
        int priority = 0;
    };

    template<typename Callable>
    struct lambda_callback
    {
        Callable lambda;
        int priority = 0;
    };

    template<typename Ret, typename... Args>
    class delegate
    {
      public:
        delegate() : next_id_(1) {}
        ~delegate();

        delegate_handle add_lambda(callable_with_signature<Ret, Args...> auto&& func, int priority = 0);
        template<typename T>
        delegate_handle add_shared(const std::shared_ptr<T>& instance, Ret (T::*member)(Args...), int priority = 0);
        template<typename T>
        delegate_handle add_raw(T* instance, Ret (T::*member)(Args...), int priority = 0);
        void remove(delegate_handle& handle);
        void clear();
        void broadcast(Args... args);

        // 여기 있던 UnsafeBroadcast·TargetInvoke·AsyncBroadcast를 걷어냈다 (PHASE 9-5).
        //
        // 셋 다 사용처가 0이었고, 둘은 이 엔진이 세운 규약을 정면으로 깨는 함정이었다:
        //   · UnsafeBroadcast — 락 없이 callbacks_를 순회한다. 다른 스레드가 Add/remove
        //     하는 중이면 컨테이너가 발밑에서 재할당된다.
        //   · AsyncBroadcast  — 콜백을 std::async로 흩뿌린다. "관리 상태는 게임 스레드에서만"
        //     이라는 규약(PHASE 9가 세운 것)을 쓰는 쪽이 알아채지 못한 채 깨게 된다.
        // 쓰이지 않는 함정은 지우는 편이 낫다 — 남겨 두면 언젠가 누군가 이름만 보고 쓴다.

        delegate& operator()(Args... args)
        {
            broadcast(args...);
            return *this;
        }

        delegate& operator-=(delegate_handle& handle)
        {
            remove(handle);
            return *this;
        }

      private:
        struct callback_info
        {
            delegate_handle handle;
            std::function<Ret(Args...)> callback;
            int priority{};
        };

        delegate_handle add_internal(std::function<Ret(Args...)> func, int priority);

        std::atomic_flag atomic_flag_{};
        std::vector<callback_info> callbacks_;
        std::uint32_t next_id_;

        // 브로드캐스트를 도중에 끊는 신호 (PHASE 9-5).
        //
        // 원자 변수인 이유: 읽는 쪽은 순회 중이라 락 밖이고, 쓰는 쪽(clear)은 락 안이다.
        // 평범한 bool이면 그 조합이 형식상 데이터 경합이고, 컴파일러가 루프 밖으로
        // 끌어올려 캐시해 버리면 Clear가 걸어 둔 신호를 영영 못 볼 수도 있다.
        //
        // 한 번 서면 되돌리지 않는다 — 종료 가드이기 때문이다. 자세한 근거는
        // 아래 clear 구현의 종료 순서 설명을 따른다.
        std::atomic<bool> stopped_{false};
    };
} // namespace ce

namespace ce
{
    template<typename Ret, typename... Args>
    inline delegate<Ret, Args...>::~delegate()
    {
        // 여기서 Clear를 부르지 않는다.
        //
        // 소멸자가 도는 시점에는 이 델리게이트를 붙들고 있던 것들도 함께 무너지는
        // 중이라, 콜백을 파괴하다 그 소멸자가 다시 이 객체를 만질 수 있다.
        // 그때는 이미 멤버가 파괴 중이므로 되돌릴 방법이 없다.
        //
        // callbacks_는 vector라 알아서 파괴된다. 명시적으로 끊어야 하는 곳은
        // 소유자가 살아 있는 동안의 Clear이고, 그건 호출부가 부른다.
    }
    template<typename Ret, typename... Args>
    auto delegate<Ret, Args...>::add_lambda(callable_with_signature<Ret, Args...> auto&& func, int priority)
        -> delegate_handle
    {
        return add_internal(std::function<Ret(Args...)>(std::forward<decltype(func)>(func)), priority);
    }

    template<typename Ret, typename... Args>
    template<typename T>
    auto delegate<Ret, Args...>::add_shared(const std::shared_ptr<T>& instance, Ret (T::*member)(Args...), int priority)
        -> delegate_handle
    {
        std::weak_ptr<T> weak_instance = instance;
        return add_internal(
            [weak_instance, member](Args... args)
            {
                if (auto shared = weak_instance.lock())
                {
                    (shared.get()->*member)(args...);
                }
            },
            priority);
    }

    template<typename Ret, typename... Args>
    template<typename T>
    auto delegate<Ret, Args...>::add_raw(T* instance, Ret (T::*member)(Args...), int priority) -> delegate_handle
    {
        return add_internal(
            [instance, member](Args... args)
            {
                if (instance)
                {
                    (instance->*member)(args...);
                }
            },
            priority);
    }

    template<typename Ret, typename... Args>
    auto delegate<Ret, Args...>::add_internal(std::function<Ret(Args...)> func, int priority) -> delegate_handle
    {
        spin_lock lock(atomic_flag_);
        delegate_handle handle(next_id_++);
        callback_info info{handle, std::move(func), priority};
        auto it =
            std::lower_bound(callbacks_.begin(), callbacks_.end(), info,
                             [](const callback_info& a, const callback_info& b) { return a.priority > b.priority; });
        callbacks_.insert(it, std::move(info));
        return handle;
    }

    template<typename Ret, typename... Args>
    void delegate<Ret, Args...>::remove(delegate_handle& handle)
    {
        spin_lock lock(atomic_flag_);
        if (callbacks_.empty())
        {
            return;
        }
        callbacks_.erase(std::remove_if(callbacks_.begin(), callbacks_.end(),
                                        [&handle](const callback_info& info) { return info.handle == handle; }),
                         callbacks_.end());

        handle.reset();
    }

    template<typename Ret, typename... Args>
    void delegate<Ret, Args...>::clear()
    {
        // ★ 콜백 파괴를 락 밖으로 뺀다.
        //
        // 예전에는 callbacks_.clear()를 락 안에서 했다. 그 clear가 람다를
        // 파괴하는데, 람다가 잡고 있던 것의 소멸자가 같은 델리게이트의
        // Remove를 부르면 같은 스핀락을 다시 잡으려 든다. spin_lock은
        // 재진입 불가이고 대기에 상한이 없으므로 거기서 영원히 돈다.
        //
        // swap으로 소유권만 옮기고 락을 놓은 뒤에 파괴하면, 그 연쇄가
        // 일어나도 락이 비어 있어 통과한다.
        std::vector<callback_info> doomed;
        {
            spin_lock lock(atomic_flag_);
            // 진행 중일 수 있는 순회를 끊는다.
            stopped_.store(true, std::memory_order_release);
            doomed.swap(callbacks_);
        }
        // 여기서 doomed가 파괴된다 — 락 밖이다.

        // ★ 이 플래그는 되돌리지 않는다. 되돌릴 수 없는 것이 의도다.
        //
        // 9-5에서 "clear 후 재사용 불가"를 결함으로 보고 되돌리게 고쳤다가 종료 순서
        // 회귀가 6회 중 1회 실패했다(그 전 세 번은 6/6이었다). 되짚어 보니 이 래치는
        // 종료 가드로 쓰이고 있었다:
        //
        //   Broadcast는 콜백 목록을 복사해 두고 락 밖에서 순회한다. 종료 중에 Clear가
        //   불리면 그 복사본은 여전히 유효하지만 콜백이 가리키는 대상은 무너지는 중이다.
        //   래치가 서 있어야 다음 반복에서 break로 빠져나온다. 되돌리면 그 창이
        //   나노초로 줄어 순회가 그대로 이어지고, 파괴 중인 객체를 호출한다.
        //
        // 즉 "재사용 불가"는 결함이 아니라 성질이다. 실제로 Clear한 델리게이트를 다시
        // 쓰는 곳도 없다. 언젠가 필요해지면 그때 별도 연산으로 명시해야 한다 —
        // Clear가 조용히 재사용을 허용하면 종료 경로가 다시 열린다.
    }

    template<typename Ret, typename... Args>
    void delegate<Ret, Args...>::broadcast(Args... args)
    {
        std::vector<callback_info> callbacks_to_invoke;
        {
            spin_lock lock(atomic_flag_);
            callbacks_to_invoke = callbacks_;
        }

        for (auto& info : callbacks_to_invoke)
        {
            try
            {
                if (stopped_.load(std::memory_order_acquire))
                {
                    break;
                }

                info.callback(args...);
            }
            catch (const std::exception& e)
            {
                creator_report_delegate_exception("Broadcast", e.what());
                continue;
            }
        }
    }

} // namespace ce
