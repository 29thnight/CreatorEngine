#pragma once

#include "InputSession.h"

#include <functional>
#include <utility>

namespace Input
{
    namespace Detail
    {
        struct SubscriptionTarget;
        struct SubscriptionEntry;
        struct SubscriptionRegistryState;
    }

    // A native subscriber owns this scope as part of its own lifetime. Invalidate
    // it on disable/world removal; destruction also invalidates every subscription.
    // Re-enabling requires a fresh scope/generation and explicit new subscriptions.
    // Subscriber objects and dispatch belong to the session owner thread.
    class InputSubscriptionScope final
    {
    public:
        InputSubscriptionScope(std::uint64_t objectID, std::uint64_t generation);
        ~InputSubscriptionScope();
        InputSubscriptionScope(const InputSubscriptionScope&) = delete;
        InputSubscriptionScope& operator=(const InputSubscriptionScope&) = delete;
        InputSubscriptionScope(InputSubscriptionScope&& other) noexcept;
        InputSubscriptionScope& operator=(InputSubscriptionScope&& other) noexcept;
        void Invalidate() noexcept;
        bool IsValid() const noexcept;

    private:
        friend class InputSubscriptionRegistry;
        own::shared_owner<Detail::SubscriptionTarget> m_target;
    };

    // A token never holds a registry pointer. Reset/destruction suppresses the next
    // callback immediately, including during dispatch and after registry teardown.
    class InputSubscription final
    {
    public:
        InputSubscription() = default;
        ~InputSubscription();
        InputSubscription(const InputSubscription&) = delete;
        InputSubscription& operator=(const InputSubscription&) = delete;
        InputSubscription(InputSubscription&& other) noexcept;
        InputSubscription& operator=(InputSubscription&& other) noexcept;
        void Reset() noexcept;
        bool IsValid() const noexcept;

    private:
        friend class InputSubscriptionRegistry;
        explicit InputSubscription(own::shared_owner<Detail::SubscriptionEntry> entry);
        own::shared_owner<Detail::SubscriptionEntry> m_entry;
    };

    class InputSubscriptionRegistry final
    {
    public:
        InputSubscriptionRegistry();
        ~InputSubscriptionRegistry();
        InputSubscriptionRegistry(const InputSubscriptionRegistry&) = delete;
        InputSubscriptionRegistry& operator=(const InputSubscriptionRegistry&) = delete;

        template<class T, class Callback>
        [[nodiscard]] InputSubscription Subscribe(InputSessionHandle session, Domain domain, InputSignal<T> signal,
            const InputSubscriptionScope& target, Callback&& callback)
        {
            std::function<void(const SignalEvent<T>&)> typed(std::forward<Callback>(callback));
            if (!typed)
            {
                return {};
            }
            return Add(session, domain, signal.graph, signal.signal, signal.interfaceHash, InputSignal<T>::kType,
                signal.schemaVersion, signal.abiVersion, target,
                [signal, typed = std::move(typed)](const InputFrame& frame, std::size_t index)
                {
                    SignalEvent<T> event;
                    if (frame.TryReadEvent(index, signal, event))
                    {
                        typed(event);
                    }
                });
        }

        // Exactly one owner-thread call at component Publish, after the frame is
        // sealed and before physics. Reentrant/duplicate/out-of-order batches fail.
        // A subscriber added in a callback first sees a later frame, never the tail
        // of the current one. Exceptions increment the diagnostic counter and the
        // remaining valid subscribers still receive that same ordered event list.
        bool Dispatch(const InputFrame& frame);
        void InvalidateSession(InputSessionHandle session);
        void InvalidateDomain(Domain domain);
        void InvalidateAll();
        std::uint64_t GetExceptionCount() const noexcept;

    private:
        InputSubscription Add(InputSessionHandle session, Domain domain, GraphID graph, SignalID signal,
            std::uint64_t interfaceHash, ValueType type, std::uint32_t schemaVersion, std::uint32_t abiVersion,
            const InputSubscriptionScope& target, std::function<void(const InputFrame&, std::size_t)> callback);
        own::shared_owner<Detail::SubscriptionRegistryState> m_state;
    };
}
