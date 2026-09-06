#include "../EditorDiagnostics.h"
// LC6 (PHASE 14.5) — SceneObject 도메인 명령.
//
// `object.*` · `scene.*` · `prefab.*` · `component.*` · `camera.*` · `undo.*` ·
// `play.*` · `ai.*`. 씬과 그 안의 오브젝트를 만들고 바꾸고 되돌린다.
//
// ★ 이 도메인이 §9 의 **Shared Editor operation** 이 가장 많이 모이는 자리다.
//   GUI 의 Play·duplicate·undo 와 서비스의 그것이 Undo 스택·selection·play
//   상태까지 같은 규약으로 움직여야 한다. 사람이 `--exec` 로 한 번 부르던 것과
//   에이전트가 초당 여러 번 부르는 것은 Undo 스택에 남기는 흔적이 다르다.
//   그 동등성 검사는 이동이 아니라 descriptor·characterization 작업의 몫이다.
//
// ── 이 이동에서 바꾸지 않은 것 ──────────────────────────────────────────
//
// 핸들러 본문과 서명 그대로다. 이동의 증거는 골든이 한 글자도 안 변하는 것
// 하나뿐이라, 기능 변경을 섞어 그 증거를 버리지 않는다(§12.3).
//
// include 는 이 TU 가 직접 소유한다(유니티에서 빠져 있다).

#include "CommandRegistrar.h"
#include "CommandSupport.h"
#include "EditorObjectOperations.h"

#include "CommandCore/CommandSession.h" // LC1: 결과 누적과 process exit code
#include "CommandCore/CommandParser.h"
#include "CommandCore/CommandRegistry.h"       // LC3: descriptor snapshot
#include "Commands/CommandRegistrar.h"          // LC6: 도메인 TU 등록 창구
#include "CommandCore/CommandDescriptorSeeds.h"
#include "EditorCommandServiceHost.h"        // LC4: 로컬 HTTP/JSON 서비스  // LC2: 토크나이저와 소유형 invocation
#include "EditorCameraRig.h"
#include "EditorSessionState.h"
#include "EngineBootstrap.h"
#include "GameBuilderSystem.h"
#include "EditorAssetDatabase.h"
#include "Interfaces/AssetAuthoringPort.h"
#include "Interfaces/FoliageInstance.h"
#include <mathematics/color.hpp>
#include "SceneManager.h"
#include "Scene.h"
#include "CameraComponent.h"
#include "CameraSystem.h"
#include "ClrHost.h"
#include "ScriptComponent.h"
#include "PrefabUtility.h"
#include "ComponentFactory.h"
#include "ModelSceneInstantiation.h" // MBC9: generation 씬 인스턴스화
#include "ModelConsumptionDiagnostics.h" // MBC10: 읽기 전용 소비 스냅샷
#include "Material.h"
#include "Mesh.h"
#include "Assets/ModelAssetGeneration.h"
#include "Assets/ModelVertexLayout.h"    // MBC9: skinbounds typed 정점 디코드
#include "Assets/ModelAnimationSampler.h" // MBC9: editorsurface frame 축(CountUniqueKeyTimes)
#include "Assets/ModelAssetAuthoringTransaction.h" // MBC11: assets.modelbench author 모드
#include "RHI/IRHIDeviceResources.h"                // MBC11: VRAM 계측
#include "LifecycleTrace.h"
#include "LifecycleRegistry.h"
#include "Animator.h"
#include "Socket.h" // X7 transform bulk probe
#include "BoneRegion.h" // MAX_BONES
#include "Experiment/Model.h" // I5-D4e-1: experiment.animtick 패리티
#include "RenderScene.h"      // I5-D4e-1: GetAnimationJob
#include "AvatarMask.h"       // I5-D4e-3: experiment.animmask A/B 대조
#include "FoliageComponent.h"      // I5-D5a: experiment.foliage 게이트
#include "Terrain.h"               // D4 Terrain YAML authoring round-trip
#include "Experiment/MaterialInstance.h"      // I5-D5c1: experiment.matruntime
#include "Experiment/MaterialAuthoringCodec.h" // I5-D5c1: 값 인코딩 대조
#include "ExperimentMaterialMigration.h"      // I5-D5c1: legacy 왕복 축
#include "Experiment/Cooked/CookedAssetCatalog.h"  // I7-C1
#include "ExperimentMaterialResolveBinding.h"       // I7-C1: 제품 resolver
#include "StandardMaterialProperty.h"              // I7-C1: probe property
#include "Experiment/MaterialPropertyBlock.h"  // I5-D5c2-1: packing 바이트 축
#include "MaterialPropertyPacker.h"           // I5-D5c2-1: 합성 layout
#include "PrimitiveRenderProxy.h"           // I5-D5c2-2: 프록시 축
#include "MaterialScriptBinding.h"          // I5-D5c3: 실물 편집 창구
#include "ProxyCommandQueue.h"             // I5-D5c3: 갱신 커맨드 소비
#include "Render/Scene/ExperimentMaterialSealing.h" // I5-D5c3-2: texture 축
#include "PrimitiveRenderProxy.h"  // I5-D5a: FoliageRenderProxy 실물 사슬
#include "RHI/IRenderDeviceServices.h" // RHIModelMeshView·BuildRHIModelMeshView
#include "ConditionParameter.h"
#include "UIManager.h"
#include "Canvas.h"
#include "ImageComponent.h"
#include "MeshRenderer.h" // X8 render proxy dirty probe
#include "RectTransformComponent.h"
#include "BoneComponent.h" // E7-b: scene.traversalbench 0 모드의 마커 보유 수 진단
#include "UIButton.h"
#include "TextComponent.h"
#include "SpriteSheetComponent.h"
#include "StateMachineComponent.h"
#include "AIManager.h"
#include "DataSystem.h"
#include "GpuDiagnostics.h"
#include "LogSystem.h"
#include "PathFinder.h"
#include "RuntimeSettings.h"
#include "AuthoringNodeEquality.h" // D3-a-1: 저작 노드 구조 비교
#include "AuthoringNodeViewAccess.h" // D3-a-5b
#include "AuthoringParsedDocument.h"
#include "AuthoringRymlErrorPolicy.h" // D3-b-1: ryml abort → 예외 정책
#include "SerializationProfiler.h" // D0(SerializationPlan): 직렬화 기준선 계측
#include "CoreWindow.h"
#include "Render/Scene/EnhancedSceneRenderer.h"
#include "RHI/DX12/Tests/DX12SelfTest.h"
#include "RHI/Vulkan/VulkanSelfTest.h"
#include "RHI/IImGuiHost.h"
#include "ProfilerSelfTest.h"
#include "ExperimentParity/ExperimentVertexLayoutSelfTest.h"
#include "AssetIdentity/AssetIdentitySelfTest.h"
#include "AssetIdentity/AssetSidecarSchemaSelfTest.h"
#include "AssetIdentity/ModelAssetGenerationSelfTest.h"
#include "AssetIdentity/SceneModelGenerationSelfTest.h"
#include "ExperimentParity/ExperimentSamplerSelfTest.h"
#include "ExperimentParity/ExperimentCookedSelfTest.h"
#include "ExperimentParity/ExperimentWeldSelfTest.h"
#include "ExperimentParity/ExperimentCacheOptSelfTest.h"
#include "ExperimentParity/ExperimentTextureCookSelfTest.h"
#include "ShaderMeta.h"
#include "ExperimentParity/ExperimentShaderMetaCookSelfTest.h"
#include "ExperimentParity/ExperimentMaterialCookSelfTest.h"
#include "ExperimentParity/ExperimentMaterialInstanceSelfTest.h"
#include "ExperimentParity/ExperimentMaterialSealSelfTest.h"
#include "ExperimentParity/ExperimentMaterialCodecSelfTest.h"
#include "ExperimentParity/ExperimentSceneCookSelfTest.h"
#include "ExperimentParity/ExperimentResolverSelfTest.h"
#include "ExperimentParity/ExperimentCatalogSelfTest.h"
#include "RHI/ScreenSizedResource.h"
#include "ReflectionYml.h"
#include "ReflectionUndo.h"
#include "GameObjectCommand.h"
#include "StringHelper.h"
#include "BlackBoard.h"
#include "TagManager.h"
#include <Windows.h>
#include <psapi.h> // MBC11: assets.modelbench peak working set
#include <crtdbg.h>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <limits>
#include <random>
#include <stdexcept>
#include <charconv>
#include <cmath>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <DbgHelp.h>
#include <DXProgrammableCapture.h>
#include <chrono>
#include <dxgidebug.h>
#include <wrl/client.h>
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <fstream>
#include <functional>
#include "../../Engine/SceneRuntime/MeshRenderer.h"
#include "../../Engine/RenderEngine/Material.h"
#include <unordered_set>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <charconv>
#include <cmath>

