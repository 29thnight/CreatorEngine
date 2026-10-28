#include "reflgen_script_reflection_probe.h"
#include "types.h"
#include "../../../Engine/SceneRuntime/ScriptApiTable.h"
#include "../../../Engine/SceneRuntime/ScriptReflectionService.h"
#include <memory>
#include <thread>

#if defined(_WIN32)
#define PROBE_EXPORT extern "C" __declspec(dllexport)
#else
#define PROBE_EXPORT extern "C" __attribute__((visibility("default")))
#endif

namespace
{
    namespace protocol = ce::script::reflection;
    using ce::script::binding::ScriptApiTable;
    constexpr std::uint64_t FirstInstance = 0xf123456789abcde0ULL;
    std::unique_ptr<reflection_probe::Component> component;
    reflection_probe::PhysicsBody physicsBody;
    reflection_probe::CharacterMovement characterMovement;
    bool physicsEnabled = false;
    std::uint64_t instance = FirstInstance;
    std::uint32_t generation = 3;
    int notifications = 0;
    std::string lastField;
    protocol::response_tickets responses;
    ScriptApiTable table{};
    std::thread::id ownerThread;

    protocol::host environment(ScriptObjectHandle owner)
    {
        protocol::host result;
        result.component_types = [] {
            return std::vector<const reflgen::type_descriptor*>{
                &reflgen::type_descriptor_of<reflection_probe::Base>(),
                &reflgen::type_descriptor_of<reflection_probe::Component>(),
                &reflgen::type_descriptor_of<reflection_probe::PhysicsBody>(),
                &reflgen::type_descriptor_of<reflection_probe::CharacterMovement>()};
        };
        result.components = [owner] {
            if (owner.index != 7 || owner.generation != generation)
                throw protocol::protocol_error("stale_owner", "The owner generation no longer exists.");
            std::vector<protocol::component_view> values;
            if (component)
            {
                const auto expected = instance;
                values.push_back({&reflgen::type_descriptor_of<reflection_probe::Component>(), component.get(), instance,
                    [expected](std::string_view field) {
                        if (component && instance == expected) { ++notifications; lastField.assign(field); }
                    },
                    [expected] { return component && instance == expected; }});
                if (physicsEnabled)
                {
                    const auto alive = [expected] { return component && instance == expected; };
                    values.push_back({&reflgen::type_descriptor_of<reflection_probe::PhysicsBody>(),
                        &physicsBody, instance, {}, alive});
                    values.push_back({&reflgen::type_descriptor_of<reflection_probe::CharacterMovement>(),
                        &characterMovement, instance, {}, alive});
                }
            }
            return values;
        };
        return result;
    }

    std::uint64_t __stdcall Execute(ScriptObjectHandle owner, const char* text, int bytes)
    {
        if (std::this_thread::get_id() != ownerThread) return 0;
        return responses.execute([&] {
            if (!text || bytes < 0) return protocol::failure("invalid_request", "Invalid UTF-8 request span.");
            return protocol::service::execute(environment(owner), std::string_view(text, static_cast<std::size_t>(bytes)));
        });
    }

    int __stdcall Read(std::uint64_t ticket, char* output, int capacity) { return responses.read(ticket, output, capacity); }
    void __stdcall Release(std::uint64_t ticket) { responses.release(ticket); }
    void __stdcall Unused() {}

    void fill_unused()
    {
        const auto unused = &Unused;
        static_assert(sizeof(unused) == sizeof(void*));
        auto* bytes = reinterpret_cast<unsigned char*>(&table);
        for (std::size_t offset = offsetof(ScriptApiTable, PrintLog); offset < sizeof(table); offset += sizeof(unused))
            std::memcpy(bytes + offset, &unused, sizeof(unused));
    }
}

PROBE_EXPORT ScriptApiTable* ProbeReflectionTable()
{
    ownerThread = std::this_thread::get_id();
    component = std::make_unique<reflection_probe::Component>();
    component->cycle.next = &component->cycle;
    generation = 3;
    instance = FirstInstance;
    notifications = 0;
    physicsEnabled = false;
    lastField.clear();
    responses.clear();
    table = {};
    table.version = CreatorScriptApiVersion;
    table.structSize = static_cast<int>(sizeof(table));
    fill_unused();
    table.NativeReflection_Execute = &Execute;
    table.NativeReflection_Read = &Read;
    table.NativeReflection_Release = &Release;
    return &table;
}

PROBE_EXPORT int ProbeReflectionInitialize(void* entry, ScriptApiTable* api, std::size_t bytes, std::uint64_t fingerprint)
{
    using Initialize = int (__cdecl*)(ScriptApiTable*, std::size_t, std::uint64_t, int);
    return reinterpret_cast<Initialize>(entry)(api, bytes, fingerprint, 0);
}

PROBE_EXPORT void ProbeReflectionReplace()
{
    component.reset();
    ++instance;
    component = std::make_unique<reflection_probe::Component>();
    component->cycle.next = &component->cycle;
}
PROBE_EXPORT void ProbeReflectionRemove() { component.reset(); }
PROBE_EXPORT void ProbeReflectionEnablePhysics() { physicsEnabled = true; }
PROBE_EXPORT void ProbeReflectionDestroyOwner() { ++generation; }
PROBE_EXPORT int ProbeReflectionNotifications() { return notifications; }
PROBE_EXPORT const char* ProbeReflectionLastField() { return lastField.c_str(); }
PROBE_EXPORT int ProbeReflectionCalls() { return component ? component->calls : -1; }
PROBE_EXPORT std::uint64_t ProbeReflectionExecute(ScriptObjectHandle owner, const char* request, int bytes) { return Execute(owner, request, bytes); }
PROBE_EXPORT int ProbeReflectionRead(std::uint64_t ticket, char* output, int capacity) { return Read(ticket, output, capacity); }
PROBE_EXPORT void ProbeReflectionRelease(std::uint64_t ticket) { Release(ticket); }
