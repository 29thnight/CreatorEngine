#include "../../../Engine/SceneRuntime/ScriptApiTable.h"
#include <cstring>
#include <cstdio>

using ce::script::binding::ScriptApiTable;
using ce::script::vector3;

namespace
{
    ScriptApiTable table{};
    vector3 position{};
    int audioState = 0;
    std::uint64_t audioScope = 0;
    std::uint64_t audioPlayback = 0;
    bool audioCompleted = false;
    int assetReleases = 0;
    int bonePinned = 0;
    int decalIntegers[5]{};
    float decalFloats[2]{};
    ce::script::binding::Float4 imageColor{1, 1, 1, 1};
    char logFile[1024]{};
    char logMember[256]{};
    int logLine = 0;

    void __stdcall Unused() {}
    // This ABI fixture does not provide a reflection catalog. Typed stubs keep
    // the required slots valid and reject accidental reflection calls safely.
    std::uint64_t __stdcall ReflectionExecute(ScriptObjectHandle, const char*, int) { return 0; }
    int __stdcall ReflectionRead(std::uint64_t, char*, int) { return -1; }
    void __stdcall ReflectionRelease(std::uint64_t) {}
    template<typename T>
    void SetUnused(T& slot)
    {
        const auto unused = &Unused;
        static_assert(sizeof(slot) == sizeof(unused));
        std::memcpy(&slot, &unused, sizeof(unused));
    }
    int __stdcall IsAlive(ScriptObjectHandle handle)
    {
        return handle.index == 7 && handle.generation == 3 ? 1 : 0;
    }
    int __stdcall ExtraExists(ScriptObjectHandle handle, int kind)
    {
        return IsAlive(handle) && (kind == 1 || kind == 2);
    }
    int __stdcall BoneGetPinned(ScriptObjectHandle handle)
    {
        return IsAlive(handle) ? bonePinned : 0;
    }
    void __stdcall BoneSetPinned(ScriptObjectHandle handle, int pinned)
    {
        if (IsAlive(handle))
        {
            bonePinned = pinned;
        }
    }
    int __stdcall BoneGetResolvedIndex(ScriptObjectHandle handle)
    {
        return IsAlive(handle) ? 17 : -1;
    }
    int __stdcall DecalGetInt(ScriptObjectHandle handle, int field)
    {
        return IsAlive(handle) && field >= 0 && field < 5 ? decalIntegers[field] : 0;
    }
    void __stdcall DecalSetInt(ScriptObjectHandle handle, int field, int value)
    {
        if (IsAlive(handle) && field >= 0 && field < 5)
        {
            decalIntegers[field] = value;
        }
    }
    float __stdcall DecalGetFloat(ScriptObjectHandle handle, int field)
    {
        return IsAlive(handle) && field >= 0 && field < 2 ? decalFloats[field] : 0.0f;
    }
    void __stdcall DecalSetFloat(ScriptObjectHandle handle, int field, float value)
    {
        if (IsAlive(handle) && field >= 0 && field < 2)
        {
            decalFloats[field] = value;
        }
    }
    int __stdcall SimulationStatus(Runtime::TickStatus* status)
    {
        if (!status)
        {
            return -1;
        }
        *status = {};
        return 0;
    }
    int __stdcall CharacterRead(ScriptObjectHandle, std::uint64_t, ce::script::physics_character_state* state)
    {
        if (!state)
        {
            return -1;
        }
        *state = {};
        return 0;
    }
    int __stdcall CharacterVelocity(ScriptObjectHandle, std::uint64_t, ce::script::binding::Float3)
    {
        return 0;
    }
    int __stdcall HasTransform(ScriptObjectHandle handle)
    {
        return IsAlive(handle);
    }
    int __stdcall SetScriptEnabled(ScriptObjectHandle, int, int)
    {
        return 0;
    }
    void __stdcall PrintLog(int, const char*, const char* file, int line, const char* member)
    {
        std::snprintf(logFile, sizeof(logFile), "%s", file ? file : "");
        std::snprintf(logMember, sizeof(logMember), "%s", member ? member : "");
        logLine = line;
    }
    int __stdcall KeyState(int key)
    {
        if (key == 0x41)
        {
            return 1;
        }
        if (key == 0x42)
        {
            return 2;
        }
        if (key == 0x43)
        {
            return 3;
        }
        return 0;
    }
    std::uint64_t __stdcall GetAudioScope(int kind)
    {
        return (1ULL << 32) | static_cast<std::uint64_t>(kind + 1);
    }
    std::uint64_t __stdcall PlayAudio(std::uint64_t scope, ce::script::binding::AudioAssetId asset, int, int,
        vector3, ScriptObjectHandle, const ce::script::binding::AudioPlaySettings* settings,
        const ce::script::binding::AudioParameter*, int)
    {
        if ((!asset.first && !asset.second) || (settings && (settings->pitch != 1.0f || settings->priority != 128)))
        {
            return 0;
        }
        audioScope = scope;
        audioPlayback = (1ULL << 32) | 7;
        audioState = 1;
        audioCompleted = false;
        return audioPlayback;
    }
    int __stdcall AudioState(std::uint64_t playback)
    {
        return playback == audioPlayback ? audioState : 0;
    }
    void __stdcall ControlAudio(std::uint64_t playback, int operation, float, float)
    {
        if (playback != audioPlayback)
        {
            return;
        }
        if (operation == 0)
        {
            audioState = 0;
            audioCompleted = true;
        }
        else if (operation == 1)
        {
            audioState = 2;
        }
        else if (operation == 2)
        {
            audioState = 1;
        }
    }
    int __stdcall TakeAudioCompletion(ce::script::binding::AudioPlaybackCompletion* output)
    {
        if (!audioCompleted || !output)
        {
            return 0;
        }
        *output = {audioPlayback, audioScope, 0, 1};
        audioCompleted = false;
        return 1;
    }
    vector3 __stdcall GetPosition(ScriptObjectHandle)
    {
        return position;
    }
    void __stdcall SetPosition(ScriptObjectHandle, vector3 value)
    {
        position = value;
    }
    void __stdcall AddPosition(ScriptObjectHandle, vector3 delta)
    {
        position.x += delta.x;
        position.y += delta.y;
        position.z += delta.z;
    }
    int __stdcall RequestAsset(const ScriptAssetLink*, std::uint32_t type, const ScriptTextureAssetVariant*, ScriptAssetToken* token)
    {
        *token = {1, 1, type | 0x80000000u};
        return 0;
    }
    int __stdcall AcquireAsset(const ScriptAssetLink*, std::uint32_t type, const ScriptTextureAssetVariant*, ScriptAssetToken* token)
    {
        *token = {2, 1, type};
        return 0;
    }
    int __stdcall ReleaseAsset(ScriptAssetToken)
    {
        ++assetReleases;
        return 0;
    }
    int __stdcall AssetSnapshot(ScriptAssetToken, ScriptAssetRequestSnapshot* snapshot, char* message, int capacity)
    {
        *snapshot = { 1, 0, 1, 5 };
        if (message && capacity > 5)
        {
            std::memcpy(message, "ready", 6);
        }
        return 0;
    }
    int __stdcall AssetAcquireResult(ScriptAssetToken request, ScriptAssetToken* token)
    {
        *token = { 3, 1, request.type & 0x7fffffffu };
        return 0;
    }
    int __stdcall AssetCancel(ScriptAssetToken)
    {
        return 0;
    }
    int __stdcall AssetTexture(ScriptAssetToken, ScriptTextureDescriptor* output)
    {
        *output = { 64, 32, 3, 1, 0 };
        return 0;
    }
    ce::script::binding::Float4 __stdcall GetImageColor(ScriptObjectHandle)
    {
        return imageColor;
    }
    void __stdcall SetImageColor(ScriptObjectHandle, ce::script::binding::Float4 value)
    {
        imageColor = value;
    }
    float __stdcall GetCameraFov(ScriptObjectHandle)
    {
        return 65.0f;
    }
}