namespace ConsoleCmd
{
    static CommandCore::CommandResult Cmd_scene_load(const ConsoleCommandContext& ctx)
    {
        const std::vector<std::string>& parts = ctx.parts;
        const std::string& cmd = ctx.cmd;

        if (parts.size() < 2)
        {
            std::printf("[CLI] 사용법: %s <씬 경로>\n", cmd.c_str());
            return CommandCore::InvalidArguments(
                cmd + ": 씬 경로가 없다", "scene.path_missing");
        }

        // scene.load  : 씬을 열기만 한다(기존 씬 유지)
        // scene.switch: 씬을 열고 활성 씬으로 교체한다(기존 씬 파괴 → 언로드 유발)
        //
        // ★ 단계마다 즉시 찍는다.
        //
        //   씬 교체가 멈추는 것을 쫓다가 출력이 0바이트인 실행을 만났다.
        //   프로세스를 죽여도 아무것도 안 남아 어디까지 갔는지조차 알 수
        //   없었다. 함수가 끝나야 찍히는 로그는 멈춘 자리를 못 알려 준다 —
        //   dx12.compare 크래시 때와 같은 자리다(그때도 outLog가 함수 끝에
        //   가서야 쓰여서 세 번을 헛짚었다).
        std::printf("[CLI] %s 시작: %s\n", cmd.c_str(), parts[1].c_str());

        Scene* scene = SceneManagers->LoadScene(parts[1]);
        std::printf("[CLI] LoadScene 반환: %s\n",
            (nullptr != scene) ? "성공" : "널");

        if (!scene)
        {
            Debug->LogError("[CLI] 씬 로드 실패: " + parts[1]);
            std::printf("[CLI] 씬 로드 실패: %s\n", parts[1].c_str());

            // 선행조건 불충족이지 명령의 결함이 아니다 — 부를 수는 있으나
            // 그 경로에 씬이 없다. §5.4 의 exit 3 이고 서비스에서는 409 다.
            return CommandCore::PreconditionFailed(
                "scene.not_found", cmd + ": 씬을 열 수 없다: " + parts[1]);
        }

        if (cmd == "scene.switch")
        {
            std::printf("[CLI] ActivateScene 진입\n");
            SceneManagers->ActivateScene(scene, true);
            std::printf("[CLI] ActivateScene 반환\n");
        }
        std::printf("[CLI] %s 완료: %s\n", cmd.c_str(), parts[1].c_str());

        CommandCore::CommandData data = CommandCore::CommandData::Object();
        data.Set("path",     CommandCore::CommandData::String(parts[1]));
        data.Set("activated", CommandCore::CommandData::Bool(cmd == "scene.switch"));
        return CommandCore::Ok(cmd + " 완료", std::move(data));
    }

    // DontDestroyOnLoad 지정 — 씬 이송 경로를 시나리오에서 태우기 위한 진단 명령.
    //
    // 이 경로(Scene::DetachEntityHierarchy / AttachExistingEntity*)는
    // SceneManager의 씬 로드 안에서만 불려, 지금까지 회귀 세트가 **단 한 번도
    // 태운 적이 없다.** 그래서 E5-R2(캔버스 캐시 핸들화)는 델타를 잴 자를 못
    // 만들었고, L3의 잔여(이송 신호를 C#까지 전달)도 검증 수단이 없었다.
    // 이 명령이 그 둘의 공통 선행이다.

    static CommandCore::CommandResult Cmd_scene_ddol(const ConsoleCommandContext& ctx)
    {
        Scene* scene = SceneManagers->GetActiveScene();
        if (!scene)
        {
            std::printf("[CLI] 활성 씬 없음\n");
            return CommandCore::PreconditionFailed("scene.none", "활성 씬이 없다");
        }

        if (ctx.parts.size() < 2)
        {
            std::printf("[CLI] 사용법: scene.ddol <오브젝트이름>\n");
            return CommandCore::InvalidArguments("scene.ddol: <오브젝트이름> 이 필요하다");
        }

        const std::string name = CommandCore::JoinFrom(ctx.parts, 1);
        auto obj = scene->GetEntity(name);
        if (!obj)
        {
            std::printf("[CLI] scene.ddol 대상 없음: %s\n", name.c_str());
            return CommandCore::Fail("scene.ddol.not_found",
                "대상 오브젝트가 없다: " + name);
        }

		Object::SetDontDestroyOnLoad(obj);
        std::printf("[CLI] scene.ddol 지정: %s (DDOL=%d)\n",
            name.c_str(), obj->IsDontDestroyOnLoad() ? 1 : 0);

        CommandCore::CommandData data = CommandCore::CommandData::Object();
        data.Set("name", CommandCore::CommandData::String(name));
        data.Set("dontDestroyOnLoad", CommandCore::CommandData::Bool(
            obj->IsDontDestroyOnLoad()));
        return CommandCore::Ok("scene.ddol 지정: " + name, std::move(data));
    }

    static CommandCore::CommandResult Cmd_ai_status(const ConsoleCommandContext& ctx)
    {
        using namespace CommandCore;
        if (ctx.parts.size() > 2) return InvalidArguments("ai.status [object]");
        auto data = CommandData::Object();
        data.Set("total", CommandData::Int(AIManagers->GetRegisteredAIComponentCount()));
        if (ctx.parts.size() == 2)
        {
            EntityHandle target;
            auto result = EditorObjectOperations::ResolveTarget(ctx.parts[1], target);
            if (!result.IsSuccess()) return result;
            auto* scene = SceneManagers->GetActiveScene();
            auto* object = scene ? scene->Resolve(target) : nullptr;
            auto* component = object ? object->GetComponent<StateMachineComponent>() : nullptr;
            data.Set("id", CommandData::String(EditorObjectOperations::ObjectId(target)));
            data.Set("registered", CommandData::Bool(AIManagers->IsAIComponentRegistered(component)));
            data.Set("hasComponent", CommandData::Bool(component != nullptr));
        }
        return Ok({}, std::move(data));
    }

    static CommandCore::CommandResult Cmd_scene_new(const ConsoleCommandContext& ctx)
    {
        const std::vector<std::string>& parts = ctx.parts;
        const std::string& cmd = ctx.cmd;

        // 빈 씬에서 시작한다. 기능별 테스트 씬은 '무엇이 들어 있는지'를 전부
        // 알아야 결과를 판정할 수 있는데, 열려 있던 씬 위에 쌓으면 그게 깨진다.
        const std::string name = (parts.size() > 1)
            ? CommandCore::JoinFrom(parts, 1) : std::string("FeatureTest");

        // SceneManager::CreateScene을 쓰지 않는다. 그쪽은 옛 씬을 그 자리에서
        // 해체하고 새 씬을 활성으로 바꾸는데, 그 '그 자리'가 프레임 중간이라
        // 커맨드를 만들고 있던 렌더 워커의 발밑에서 자료구조가 사라진다.
        // 실측으로 ShadowMapPass::CreateCommandListCascadeShadow →
        // DX11CommandContext::UpdateBuffer에서 죽었다.
        //
        // scene.switch가 쓰는 경로를 그대로 쓴다: 씬만 만들어 두고 교체는
        // ActivateScene에 맡긴다 — 그쪽은 BeforeAwakeSceneLoad(프레임의 안전
        // 지점)까지 미룬다.
        Scene* scene = Scene::CreateNewScene(name);
        if (!scene)
        {
            Debug->LogError("[CLI] 씬 생성 실패: " + name);
            std::printf("[CLI] 씬 생성 실패: %s\n", name.c_str());
            return CommandCore::Fail("scene.create_failed", "씬을 만들지 못했다: " + name);
        }

        SceneManagers->ActivateScene(scene, true);

        Debug->LogWarning("[CLI] 새 씬: " + name);
        std::printf("[CLI] 새 씬: %s\n", name.c_str());

        CommandCore::CommandData data = CommandCore::CommandData::Object();
        data.Set("name", CommandCore::CommandData::String(name));
        return CommandCore::Ok("새 씬: " + name, std::move(data));
    }

