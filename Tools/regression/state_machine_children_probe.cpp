#include "../../Engine/Containers/Creator.Containers.h"
#include "../../Engine/SceneRuntime/StateMachineChildren.h"
#include <cstdio>
#include <set>
#include <stdexcept>
#include <type_traits>

namespace
{
    using Store = FSM::StateMachineChildren;
    static_assert(!std::is_copy_constructible_v<Store> && !std::is_copy_assignable_v<Store>);
    static_assert(!std::is_move_constructible_v<Store> && !std::is_move_assignable_v<Store>);

    unsigned checks = 0;

    void Check(bool condition, const char* message)
    {
        ++checks;
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

    template<class Action> void Reject(Action&& action, const char* message)
    {
        bool rejected = false;
        try
        {
            action();
        }
        catch (const std::invalid_argument&)
        {
            rejected = true;
        }
        Check(rejected, message);
    }

    void IdentityAndNames()
    {
        Store store;
        Check(!store.ResolveState(0) && !store.ResolveTransition(0)
            && store.IdentifyState(nullptr) == 0 && store.IdentifyTransition(nullptr) == 0,
            "the null child identity was treated as a live object");
        auto* first = store.AddState("idle");
        auto* duplicate = store.AddState("idle");
        const auto firstId = store.IdentifyState(first);
        const auto duplicateId = store.IdentifyState(duplicate);
        Check(first && duplicate && first != duplicate && firstId != 0 && duplicateId > firstId,
            "distinct same-name states did not receive distinct monotonic identities");
        Check(store.FindStateByName("idle") == first, "duplicate-name lookup changed insertion-order behavior");
        auto firstPin = store.ResolveState(firstId);
        Check(firstPin && firstPin.operator->() == first && firstPin->GetName() == "idle",
            "the state identity did not resolve to its real owned object");
        first->SetName("active");
        Check(store.IdentifyState(first) == firstId && store.FindStateByName("active") == first
            && store.FindStateByName("idle") == duplicate,
            "renaming a state changed its identity or broke name lookup");
        Check(store.FindStateByName("missing") == nullptr, "missing state lookup did not return null");

        unsigned calls = 0;
        auto* transition = store.AddTransition(first, duplicate,
            [&calls](const BlackBoard&) { ++calls; return true; });
        const auto transitionId = store.IdentifyTransition(transition);
        Check(transition && transitionId > duplicateId,
            "states and transitions did not share one monotonic identity sequence");
        auto transitionPin = store.ResolveTransition(transitionId);
        Check(transitionPin && transitionPin.operator->() == transition
            && transitionPin->GetFrom() == first && transitionPin->GetTo() == duplicate,
            "transition identity or endpoint identity changed");
        BlackBoard board;
        Check((*transitionPin)(board) && calls == 1, "the stored transition did not invoke its real native condition");
        Check(!store.ResolveState(transitionId) && !store.ResolveTransition(firstId)
            && !store.ResolveState(duplicateId + transitionId + 1)
            && !store.ResolveTransition(duplicateId + transitionId + 1),
            "wrong-kind or unknown child identity resolved to an object");

        own::weak_owner<FSM::Transition> weakTransition(transitionPin);
        store.RemoveTransition(transition);
        Check(!store.ResolveTransition(transitionId) && store.IdentifyTransition(transition) == 0,
            "removed transition remained logically addressable while pinned");
        Check(!weakTransition.expired() && transitionPin->GetFrom() == first,
            "transition removal invalidated the active native owner pin");
        Reject([&] { store.RemoveTransition(transition); }, "a removed transition was accepted twice");
        Check(store.ResolveState(firstId) && store.ResolveState(duplicateId),
            "removing a transition also removed one of its states");
        transitionPin.reset();
        Check(weakTransition.expired(), "an unregistered transition remained owned after its last pin was released");
    }

    void ForeignObjectsAndNulls()
    {
        Store local;
        Store foreign;
        auto* from = local.AddState("from");
        auto* to = local.AddState("to");
        auto* outsider = foreign.AddState("outside");
        auto* localTransition = local.AddTransition(from, to, [](const BlackBoard&) { return true; });
        auto* foreignTransition = foreign.AddTransition(outsider, outsider, [](const BlackBoard&) { return false; });
        const auto fromId = local.IdentifyState(from);
        const auto localTransitionId = local.IdentifyTransition(localTransition);
        Check(local.IdentifyState(outsider) == 0 && local.IdentifyTransition(foreignTransition) == 0,
            "an object owned by another component received a local identity");
        Reject([&] { local.AddTransition(nullptr, to, [](const BlackBoard&) { return true; }); },
            "a transition with a null source was accepted");
        Reject([&] { local.AddTransition(from, nullptr, [](const BlackBoard&) { return true; }); },
            "a transition with a null destination was accepted");
        Reject([&] { local.AddTransition(outsider, to, [](const BlackBoard&) { return true; }); },
            "a transition with a foreign source was accepted");
        Reject([&] { local.AddTransition(from, outsider, [](const BlackBoard&) { return true; }); },
            "a transition with a foreign destination was accepted");
        Reject([&] { local.RemoveState(outsider); }, "foreign state removal was accepted");
        Reject([&] { local.RemoveTransition(foreignTransition); }, "foreign transition removal was accepted");
        local.RemoveState(nullptr);
        local.RemoveTransition(nullptr);
        Check(local.IdentifyState(from) == fromId && local.IdentifyTransition(localTransition) == localTransitionId
            && local.FindStateByName("from") == from && foreign.FindStateByName("outside") == outsider,
            "a rejected or null mutation changed an existing component child");
    }

    void ConnectedTransitionRetirement()
    {
        Store store;
        auto* removed = store.AddState("removed");
        auto* remaining = store.AddState("remaining");
        auto* third = store.AddState("third");
        const auto removedId = store.IdentifyState(removed);
        const auto remainingId = store.IdentifyState(remaining);
        const auto thirdId = store.IdentifyState(third);
        auto removedPin = store.ResolveState(removedId);
        own::weak_owner<FSM::FSMState> weakState(removedPin);

        auto* outgoing = store.AddTransition(removed, remaining, [](const BlackBoard&) { return true; });
        auto* incoming = store.AddTransition(remaining, removed, [](const BlackBoard&) { return true; });
        auto* self = store.AddTransition(removed, removed, [](const BlackBoard&) { return true; });
        auto* unrelated = store.AddTransition(remaining, third, [](const BlackBoard&) { return true; });
        const auto outgoingId = store.IdentifyTransition(outgoing);
        const auto incomingId = store.IdentifyTransition(incoming);
        const auto selfId = store.IdentifyTransition(self);
        const auto unrelatedId = store.IdentifyTransition(unrelated);
        auto outgoingPin = store.ResolveTransition(outgoingId);
        auto incomingPin = store.ResolveTransition(incomingId);
        auto selfPin = store.ResolveTransition(selfId);
        own::weak_owner<FSM::Transition> weakOutgoing(outgoingPin);
        own::weak_owner<FSM::Transition> weakIncoming(incomingPin);
        own::weak_owner<FSM::Transition> weakSelf(selfPin);

        store.RemoveState(removed);
        Check(store.IdentifyState(removed) == 0 && !store.ResolveState(removedId)
            && store.FindStateByName("removed") == nullptr,
            "state removal did not revoke its logical identity immediately");
        Check(store.IdentifyTransition(outgoing) == 0 && !store.ResolveTransition(outgoingId)
            && store.IdentifyTransition(incoming) == 0 && !store.ResolveTransition(incomingId)
            && store.IdentifyTransition(self) == 0 && !store.ResolveTransition(selfId),
            "state removal retained a connected transition identity");
        Check(store.ResolveTransition(unrelatedId) && store.ResolveState(remainingId) && store.ResolveState(thirdId),
            "state removal invalidated unrelated states or transitions");
        Check(!weakState.expired() && !weakOutgoing.expired() && !weakIncoming.expired() && !weakSelf.expired()
            && removedPin->GetName() == "removed" && outgoingPin->GetFrom() == removedPin.operator->()
            && incomingPin->GetTo() == removedPin.operator->(),
            "logical removal freed an object still pinned by the current native call");
        Reject([&] { store.AddTransition(removed, remaining, [](const BlackBoard&) { return true; }); },
            "a retained but logically removed state was accepted as a new endpoint");
        Reject([&] { store.RemoveState(removed); }, "a retained but removed state was removed twice");

        outgoingPin.reset();
        incomingPin.reset();
        selfPin.reset();
        Check(weakOutgoing.expired() && weakIncoming.expired() && weakSelf.expired(),
            "connected transitions retained hidden ownership after pins were released");
        removedPin.reset();
        Check(weakState.expired(), "removed state retained hidden ownership after its last pin was released");
    }

    void ReentrantConditionRemoval()
    {
        Store store;
        auto* from = store.AddState("callback-source");
        auto* to = store.AddState("callback-destination");
        const auto fromId = store.IdentifyState(from);
        const auto toId = store.IdentifyState(to);
        auto fromPin = store.ResolveState(fromId);
        std::uint64_t transitionId = 0;
        bool observedStaleInsideCallback = false;
        auto* transition = store.AddTransition(from, to, [&](const BlackBoard&)
        {
            store.RemoveState(from);
            observedStaleInsideCallback = !store.ResolveState(fromId) && !store.ResolveTransition(transitionId)
                && store.IdentifyState(from) == 0 && fromPin->GetName() == "callback-source";
            return true;
        });
        transitionId = store.IdentifyTransition(transition);
        auto transitionPin = store.ResolveTransition(transitionId);
        own::weak_owner<FSM::Transition> weakTransition(transitionPin);
        BlackBoard board;
        Check((*transitionPin)(board) && observedStaleInsideCallback,
            "condition-triggered removal did not invalidate identities while keeping its active call alive");
        Check(!weakTransition.expired() && store.ResolveState(toId),
            "condition-triggered removal destroyed the active transition or unrelated destination");
        transitionPin.reset();
        Check(weakTransition.expired(), "the retired callback transition outlived its last native pin");
    }

    void CallbackTargetDestructionReentry()
    {
        Store store;
        auto* source = store.AddState("destructor-source");
        auto* destination = store.AddState("destructor-destination");
        const auto sourceId = store.IdentifyState(source);
        const auto destinationId = store.IdentifyState(destination);
        own::weak_owner<FSM::FSMState> weakSource(store.ResolveState(sourceId));
        FSM::Transition* transition = nullptr;
        std::uint64_t transitionId = 0;
        unsigned cleanupCalls = 0;
        bool identitiesRevoked = false;
        bool sourcePinnedDuringCleanup = false;
        bool mutationSucceeded = false;
        auto cleanup = std::shared_ptr<int>(new int(0), [&](int* value) noexcept
        {
            delete value;
            ++cleanupCalls;
            // Query membership before taking any new owner. The old erase
            // implementation still exposed the dying transition here.
            identitiesRevoked = store.IdentifyState(source) == 0
                && store.IdentifyTransition(transition) == 0
                && !store.ResolveState(sourceId) && !store.ResolveTransition(transitionId);
            auto sourcePin = weakSource.lock();
            sourcePinnedDuringCleanup = sourcePin && sourcePin->GetName() == "destructor-source";
            try
            {
                // Force states_ to reallocate. RemoveState must not use an
                // iterator saved before destroying this callback target.
                for (unsigned index = 0; index < 64; ++index)
                {
                    store.AddState("added-during-condition-destruction");
                }
                mutationSucceeded = true;
            }
            catch (...) {}
        });
        transition = store.AddTransition(source, destination,
            [cleanup](const BlackBoard&) { return true; });
        transitionId = store.IdentifyTransition(transition);
        cleanup.reset();
        store.RemoveState(source);
        Check(cleanupCalls == 1 && identitiesRevoked,
            "state/transition IDs remained visible while a retired callback target was being destroyed");
        Check(sourcePinnedDuringCleanup && weakSource.expired(),
            "state retirement did not preserve the source until connected callback destruction completed");
        Check(mutationSucceeded && store.ResolveState(destinationId)
            && store.FindStateByName("added-during-condition-destruction") != nullptr,
            "callback destruction invalidated a pending state iterator or lost its reentrant mutation");

        FSM::Transition* single = nullptr;
        std::uint64_t singleId = 0;
        bool singleRevoked = false;
        unsigned singleCleanupCalls = 0;
        auto singleCleanup = std::shared_ptr<int>(new int(0), [&](int* value) noexcept
        {
            delete value;
            ++singleCleanupCalls;
            singleRevoked = store.IdentifyTransition(single) == 0 && !store.ResolveTransition(singleId);
            try
            {
                store.AddState("added-during-transition-destruction");
            }
            catch (...) {}
        });
        single = store.AddTransition(destination, destination,
            [singleCleanup](const BlackBoard&) { return true; });
        singleId = store.IdentifyTransition(single);
        singleCleanup.reset();
        store.RemoveTransition(single);
        Check(singleCleanupCalls == 1 && singleRevoked
            && store.FindStateByName("added-during-transition-destruction") != nullptr
            && store.ResolveState(destinationId),
            "transition removal exposed its dying owner or rejected a safe callback-destructor mutation");
    }

    struct ConstructionMutation
    {
        Store* store{};
        FSM::FSMState* endpoint{};
        own::weak_owner<FSM::FSMState> weakEndpoint;
        bool armed{};
        bool endpointStayedPinned{};
        unsigned calls{};
    };

    struct ConditionWithReentrantCopy
    {
        std::shared_ptr<ConstructionMutation> mutation;
        explicit ConditionWithReentrantCopy(std::shared_ptr<ConstructionMutation> value)
            : mutation(std::move(value)) {}
        ConditionWithReentrantCopy(ConditionWithReentrantCopy&&) noexcept = default;
        ConditionWithReentrantCopy(const ConditionWithReentrantCopy& other)
            : mutation(other.mutation)
        {
            if (std::exchange(mutation->armed, false))
            {
                ++mutation->calls;
                mutation->store->RemoveState(mutation->endpoint);
                mutation->endpointStayedPinned = !mutation->weakEndpoint.expired();
            }
        }
        bool operator()(const BlackBoard&) const { return true; }
    };

    void CallbackConstructionReentry()
    {
        Store store;
        auto* from = store.AddState("constructor-source");
        auto* to = store.AddState("constructor-destination");
        const auto fromId = store.IdentifyState(from);
        const auto toId = store.IdentifyState(to);
        auto mutation = std::make_shared<ConstructionMutation>();
        mutation->store = &store;
        mutation->endpoint = from;
        mutation->weakEndpoint = store.ResolveState(fromId);
        ConditionFunc condition(ConditionWithReentrantCopy{mutation});
        mutation->armed = true;
        // Moving into AddTransition leaves the first mutation for the actual
        // Transition's callback copy, after initial endpoint validation.
        Reject([&] { store.AddTransition(from, to, std::move(condition)); },
            "transition construction published an endpoint removed by its callback's copy constructor");
        Check(mutation->calls == 1 && mutation->endpointStayedPinned,
            "transition construction failed to pin endpoints across callable ownership changes");
        Check(mutation->weakEndpoint.expired() && !store.ResolveState(fromId) && store.ResolveState(toId),
            "rejected transition construction retained the removed endpoint or damaged another state");
    }

    void StoreDestructionReentry()
    {
        own::weak_owner<FSM::FSMState> weakSource;
        own::weak_owner<FSM::FSMState> weakDestination;
        FSM::Transition* transition = nullptr;
        std::uint64_t transitionId = 0;
        unsigned cleanupCalls = 0;
        bool identitiesRevoked = false;
        bool endpointsPinned = false;
        bool mutationRejected = false;
        {
            Store store;
            auto* source = store.AddState("teardown-source");
            auto* destination = store.AddState("teardown-destination");
            const auto sourceId = store.IdentifyState(source);
            const auto destinationId = store.IdentifyState(destination);
            weakSource = store.ResolveState(sourceId);
            weakDestination = store.ResolveState(destinationId);
            auto cleanup = std::shared_ptr<int>(new int(0),
                [&, storePtr = &store, source, destination, sourceId, destinationId](int* value) noexcept
            {
                delete value;
                ++cleanupCalls;
                identitiesRevoked = storePtr->IdentifyState(source) == 0
                    && storePtr->IdentifyState(destination) == 0
                    && storePtr->IdentifyTransition(transition) == 0
                    && !storePtr->ResolveState(sourceId) && !storePtr->ResolveState(destinationId)
                    && !storePtr->ResolveTransition(transitionId)
                    && storePtr->FindStateByName("teardown-source") == nullptr;
                auto sourcePin = weakSource.lock();
                auto destinationPin = weakDestination.lock();
                endpointsPinned = sourcePin && destinationPin
                    && sourcePin->GetName() == "teardown-source"
                    && destinationPin->GetName() == "teardown-destination";
                try
                {
                    storePtr->AddState("added-during-store-destruction");
                }
                catch (const std::logic_error&)
                {
                    mutationRejected = true;
                }
                catch (...) {}
            });
            transition = store.AddTransition(source, destination,
                [cleanup](const BlackBoard&) { return true; });
            transitionId = store.IdentifyTransition(transition);
            cleanup.reset();
        }
        Check(cleanupCalls == 1 && identitiesRevoked,
            "store teardown exposed child identities while destroying a callback target");
        Check(endpointsPinned,
            "store teardown released transition endpoints before callback target destruction completed");
        Check(mutationRejected, "store teardown accepted a callback-destructor mutation");
        Check(weakSource.expired() && weakDestination.expired(),
            "store teardown retained endpoint owners after callback target destruction completed");
    }

    void IdentityReuseAndStoreDestruction()
    {
        Store store;
        std::set<std::uint64_t> seen;
        std::uint64_t previous = 0;
        for (unsigned index = 0; index < 64; ++index)
        {
            auto* state = store.AddState("reused-name");
            const auto stateId = store.IdentifyState(state);
            auto* edge = store.AddTransition(state, state, [](const BlackBoard&) { return true; });
            const auto edgeId = store.IdentifyTransition(edge);
            Check(stateId > previous && edgeId > stateId && seen.insert(stateId).second && seen.insert(edgeId).second,
                "child identity was reused after removal or allocated from independent type counters");
            previous = edgeId;
            store.RemoveState(state);
            Check(!store.ResolveState(stateId) && !store.ResolveTransition(edgeId),
                "remove/recreate revived an earlier child identity");
        }
        for (const auto id : seen)
        {
            Check(!store.ResolveState(id) && !store.ResolveTransition(id),
                "a previous identity resolved after repeated child replacement");
        }

        own::shared_owner<FSM::FSMState> pinnedState;
        own::shared_owner<FSM::Transition> pinnedTransition;
        own::weak_owner<FSM::FSMState> weakState;
        own::weak_owner<FSM::Transition> weakTransition;
        {
            Store temporary;
            auto* state = temporary.AddState("temporary-store");
            pinnedState = temporary.ResolveState(temporary.IdentifyState(state));
            auto* transition = temporary.AddTransition(state, state, [](const BlackBoard&) { return false; });
            pinnedTransition = temporary.ResolveTransition(temporary.IdentifyTransition(transition));
            weakState = pinnedState;
            weakTransition = pinnedTransition;
        }
        BlackBoard board;
        Check(!weakState.expired() && !weakTransition.expired() && pinnedState->GetName() == "temporary-store"
            && pinnedTransition->GetFrom() == pinnedState.operator->() && !(*pinnedTransition)(board),
            "destroying the child store invalidated retained native owner pins");
        pinnedTransition.reset();
        pinnedState.reset();
        Check(weakState.expired() && weakTransition.expired(),
            "destroyed store children retained hidden ownership after their pins were released");
    }
}

int main()
{
    try
    {
        IdentityAndNames();
        ForeignObjectsAndNulls();
        ConnectedTransitionRetirement();
        ReentrantConditionRemoval();
        CallbackTargetDestructionReentry();
        CallbackConstructionReentry();
        StoreDestructionReentry();
        IdentityReuseAndStoreDestruction();
        std::printf("STATE_MACHINE_CHILDREN_OK checks=%u\n", checks);
        return 0;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "STATE_MACHINE_CHILDREN_FAILED %s\n", error.what());
        return 1;
    }
}
