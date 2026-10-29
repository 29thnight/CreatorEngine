#include "../Containers/ContainerAPI.h"
#include "../Utility_Framework/Foundation/StandardLibrary.h"
#include "ScriptReflectionBindings.h"
#include "ScriptReflectionCallbacks.h"
#include "ScriptReflectionService.h"
#include "ScriptObjectRegistry.h"
#include "Component.h"
#include "Entity.h"
#include "../Utility_Framework/Reflection/ReflgenRuntime.h"
#include <thread>

namespace
{
    using namespace ce::script::reflection;
    response_tickets g_reflectionResponses;
    std::mutex g_reflectionSessionMutex;
    std::thread::id g_reflectionOwner;
    bool g_reflectionActive = false;

    bool IsComponentType(const reflgen::type_descriptor& type)
    {
        if (type.id() == Meta::ToTypeID(type_guid(Component))) return true;
        for (const auto& base : type.bases())
            if (const auto* parent = base.descriptor(); parent && IsComponentType(*parent)) return true;
        return false;
    }

    std::vector<const reflgen::type_descriptor*> ComponentTypes()
    {
        std::vector<const reflgen::type_descriptor*> result;
        for (const auto* type : Meta::Types().types())
            if (IsComponentType(*type)) result.push_back(type);
        std::sort(result.begin(), result.end(), [](const auto* left, const auto* right)
        {
            return left->name() < right->name();
        });
        return result;
    }

    std::vector<component_view> LiveComponents(ScriptObjectHandle handle)
    {
        Entity* owner = ScriptObjectRegistry::Get().Resolve(handle);
        if (!owner || owner->IsDestroyMark())
            throw protocol_error("stale_owner", "the native entity handle is no longer alive");
        // These roots are confined to this native request. Removing a component
        // during a method call can invalidate its identity, but cannot free the
        // object while the current invocation/notification still unwinds.
        auto ownerPin = owner->root_from_this();
        std::vector<component_view> result;
        for (const auto& edge : owner->m_components)
        {
            Component* component = edge.get();
            if (!component || component->IsDestroyMark()) continue;
            const auto* type = Meta::Find(component->GetTypeID());
            if (!type || !IsComponentType(*type))
                throw protocol_error("unregistered_component", "a live native component has no registered runtime descriptor");
            auto componentPin = component->root_from_this();
            const auto instance = static_cast<std::uint64_t>(component->GetInstanceID());
            auto alive = [ownerPin, componentPin, owner, component, handle, instance]
            {
                (void)ownerPin;
                (void)componentPin;
                if (ScriptObjectRegistry::Get().Resolve(handle) != owner || owner->IsDestroyMark()
                    || component->IsDestroyMark()
                    || static_cast<std::uint64_t>(component->GetInstanceID()) != instance) return false;
                return std::any_of(owner->m_components.begin(), owner->m_components.end(),
                    [component](const auto& current) { return current.get() == component; });
            };
            result.push_back(component_view{type, Meta::MostDerived(component),
                instance,
                [alive, component](std::string_view field)
                {
                    if (alive())
                    {
                        Meta::ScopedPropertyChangeSource source(Meta::PropertyChangeSource::Script);
                        component->NotifyPropertyChanged(field, Meta::PropertyChangeSource::Script);
                    }
                },
                std::move(alive)});
        }
        return result;
    }

    std::uint64_t __stdcall Api_NativeReflection_Execute(ScriptObjectHandle owner,
        const char* request, int bytes) noexcept
    {
        return g_reflectionResponses.execute([=]
        {
            {
                std::lock_guard lock(g_reflectionSessionMutex);
                if (!g_reflectionActive)
                    return failure("not_bound", "the native reflection session has ended");
                if (g_reflectionOwner != std::this_thread::get_id())
                    return failure("wrong_thread", "native component access requires the managed host owner thread");
            }
            if (!request || bytes <= 0 || static_cast<std::size_t>(bytes) > MaximumRequestBytes)
                return failure("invalid_request", "request must be a nonempty UTF-8 buffer of at most 1 MiB");
            host environment;
            environment.component_types = &ComponentTypes;
            environment.components = [owner] { return LiveComponents(owner); };
            environment.components_for = [](std::uint32_t index, std::uint32_t generation)
            {
                return LiveComponents(ScriptObjectHandle{index, generation});
            };
            environment.bind_callback = &ResolveCallback;
            Meta::ScopedPropertyChangeSource source(Meta::PropertyChangeSource::Script);
            return service::execute(environment, std::string_view(request, static_cast<std::size_t>(bytes)));
        });
    }

    int __stdcall Api_NativeReflection_Read(std::uint64_t ticket, char* output, int capacity) noexcept
    {
        return g_reflectionResponses.read(ticket, output, capacity);
    }

    void __stdcall Api_NativeReflection_Release(std::uint64_t ticket) noexcept
    {
        g_reflectionResponses.release(ticket);
    }
}

namespace ce::script::binding
{
    void RegisterReflectionApi(ScriptApiTable& table)
    {
        // Callback rebinding can refuse an active invocation. Perform that
        // preflight before retiring any existing reflection-session state.
        RegisterReflectionCallbackApi(table);
        g_reflectionResponses.clear();
        {
            std::lock_guard lock(g_reflectionSessionMutex);
            g_reflectionOwner = std::this_thread::get_id();
            g_reflectionActive = true;
        }
        table.NativeReflection_Execute = &Api_NativeReflection_Execute;
        table.NativeReflection_Read = &Api_NativeReflection_Read;
        table.NativeReflection_Release = &Api_NativeReflection_Release;
    }

    void EndReflectionSession() noexcept
    {
        EndReflectionCallbackSession();
        try
        {
            std::lock_guard lock(g_reflectionSessionMutex);
            g_reflectionActive = false;
            g_reflectionOwner = {};
        }
        catch (...) {}
        g_reflectionResponses.clear();
    }
}
