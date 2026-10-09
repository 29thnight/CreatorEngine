#!/usr/bin/env python3
"""Compile real audio consumer source against small scene stubs and check native/managed ABI.
This does not compile the Windows product, exercise the real scene lifecycle, or run C#.
Run: python3 Tools/regression/verify-audio-consumer-contract.py
"""
from pathlib import Path
import json
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
STUBS = {'Component.h': '#pragma once\n'
                '#include <string>\n'
                '#include <cstddef>\n'
                '#include <algorithm>\n'
                '#include <cmath>\n'
                '#include <vector>\n'
                'class Entity;\n'
                'namespace gc\n'
                '{\n'
                '    class tracer;\n'
                '}\n'
                'namespace meta { template<class D,class B> using identity = B; }\n'
                'namespace spdlog { namespace level { enum level_enum { err, warn }; } }\n'
                'struct Debug { static inline std::vector<std::string> messages; static void '
                'PrintLog(spdlog::level::level_enum,const std::string&s) { messages.push_back(s); } };\n'
                'struct Component {\n'
                ' virtual ~Component() = default;\n'
                ' virtual void gc_trace(gc::tracer&) const {}\n'
                ' virtual void OnAddedToScene() {} virtual void OnRemovingFromScene() {} virtual void '
                'OnBeginSimulation() {} virtual void OnEndSimulation() {} virtual void OnUninitializing() '
                '{}\n'
                ' Entity* owner{}; bool enabled{true}; std::size_t id{1};\n'
                ' Entity* GetOwner() const { return owner; } bool IsEnabled() const { return enabled; } '
                'std::size_t GetInstanceID() const { return id; }\n'
                '};\n',
 'Reflection.hpp': '#pragma once\n',
 'CurvePoint.h': '#pragma once\n#include <vector>\nstruct CurvePoint { float distance{}; float gain{1}; };\n',
 'EntityHandle.h': '#pragma once\n'
                   '#include <cstdint>\n'
                   'struct EntityHandle { std::uint32_t index{}, generation{}, sceneId{}; };\n',
 'Transform.h': '#pragma once\n'
                '#include <mathematics/vector3.hpp>\n'
                '#include <mathematics/quaternion.hpp>\n'
                'struct Transform {\n'
                ' math::vector3 position{}; math::vector3 pending{}; bool dirty{};\n'
                ' void SetPending(math::vector3 value) { pending=value; dirty=true; }\n'
                ' math::vector3 GetWorldPosition() const { return position; }\n'
                ' math::quaternion GetWorldQuaternion() const { return {}; }\n'
                '};\n',
 'Entity.h': '#pragma once\n'
             '#include "Component.h"\n'
             '#include "Transform.h"\n'
             '#include <type_traits>\n'
             'class Scene; class SoundComponent; class AudioListenerComponent;\n'
             'struct Entity {\n'
             ' Scene* scene{}; Transform transform; SoundComponent* sound{}; AudioListenerComponent* '
             'listener{};\n'
             ' bool destroyed{}; std::uint32_t m_index{}; std::uint32_t generation{1}; std::size_t id{7};\n'
             ' struct Name { std::string ToString() const { return "test"; } } m_name;\n'
             ' Scene* GetScene() const { return scene; } bool IsDestroyMark() const { return destroyed; } '
             'std::size_t GetInstanceID() const { return id; }\n'
             ' template<class T> T* GetComponent() const {\n'
             '  if constexpr(std::is_same_v<T,Transform>) { return const_cast<Transform*>(&transform); }\n'
             '  else if constexpr(std::is_same_v<T,SoundComponent>) { return sound; }\n'
             '  else { return listener; }\n'
             ' }\n'
             '};\n',
 'CameraComponent.h': '#pragma once\n'
                      '#include "Audio/ListenerState.h"\n'
                      'class Entity;\n'
                      'struct CameraComponent {\n'
                      ' Entity* GetOwner() const { return nullptr; }\n'
                      ' struct Snapshot { math::vector3 eyePosition{}, forward{0,0,1}, up{0,1,0}; };\n'
                      ' Snapshot CaptureFrameSnapshot() const { return {}; }\n'
                      '};\n',
 'Scene.h': '#pragma once\n'
            '#include "SoundSystem.h"\n'
            '#include "Entity.h"\n'
            '#include "CameraComponent.h"\n'
            'struct Scene {\n'
            ' SoundSystem sounds; Entity* entity{};\n'
            ' struct CameraRegistry { CameraComponent* GetPrimaryCamera() const { return nullptr; } } '
            'cameras;\n'
            ' SoundSystem& Sounds() { return sounds; } const SoundSystem& Sounds() const { return sounds; }\n'
            ' CameraRegistry& Cameras() { return cameras; }\n'
            ' Entity* Resolve(EntityHandle h) const { return entity && h.generation == entity->generation ? '
            'entity : nullptr; }\n'
            ' bool EnsureResolved(EntityHandle handle) { auto* value=Resolve(handle); if (!value) { return '
            'false; } if (value->transform.dirty) { value->transform.position=value->transform.pending; '
            'value->transform.dirty=false; } return true; }\n'
            ' EntityHandle HandleOf(std::uint32_t i) const { return {i,entity?entity->generation:1u,1}; }\n'
            '};\n',
 'LifecycleTrace.h': '#pragma once\n#define LIFECYCLE_TRACE(...)\n'}

