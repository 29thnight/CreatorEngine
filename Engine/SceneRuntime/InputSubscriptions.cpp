#include "InputSubscriptions.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <thread>
#include <vector>

namespace Input
{
    namespace Detail
    {
        struct SubscriptionTarget final
        {
            std::uint64_t objectID{};
            std::uint64_t generation{};
            std::thread::id owner;
            std::atomic<bool> alive{ true };
        };

        struct SubscriptionEntry final
        {
            InputSessionHandle session{};
            Domain domain{ Domain::Game };
            GraphID graph{};
            SignalID signal{};
            std::uint64_t interfaceHash{};
            ValueType type{ ValueType::Button };
            std::uint64_t objectID{};
            std::uint64_t targetGeneration{};
            own::weak_owner<SubscriptionTarget> target;
            std::function<void(const InputFrame&, std::size_t)> callback;
            std::atomic<bool> active{ true };
        };

        struct SubscriptionRegistryState final
        {
            std::thread::id owner;
            std::atomic<bool> alive{ true };
            std::vector<own::shared_owner<SubscriptionEntry>> entries;
            InputSessionHandle session{};
            std::uint64_t retiredGeneration{};
            std::array<std::uint64_t, 2> lastSequence{};
            std::uint64_t exceptions{};
            bool dispatching{};
        };
    }

    InputSubscriptionScope::InputSubscriptionScope(std::uint64_t objectID, std::uint64_t generation)
        : m_target(own::make_shared<Detail::SubscriptionTarget>())
    {
        m_target->objectID = objectID;
        m_target->generation = generation;
        m_target->owner = std::this_thread::get_id();
        m_target->alive.store(objectID != 0 && generation != 0, std::memory_order_relaxed);
    }

    InputSubscriptionScope::~InputSubscriptionScope() { Invalidate(); }
    InputSubscriptionScope::InputSubscriptionScope(InputSubscriptionScope&& other) noexcept
        : m_target(std::move(other.m_target)) {}

    InputSubscriptionScope& InputSubscriptionScope::operator=(InputSubscriptionScope&& other) noexcept
    {
        if (this != &other)
        {
            Invalidate();
            m_target = std::move(other.m_target);
        }
        return *this;
    }

    void InputSubscriptionScope::Invalidate() noexcept
    {
        if (m_target)
        {
            m_target->alive.store(false, std::memory_order_release);
        }
    }

    bool InputSubscriptionScope::IsValid() const noexcept
    {
        return m_target && m_target->alive.load(std::memory_order_acquire);
    }

    InputSubscription::InputSubscription(own::shared_owner<Detail::SubscriptionEntry> entry)
        : m_entry(std::move(entry)) {}
    InputSubscription::~InputSubscription() { Reset(); }
    InputSubscription::InputSubscription(InputSubscription&& other) noexcept : m_entry(std::move(other.m_entry)) {}

    InputSubscription& InputSubscription::operator=(InputSubscription&& other) noexcept
    {
        if (this != &other)
        {
            Reset();
            m_entry = std::move(other.m_entry);
        }
        return *this;
    }

    void InputSubscription::Reset() noexcept
    {
        if (m_entry)
        {
            m_entry->active.store(false, std::memory_order_release);
            m_entry = {};
        }
    }

    bool InputSubscription::IsValid() const noexcept
    {
        if (!m_entry || !m_entry->active.load(std::memory_order_acquire))
        {
            return false;
        }
        const auto target = m_entry->target.lock();
        return target && target->alive.load(std::memory_order_acquire);
    }

    InputSubscriptionRegistry::InputSubscriptionRegistry()
        : m_state(own::make_shared<Detail::SubscriptionRegistryState>())
    {
        // The owning component may be constructed off-GT. Bind the thread on the
        // first actual subscription/dispatch/invalidation, not in its constructor.
    }

    InputSubscriptionRegistry::~InputSubscriptionRegistry()
    {
        m_state->alive.store(false, std::memory_order_release);
        for (const auto& entry : m_state->entries)
        {
            entry->active.store(false, std::memory_order_release);
        }
    }