    static CommandCore::CommandResult Cmd_scene_save(const ConsoleCommandContext& ctx)
    {
        const std::vector<std::string>& parts = ctx.parts;
        const std::string& cmd = ctx.cmd;

        if (parts.size() < 2)
        {
            std::printf("[CLI] 사용법: scene.save <저장 경로>\n");
            return CommandCore::InvalidArguments("scene.save: <저장 경로> 가 필요하다");
        }

        // 경로에 공백이 들어갈 수 있으므로 명령어 뒤 전체를 경로로 본다.
        const std::string path = CommandCore::JoinFrom(parts, 1);

        if (!SceneManagers->GetActiveScene())
        {
            std::printf("[CLI] 활성 씬 없음\n");
            return CommandCore::PreconditionFailed("scene.none", "활성 씬이 없다");
        }

        // 상위 디렉터리를 만들어 준다. 없으면 ofstream이 조용히 실패하고
        // '저장했다'는 메시지만 남는다.
        std::error_code directoryError{};
        file::create_directories(file::path(path).parent_path(), directoryError);

        SceneManagers->SaveScene(path);

        // 실제로 파일이 생겼는지 확인한다 — SaveScene은 실패를 돌려주지 않는다.
        if (!file::exists(path))
        {
            Debug->LogError("[CLI] 씬 저장 실패: " + path);
            std::printf("[CLI] 씬 저장 실패: %s\n", path.c_str());
            // ★ `SaveScene` 은 실패를 돌려주지 않는다. 파일 존재로 확인한 이
            //   판정이 이제 종료 코드에 닿는다 — 예전에는 "저장 실패" 를 찍고도
            //   프로세스가 0 으로 끝났다.
            return CommandCore::Fail("scene.save_failed", "씬을 저장하지 못했다: " + path);
        }

        const auto bytes = file::file_size(path);
        Debug->LogWarning("[CLI] 씬 저장: " + path);
        std::printf("[CLI] 씬 저장: %s (%llu 바이트)\n", path.c_str(),
            static_cast<unsigned long long>(bytes));

        CommandCore::CommandData data = CommandCore::CommandData::Object();
        data.Set("path", CommandCore::CommandData::String(path));
        data.Set("bytes", CommandCore::CommandData::Int(static_cast<int64_t>(bytes)));
        return CommandCore::Ok("씬 저장: " + path, std::move(data));
    }

    static CommandCore::CommandResult Cmd_object_properties(const ConsoleCommandContext& ctx)
    {
        if (ctx.parts.size() != 3) return CommandCore::InvalidArguments("object.properties <target> <component>");
        EntityHandle target;
        auto result = EditorObjectOperations::ResolveTarget(ctx.parts[1], target);
        return result.IsSuccess() ? EditorObjectOperations::Properties(target, ctx.parts[2]) : result;
    }

    static CommandCore::CommandResult Cmd_object_delete(const ConsoleCommandContext& ctx)
    {
        if (ctx.parts.size() != 2) return CommandCore::InvalidArguments("object.delete <target>");
        EntityHandle target;
        auto resolved = EditorObjectOperations::ResolveTarget(ctx.parts[1], target);
        if (!resolved.IsSuccess()) return resolved;
        return EditorObjectOperations::Delete(target);
    }

    static CommandCore::CommandResult Cmd_component_remove(const ConsoleCommandContext& ctx)
    {
        if (ctx.parts.size() != 3) return CommandCore::InvalidArguments("component.remove <target> <component>");
        EntityHandle target;
        auto resolved = EditorObjectOperations::ResolveTarget(ctx.parts[1], target);
        if (!resolved.IsSuccess()) return resolved;
        return EditorObjectOperations::RemoveComponent(target, ctx.parts[2]);
    }

    static CommandCore::CommandResult Cmd_object_create(const ConsoleCommandContext& ctx)
    {
        if (ctx.parts.size() < 2 || ctx.parts.size() > 3) return CommandCore::InvalidArguments("object.create <name> [type]");
        GameObjectType type = GameObjectType::Empty;
        if (ctx.parts.size() == 3)
        {
            const auto& name = ctx.parts[2];
            if (name == "Light") type = GameObjectType::Light;
            else if (name == "Camera") type = GameObjectType::Camera;
            else if (name == "Mesh") type = GameObjectType::Mesh;
            else if (name == "UI") type = GameObjectType::UI;
            else if (name == "Canvas") type = GameObjectType::Canvas;
            else if (name != "Empty") return CommandCore::InvalidArguments("Unknown object type");
        }
        return EditorObjectOperations::Create(SceneManagers->GetActiveScene(), ctx.parts[1], type);
    }

    static CommandCore::CommandResult Cmd_object_rename(const ConsoleCommandContext& ctx)
    {
        if (ctx.parts.size() != 3)
            return CommandCore::InvalidArguments("object.rename <name-or-id> <new-name>; quote names with spaces");
        EntityHandle target;
        auto result = EditorObjectOperations::ResolveTarget(ctx.parts[1], target);
        if (result.status != CommandCore::CommandStatus::Succeeded) return result;
        return EditorObjectOperations::Rename(target, ctx.parts[2]);
    }

    static CommandCore::CommandResult Cmd_object_describe(const ConsoleCommandContext& ctx)
    {
        if (ctx.parts.size() != 2) return CommandCore::InvalidArguments("object.describe <name-or-id>");
        EntityHandle target;
        auto result = EditorObjectOperations::ResolveTarget(ctx.parts[1], target);
        if (result.status != CommandCore::CommandStatus::Succeeded) return result;
        return EditorObjectOperations::Describe(target);
    }

    static CommandCore::CommandResult Cmd_scene_hierarchycheck(const ConsoleCommandContext& ctx)
    {
        if (ctx.parts.size() != 1) return CommandCore::InvalidArguments("scene.hierarchycheck accepts no arguments");
        return EditorDiagnostics::ValidateHierarchy(SceneManagers->GetActiveScene());
    }

    static CommandCore::CommandResult Cmd_object_duplicate(const ConsoleCommandContext& ctx)
    {
        if (ctx.parts.size() < 2 || ctx.parts.size() > 3) return CommandCore::InvalidArguments("object.duplicate <target> [name]");
        EntityHandle target;
        auto resolved = EditorObjectOperations::ResolveTarget(ctx.parts[1], target);
        if (!resolved.IsSuccess()) return resolved;
        return EditorObjectOperations::Duplicate(target, ctx.parts.size() == 3 ? ctx.parts[2] : "");
    }

    static CommandCore::CommandResult Cmd_object_parent(const ConsoleCommandContext& ctx)
    {
        if (ctx.parts.size() != 3) return CommandCore::InvalidArguments("object.parent <target> <parent|->");
        EntityHandle target;
        auto resolved = EditorObjectOperations::ResolveTarget(ctx.parts[1], target);
        if (!resolved.IsSuccess()) return resolved;
        EntityHandle parent;
        if (ctx.parts[2] == "-") parent = SceneManagers->GetActiveScene()->HandleOf(0);
        else { auto result = EditorObjectOperations::ResolveTarget(ctx.parts[2], parent); if (!result.IsSuccess()) return result; }
        return EditorObjectOperations::Parent(target, parent);
    }

    // H2 root-reference 회귀용. m_rootIndex는 일반 parent와 별개의 same-scene
    // 참조라서 DDOL/Prefab 슬롯 재배정 때 따로 remap해야 한다. 모델 자산 없이
    // 그 경계를 태울 수 있도록 설정과 조회를 한 명령에 둔다.

