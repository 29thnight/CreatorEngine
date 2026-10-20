"""Exercise extracted production proxy commit code with a throwing CPU sink.

This tests queue/state transitions, not RenderScene payloads or GPU completion.
"""
import argparse
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source', type=Path, help='Optional pre-change Scene.cpp snapshot')
parser.add_argument('--work', type=Path, required=True)
parser.add_argument('--msbuild', type=Path, default=Path(
    r'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\amd64\MSBuild.exe'))
args = parser.parse_args()
repo = Path(__file__).resolve().parents[2]
work = args.work.resolve()
work.mkdir(parents=True, exist_ok=True)
source = (args.source or repo / 'Engine/SceneRuntime/Scene.cpp').read_text(encoding='utf-8')
state = source[source.index('struct SceneRenderRegistryState\n'):source.index('\nScene::Scene(gc::domain&')]
commit = source[source.index('\tusing Kind = SceneRenderRegistryState::Kind;', source.index('void Scene::CommitRenderProxies()')):source.index('\nsize_t Scene::RenderProxyComponentCount()')]
publish = source[source.index('bool Scene::PublishRenderProxyDirty(Component*'):source.index('\nsize_t Scene::PublishRenderProxyDirty(EntityHandle')]
contracts = (repo / 'Engine/SceneRuntime/RenderProxyDirty.h').read_text(encoding='utf-8')
contracts = contracts[contracts.index('enum class ProxyDirty'):]
header = r'''
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <unordered_map>
#include <vector>
struct EntityHandle {
    unsigned index=~0u;
    bool IsValid() const { return index!=~0u; }
    bool operator==(const EntityHandle&) const = default;
};
struct Entity { bool destroyed=false; bool IsDestroyMark() const { return destroyed; } };
struct Component {
    Entity* owner=nullptr;
    size_t id=1;
    Entity* GetOwner() const { return owner; }
    size_t GetInstanceID() const { return id; }
};
struct LightComponent : Component {};
struct MeshRenderer : Component {};
struct TerrainComponent : Component {};
struct FoliageComponent : Component {};
struct DecalComponent : Component {};
struct SpriteRenderer : Component {};
struct ImageComponent : Component {};
struct TextComponent : Component {};
struct SpriteSheetComponent : Component {};
'''
sink = r'''
static void (*onUpdate)(Component*) = nullptr;
struct RenderScene {
    unsigned attempts=0;
    template<class T> void UpdateCommand(T* component) {
        ++attempts;
        if (onUpdate) onUpdate(component);
    }
};
struct Scene {
    std::unique_ptr<SceneRenderRegistryState> m_renderRegistry=std::make_unique<SceneRenderRegistryState>();
    Entity entity;
    RenderScene sink;
    Entity* Resolve(EntityHandle handle) { return handle.index==0 ? &entity : nullptr; }
    void CommitRenderProxies();
    bool PublishRenderProxyDirty(Component*, ProxyDirty);
};
void Scene::CommitRenderProxies() {
    auto& registry=*m_renderRegistry;
    auto* renderScene=&sink;
'''
tests = r'''
static Scene* active=nullptr;
static int fault=0;
static void Require(bool value,const char* why) {
    if (!value) throw std::runtime_error(why);
}
static void ThrowOnce(Component* component) {
    const int mode=std::exchange(fault,0);
    if (mode==2) active->PublishRenderProxyDirty(component,ProxyDirty::Payload);
    if (mode==3) UnregisterRenderProxy(*active->m_renderRegistry,component);
    if (mode==4) {
        UnregisterRenderProxy(*active->m_renderRegistry,component);
        RegisterRenderProxy(*active->m_renderRegistry,component,SceneRenderRegistryState::Kind::Light,{0});
    }
    if (mode) throw std::runtime_error("injected CPU publication failure");
}
int main() {
    try {
        Scene scene; active=&scene;
        auto& state=*scene.m_renderRegistry;
        LightComponent component; component.owner=&scene.entity;
        onUpdate=ThrowOnce;
        RegisterRenderProxy(state,&component,SceneRenderRegistryState::Kind::Light,{0});
        fault=1; scene.CommitRenderProxies();
        Require(state.dirtyQueue.size()==1 && state.registrations.at(&component).pending==ProxyDirty::All,
            "failed publication lost pending dirtiness");
        Require(state.metrics.lastCommitted==0,"failed CPU publication counted as committed");
        scene.CommitRenderProxies();
        Require(state.metrics.lastCommitted==1 && state.dirtyQueue.empty(),"retry did not complete once");

        scene.PublishRenderProxyDirty(&component,ProxyDirty::Transform);
        fault=2; scene.CommitRenderProxies();
        Require(state.dirtyQueue.size()==1 && state.registrations.at(&component).pending==
            (ProxyDirty::Transform|ProxyDirty::Payload),"retry replaced or duplicated a newer publication");
        scene.CommitRenderProxies();
        Require(state.metrics.lastMask==(ProxyDirty::Transform|ProxyDirty::Payload),"merged mask was not retried");

        scene.PublishRenderProxyDirty(&component,ProxyDirty::Transform);
        fault=3; scene.CommitRenderProxies();
        Require(state.registrations.empty() && state.dirtyQueue.empty(),"failure resurrected removed registration");

        RegisterRenderProxy(state,&component,SceneRenderRegistryState::Kind::Light,{0});
        UnregisterRenderProxy(state,&component);
        RegisterRenderProxy(state,&component,SceneRenderRegistryState::Kind::Light,{0});
        scene.CommitRenderProxies();
        Require(state.metrics.lastStale==1 && state.metrics.lastCommitted==1,"old generation reached replacement");

        const auto oldGeneration=state.registrations.at(&component).generation;
        scene.PublishRenderProxyDirty(&component,ProxyDirty::Transform);
        fault=4; scene.CommitRenderProxies();
        const auto replacementGeneration=state.registrations.at(&component).generation;
        Require(replacementGeneration!=oldGeneration && state.dirtyQueue.size()==1
            && state.dirtyQueue.front().generation==replacementGeneration,
            "failure restored an old ticket into a replacement registration");
        scene.CommitRenderProxies();
        Require(state.metrics.lastCommitted==1 && state.dirtyQueue.empty(),"replacement did not commit once");

        scene.PublishRenderProxyDirty(&component,ProxyDirty::Payload);
        component.owner=nullptr;
        const auto attempts=scene.sink.attempts;
        scene.CommitRenderProxies();
        Require(state.metrics.lastStale==1 && scene.sink.attempts==attempts,"invalid owner reached CPU sink");
        std::puts("PROXY_COMMIT_FAILURE_OK cases=6 retry=1 mergedMask=1 removed=1 generation=1 replacement=1 owner=1");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr,"PROXY_COMMIT_FAILURE_FAILED %s\n",error.what());
        return 1;
    }
}
'''
cpp = header + '\n' + contracts + '\n' + state + sink + commit + '\n' + publish + tests
cpp = '#include <utility>\n' + cpp
(work / 'probe.cpp').write_text(cpp, encoding='utf-8')
project = '''<Project DefaultTargets="Build" xmlns="http://schemas.microsoft.com/developer/msbuild/2003"><ItemGroup Label="ProjectConfigurations"><ProjectConfiguration Include="Release|x64"><Configuration>Release</Configuration><Platform>x64</Platform></ProjectConfiguration></ItemGroup><PropertyGroup Label="Globals"><ProjectGuid>{3B0E7AC9-B491-435B-9BE8-2EE8D55B4492}</ProjectGuid><WindowsTargetPlatformVersion>10.0</WindowsTargetPlatformVersion></PropertyGroup><Import Project="$(VCTargetsPath)Microsoft.Cpp.Default.props"/><PropertyGroup Label="Configuration"><ConfigurationType>Application</ConfigurationType><PlatformToolset>v145</PlatformToolset><UseDebugLibraries>false</UseDebugLibraries></PropertyGroup><Import Project="$(VCTargetsPath)Microsoft.Cpp.props"/><PropertyGroup><OutDir>$(ProjectDir)bin/</OutDir><IntDir>$(ProjectDir)obj/</IntDir><TargetName>proxy-commit</TargetName><VcpkgEnabled>false</VcpkgEnabled><VcpkgEnableManifest>false</VcpkgEnableManifest></PropertyGroup><ItemDefinitionGroup><ClCompile><LanguageStandard>stdcpp20</LanguageStandard><Optimization>MaxSpeed</Optimization><ExceptionHandling>Sync</ExceptionHandling><AdditionalOptions>/utf-8 %(AdditionalOptions)</AdditionalOptions></ClCompile></ItemDefinitionGroup><ItemGroup><ClCompile Include="probe.cpp"/></ItemGroup><Import Project="$(VCTargetsPath)Microsoft.Cpp.targets"/></Project>'''
(work / 'probe.proj').write_text(project, encoding='utf-8')
subprocess.run([str(args.msbuild), str(work / 'probe.proj'), '/t:Build', '/p:Configuration=Release',
    '/p:Platform=x64', '/p:ImportDirectoryBuildProps=false', '/p:ImportDirectoryBuildTargets=false', '/v:minimal'], check=True)
result = subprocess.run([str(work / 'bin/proxy-commit.exe')], capture_output=True, text=True)
(work / 'result.txt').write_text(result.stdout + result.stderr, encoding='utf-8')
print(result.stdout + result.stderr)
raise SystemExit(result.returncode)
