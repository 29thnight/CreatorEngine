#include <windows.h>
#include <nethost.h>
#include <hostfxr.h>
#include <coreclr_delegates.h>
#include <cstdint>
#include <cstdio>
#include <string>
#include <cstring>
#include "../../../Engine/SceneRuntime/ScriptApiTable.h"

using Initialize = int (*)(void*, std::size_t, std::uint64_t, int);
using Shutdown = int (*)();
using IsAlive = int (*)(std::uint32_t, std::uint32_t);
using Table = void* (*)();
using Fingerprint = std::uint64_t (*)();
using LoadScripts = int (*)(const char*);
using CreateComponent = int (*)(ScriptObjectHandle, const char*);
using Lifecycle = int (*)(int, int);
using Tick = int (*)(float);
using SetFloat = void (*)(int, int, float);
using GetFloat = float (*)(int, int);
using GetName = int (*)(int, int, char*, int);
using PublishContacts = int (*)(const void*, int);
using Destroy = int (*)(int);
struct BTNodeDesc
{
    std::uint64_t id, parentId;
    int type, policy, isRoot, hasScript;
    float weight;
    int childOrder;
    char name[64], scriptName[64];
};
struct BBEntry
{
    int type, boolValue, intValue;
    float floatValue, x, y, z, w;
    char key[64], stringValue[128];
};
struct AITick { int instanceId; float deltaTime; };
struct BTStats { int treeCount, nodeTypeCount; std::int64_t tickCount, skippedCount; };
static_assert(sizeof(BTNodeDesc) == 168 && sizeof(BBEntry) == 224 && sizeof(AITick) == 8 && sizeof(BTStats) == 24);
using CreateTree = int (*)(ScriptObjectHandle, const BTNodeDesc*, int, const BBEntry*, int);
using FlushAI = int (*)(const AITick*, int);
using ReadStats = int (*)(BTStats*);
using Invoke = int (*)(const char*, const char*, const char*, int, char*, int);