    static CommandCore::CommandResult Cmd_object_rootref(const ConsoleCommandContext& ctx)
    {
        if (ctx.parts.size() < 2)
        {
            std::printf("[CLI] 사용법: object.rootref <오브젝트> [루트오브젝트 | -]\n");
            return CommandCore::InvalidArguments("object.rootref: 인자가 올바르지 않다");
        }

        Scene* scene = SceneManagers->GetActiveScene();
        if (!scene)
        {
            std::printf("[CLI] 활성 씬 없음\n");
            return CommandCore::PreconditionFailed("object.rootref.no_scene", "활성 씬이 없다");
        }

        Entity* object = scene->GetEntity(ctx.parts[1]);
        if (!object)
        {
            std::printf("[CLI] rootref 대상 없음: %s\n", ctx.parts[1].c_str());
            return CommandCore::Fail("object.rootref.target_not_found", "rootref 대상이 없다");
        }

        if (ctx.parts.size() >= 3)
        {
            if (ctx.parts[2] == "-")
            {
                object->SetRootIndex(Entity::INVALID_INDEX);
            }
            else
            {
                Entity* root = scene->GetEntity(ctx.parts[2]);
                if (!root)
                {
                    std::printf("[CLI] rootref 루트 없음: %s\n", ctx.parts[2].c_str());
                    return CommandCore::Fail("object.rootref.root_not_found", "rootref 루트가 없다");
                }
                object->SetRootIndex(root->m_index);
            }
        }

        const Entity::Index rootIndex = object->GetRootIndex();
        Entity* root = scene->TryGetEntity(rootIndex);
        std::printf("[object.rootref] %s root=%d name=%s\n",
            object->GetHashedName().ToString().c_str(), static_cast<int>(rootIndex),
            root ? root->GetHashedName().ToString().c_str() : "<invalid>");

        // ★ root 가 무효인 것은 **실패가 아니다.** 최상위 오브젝트는 루트 참조가
        //   없는 것이 정상이고, 이 명령은 그 상태를 답하는 조회다. 값으로 낸다.
        CommandCore::CommandData data = CommandCore::CommandData::Object();
        data.Set("object", CommandCore::CommandData::String(object->GetHashedName().ToString()));
        data.Set("rootIndex", CommandCore::CommandData::Int(static_cast<int64_t>(rootIndex)));
        data.Set("rootValid", CommandCore::CommandData::Bool(nullptr != root));
        return CommandCore::Ok("object.rootref", std::move(data));
    }

    static CommandCore::CommandResult Cmd_object_transform(const ConsoleCommandContext& ctx)
    {
        if (ctx.parts.size() != 5 && ctx.parts.size() != 8 && ctx.parts.size() != 11)
            return CommandCore::InvalidArguments("object.transform <target> px py pz [rx ry rz] [sx sy sz]");
        EntityHandle target;
        auto resolved = EditorObjectOperations::ResolveTarget(ctx.parts[1], target);
        if (!resolved.IsSuccess()) return resolved;
        float values[]{0,0,0,0,0,0,1,1,1};
        for (size_t i = 2; i < ctx.parts.size(); ++i)
        {
            const auto& raw = ctx.parts[i];
            auto parsed = std::from_chars(raw.data(), raw.data()+raw.size(), values[i-2]);
            if (parsed.ec != std::errc{} || parsed.ptr != raw.data()+raw.size() || !std::isfinite(values[i-2]))
                return CommandCore::InvalidArguments("Transform requires finite numbers", "transform.number_invalid");
        }
        return EditorObjectOperations::Transform(target, {values[0],values[1],values[2]},
            math::quaternion_from_pitch_yaw_roll(math::radians(values[3]), math::radians(values[4]), math::radians(values[5])),
            {values[6],values[7],values[8]});
    }

    static CommandCore::CommandResult Cmd_object_property(const ConsoleCommandContext& ctx)
    {
        if (ctx.parts.size() < 5) return CommandCore::InvalidArguments("object.property <target> <component> <field> <value>");
        EntityHandle target;
        auto resolved = EditorObjectOperations::ResolveTarget(ctx.parts[1], target);
        if (!resolved.IsSuccess()) return resolved;
        return EditorObjectOperations::Property(target, ctx.parts[2], ctx.parts[3], CommandCore::JoinFrom(ctx.parts, 4));
    }

    static CommandCore::CommandResult Cmd_scene_select(const ConsoleCommandContext& ctx)
    {
        if (ctx.parts.size() != 2) return CommandCore::InvalidArguments("scene.select <target|->");
        if (ctx.parts[1] == "-") return EditorObjectOperations::Select(SceneManagers->GetActiveScene(), {});
        EntityHandle target;
        auto resolved = EditorObjectOperations::ResolveTarget(ctx.parts[1], target);
        if (!resolved.IsSuccess()) return resolved;
        return EditorObjectOperations::Select(SceneManagers->GetActiveScene(), {target});
    }

    static CommandCore::CommandResult Cmd_component_add(const ConsoleCommandContext& ctx)
    {
        if (ctx.parts.size() != 3) return CommandCore::InvalidArguments("component.add <target> <type>");
        EntityHandle target;
        auto resolved = EditorObjectOperations::ResolveTarget(ctx.parts[1], target);
        if (!resolved.IsSuccess()) return resolved;
        return EditorObjectOperations::AddComponent(target, ctx.parts[2]);
    }

    static CommandCore::CommandResult Cmd_prefab_instantiate(const ConsoleCommandContext& ctx)
    {
        if (ctx.parts.size() < 2 || ctx.parts.size() > 3) return CommandCore::InvalidArguments("prefab.instantiate <prefab> [name]");
        return EditorObjectOperations::InstantiatePrefab(ctx.parts[1], ctx.parts.size() == 3 ? ctx.parts[2] : "");
    }

    static CommandCore::CommandResult Cmd_prefab_overrides(const ConsoleCommandContext& ctx)
    {
        const std::vector<std::string>& parts = ctx.parts;
        const std::string& cmd = ctx.cmd;

        // prefab.overrides <오브젝트>
        //
        // 프리팹 인스턴스에 기록된 로컬 수정(m_prefabOverrides)을 그대로 덤프한다.
        //
        // ── 이 명령이 왜 먼저 서는가 (P-write S0) ──
        //
        // 지금 이 목록은 **항상 비어 있다.** 채우는 지점이 SeedOverridesFromSnapshot
        // 하나뿐이고, 그 기준인 m_prefabOriginal이 reflect()에 없어 비직렬화라
        // 씬을 다시 열면 근거가 사라진다(PrefabUtility.cpp:62-67의 자백 참고).
        // 그래서 프리팹을 갱신하면 인스턴스의 로컬 수정이 에러도 로그도 없이 덮인다.
        //
        // 기록 배선을 넣기 **전에** 이 명령을 먼저 세우는 이유는, 그래야 게이트가
        // "0이던 것이 1이 됐다"는 전환 자체를 증명할 수 있기 때문이다. 배선을 먼저
        // 넣으면 게이트가 처음부터 통과해, 그 게이트가 무엇을 재는지 알 수 없다.
        if (parts.size() < 2)
        {
            std::printf("[CLI] 사용법: prefab.overrides <오브젝트>\n");
            return CommandCore::InvalidArguments("prefab.overrides: 인자가 올바르지 않다");
        }

        Scene* scene = SceneManagers->GetActiveScene();
        if (!scene)
        {
            std::printf("[CLI] 활성 씬 없음\n");
            return CommandCore::PreconditionFailed("prefab.overrides.no_scene", "활성 씬이 없다");
        }

        const std::string objectName = CommandCore::JoinFrom(parts, 1);
        auto object = scene->GetEntity(objectName);
        if (!object)
        {
            Debug->LogError("[CLI] 오브젝트를 찾을 수 없음: " + objectName);
            std::printf("[CLI] 오브젝트를 찾을 수 없음: %s\n", objectName.c_str());
            return CommandCore::Fail("prefab.overrides.not_found", "오브젝트가 없다");
        }

        static const FileGuid nullGuid{};
        const bool isInstance = (object->m_prefabFileGuid != nullGuid);

        std::printf("[prefab.overrides] %s · 인스턴스=%s · 기록 %zu건\n",
            objectName.c_str(),
            isInstance ? "예" : "아니오",
            object->m_prefabOverrides.size());

        for (const auto& ov : object->m_prefabOverrides)
        {
            // 컴포넌트 타입이 비면 Entity 자신의 프로퍼티다.
            // 순번(-1 = 타입 전체, 순번 필드가 없던 시절 데이터)도 함께 찍는다 —
            // 같은 타입 컴포넌트가 여럿일 때 어느 것에 걸린 기록인지 눈으로 갈라야 한다.
            char slot[16]{};
            std::snprintf(slot, sizeof(slot), "#%d", ov.m_componentSlot);
            std::printf("[prefab.overrides]   %s%s.%s = %s\n",
                ov.m_componentType.empty() ? "(Entity)" : ov.m_componentType.c_str(),
                ov.m_componentType.empty() ? "" : slot,
                ov.m_propertyName.c_str(),
                ov.m_valueYaml.c_str());
        }

        // override 가 0 개인 것은 **실패가 아니다.** 프리팹 인스턴스가 원본과
        // 같다는 뜻이고, 이 명령은 그 상태를 답하는 조회다.
        CommandCore::CommandData data = CommandCore::CommandData::Object();
        data.Set("object", CommandCore::CommandData::String(objectName));
        return CommandCore::Ok("prefab.overrides", std::move(data));
    }