    InputSubscription InputSubscriptionRegistry::Add(InputSessionHandle session, Domain domain, GraphID graph,
        SignalID signal, std::uint64_t interfaceHash, ValueType type, std::uint32_t schemaVersion,
        std::uint32_t abiVersion, const InputSubscriptionScope& target,
        std::function<void(const InputFrame&, std::size_t)> callback)
    {
        auto& state = *m_state;
        const auto owner = std::this_thread::get_id();
        if (state.owner == std::thread::id{})
        {
            state.owner = owner;
        }
        if (state.owner != owner || !state.alive.load(std::memory_order_acquire) || session.id == 0 ||
            session.generation == 0 || static_cast<unsigned>(domain) > static_cast<unsigned>(Domain::UI) ||
            !graph.IsValid() || !signal.IsValid() ||
            schemaVersion != kInputSchemaVersion || abiVersion != kInputABIVersion || !target.IsValid() ||
            target.m_target->owner != owner || state.entries.size() >= 65'536)
        {
            return {};
        }
        if (state.session.id != 0 && (state.session.id != session.id || session.generation <= state.retiredGeneration))
        {
            return {};
        }
        if (state.session.id == 0)
        {
            state.session = session;
        }
        auto entry = own::make_shared<Detail::SubscriptionEntry>();
        entry->session = session;
        entry->domain = domain;
        entry->graph = graph;
        entry->signal = signal;
        entry->interfaceHash = interfaceHash;
        entry->type = type;
        entry->objectID = target.m_target->objectID;
        entry->targetGeneration = target.m_target->generation;
        entry->target = target.m_target;
        entry->callback = std::move(callback);
        state.entries.push_back(entry);
        return InputSubscription(std::move(entry));
    }

    bool InputSubscriptionRegistry::Dispatch(const InputFrame& frame)
    {
        // Keep this shared dispatch state alive even if a callback destroys the
        // registry/component. Do not dereference this after entering callbacks.
        const auto state = m_state;
        const auto owner = std::this_thread::get_id();
        if (state->owner == std::thread::id{})
        {
            state->owner = owner;
        }
        const auto domain = static_cast<std::size_t>(frame.GetDomain());
        if (state->owner != owner || !state->alive.load(std::memory_order_acquire) || state->dispatching ||
            domain >= state->lastSequence.size() || frame.GetSession().id == 0 || frame.GetSession().generation == 0 ||
            frame.GetSession().generation <= state->retiredGeneration)
        {
            return false;
        }
        if (state->session != frame.GetSession())
        {
            if (state->session.id != 0 && (state->session.id != frame.GetSession().id ||
                frame.GetSession().generation <= state->session.generation))
            {
                return false;
            }
            for (const auto& entry : state->entries)
            {
                if (entry->session == state->session)
                {
                    entry->active.store(false, std::memory_order_release);
                }
            }
            state->session = frame.GetSession();
            state->lastSequence = {};
        }
        if (frame.GetBoundary().sequence <= state->lastSequence[domain])
        {
            return false;
        }
        const auto subscribers = state->entries;
        const auto events = frame.GetEvents();
        state->lastSequence[domain] = frame.GetBoundary().sequence;
        state->dispatching = true;
        for (std::size_t index = 0; index < events.size(); ++index)
        {
            for (const auto& entry : subscribers)
            {
                if (!state->alive.load(std::memory_order_acquire))
                {
                    state->dispatching = false;
                    return true;
                }
                if (!entry->active.load(std::memory_order_acquire) || entry->session != frame.GetSession() ||
                    entry->domain != frame.GetDomain() || entry->signal != events[index].signal ||
                    entry->type != events[index].value.type)
                {
                    continue;
                }
                const auto target = entry->target.lock();
                if (!target || !target->alive.load(std::memory_order_acquire) ||
                    target->objectID != entry->objectID || target->generation != entry->targetGeneration ||
                    entry->graph != frame.GetGraphID() || entry->interfaceHash != frame.GetInterfaceHash())
                {
                    entry->active.store(false, std::memory_order_release);
                    continue;
                }
                try
                {
                    entry->callback(frame, index);
                }
                catch (...)
                {
                    ++state->exceptions;
                }
            }
        }
        state->dispatching = false;
        std::erase_if(state->entries, [](const auto& entry)
        {
            const auto target = entry->target.lock();
            return !entry->active.load(std::memory_order_acquire) || !target ||
                !target->alive.load(std::memory_order_acquire);
        });
        return true;
    }

    void InputSubscriptionRegistry::InvalidateSession(InputSessionHandle session)
    {
        if (m_state->owner != std::thread::id{} && m_state->owner != std::this_thread::get_id())
        {
            return;
        }
        if (m_state->session.id == 0)
        {
            m_state->session = session;
        }
        if (m_state->session.id == session.id)
        {
            m_state->retiredGeneration = (std::max)(m_state->retiredGeneration, session.generation);
        }
        for (const auto& entry : m_state->entries)
        {
            if (entry->session == session)
            {
                entry->active.store(false, std::memory_order_release);
            }
        }
    }

    void InputSubscriptionRegistry::InvalidateDomain(Domain domain)
    {
        if (m_state->owner != std::thread::id{} && m_state->owner != std::this_thread::get_id())
        {
            return;
        }
        for (const auto& entry : m_state->entries)
        {
            if (entry->domain == domain)
            {
                entry->active.store(false, std::memory_order_release);
            }
        }
    }

    void InputSubscriptionRegistry::InvalidateAll()
    {
        if (m_state->owner != std::thread::id{} && m_state->owner != std::this_thread::get_id())
        {
            return;
        }
        for (const auto& entry : m_state->entries)
        {
            entry->active.store(false, std::memory_order_release);
        }
        m_state->entries.clear();
    }

    std::uint64_t InputSubscriptionRegistry::GetExceptionCount() const noexcept
    {
        return m_state->owner == std::this_thread::get_id() ? m_state->exceptions : 0;
    }
}