extern "C" __declspec(dllexport) int __cdecl ProbeAssetReleases()
{
    return assetReleases;
}

extern "C" __declspec(dllexport) const char* __cdecl ProbeLogFile() { return logFile; }
extern "C" __declspec(dllexport) const char* __cdecl ProbeLogMember() { return logMember; }
extern "C" __declspec(dllexport) int __cdecl ProbeLogLine() { return logLine; }

extern "C" __declspec(dllexport) ScriptApiTable* __cdecl ProbeTable()
{
    table = {};
    table.version = CreatorScriptApiVersion;
    table.structSize = static_cast<int>(sizeof(table));
    const auto unused = &Unused;
    static_assert(sizeof(unused) == sizeof(void*));
    auto* bytes = reinterpret_cast<unsigned char*>(&table);
    for (std::size_t offset = offsetof(ScriptApiTable, PrintLog); offset < sizeof(table); offset += sizeof(unused))
    {
        std::memcpy(bytes + offset, &unused, sizeof(unused));
    }
    table.Entity_IsAlive = &IsAlive;
    table.ExtraComponent_Exists = &ExtraExists;
    table.Bone_GetPinned = &BoneGetPinned;
    table.Bone_SetPinned = &BoneSetPinned;
    table.Bone_GetResolvedIndex = &BoneGetResolvedIndex;
    table.Decal_GetInt = &DecalGetInt;
    table.Decal_SetInt = &DecalSetInt;
    table.Decal_GetFloat = &DecalGetFloat;
    table.Decal_SetFloat = &DecalSetFloat;
    table.Simulation_Status = &SimulationStatus;
    table.Character_Read = &CharacterRead;
    table.Character_Velocity = &CharacterVelocity;
    table.Script_SetEnabled = &SetScriptEnabled;
    table.PrintLog = &PrintLog;
    table.Input_GetKeyState = &KeyState;
    table.Audio_GetScope = &GetAudioScope;
    table.Audio_Play = &PlayAudio;
    table.Audio_State = &AudioState;
    table.Audio_Control = &ControlAudio;
    table.Audio_TakeCompletion = &TakeAudioCompletion;
    table.Asset_RequestTyped = &RequestAsset;
    table.Asset_TryAcquireTyped = &AcquireAsset;
    table.Asset_Release = &ReleaseAsset;
    table.Asset_Snapshot = &AssetSnapshot;
    table.Asset_AcquireResult = &AssetAcquireResult;
    table.Asset_Cancel = &AssetCancel;
    table.Asset_ReadTexture = &AssetTexture;
    table.Image_Exists = &IsAlive;
    table.Image_GetColor = &GetImageColor;
    table.Image_SetColor = &SetImageColor;
    table.Camera_Exists = &IsAlive;
    table.Camera_GetFov = &GetCameraFov;
    table.Transform_Exists = &HasTransform;
    table.Transform_GetLocalPosition = &GetPosition;
    table.Transform_SetLocalPosition = &SetPosition;
    table.Transform_AddLocalPosition = &AddPosition;
    table.NativeReflection_Execute = &ReflectionExecute;
    table.NativeReflection_Read = &ReflectionRead;
    table.NativeReflection_Release = &ReflectionRelease;
    return &table;
}