    static CommandCore::CommandResult Cmd_prefab_update(const ConsoleCommandContext& ctx)
    {
        const std::vector<std::string>& parts = ctx.parts;
        const std::string& cmd = ctx.cmd;

        // prefab.update <소스 오브젝트> <프리팹 이름>
        //
        // 소스 오브젝트의 현재 형상으로 프리팹 정의를 다시 뜨고, 그 정의를
        // 등록된 인스턴스 전체에 적용한다(PrefabUtility::UpdateInstances).
        //
        // ★ 불변식: UpdateInstances에는 **반드시 CreatePrefab의 산출물만** 넘긴다.
        // LoadPrefab이 돌려주는 노드를 직접 넘기면 형상이 Sequence로 와서
        // UpdateInstances가 Map으로 전제하고 읽는 자리와 어긋난다. 지금까지 이
        // 경로는 PrefabEditor::Close(CreatePrefab 산출물만 넘긴다)에서만 도달했고,
        // 이 명령이 CLI에서 처음으로 도달 가능하게 만든다 — 그래서 여기에 못박는다.
        if (parts.size() < 3)
        {
            std::printf("[CLI] 사용법: prefab.update <소스 오브젝트> <프리팹 이름>\n");
            return CommandCore::InvalidArguments("prefab.update: 인자가 올바르지 않다");
        }

        Scene* scene = SceneManagers->GetActiveScene();
        if (!scene)
        {
            std::printf("[CLI] 활성 씬 없음\n");
            return CommandCore::PreconditionFailed("prefab.update.no_scene", "활성 씬이 없다");
        }

        // 이름 규칙은 prefab.create/object.rename과 같다 — 마지막 토큰이 프리팹
        // 이름이고 그 앞 전체가 오브젝트 이름이다(엔진이 공백 있는 이름을 만든다).
        const auto names = CommandCore::SplitTrailingName(parts, 1);
        const std::string& objectName = names.leading;
        const std::string& prefabName = names.trailing;

        auto source = scene->GetEntity(objectName);
        if (!source)
        {
            Debug->LogError("[CLI] 소스 오브젝트를 찾을 수 없음: " + objectName);
            std::printf("[CLI] 소스 오브젝트를 찾을 수 없음: %s\n", objectName.c_str());
            return CommandCore::Fail("prefab.update.source_not_found", "소스 오브젝트가 없다");
        }

        // ★ 정체성 승계. CreatePrefab 산출물은 FileGuid가 널이고(Prefab.cpp:25),
        // UpdateInstances는 m_instanceMap[prefab->GetFileGuid()]로 대상을 찾는다
        // (PrefabUtility.cpp:378). 승계하지 않으면 널 키로 조회해 **아무 인스턴스도
        // 찾지 못하고 조용히 0건 적용**된다 — 게이트가 아무것도 재지 못하는 모습으로
        // 나타난다. PrefabEditor::Close(:43)가 같은 이유로 같은 일을 한다.
        Prefab* existing = PrefabUtilitys->LoadPrefab(prefabName);
        if (!existing)
        {
            std::printf("[CLI] 기존 프리팹을 찾을 수 없음(먼저 prefab.create): %s\n", prefabName.c_str());
            return CommandCore::Fail("prefab.update.prefab_not_found", "프리팹이 없다");
        }
        const FileGuid identity = existing->GetFileGuid();

        // ★ 널 identity로는 진행하지 않는다 (2026-08-30).
        //
        // 그대로 두면 SavePrefab이 널을 보고 CreateRandomV4()로 **새 GUID를
        // 발급**하고, 아래 UpdateInstances가 그 새 키로 m_instanceMap을 조회해
        // 아무 인스턴스도 못 찾은 채 조용히 0건 적용한다 — 에러도 로그도 없이
        // 인스턴스만 옛 값으로 남고, 게다가 sidecar가 새 GUID로 덮여 살아 있는
        // 인스턴스 전부가 프리팹에서 영구히 떨어져 나간다. 되돌릴 수 없는 손상을
        // 조용히 저지르느니 여기서 멈추는 편이 낫다.
        //
        // 여기 도달하는 유일한 길은 catalog가 그 순간 항목을 잃는 것이었고, 그
        // 근본(워처의 게시 Delete 오독)과 LoadPrefab의 무조건 덮어쓰기를 함께
        // 고쳤다. 이 가드는 세 번째 방어선이다 — 어느 경로로 널이 오든 막는다.
        if (identity == nullFileGuid)
        {
            Debug->LogError("[CLI] 프리팹 identity를 확인할 수 없어 갱신을 중단한다: " + prefabName);
            std::printf("[CLI] 프리팹 identity 없음(catalog 항목 부재) — 갱신 중단: %s\n",
                prefabName.c_str());
            return CommandCore::Fail("prefab.update.identity_missing",
                "프리팹 identity 를 확인할 수 없다(catalog 항목 부재)");
        }

		Prefab* prefab = PrefabUtilitys->CreatePrefab(source, prefabName);
        if (!prefab)
        {
            std::printf("[CLI] 프리팹 정의 생성 실패: %s\n", prefabName.c_str());
            return CommandCore::Fail("prefab.update.define_failed", "프리팹 정의를 만들지 못했다");
        }
        prefab->SetFileGuid(identity);

        // ★ 정의를 영속화한다 (P4-b에서 추가). 이 명령은 원래 인스턴스만 갱신하고
        // **정의는 메모리의 임시 객체에만** 두었다 — 디스크도 캐시도 그대로였다.
        // 그래서 이후 그 프리팹을 다시 로드하면 갱신 전 정의가 돌아왔다.
        //
        // P4-b 이전에는 이것이 드러나지 않았다. 아무도 정의를 다시 읽지 않았기
        // 때문이다(인스턴스는 UpdateInstances가 이미 고쳐 놓았다). 중첩 참조 노드가
        // **소환 시점에 정의를 다시 읽게** 되면서 비로소 보였다 — 잠재해 있던
        // 결함을 P4-b가 드러낸 것이지 새로 만든 것이 아니다.
        //
        // 경로 규칙은 prefab.create와 같아야 한다(같은 파일을 가리켜야 하므로).
        const file::path savePath = PathFinder::Relative("Prefabs\\") / (prefabName + ".prefab");
        if (!PrefabUtilitys->SavePrefab(prefab, savePath.string()))
        {
            std::printf("[CLI] 프리팹 정의 저장 실패: %s\n", savePath.string().c_str());
            return CommandCore::Fail("prefab.update.save_failed", "프리팹 정의를 저장하지 못했다");
        }

        // ★ 등록 수와 **실제 적용 수**를 따로 찍는다 (2026-08-30).
        //
        // 예전에는 등록 수 하나만 찍었다 — 그런데 UpdateInstances는
        // m_instanceMap[prefab->GetFileGuid()]로 대상을 찾으므로, identity가
        // 어긋나면 등록부에 인스턴스가 멀쩡히 있어도 0건이 적용된다. 그때도
        // 로그는 "등록 인스턴스 2개에 적용"이라고 말해, 게이트가 읽을 수 있는
        // 유일한 창이 결함을 **정확히 가렸다**. 둘이 벌어지면 그게 신호다.
        const size_t registered = PrefabUtilitys->RegisteredInstanceCount();
        const size_t applied = PrefabUtilitys->UpdateInstances(prefab);

        Debug->LogWarning("[CLI] 프리팹 갱신 적용: " + prefabName + " <- " + objectName);
        std::printf("[prefab.update] %s <- %s · 등록 인스턴스 %zu개 중 %zu개에 적용\n",
            prefabName.c_str(), objectName.c_str(), registered, applied);

        CommandCore::CommandData data = CommandCore::CommandData::Object();
        data.Set("prefab", CommandCore::CommandData::String(prefabName));
        data.Set("source", CommandCore::CommandData::String(objectName));
        data.Set("registered", CommandCore::CommandData::Int(static_cast<int64_t>(registered)));
        data.Set("applied", CommandCore::CommandData::Int(static_cast<int64_t>(applied)));
        return CommandCore::Ok("프리팹 갱신: " + prefabName, std::move(data));
    }