def main():
    native = (ROOT / "Engine/SceneRuntime/ClrHost.cpp").read_text(encoding="utf-8-sig")
    managed = (ROOT / "ScriptCore/Native.cs").read_text(encoding="utf-8-sig")
    native_table = native.split("struct ScriptApiTable", 1)[1].split("ScriptApiTable g_apiTable", 1)[0]
    managed_table = managed.split("internal static unsafe class Native", 1)[0]
    native_names = re.findall(r"\(__stdcall\*\s*(\w+)\)", native_table)
    managed_names = re.findall(r"delegate\* unmanaged<[^;]+>\s+(\w+)\s*;", managed_table)
    assert native_names == managed_names, "Native/managed API table order mismatch"
    native_version = re.search(r"CreatorScriptApiVersion\s*=\s*(\d+)",
        (ROOT / "Engine/Utility_Framework/ScriptApiVersion.h").read_text()).group(1)
    managed_version = re.search(r"ExpectedVersion\s*=\s*(\d+)", managed).group(1)
    assert native_version == managed_version, "Native/managed API version mismatch"
    audio_names = [name for name in native_names if name.startswith(("Audio_", "AudioListener_", "Sound_"))]
    for name in audio_names:
        assert re.search(r"g_apiTable\." + name + r"\s*=\s*&Api_" + name + r"\s*;", native), name
        assert re.search(r"\bApi_" + name + r"\s*\([^;]*?\)\s*\{", native, re.S), name
    settings_native = native.split("    struct AudioPlaySettings", 1)[1].split("    struct AudioParameter", 1)[0]
    audio_cs = (ROOT / "ScriptCore/Audio.cs").read_text()
    settings_managed = audio_cs.split("public struct AudioPlaySettings", 1)[1].split("[StructLayout", 1)[0]
    native_types = re.findall(r"\b(float|int|std::uint32_t)\s+\w+\s*;", settings_native)
    managed_types = re.findall(r"public\s+(float|int|uint|AudioBus|AudioRolloff|AudioOwnerDestroyedPolicy)\s+\w+\s*(?:=[^;]+)?;", settings_managed)
    native_types = ["uint" if value == "std::uint32_t" else value for value in native_types]
    managed_types = ["int" if value.startswith("Audio") else value for value in managed_types]
    assert native_types == managed_types and len(native_types) == 15, "AudioPlaySettings packet scalar layout mismatch"
    assert "AudioPlaySettings*, Native.AudioParameterABI*, int, ulong> Audio_Play" in managed
    assert "const AudioPlaySettings* settings, const AudioParameter* parameters, int parameterCount" in native
    for relative in ["Engine/SceneRuntime/SoundComponent.h", "Engine/SceneRuntime/SoundDefinition.h",
                     "Engine/SceneRuntime/SoundSystem.h", "Engine/SceneRuntime/AudioListenerComponent.h",
                     "ScriptCore/SoundComponent.cs", "ScriptCore/Audio.cs"]:
        assert not re.search(r"FMOD|fmod|ma_sound|ma_engine|BackendVoiceId", (ROOT / relative).read_text()), relative
    component = (ROOT / "Engine/SceneRuntime/SoundComponent.cpp").read_text()
    assert "PlayAttached(" not in component, "Emitter must preserve authored spatial blend"
    assert "request.loopOverride = false;" in component, "One-shots must override looping presets"
    scene = (ROOT / "Engine/SceneRuntime/Scene.h").read_text(encoding="utf-8-sig")
    assert re.search(r"\[\[reflgen::ignore\]\]\s+SoundSystem m_soundSystem", scene)
    assert "class SoundSystem final" in (ROOT / "Engine/SceneRuntime/SoundSystem.h").read_text()
    for host in ["Editor/EngineEntry/EditorMain.cpp", "Player/PlayerMain.cpp"]:
        text = (ROOT / host).read_text(encoding="utf-8-sig")
        for expected in ["BindAudioPlayback(nullptr)", "m_audioPlayback->Shutdown()", "m_audioHost->Shutdown()",
                         "m_audioHost->Update(", "m_audioPlayback->Update()", "PublishAudioProfile("]:
            assert expected in text, (host, expected)
    compiler = shutil.which("g++") or shutil.which("clang++")
    if not compiler:
        raise RuntimeError("g++ or clang++ is required for the isolated consumer probe")
    with tempfile.TemporaryDirectory(prefix="audio-consumer-contract-") as directory:
        target = Path(directory)
        for name, text in STUBS.items():
            (target / name).write_text(text)
        for name in ["SoundComponent", "SoundSystem", "AudioListenerComponent"]:
            for extension in ["h", "cpp"]:
                shutil.copyfile(ROOT / "Engine/SceneRuntime" / (name + "." + extension), target / (name + "." + extension))
        shutil.copyfile(ROOT / "Engine/SceneRuntime/SoundDefinition.h", target / "SoundDefinition.h")
        shutil.copyfile(ROOT / "Tools/regression/audio_consumer_contract_probe.cpp", target / "probe.cpp")
        command = [compiler, "-std=c++20", "-Wno-attributes", "-I", str(target), "-I", str(ROOT / "Engine/SceneRuntime"),
                   "-I", str(ROOT / "ThirdParty/Mathematics/include")]
        command += [str(target / name) for name in ["probe.cpp", "SoundComponent.cpp", "SoundSystem.cpp", "AudioListenerComponent.cpp"]]
        command += [str(ROOT / "Engine/SceneRuntime/Audio" / name) for name in ["PlaybackService.cpp", "SoundGraph.cpp"]]
        command += ["-o", str(target / "probe")]
        subprocess.run(command, check=True)
        subprocess.run([str(target / "probe")], check=True)
        # Compile the actual native binding implementation separately. Windows
        # calling convention, CLR loading and real Entity/Scene remain outside this probe.
        abi_structs = native.split("    struct AudioAssetId", 1)[1].split("\tstruct ScriptApiTable", 1)[0]
        abi_functions = native.split("    bool AudioApiEntered()", 1)[1].split("\t// ── Animator ──", 1)[0]
        preamble = (target / "probe.cpp").read_text().split("int main()", 1)[0]
        glue = preamble + r"""
#include <thread>
#include <deque>
#include <cstring>
#include <cstddef>
#define __stdcall
using Float3 = math::vector3;
struct ScriptObjectHandle { unsigned int index{}, generation{}; };
struct ScriptObjectRegistry {
    Entity* entity{};
    static ScriptObjectRegistry& Get() { static ScriptObjectRegistry value; return value; }
    Entity* Resolve(ScriptObjectHandle handle) { return handle.generation == 1 ? entity : nullptr; }
};
struct Manager {
    Scene* scene{}; wave::PlaybackService* playback{}; wave::PlaybackScope session;
    wave::PlaybackService* AudioPlayback() { return playback; }
    wave::PlaybackScope AudioSessionScope() { return session; }
    Scene* GetActiveScene() { return scene; }
} manager;
auto* SceneManagers = &manager;
std::thread::id g_physicsApiOwner = std::this_thread::get_id();
SoundComponent* ResolveSound(ScriptObjectHandle handle) {
    auto* entity = ScriptObjectRegistry::Get().Resolve(handle); return entity ? entity->sound : nullptr;
}
"""
        glue += "    struct AudioAssetId" + abi_structs + "    bool AudioApiEntered()" + abi_functions
        glue += (ROOT / "Tools/regression/audio_managed_contract_probe.cpp").read_text()
        (target / "abi.cpp").write_text(glue)
        abi_command = [str(target / "abi.cpp") if part == str(target / "probe.cpp") else
                       str(target / "abi_probe") if part == str(target / "probe") else part for part in command]
        subprocess.run(abi_command, check=True)
        subprocess.run([str(target / "abi_probe")], check=True)
    print(json.dumps({"apiVersion": int(native_version), "apiFields": len(native_names),
                      "audioApiBindings": len(audio_names), "consumerProbe": "passed", "productBuild": "not_run",
                      "managedBuild": "not_run", "sceneDependencies": "stubbed"}))

if __name__ == "__main__":
    main()