int wmain(int argc, wchar_t** argv)
{
    if (argc != 5)
    {
        return 1;
    }
    wchar_t hostfxrPath[4096]{};
    std::size_t length = sizeof(hostfxrPath) / sizeof(hostfxrPath[0]);
    if (get_hostfxr_path(hostfxrPath, &length, nullptr) != 0)
    {
        return 2;
    }
    const HMODULE host = LoadLibraryW(hostfxrPath);
    const HMODULE provider = LoadLibraryW(argv[3]);
    if (!host || !provider)
    {
        return 3;
    }
    const auto initializeRuntime = reinterpret_cast<hostfxr_initialize_for_runtime_config_fn>(GetProcAddress(host, "hostfxr_initialize_for_runtime_config"));
    const auto getDelegate = reinterpret_cast<hostfxr_get_runtime_delegate_fn>(GetProcAddress(host, "hostfxr_get_runtime_delegate"));
    const auto closeRuntime = reinterpret_cast<hostfxr_close_fn>(GetProcAddress(host, "hostfxr_close"));
    const auto table = reinterpret_cast<Table>(GetProcAddress(provider, "ProbeTable"));
    const auto fingerprint = reinterpret_cast<Fingerprint>(GetProcAddress(provider, "reflgen_creator_script_native_abi_fingerprint"));
    if (!initializeRuntime || !getDelegate || !closeRuntime || !table || !fingerprint)
    {
        return 4;
    }
    hostfxr_handle context{};
    if (initializeRuntime(argv[1], nullptr, &context) != 0 || !context)
    {
        return 5;
    }
    load_assembly_and_get_function_pointer_fn load{};
    const int getResult = getDelegate(context, hdt_load_assembly_and_get_function_pointer, reinterpret_cast<void**>(&load));
    closeRuntime(context);
    if (getResult != 0 || !load)
    {
        return 6;
    }
    constexpr auto type = L"CreatorEngine.Native.NativeBootstrap, CreatorEngine.NativeBindings";
    constexpr auto runtimeType = L"CreatorEngine.Native.RuntimeBootstrap, CreatorEngine.NativeBindings";
    Initialize initialize{};
    Shutdown shutdown{};
    IsAlive isAlive{};
    if (load(argv[2], type, L"Initialize", UNMANAGEDCALLERSONLY_METHOD, nullptr, reinterpret_cast<void**>(&initialize)) != 0 ||
        load(argv[2], type, L"Shutdown", UNMANAGEDCALLERSONLY_METHOD, nullptr, reinterpret_cast<void**>(&shutdown)) != 0 ||
        load(argv[2], type, L"EntityIsAlive", UNMANAGEDCALLERSONLY_METHOD, nullptr, reinterpret_cast<void**>(&isAlive)) != 0)
    {
        return 7;
    }
    constexpr std::size_t tableSize = sizeof(ce::script::binding::ScriptApiTable);
    if (initialize(table(), tableSize, fingerprint() ^ 1, 0) != -1 || initialize(table(), tableSize, fingerprint(), 0) != 0 ||
        isAlive(7, 3) != 1 || isAlive(7, 4) != 0)
    {
        return 8;
    }
    auto get = [&](const wchar_t* method, void** target)
    {
        return load(argv[2], runtimeType, method, UNMANAGEDCALLERSONLY_METHOD, nullptr, target) == 0 && *target;
    };
    Shutdown start{}, stop{}, flush{};
    LoadScripts loadScripts{};
    CreateComponent create{};
    Lifecycle lifecycle{};
    Tick tick{};
    SetFloat setFloat{};
    GetFloat getFloat{};
    GetName getName{};
    PublishContacts publishContacts{};
    Destroy destroy{};
    CreateTree createTree{};
    FlushAI flushAI{};
    ReadStats readStats{};
    Destroy destroyTree{};
    Invoke invoke{};
    if (!get(L"Initialize", reinterpret_cast<void**>(&start)) || !get(L"Shutdown", reinterpret_cast<void**>(&stop)) ||
        !get(L"LoadScripts", reinterpret_cast<void**>(&loadScripts)) || !get(L"CreateComponent", reinterpret_cast<void**>(&create)) ||
        !get(L"DispatchLifecycle", reinterpret_cast<void**>(&lifecycle)) || !get(L"OnTickSimulate", reinterpret_cast<void**>(&tick)) ||
        !get(L"SetFieldFloat", reinterpret_cast<void**>(&setFloat)) || !get(L"DestroyComponent", reinterpret_cast<void**>(&destroy)) ||
        !get(L"FlushRegistrations", reinterpret_cast<void**>(&flush)) || !get(L"GetFieldFloat", reinterpret_cast<void**>(&getFloat)) ||
        !get(L"GetFieldName", reinterpret_cast<void**>(&getName)) || !get(L"PublishContacts", reinterpret_cast<void**>(&publishContacts)) ||
        !get(L"CreateBehaviorTree", reinterpret_cast<void**>(&createTree)) || !get(L"FlushAITicks", reinterpret_cast<void**>(&flushAI)) ||
        !get(L"GetBTStats", reinterpret_cast<void**>(&readStats)) || !get(L"DestroyBehaviorTree", reinterpret_cast<void**>(&destroyTree)) ||
        !get(L"InvokeCallable", reinterpret_cast<void**>(&invoke)))
    {
        return 9;
    }
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, argv[4], -1, nullptr, 0, nullptr, nullptr);
    if (bytes <= 0)
    {
        return 10;
    }
    std::string gamePath(static_cast<std::size_t>(bytes), '\0');
    WideCharToMultiByte(CP_UTF8, 0, argv[4], -1, gamePath.data(), bytes, nullptr, nullptr);
    if (start() != 0 || loadScripts(gamePath.c_str()) != 0)
    {
        return 11;
    }
    char callableResult[128]{};
    if (invoke("NativeCallableProbe", "Add", "3\x1f" "4", 2, callableResult, sizeof(callableResult)) != 0 ||
        std::string(callableResult) != "Int32\x1f" "7" ||
        invoke("NativeCallableProbe", "Hidden", "", 0, callableResult, sizeof(callableResult)) != -6)
    {
        return 16;
    }
    const int instance = create({7, 3}, "FSharpNativeMover");
    if (instance <= 0)
    {
        return 12;
    }
    setFloat(instance, 0, 4.0f);
    char fieldName[32]{};
    if (getFloat(instance, 0) != 4.0f || getName(instance, 0, fieldName, sizeof(fieldName)) != 5 ||
        std::string(fieldName, 5) != "Speed" || publishContacts(nullptr, 0) != 0 || publishContacts(nullptr, 1) != -1 ||
        lifecycle(instance, 0) != 0 || lifecycle(instance, 1) != 0 || lifecycle(instance, 2) != 0 || tick(0.5f) != 1)
    {
        return 13;
    }
    BTNodeDesc nodes[2]{};
    nodes[0].id = 1;
    nodes[0].isRoot = 1;
    std::strcpy(nodes[0].name, "RootSequence");
    nodes[1].id = 2;
    nodes[1].parentId = 1;
    std::strcpy(nodes[1].name, "Condition");
    std::strcpy(nodes[1].scriptName, "NativeConditionProbe");
    BBEntry entry{};
    entry.type = 1;
    entry.boolValue = 1;
    std::strcpy(entry.key, "ready");
    const int tree = createTree({7, 3}, nodes, 2, &entry, 1);
    const AITick aiTick{tree, 0.1f};
    BTStats stats{};
    if (tree <= 0 || flushAI(&aiTick, 1) != 1 || readStats(&stats) != 0 || stats.treeCount != 1 ||
        stats.tickCount != 1 || destroyTree(tree) != 0)
    {
        return 15;
    }
    const auto* api = static_cast<ce::script::binding::ScriptApiTable*>(table());
    // ProbeTable resets the provider; the value is kept in its independent position state.
    if (api->Transform_GetLocalPosition({7, 3}).x != 2.0f || destroy(instance) != 0 || flush() != 0 || stop() != 0 ||
        shutdown() != 0 || isAlive(7, 3) != -1)
    {
        return 14;
    }
    std::puts("SCRIPT_NATIVE_HOSTFXR_OK");
    return 0;
}