    static CommandCore::CommandResult Cmd_prefab_status(const ConsoleCommandContext& ctx)
    {
        // 프리팹 연결 진단(트랙 P).
        //
        // 왜 필요한가: 인스턴스가 프리팹과의 연결을 잃어도 화면은 그대로다. 연결은
        // 다음에 프리팹을 고쳐서 반영이 안 될 때에야 드러나므로, 밖에서 볼 수 있는
        // 창이 없으면 왕복 회귀를 판정할 수 없다. bt.status가 BT에 대해 하는 일을
        // 프리팹에 대해 한다.
        //
        // 두 수를 따로 세는 것이 요점이다.
        //   씬 인스턴스 — 오브젝트가 든 m_prefabFileGuid. 직렬화되므로 왕복을 건넌다.
        //   등록        — PrefabUtility의 인스턴스 목록. 메모리에만 있어 왕복에서 끊긴다.
        // 둘이 벌어지면 "저장은 됐는데 연결은 복원되지 않았다"는 뜻이다.
        int sceneInstances = 0;
        if (Scene* scene = SceneManagers->GetActiveScene())
        {
            for (const auto& obj : scene->m_Entities)
            {
                if (obj && obj->m_prefabFileGuid != nullFileGuid)
                {
                    ++sceneInstances;
                }
            }
        }

        char line[256]{};
        std::snprintf(line, sizeof(line),
            "[prefab.status] 씬 인스턴스 %d개 · 등록 %zu개 · 캐시 %zu개",
            sceneInstances,
            PrefabUtilitys->RegisteredInstanceCount(),
            PrefabUtilitys->OwnedPrefabCount());
        std::printf("[CLI] %s\n", line);
        Debug->LogWarning(line);

        CommandCore::CommandData data = CommandCore::CommandData::Object();
        data.Set("owned", CommandCore::CommandData::Int(
            static_cast<int64_t>(PrefabUtilitys->OwnedPrefabCount())));
        data.Set("registered", CommandCore::CommandData::Int(
            static_cast<int64_t>(PrefabUtilitys->RegisteredInstanceCount())));
        return CommandCore::Ok("prefab.status", std::move(data));
    }

	static CommandCore::CommandResult Cmd_prefab_corpus_digest(const ConsoleCommandContext& ctx)
	{
		// D2-d: 호출자가 지정한 9개 프리팹의 인스턴스 루트 identity와 활성 씬의
		// 전체 prefab identity/override multiset을 저장 전후 비교 가능한 digest로
		// 출력한다. instanceID나 scene index처럼 재로드에서 바뀔 수 있는 값은 섞지
		// 않는다.
		if (ctx.parts.size() < 3)
		{
			std::printf("[CLI] 사용법: prefab.corpus.digest <라벨> <프리팹 이름>...\n");
			return CommandCore::InvalidArguments("prefab.corpus.digest: 인자가 올바르지 않다");
		}

		Scene* scene = SceneManagers->GetActiveScene();
		if (!scene)
		{
			std::printf("[CLI] 활성 씬 없음\n");
			return CommandCore::PreconditionFailed("prefab.corpus.digest.no_scene",
				"활성 씬이 없다");
		}

		const std::string& label = ctx.parts[1];
		const size_t expectedRoots = ctx.parts.size() - 2;
		size_t validRoots = 0;
		for (size_t index = 2; index < ctx.parts.size(); ++index)
		{
			const std::string& prefabName = ctx.parts[index];
			Prefab* prefab = PrefabUtilitys->LoadPrefab(prefabName);
			Entity* root = scene->GetEntity("D2Corpus_" + prefabName);
			if (prefab && root && prefab->GetFileGuid() != FileGuid{}
				&& root->m_prefabFileGuid == prefab->GetFileGuid())
			{
				++validRoots;
			}
		}

		std::vector<std::string> rows;
		size_t entityCount = 0;
		size_t overrideCount = 0;
		for (const auto& owner : scene->m_Entities)
		{
			Entity* entity = owner.get();
			if (!entity || entity->m_prefabFileGuid == FileGuid{}) continue;
			++entityCount;
			Entity* parent = scene->TryGetEntity(entity->GetParentIndex());
			const std::string parentName = parent
				? parent->m_name.ToString() : std::string("<none>");
			const std::string prefix = entity->m_name.ToString() + "|" + parentName
				+ "|" + entity->m_prefabFileGuid.ToString();
			rows.push_back("entity|" + prefix);
			for (const PrefabOverride& item : entity->m_prefabOverrides)
			{
				++overrideCount;
				rows.push_back("override|" + prefix + "|" + item.m_componentType
					+ "|" + std::to_string(item.m_componentSlot) + "|"
					+ item.m_propertyName + "|" + item.m_valueYaml);
			}
		}
		std::ranges::sort(rows);

		uint64_t digest = 1469598103934665603ull;
		for (const std::string& row : rows)
		{
			for (const unsigned char byte : row)
			{
				digest ^= byte;
				digest *= 1099511628211ull;
			}
			digest ^= static_cast<unsigned char>('\n');
			digest *= 1099511628211ull;
		}

		const size_t registered = PrefabUtilitys->RegisteredInstanceCount();
		const bool passed = validRoots == expectedRoots
			&& entityCount >= expectedRoots && registered >= expectedRoots;
		std::printf("[prefab.corpus:%s] %s roots=%zu/%zu entities=%zu "
			"overrides=%zu registered=%zu digest=%016llx\n",
			label.c_str(), passed ? "pass" : "fail", validRoots, expectedRoots,
			entityCount, overrideCount, registered,
			static_cast<unsigned long long>(digest));

		// ★ **exit code 를 직접 쓰던 마지막 자리다.** 이 명령은 `SetExitCode(6)` 를
		//   세 곳에서 불렀고, `cli_exit_spine.ratchet.json` 의 주석이 그것을
		//   "LC6 이 도메인 분리와 함께 옮긴다" 로 적어 두었다. 이제 session 이
		//   유일한 쓰기 주체다(§14.1) — 값은 6 이 아니라 §5.4 의 4 가 된다.
		//   소비자(`verify-prefab-authoring-corpus.ps1`)는 `0 -eq ExitCode` 만
		//   보므로 그 값 변화에 걸리지 않는다.
		CommandCore::CommandData data = CommandCore::CommandData::Object();
		data.Set("label", CommandCore::CommandData::String(label));
		data.Set("validRoots", CommandCore::CommandData::Int(static_cast<int64_t>(validRoots)));
		data.Set("expectedRoots", CommandCore::CommandData::Int(static_cast<int64_t>(expectedRoots)));
		data.Set("entities", CommandCore::CommandData::Int(static_cast<int64_t>(entityCount)));
		data.Set("overrides", CommandCore::CommandData::Int(static_cast<int64_t>(overrideCount)));
		data.Set("registered", CommandCore::CommandData::Int(static_cast<int64_t>(registered)));
		if (!passed)
		{
			return CommandCore::Fail("prefab.corpus.digest.failed",
				"프리팹 코퍼스 다이제스트 판정 실패", std::move(data));
		}
		return CommandCore::Ok("prefab.corpus.digest", std::move(data));
	}

    static CommandCore::CommandResult Cmd_camera_editor(const ConsoleCommandContext& ctx)
    {
        using namespace CommandCore;
        const auto& parts = ctx.parts;
        const std::string action = parts.size() >= 2 ? parts[1] : "status";
        if ((action != "status" && action != "match" && action != "follow") ||
            parts.size() > (action == "follow" ? 3u : 2u))
            return InvalidArguments("camera.editor match | follow [on|off] | status");
        if (action == "match" && !ConsoleCommandSystem::MatchEditorCameraToGameCamera())
            return PreconditionFailed("camera.unavailable", "No game camera to match");
        if (action == "follow")
        {
            const std::string mode = parts.size() == 3 ? parts[2] : "on";
            if (mode != "on" && mode != "off" && mode != "1" && mode != "0")
                return InvalidArguments("follow requires on or off");
            const bool following = mode == "on" || mode == "1";
            if (following && !ConsoleCommandSystem::MatchEditorCameraToGameCamera())
                return PreconditionFailed("camera.unavailable", "No game camera to follow");
            ConsoleCommandSystem::SetEditorCameraFollowing(following);
        }
        const auto vector = [](const auto& v) { auto a = CommandData::Array(); a.Append(CommandData::Double(v.x)); a.Append(CommandData::Double(v.y)); a.Append(CommandData::Double(v.z)); return a; };
        const auto describe = [&](const FrameCameraSnapshot& s, uint64_t id) {
            auto d = CommandData::Object();
            d.Set("viewId", CommandData::Int(id));
            d.Set("position", vector(s.eyePosition)); d.Set("forward", vector(s.forward));
            d.Set("fov", CommandData::Double(s.fov));
            return d;
        };
        auto data = CommandData::Object();
        data.Set("following", CommandData::Bool(ConsoleCommandSystem::IsEditorCameraFollowing()));
        const auto* editor = EditorSessionState::Get().EditorCamera();
        auto* scene = SceneManagers->GetActiveScene();
        const auto* game = scene ? scene->Cameras().GetPrimaryCamera() : nullptr;
        data.Set("editor", editor ? describe(editor->CaptureFrameSnapshot(), kEnhancedEditorViewId) : CommandData{});
        data.Set("game", game ? describe(game->CaptureFrameSnapshot(), game->GetInstanceID()) : CommandData{});
        return Ok({}, std::move(data));
    }