extern "C" __declspec(dllexport) void __cdecl ProbeMissingSlot()
{
    table.Entity_IsAlive = nullptr;
}

extern "C" __declspec(dllexport) ScriptApiTable* __cdecl ProbeHeadlessTable()
{
    table = {};
    table.version = CreatorScriptApiVersion;
    table.structSize = static_cast<int>(sizeof(table));
    SetUnused(table.Asset_RequestTyped);
    SetUnused(table.Asset_TryAcquireTyped);
    SetUnused(table.Camera_NotifyCameraCut);
    table.Simulation_Status = &SimulationStatus;
    table.Character_Read = &CharacterRead;
    table.Character_Velocity = &CharacterVelocity;
    SetUnused(table.Simulation_Submit);
    SetUnused(table.Simulation_Read);
    SetUnused(table.ExtraComponent_Exists);
    SetUnused(table.ExtraComponent_GetEnabled);
    SetUnused(table.ExtraComponent_SetEnabled);
    table.NativeReflection_Execute = &ReflectionExecute;
    table.NativeReflection_Read = &ReflectionRead;
    table.NativeReflection_Release = &ReflectionRelease;
    table.Entity_IsAlive = &IsAlive;
    return &table;
}

extern "C" __declspec(dllexport) void __cdecl ProbeMissingRequiredSlot()
{
    table.Simulation_Read = nullptr;
}

extern "C" __declspec(dllexport) void __cdecl ProbeMissingReflectionSlot(int slot)
{
    if (slot == 0) table.NativeReflection_Execute = nullptr;
    else if (slot == 1) table.NativeReflection_Read = nullptr;
    else if (slot == 2) table.NativeReflection_Release = nullptr;
}

extern "C" __declspec(dllexport) int __cdecl ProbeInitialize(
    void* entry, ScriptApiTable* api, std::size_t size, std::uint64_t fingerprint, int headless)
{
    using Initialize = int (__cdecl*)(ScriptApiTable*, std::size_t, std::uint64_t, int);
    return reinterpret_cast<Initialize>(entry)(api, size, fingerprint, headless);
}

extern "C" __declspec(dllexport) int __cdecl ProbeShutdown(void* entry)
{
    using Shutdown = int (__cdecl*)();
    return reinterpret_cast<Shutdown>(entry)();
}

extern "C" __declspec(dllexport) int __cdecl ProbeEntityIsAlive(void* entry, std::uint32_t index, std::uint32_t generation)
{
    using IsAlive = int (__cdecl*)(std::uint32_t, std::uint32_t);
    return reinterpret_cast<IsAlive>(entry)(index, generation);
}
