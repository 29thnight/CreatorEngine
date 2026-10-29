#pragma once

#include "StateMachineChildren.h"
#include "ScriptReflectionService.h"

namespace ce::script::reflection
{
    namespace state_machine_child_detail
    {
        template<class T>
        bool exact_pointer_type(const reflgen::value_descriptor& value) noexcept
        {
            return value.id() == reflgen::type_id_of<T*>()
                && value.name() == reflgen::type_name_of<T*>()
                && value.size() == sizeof(T*) && value.alignment() == alignof(T*)
                && value.can_dereference();
        }

        inline bool same_type(const reflgen::value_descriptor& left,
            const reflgen::value_descriptor& right) noexcept
        {
            return left.id() == right.id() && left.name() == right.name()
                && left.size() == right.size() && left.alignment() == right.alignment();
        }

        template<class T>
        const reflgen::value_descriptor* result_descriptor(const component_view& component,
            std::string_view method_name)
        {
            if (component.type)
                for (const auto& method : component.type->methods())
                    if (method.name() == method_name && exact_pointer_type<T>(method.result()))
                        return &method.result();
            return nullptr;
        }

        template<class T>
        struct child_pointer_storage
        {
            own::shared_owner<T> owner;
            T* pointer;
            explicit child_pointer_storage(own::shared_owner<T> value)
                : owner(std::move(value)), pointer(owner.borrow().unsafe_get()) {}
        };

        template<class T>
        reflgen::value_view pinned_pointer(const reflgen::value_descriptor& canonical,
            own::shared_owner<T> owner)
        {
            auto storage = std::make_shared<child_pointer_storage<T>>(std::move(owner));
            return reflgen::value_view(canonical, &storage->pointer, &storage->pointer, storage);
        }
    }

    // Shared by the real component adapter and the production-service fixture.
    // Canonical descriptors come from the component's registered methods: do
    // not instantiate value_descriptor_of<FSM*> in a consumer translation unit
    // that does not see the complete generated engine runtime schemas.
    inline void BindStateMachineChildren(component_view& component, const FSM::StateMachineChildren& children)
    {
        namespace detail = state_machine_child_detail;
        const auto* state_type = detail::result_descriptor<FSM::FSMState>(component, "AddState");
        const auto* transition_type = detail::result_descriptor<FSM::Transition>(component, "AddTransition");
        if (!state_type || !transition_type)
            throw std::logic_error("state machine child identity requires registered state and transition pointer descriptors");
        const auto alive = component.alive;
        component.child_supported = [state_type, transition_type](const reflgen::value_descriptor& result)
        {
            return detail::same_type(result, *state_type) || detail::same_type(result, *transition_type);
        };
        component.child_identify = [&children, alive, state_type, transition_type](const reflgen::value_view& result)
            -> std::uint64_t
        {
            if (alive && !alive())
                throw protocol_error("stale_component", "the state machine component no longer exists");
            if (!result)
                throw protocol_error("unsupported", "the state machine child result has no native storage");
            std::uint64_t id{};
            if (detail::same_type(result.descriptor(), *state_type))
            {
                const auto* pointer = *static_cast<FSM::FSMState* const*>(result.data());
                if (!pointer) return 0;
                id = children.IdentifyState(pointer);
            }
            else if (detail::same_type(result.descriptor(), *transition_type))
            {
                const auto* pointer = *static_cast<FSM::Transition* const*>(result.data());
                if (!pointer) return 0;
                id = children.IdentifyTransition(pointer);
            }
            else
                throw protocol_error("unsupported", "the result is not a registered state machine child type");
            if (id == 0)
                throw protocol_error("stale_object", "the returned child does not belong to this state machine");
            return id;
        };
        component.child_resolve = [&children, alive, state_type, transition_type](std::uint64_t type_id,
            std::uint64_t id) -> reflgen::value_view
        {
            if (alive && !alive()) return {};
            if (type_id == state_type->id().value())
            {
                auto owner = children.ResolveState(id);
                if (id != 0 && !owner) return {};
                return detail::pinned_pointer(*state_type, std::move(owner));
            }
            if (type_id == transition_type->id().value())
            {
                auto owner = children.ResolveTransition(id);
                if (id != 0 && !owner) return {};
                return detail::pinned_pointer(*transition_type, std::move(owner));
            }
            throw protocol_error("unsupported", "the requested type is not a registered state machine child type");
        };
    }
}