    static CommandCore::CommandResult Cmd_component_list(const ConsoleCommandContext& ctx)
    {
        using namespace CommandCore;
        if (ctx.parts.size() > 2) return InvalidArguments("component.list [filter]");
        const std::string filter = ctx.parts.size() == 2 ? ctx.parts[1] : "";
        std::vector<std::string> names;
        for (const auto& [name, type] : ComponentFactorys->m_componentTypes)
            if (filter.empty() || name.find(filter) != std::string::npos) names.push_back(name);
        std::sort(names.begin(), names.end());
        auto data = CommandData::Object();
        auto types = CommandData::Array();
        for (const auto& name : names) { types.Append(CommandData::String(name)); Debug->LogWarning("[CLI] " + name); }
        data.Set("types", std::move(types));
        data.Set("count", CommandData::Int(names.size()));
        return Ok({}, std::move(data));
    }

    static CommandCore::CommandResult Cmd_prefab_create(const ConsoleCommandContext& ctx)
    {
        const std::vector<std::string>& parts = ctx.parts;
        const std::string& cmd = ctx.cmd;

        if (parts.size() < 3)
        {
            std::printf("[CLI] 사용법: prefab.create <오브젝트 이름> <프리팹 이름>\n");
            return CommandCore::InvalidArguments("prefab.create: 인자가 올바르지 않다");
        }

        Scene* scene = SceneManagers->GetActiveScene();
        if (!scene)
        {
            std::printf("[CLI] 활성 씬 없음\n");
            return CommandCore::PreconditionFailed("prefab.create.no_scene", "활성 씬이 없다");
        }

        // 오브젝트 이름에 공백이 흔하므로 프리팹 이름을 마지막 토큰으로 본다.
        // LC2: 원문 재해석 제거.
        const auto names = CommandCore::SplitTrailingName(parts, 1);
        const std::string& objectName = names.leading;
        const std::string& prefabName = names.trailing;

        auto object = scene->GetEntity(objectName);
        if (!object)
        {
            std::printf("[CLI] 오브젝트를 찾을 수 없음: %s\n", objectName.c_str());
            return CommandCore::Fail("prefab.create.not_found", "오브젝트가 없다");
        }

		Prefab* prefab = PrefabUtilitys->CreatePrefab(object, prefabName);
        if (!prefab)
        {
            std::printf("[CLI] 프리팹 생성 실패\n");
            return CommandCore::Fail("prefab.create.create_failed", "프리팹을 만들지 못했다");
        }

        const file::path path = PathFinder::Relative("Prefabs\\") / (prefabName + ".prefab");
        if (!PrefabUtilitys->SavePrefab(prefab, path.string()))
        {
            std::printf("[CLI] 프리팹 저장 실패: %s\n", path.string().c_str());
            return CommandCore::Fail("prefab.create.save_failed", "프리팹을 저장하지 못했다");
        }

        Debug->LogWarning("[CLI] 프리팹 생성: " + prefabName + " <- " + objectName);
        std::printf("[CLI] 프리팹 생성: %s\n", path.string().c_str());

        CommandCore::CommandData data = CommandCore::CommandData::Object();
        data.Set("object", CommandCore::CommandData::String(objectName));
        data.Set("prefab", CommandCore::CommandData::String(prefabName));
        data.Set("path", CommandCore::CommandData::String(path.string()));
        return CommandCore::Ok("프리팹 생성: " + prefabName, std::move(data));
    }

    static CommandCore::CommandResult Cmd_play(const ConsoleCommandContext& ctx)
    {
        using namespace CommandCore;
        if (ctx.parts.size() != 1) return InvalidArguments("play | stop takes no arguments");
        if (!SceneManagers->GetActiveScene()) return PreconditionFailed("scene.not_found", "No active scene");
        SceneManagers->SetGameStart(ctx.cmd == "play");
        auto data = CommandData::Object();
        data.Set("requestedPlaying", CommandData::Bool(ctx.cmd == "play"));
        return Ok("Play state change requested; scene manager applies it at the next frame", std::move(data));
    }

    // 재생 상태를 관측한다. 재생 진입은 좌표를 바꾸지 않고 phase만 바꾸므로,
    // transform digest만으로는 "play가 무시됐는데 상태가 그대로라 통과"가 성립한다.
    // 왕복 게이트가 전이 자체를 확인할 수 있어야 그 뒤의 복원 단정이 의미를 갖는다.

    static CommandCore::CommandResult Cmd_play_state(const ConsoleCommandContext& ctx)
    {
        (void)ctx;
        const Scene* scene = SceneManagers->GetActiveScene();

        const bool        gameStart   = SceneManagers->IsGameStart();
        const bool        paused      = SceneManagers->IsGamePaused();
        const bool        editorReady = SceneManagers->IsEditorSceneLoaded();
        const bool        pending     = SceneManagers->HasPendingSceneStructureChange();
        const std::size_t entities    = scene ? scene->m_Entities.size() : 0u;

        std::printf("[play.state] gameStart=%d paused=%d editorSceneLoaded=%d "
            "pending=%d entities=%zu\n",
            gameStart ? 1 : 0, paused ? 1 : 0, editorReady ? 1 : 0,
            pending ? 1 : 0, entities);

        CommandCore::CommandData data = CommandCore::CommandData::Object();
        data.Set("gameStart",         CommandCore::CommandData::Bool(gameStart));
        data.Set("paused",            CommandCore::CommandData::Bool(paused));
        data.Set("editorSceneLoaded", CommandCore::CommandData::Bool(editorReady));
        data.Set("pending",           CommandCore::CommandData::Bool(pending));
        data.Set("entities",          CommandCore::CommandData::Int(static_cast<int64_t>(entities)));
        return CommandCore::Ok("play state", std::move(data));
    }

    // ── 파이프라인 구성 프로브 (E4 게이트용) ──
    //
    // E4는 Editor 렌더 패스를 RenderCore 밖으로 옮기고, 판정 기준은
    // "Grid/Gizmo는 Scene View에만 기여하고 Player pipeline에는 node 자체가 없다"다.
    // 그런데 패스가 **어느 뷰에 조립되는지**를 밖에서 볼 수단이 없었다. 패스 내부
    // 렌더링은 dx12.*/vk.* 자가 검사가 리드백으로 픽셀까지 재지만, 그것은 격리된
    // 합성 씬이라 조립 결과는 안 본다.
    //
    // LivePipelineDesc::Dump()는 이미 있고 디버그 스냅샷에도 실려 있었는데
    // 아무도 찍지 않았다. 한 줄씩 파싱 가능한 형태로 내보낸다.

