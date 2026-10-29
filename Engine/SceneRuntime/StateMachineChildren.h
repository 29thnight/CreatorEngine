#pragma once

#include "../Containers/ContainerAPI.h"
#if defined(CE_CONTRACT_STANDALONE)
#include "../../Tools/build/ModuleAdapters/Creator.Core.Ownership.h"
#else
import Creator.Core.Ownership;
#endif
#include "FSMState.h"
#include "Transition.h"

namespace FSM
{
    // The component owns logical membership. A request may pin a removed child
    // until it returns, but that pin does not make its ID resolvable again.
    // IDs are shared across child kinds and never reused within this component;
    // the script protocol also anchors them to owner generation/component ID.
    class StateMachineChildren final
    {
        struct state_entry
        {
            std::uint64_t id;
            own::shared_owner<FSMState> value;
        };
        struct transition_entry
        {
            std::uint64_t id;
            own::shared_owner<Transition> value;
        };

        std::uint64_t next_id_{};
        bool retiring_{};
        ce::dynamic_array<state_entry> states_;
        ce::dynamic_array<transition_entry> transitions_;

        std::uint64_t NextId()
        {
            if (next_id_ == (std::numeric_limits<std::uint64_t>::max)())
                throw std::overflow_error("state machine child identity space exhausted");
            return ++next_id_;
        }

    public:
        StateMachineChildren() = default;
        ~StateMachineChildren()
        {
            // A native condition destructor can observe or reenter its former
            // component. Revoke the whole graph before any such code runs,
            // and keep endpoint owners until every transition is released.
            retiring_ = true;
            [[maybe_unused]] auto retiringStates = std::move(states_);
            [[maybe_unused]] auto retiringTransitions = std::move(transitions_);
        }
        StateMachineChildren(const StateMachineChildren&) = delete;
        StateMachineChildren& operator=(const StateMachineChildren&) = delete;
        StateMachineChildren(StateMachineChildren&&) = delete;
        StateMachineChildren& operator=(StateMachineChildren&&) = delete;

        FSMState* AddState(const std::string& name)
        {
            if (retiring_) throw std::logic_error("state machine children are being destroyed");
            auto state = own::make_shared<FSMState>(name);
            states_.push_back({NextId(), state});
            return state.borrow().unsafe_get();
        }

        void RemoveState(FSMState* state)
        {
            if (!state) return;
            const auto found = std::find_if(states_.begin(), states_.end(),
                [state](const state_entry& item) { return item.value.borrow().unsafe_get() == state; });
            if (found == states_.end())
                throw std::invalid_argument("state does not belong to this state machine");
            // A condition target's native destructor may reenter this store.
            // Pin every retiring object before any vector compaction, remove
            // all logical membership, then let user-owned targets be destroyed.
            // Keep the state pin until after all transition pins are released.
            [[maybe_unused]] const auto retiringState = found->value;
            ce::dynamic_array<own::shared_owner<Transition>> retiringTransitions;
            retiringTransitions.reserve(transitions_.size());
            for (const auto& item : transitions_)
                if (item.value->GetFrom() == state || item.value->GetTo() == state)
                    retiringTransitions.push_back(item.value);
            transitions_.erase(std::remove_if(transitions_.begin(), transitions_.end(),
                [state](const transition_entry& item)
                { return item.value->GetFrom() == state || item.value->GetTo() == state; }), transitions_.end());
            states_.erase(found);
        }

        Transition* AddTransition(FSMState* from, FSMState* to, ::ConditionFunc condition)
        {
            const auto fromId = IdentifyState(from);
            const auto toId = IdentifyState(to);
            if (!from || !to || fromId == 0 || toId == 0)
                throw std::invalid_argument("transition endpoints must belong to this state machine");
            [[maybe_unused]] const auto fromPin = ResolveState(fromId);
            [[maybe_unused]] const auto toPin = ResolveState(toId);
            auto transition = own::make_shared<Transition>(from, to, std::move(condition));
            // Copying an arbitrary native callable may execute user-owned copy
            // or move code. The pins prevent address reuse; membership must
            // still match before this new transition stores its raw endpoints.
            if (IdentifyState(from) != fromId || IdentifyState(to) != toId)
                throw std::invalid_argument("transition endpoint was removed while constructing the condition");
            transitions_.push_back({NextId(), transition});
            return transition.borrow().unsafe_get();
        }

        void RemoveTransition(Transition* transition)
        {
            if (!transition) return;
            const auto found = std::find_if(transitions_.begin(), transitions_.end(),
                [transition](const transition_entry& item)
                { return item.value.borrow().unsafe_get() == transition; });
            if (found == transitions_.end())
                throw std::invalid_argument("transition does not belong to this state machine");
            [[maybe_unused]] const auto retiringTransition = found->value;
            transitions_.erase(found);
        }

        FSMState* FindStateByName(const std::string& name) const
        {
            if (retiring_) return nullptr;
            for (const auto& state : states_)
                if (state.value->GetName() == name) return state.value.borrow().unsafe_get();
            return nullptr;
        }

        std::uint64_t IdentifyState(const FSMState* state) const noexcept
        {
            if (!retiring_ && state)
                for (const auto& entry : states_)
                    if (entry.value.borrow().unsafe_get() == state) return entry.id;
            return 0;
        }

        std::uint64_t IdentifyTransition(const Transition* transition) const noexcept
        {
            if (!retiring_ && transition)
                for (const auto& entry : transitions_)
                    if (entry.value.borrow().unsafe_get() == transition) return entry.id;
            return 0;
        }

        own::shared_owner<FSMState> ResolveState(std::uint64_t id) const
        {
            if (!retiring_ && id != 0)
                for (const auto& entry : states_)
                    if (entry.id == id) return entry.value;
            return {};
        }

        own::shared_owner<Transition> ResolveTransition(std::uint64_t id) const
        {
            if (!retiring_ && id != 0)
                for (const auto& entry : transitions_)
                    if (entry.id == id) return entry.value;
            return {};
        }
    };
}