    // ★ LC6: 값으로 낸다.
    //
    //   §9 의 동등성(GUI 와 서비스가 Undo·selection·play 상태를 같은 규약으로
    //   전이하는가)을 검사하려면 그 상태를 **밖에서 읽을 수 있어야** 한다.
    //   printf 만 있으면 게이트가 stdout 을 정규식으로 긁게 되고, 그것은 LC6 이
    //   RenderTest 56 개에서 없앤 바로 그 모양이다. printf 는 그대로 둔다 —
    //   기존 소비자가 읽고 있다.
    static CommandCore::CommandResult Cmd_undo_state(const ConsoleCommandContext& ctx)
    {
        const std::vector<std::string>& parts = ctx.parts;
        const std::string label = (parts.size() >= 2) ? parts[1] : "state";

        Meta::UndoManager* undo = Meta::UndoManager::GetInstance();
        const bool        isGameMode = undo->m_isGameMode;
        const bool        gameStart  = SceneManagers->IsGameStart();
        const std::size_t editUndo   = undo->EditUndoDepth();
        const std::size_t editRedo   = undo->EditRedoDepth();
        const std::size_t gameUndo   = undo->GameUndoDepth();
        const std::size_t gameRedo   = undo->GameRedoDepth();

        std::printf("[undo.state:%s] isGameMode=%d gameStart=%d "
            "editUndo=%zu editRedo=%zu gameUndo=%zu gameRedo=%zu\n",
            label.c_str(), isGameMode ? 1 : 0, gameStart ? 1 : 0,
            editUndo, editRedo, gameUndo, gameRedo);

        CommandCore::CommandData data = CommandCore::CommandData::Object();
        data.Set("label",      CommandCore::CommandData::String(label));
        data.Set("isGameMode", CommandCore::CommandData::Bool(isGameMode));
        data.Set("gameStart",  CommandCore::CommandData::Bool(gameStart));
        data.Set("editUndo",   CommandCore::CommandData::Int(static_cast<int64_t>(editUndo)));
        data.Set("editRedo",   CommandCore::CommandData::Int(static_cast<int64_t>(editRedo)));
        data.Set("gameUndo",   CommandCore::CommandData::Int(static_cast<int64_t>(gameUndo)));
        data.Set("gameRedo",   CommandCore::CommandData::Int(static_cast<int64_t>(gameRedo)));
        return CommandCore::Ok("undo state", std::move(data));
    }

    // 에디터의 Ctrl+Z / Ctrl+Y와 같은 호출.

    static CommandCore::CommandResult Cmd_undo_redo(const ConsoleCommandContext& ctx)
    {
        if (ctx.parts.size() != 1) return CommandCore::InvalidArguments("undo/redo accepts no arguments");
        return EditorObjectOperations::UndoRedo(ctx.cmd == "redo");
    }


    // 선택 상태 관측. scene.select가 실제로 먹었는지, 정지가 선택을 어떻게 하는지
    // 둘 다 이것으로 본다.
    //
    // ⚠ m_selectedEntity(단일)와 m_selectedEntities(복수)를 **따로** 찍는다.
    //   scene.select는 단일만 대입하고 벡터는 건드리지 않아 둘이 어긋나 있다.
    //   합쳐서 찍으면 그 어긋남이 가려진다 — 지금 동작을 정직하게 못 박는다.

    static CommandCore::CommandResult Cmd_scene_selection(const ConsoleCommandContext& ctx)
    {
        const std::vector<std::string>& parts = ctx.parts;
        const std::string label = (parts.size() >= 2) ? parts[1] : "selection";

        Scene* scene = SceneManagers->GetActiveScene();
        if (!scene)
        {
            std::printf("[selection:%s] 활성 씬 없음\n", label.c_str());
            return CommandCore::PreconditionFailed("scene.none", "활성 씬이 없다");
        }

        const Entity* primary = scene->m_selectedEntity;
        std::printf("[selection:%s] primary=%s multi=%zu\n",
            label.c_str(),
            primary ? primary->m_name.ToString().c_str() : "(none)",
            scene->m_selectedEntities.size());

        for (const Entity* entity : scene->m_selectedEntities)
        {
            std::printf("[selection:%s] multi|%s\n", label.c_str(),
                entity ? entity->m_name.ToString().c_str() : "(null)");
        }

        CommandCore::CommandData data = CommandCore::CommandData::Object();
        data.Set("primary", CommandCore::CommandData::String(
            primary ? primary->m_name.ToString() : std::string("")));
        data.Set("multi", CommandCore::CommandData::Int(
            static_cast<int64_t>(scene->m_selectedEntities.size())));
        return CommandCore::Ok("scene.selection", std::move(data));
    }

    static CommandCore::CommandResult Cmd_scene_dump(const ConsoleCommandContext& ctx)
    {
        const std::vector<std::string>& parts = ctx.parts;

        // 활성 씬의 오브젝트 계층을 로그로 남긴다. 재생/정지 전후로 찍어 비교하면
        // 무엇이 사라졌는지가 그대로 드러난다.
        Scene* scene = SceneManagers->GetActiveScene();
        if (!scene)
        {
            std::printf("[CLI] 활성 씬 없음\n");
            return CommandCore::PreconditionFailed("scene.none", "활성 씬이 없다");
        }

        const std::string label = (parts.size() > 1) ? parts[1] : std::string("dump");
        Debug->LogWarning("[씬 덤프] " + label + " · 오브젝트 " +
            std::to_string(scene->m_Entities.size()) + "개");

        for (const auto& object : scene->m_Entities)
        {
            if (!object) continue;
		const auto& p = object->Transform_().GetPositionValue();
            char position[96]{};
            std::snprintf(position, sizeof(position), "(%.3f, %.3f, %.3f)", p.x, p.y, p.z);

            Debug->LogWarning("[씬 덤프]   " + object->m_name.ToString() +
                " (index=" + std::to_string(object->m_index) +
                ", parent=" + std::to_string(object->GetParentIndex()) +
                ", 컴포넌트 " + std::to_string(object->m_components.size()) + "개"
                ", pos=" + position + ")");
        }
        std::printf("[CLI] 씬 덤프 기록: %s (%zu개)\n", label.c_str(), scene->m_Entities.size());

        CommandCore::CommandData data = CommandCore::CommandData::Object();
        data.Set("label", CommandCore::CommandData::String(label));
        data.Set("objects", CommandCore::CommandData::Int(
            static_cast<int64_t>(scene->m_Entities.size())));
        return CommandCore::Ok("씬 덤프: " + label, std::move(data));
    }

    void RegisterSceneObjectCommands(Registrar& reg)
    {
        reg.Result({ "scene.load", "scene.switch" }, &Cmd_scene_load);
        reg.Result({ "scene.new" }, &Cmd_scene_new);
        reg.Result({ "scene.ddol" }, &Cmd_scene_ddol);
        reg.Result({ "ai.status" }, &Cmd_ai_status);
        reg.Result({ "scene.save" }, &Cmd_scene_save);
        reg.Result({ "object.create" }, &Cmd_object_create);
        reg.Result({ "object.delete" }, &Cmd_object_delete);
        reg.Result({ "object.properties" }, &Cmd_object_properties);
        reg.Result({ "component.remove" }, &Cmd_component_remove);
        reg.Result({ "object.describe" }, &Cmd_object_describe);
        reg.Result({ "object.rename" }, &Cmd_object_rename);
        reg.Result({ "object.transform" }, &Cmd_object_transform);
        reg.Result({ "object.parent" }, &Cmd_object_parent);
        reg.Result({ "object.rootref" }, &Cmd_object_rootref);
        reg.Result({ "object.duplicate" }, &Cmd_object_duplicate);
        reg.Result({ "scene.hierarchycheck" }, &Cmd_scene_hierarchycheck);
        reg.Result({ "object.property" }, &Cmd_object_property);
        reg.Result({ "scene.select" }, &Cmd_scene_select);
        reg.Result({ "component.add" }, &Cmd_component_add);
        reg.Result({ "prefab.instantiate" }, &Cmd_prefab_instantiate);
        reg.Result({ "prefab.status" }, &Cmd_prefab_status);
        reg.Result({ "prefab.corpus.digest" }, &Cmd_prefab_corpus_digest);
        reg.Result({ "prefab.overrides" }, &Cmd_prefab_overrides);
        reg.Result({ "prefab.update" }, &Cmd_prefab_update);
        reg.Result({ "camera.editor" }, &Cmd_camera_editor);
        reg.Result({ "component.list" }, &Cmd_component_list);
        reg.Result({ "prefab.create" }, &Cmd_prefab_create);
        reg.Result({ "play", "stop" }, &Cmd_play);
        reg.Result({ "play.state" }, &Cmd_play_state);
        reg.Result({ "undo.state" }, &Cmd_undo_state);
        reg.Result({ "undo", "redo" }, &Cmd_undo_redo);
        reg.Result({ "scene.selection" }, &Cmd_scene_selection);
        reg.Result({ "scene.dump" }, &Cmd_scene_dump);
    }
}
