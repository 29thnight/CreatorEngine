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

#include "CommandBaseline.h"            // LC0(PHASE 14.5): 등록 표·프레임·왕복 지연 계측
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
#include "ExperimentParity/ExperimentMaterialParitySelfTest.h"
#include "ExperimentParity/ExperimentMaterialResolveSelfTest.h"
#include "ExperimentParity/ExperimentMaterialInstanceSelfTest.h"
#include "ExperimentParity/ExperimentMaterialSealSelfTest.h"
#include "ExperimentParity/ExperimentMaterialCodecSelfTest.h"
#include "ExperimentParity/ExperimentMaterialMigrateSelfTest.h"
#include "ExperimentParity/ExperimentMaterialScriptSelfTest.h"
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

namespace
{
    // "1,2,3" 또는 "1 2 3"을 성분으로 쪼갠다. 두 형태를 다 받는 이유는
    // 벡터를 한 토큰으로 쓰는 편이 스크립트에서 읽기 쉽지만, 손으로 칠 때는
    // 공백이 더 자연스러워서다.
    std::vector<float> ParseNumbers(const std::string& raw)
    {
        std::vector<float> numbers;
        std::string buffer = raw;
        for (char& c : buffer) { if (',' == c) c = ' '; }

        std::istringstream iss(buffer);
        float value = 0.f;
        while (iss >> value) numbers.push_back(value);
        return numbers;
    }

    float NumberAt(const std::vector<float>& numbers, size_t index, float fallback)
    {
        return (index < numbers.size()) ? numbers[index] : fallback;
    }

    // ★ LC6: `object.property` 전용 반사 헬퍼라 함께 옮겨 왔다.
    // 리플렉션으로 프로퍼티 하나를 설정한다.
    //
    // 인스펙터(ReflectionImGuiHelper)가 하는 일과 같은 목록을 훑는다. 컴포넌트마다
    // 전용 CLI를 만들지 않는 이유가 그것이다 — 종류가 늘 때마다 두 곳을 고치게 된다.
    //
    // 부모 타입까지 올라간다. 컴포넌트 프로퍼티는 상속 계층에 흩어져 있고
    // (m_isEnabled는 Component에, m_lightType은 LightComponent에) 인스펙터도
    // 재귀로 훑는다.
    bool ApplyReflectedProperty(void* instance, const Meta::Type* type,
        const std::string& field, const std::string& raw)
    {
        if (nullptr == type) return false;

        for (const auto& prop : type->properties)
        {
            if (nullptr == prop.name || field != prop.name) continue;
            if (!prop.setter) return false;

            const HashedGuid hash = prop.typeID;
            const auto numbers = ParseNumbers(raw);

            if (hash == GUIDCreator::GetTypeID<float>())
            {
                prop.setter(instance, NumberAt(numbers, 0, 0.f));
                return true;
            }
            if (hash == GUIDCreator::GetTypeID<int>())
            {
                prop.setter(instance, static_cast<int>(NumberAt(numbers, 0, 0.f)));
                return true;
            }
            if (hash == GUIDCreator::GetTypeID<unsigned int>() || prop.typeName == "UINT")
            {
                prop.setter(instance, static_cast<unsigned int>(NumberAt(numbers, 0, 0.f)));
                return true;
            }
            if (hash == GUIDCreator::GetTypeID<bool>() || prop.typeName == "bool32")
            {
                prop.setter(instance, raw == "true" || raw == "1");
                return true;
            }
            if (hash == GUIDCreator::GetTypeID<std::string>())
            {
                prop.setter(instance, raw);
                return true;
            }
            // ★ 자산 참조(FileGuid) — 2026-08-20 추가.
            //
            // 없는 동안 **CLI로는 자산을 참조하는 컴포넌트를 저작할 수 없었다.**
            // BehaviorTreeComponent의 m_BehaviorTreeGuid·m_BlackBoardGuid가 그렇고
            // (실측: "지원하지 않는 프로퍼티 타입 ... (FileGuid)" -> 트리 0개),
            // 머티리얼·메시 참조도 같은 타입이다. 즉 BT만의 문제가 아니라 자산을
            // 가리키는 모든 필드에 걸리던 구멍이다.
            //
            // FileGuid는 문자열 생성자를 갖고(TypeTrait.h) FromString은 못 읽으면
            // 던진다 — 잘못된 값을 조용히 널 guid로 삼키지 않도록 여기서 잡아
            // 실패로 돌려준다(널 guid는 "자산 없음"이라 조용히 넘기면 컴포넌트가
            // 초기화에 실패하고 그 이유가 로그에 안 남는다).
            if (prop.typeName == "FileGuid")
            {
                try
                {
                    prop.setter(instance, FileGuid(raw));
                    return true;
                }
                catch (const std::exception&)
                {
                    std::printf("[CLI] guid 형식이 아니다: %s = '%s'\n",
                        prop.name, raw.c_str());
                    return false;
                }
            }
            if (hash == GUIDCreator::GetTypeID<math::vector2>())
            {
                prop.setter(instance, math::vector2{
                    NumberAt(numbers, 0, 0.f), NumberAt(numbers, 1, 0.f) });
                return true;
            }
            if (hash == GUIDCreator::GetTypeID<math::vector3>())
            {
                prop.setter(instance, math::vector3{
                    NumberAt(numbers, 0, 0.f), NumberAt(numbers, 1, 0.f),
                    NumberAt(numbers, 2, 0.f) });
                return true;
            }
            if (hash == GUIDCreator::GetTypeID<math::vector4>())
            {
                prop.setter(instance, math::vector4{
                    NumberAt(numbers, 0, 0.f), NumberAt(numbers, 1, 0.f),
                    NumberAt(numbers, 2, 0.f), NumberAt(numbers, 3, 1.f) });
                return true;
            }
            if (hash == GUIDCreator::GetTypeID<math::color>())
            {
                prop.setter(instance, math::color{
                    NumberAt(numbers, 0, 1.f), NumberAt(numbers, 1, 1.f),
                    NumberAt(numbers, 2, 1.f), NumberAt(numbers, 3, 1.f) });
                return true;
            }

            // 열거형은 이름으로도 숫자로도 받는다. 이름 쪽이 스크립트를 읽을 때
            // 무슨 뜻인지 바로 보인다(Directional vs 0). 열거형 점검(8-17):
            // 이름 키 등록소 조회 → 프로퍼티가 직접 든 enum 표. 등록 안 된
            // 열거형이 조용히 "지원하지 않는 타입"으로 빠지던 구멍도 함께 닫힌다.
            if (const Meta::EnumType* enumType = prop.enumType)
            {
                for (const auto& entry : enumType->values)
                {
                    if (nullptr != entry.name && raw == entry.name)
                    {
                        prop.setter(instance, entry.value);
                        return true;
                    }
                }

                if (!numbers.empty())
                {
                    prop.setter(instance, static_cast<int>(numbers[0]));
                    return true;
                }
            }

            std::printf("[CLI] 지원하지 않는 프로퍼티 타입: %s (%s)\n",
                prop.name, prop.typeName.c_str());
            return false;
        }

        return ApplyReflectedProperty(instance, type->parent, field, raw);
    }

    bool ApplyReflectedProperty(Component* component, const std::string& field,
        const std::string& raw)
    {
        if (nullptr == component) return false;
        return ApplyReflectedProperty(component, Meta::Find(component->GetTypeID().m_ID_Data), field, raw);
    }
}

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

	static void Cmd_ai_status(const ConsoleCommandContext& ctx)
	{
		const size_t total = AIManagers->GetRegisteredAIComponentCount();
		if (ctx.parts.size() < 2)
		{
			std::printf("[AI 레지스트리] total=%zu\n", total);
			return;
		}

		Scene* scene = SceneManagers->GetActiveScene();
		Entity* object = scene ? scene->GetEntity(ctx.parts[1]) : nullptr;
		StateMachineComponent* component = object
			? object->GetComponent<StateMachineComponent>() : nullptr;
		const bool registered = AIManagers->IsAIComponentRegistered(component);
		std::printf("[AI 레지스트리] object=%s registered=%d total=%zu scene=%u\n",
			ctx.parts[1].c_str(), registered ? 1 : 0, total,
			scene ? scene->GetSceneId() : 0u);
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

    static void Cmd_object_create(const ConsoleCommandContext& ctx)
    {
        const std::vector<std::string>& parts = ctx.parts;

        if (parts.size() < 2)
        {
            std::printf("[CLI] 사용법: object.create <이름> [Empty|Light|Camera|Mesh|UI|Canvas]\n");
            return;
        }

        Scene* scene = SceneManagers->GetActiveScene();
        if (!scene) { std::printf("[CLI] 활성 씬 없음\n"); return; }

        GameObjectType type = GameObjectType::Empty;
        std::string name = parts[1];
        if (parts.size() > 2)
        {
            const std::string& typeName = parts[2];
            if (typeName == "Light")       type = GameObjectType::Light;
            else if (typeName == "Camera") type = GameObjectType::Camera;
            else if (typeName == "Mesh")   type = GameObjectType::Mesh;
            // ★ UI·Canvas 추가 (2026-08-20, 자산·게이트 CLI 이전).
            //
            // 이 둘이 없어 **CLI로는 UI 오브젝트를 아예 저작할 수 없었다.**
            // RectTransformComponent는 손으로 붙일 수 없고(ComponentFactory가
            // 의도적으로 목록에서 뺀다 — 3D 오브젝트에 붙으면 UI 레이아웃 순회에
            // 끼어들어 자식에게 스크린 좌표계를 조용히 전파한다), 그 부착은
            // GameObject::AttachSpatialComponent가 **오브젝트 타입으로** 정한다:
            // UI는 rect만, Canvas는 rect와 Transform 둘 다, 나머지는 Transform만.
            //
            // 즉 타입을 못 주면 rect가 없는 오브젝트만 만들 수 있고, ui.rect·
            // ui.hitbox가 전부 "RectTransform 없음"으로 떨어진다(실측). ui.* 명령은
            // 관측·설정이지 **생성이 아니다** — §0.05의 "CLI 저작 표면은 이미 서
            // 있다"가 UI에 대해서는 성립하지 않았다.
            //
            // 해상도 스윕 게이트 이전과 verify-authored-rects의 후계가 **둘 다**
            // 이 능력을 선행으로 요구한다.
            else if (typeName == "UI")     type = GameObjectType::UI;
            else if (typeName == "Canvas") type = GameObjectType::Canvas;
            else if (typeName != "Empty")
            {
                std::printf("[CLI] 알 수 없는 오브젝트 타입: %s\n", typeName.c_str());
                return;
            }
        }

        auto object = scene->CreateEntity(name, type);
        if (!object)
        {
            std::printf("[CLI] 오브젝트 생성 실패: %s\n", name.c_str());
            return;
        }

        Debug->LogWarning("[CLI] 오브젝트 생성: " + name);
        std::printf("[CLI] 오브젝트 생성: %s\n", name.c_str());
    }

    static void Cmd_object_rename(const ConsoleCommandContext& ctx)
    {
        const std::vector<std::string>& parts = ctx.parts;
        const std::string& cmd = ctx.cmd;

        // 같은 모델을 여러 번 배치하면 이름이 겹쳐 이후 명령이 첫 번째만 잡는다.
        // 하나 놓고 바로 이름을 바꾸면 그 문제가 없다.
        if (parts.size() < 3)
        {
            std::printf("[CLI] 사용법: object.rename <이전 이름> <새 이름>\n");
            return;
        }

        Scene* scene = SceneManagers->GetActiveScene();
        if (!scene) { std::printf("[CLI] 활성 씬 없음\n"); return; }

        // 이전 이름에 공백이 흔하다. 같은 모델을 두 번 놓으면 엔진이
        // "Prim_Cube (1)"처럼 번호를 붙이기 때문이다. 그래서 새 이름을 마지막
        // 토큰으로 보고 그 앞 전체를 이전 이름으로 본다(prefab.create와 같은 규칙).
        const auto names = CommandCore::SplitTrailingName(parts, 1);
        const std::string& oldName = names.leading;
        const std::string& newName = names.trailing;

        auto object = scene->GetEntity(oldName);
        if (!object)
        {
            Debug->LogError("[CLI] 오브젝트를 찾을 수 없음: " + oldName);
            std::printf("[CLI] 오브젝트를 찾을 수 없음: %s\n", oldName.c_str());
            return;
        }

        object->m_name = newName;
        Debug->LogWarning("[CLI] 이름 변경: " + oldName + " -> " + newName);
        std::printf("[CLI] 이름 변경: %s -> %s\n", oldName.c_str(), newName.c_str());
    }

    static CommandCore::CommandResult Cmd_scene_hierarchycheck(const ConsoleCommandContext& ctx)
    {
        (void)ctx;

        // scene.hierarchycheck — 계층 표기의 불변식을 잰다.
        //
        // 재는 불변식은 하나다:
        //
        //     자식이 부모의 m_childrenIndices에 실려 있다  <=>  자식의 m_parentIndex가 그 부모다
        //
        // 이 쌍이 깨지면 순회(m_Entities[0]->m_childrenIndices에서만 내려간다)가
        // 서브트리를 통째로 빠뜨리는데 에러도 로그도 없다 — 뼈 61개가 그렇게
        // 순회 밖에 있었다.
        //
        // 최상위 오브젝트의 표기가 갈려 있는 것이 그 뿌리다(SceneGraphRedesignPlan
        // 트랙 E). 같은 뜻인데 두 값이 쓰인다:
        //   · Entity::AddChild            -> m_parentIndex = 부모 인덱스(루트면 0)
        //   · Scene::AttachExistingEntity / DDOL 이탈 -> INVALID_INDEX(-1)
        // 둘 다 씬 루트의 children에는 들어가므로, "-1인데 루트 children에 있음"이
        // 정상처럼 보인다. 그 상태를 세는 것이 topLevelInvalid다.
        Scene* scene = SceneManagers->GetActiveScene();
        if (!scene)
        {
            std::printf("[CLI] 활성 씬 없음\n");
            return CommandCore::PreconditionFailed("scene.none", "활성 씬이 없다");
        }

        const auto& objects = scene->m_Entities;

        size_t total = 0;
        size_t topLevelRoot = 0;      // 최상위인데 m_parentIndex == 0 (쌍이 맞는 표기)
        size_t topLevelInvalid = 0;   // 최상위인데 m_parentIndex == INVALID (쌍이 어긋난 표기)
        size_t pairMismatch = 0;      // 부모의 children에 있는데 m_parentIndex가 그 부모가 아님
        size_t orphan = 0;            // 아무의 children에도 없음(씬 루트 제외)
        size_t unreachable = 0;       // 씬 루트에서 children만 따라 내려가 닿지 못함

        // 어느 부모의 children에 실려 있는지 역인덱스를 만든다.
        std::unordered_map<Entity::Index, Entity::Index> listedUnder;
        for (const auto& obj : objects)
        {
            if (!obj) continue;
            for (Entity::Index childIdx : obj->GetChildrenIndices())
            {
                listedUnder[childIdx] = obj->m_index;
            }
        }

        // 씬 루트에서 children만 따라 내려가 닿는 집합.
        std::unordered_set<Entity::Index> reached;
        if (!objects.empty() && objects[0])
        {
            std::vector<Entity::Index> stack{ objects[0]->m_index };
            reached.insert(objects[0]->m_index);
            while (!stack.empty())
            {
                const Entity::Index cur = stack.back();
                stack.pop_back();
                const auto& node = scene->TryGetEntity(cur);
                if (!node) continue;
                for (Entity::Index childIdx : node->GetChildrenIndices())
                {
                    if (reached.insert(childIdx).second) stack.push_back(childIdx);
                }
            }
        }

        for (const auto& obj : objects)
        {
            if (!obj) continue;
            ++total;
            if (Entity::kSceneRootIndex == obj->m_index) continue;   // 씬 루트 자신은 제외

            auto it = listedUnder.find(obj->m_index);
            if (it == listedUnder.end())
            {
                ++orphan;
            }
            else if (it->second == Entity::kSceneRootIndex)
            {
                if (Entity::IsInvalidIndex(obj->GetParentIndex())) ++topLevelInvalid;
                else if (Entity::kSceneRootIndex == obj->GetParentIndex()) ++topLevelRoot;
                else ++pairMismatch;
            }
            else if (it->second != obj->GetParentIndex())
            {
                ++pairMismatch;
            }

            if (reached.find(obj->m_index) == reached.end()) ++unreachable;
        }

        const size_t storeMismatch = scene->CountHierarchyStoreMismatches();
        std::printf("[scene.hierarchycheck] 오브젝트 %zu · 최상위(0표기) %zu · 최상위(-1표기) %zu"
            " · 쌍불일치 %zu · 고아 %zu · 순회미도달 %zu · Store불일치 %zu\n",
            total, topLevelRoot, topLevelInvalid, pairMismatch, orphan, unreachable, storeMismatch);

        // ★ **이 명령에는 진짜 판정이 있다.** 쌍불일치·고아·순회미도달·Store불일치는
        //   전부 "순회가 서브트리를 통째로 빠뜨리는" 상태이고, 그것이 뼈 61 개를
        //   순회 밖에 두었던 결함이다(위 주석). 지금까지는 그 수를 찍기만 하고
        //   프로세스는 0 으로 끝났다 — 세어 놓고 판정하지 않고 있었다.
        //
        //   최상위(-1표기)는 세되 **판정에 넣지 않는다.** 그것은 같은 뜻의 표기가
        //   둘이라는 이미 알려진 상태이고(트랙 E), 순회를 깨뜨리지는 않는다.
        const size_t broken = pairMismatch + orphan + unreachable + storeMismatch;

        CommandCore::CommandData data = CommandCore::CommandData::Object();
        data.Set("objects", CommandCore::CommandData::Int(static_cast<int64_t>(total)));
        data.Set("topLevelRoot", CommandCore::CommandData::Int(static_cast<int64_t>(topLevelRoot)));
        data.Set("topLevelInvalid", CommandCore::CommandData::Int(static_cast<int64_t>(topLevelInvalid)));
        data.Set("pairMismatch", CommandCore::CommandData::Int(static_cast<int64_t>(pairMismatch)));
        data.Set("orphan", CommandCore::CommandData::Int(static_cast<int64_t>(orphan)));
        data.Set("unreachable", CommandCore::CommandData::Int(static_cast<int64_t>(unreachable)));
        data.Set("storeMismatch", CommandCore::CommandData::Int(static_cast<int64_t>(storeMismatch)));
        if (broken > 0)
        {
            return CommandCore::Fail("scene.hierarchy_broken",
                "계층 불변식 위반 " + std::to_string(broken) + "건", std::move(data));
        }
        return CommandCore::Ok("계층 불변식 통과", std::move(data));
    }

    static void Cmd_object_duplicate(const ConsoleCommandContext& ctx)
    {
        const std::vector<std::string>& parts = ctx.parts;
        const std::string& cmd = ctx.cmd;

        // object.duplicate <오브젝트> [새 이름]
        //
        // 에디터의 Ctrl+D(DuplicateGameObjectCommand::Redo)와 **같은 원시 함수**를
        // 부른다 — Object::Instantiate. 에디터 전용 경로를 CLI에서도 태울 수 있어야
        // 회귀가 그 경로를 잴 수 있다. 지금 이 경로는 게이트가 하나도 없다.
        if (parts.size() < 2)
        {
            std::printf("[CLI] 사용법: object.duplicate <오브젝트> [새 이름]\n");
            return;
        }

        Scene* scene = SceneManagers->GetActiveScene();
        if (!scene) { std::printf("[CLI] 활성 씬 없음\n"); return; }

        // 이름 규칙은 object.rename·prefab.create와 같다 — 인자가 둘이면 마지막
        // 토큰이 새 이름이고 그 앞 전체가 원본 이름이다(공백 있는 이름 때문).
        // LC2: 원문 재해석 제거. 토큰만 본다.
        std::string sourceName;
        std::string newName;
        if (parts.size() >= 3)
        {
            const auto names = CommandCore::SplitTrailingName(parts, 1);
            sourceName = names.leading;
            newName    = names.trailing;
        }
        else
        {
            sourceName = CommandCore::JoinFrom(parts, 1);
        }

        auto source = scene->GetEntity(sourceName);
        if (!source)
        {
            Debug->LogError("[CLI] 오브젝트를 찾을 수 없음: " + sourceName);
            std::printf("[CLI] 오브젝트를 찾을 수 없음: %s\n", sourceName.c_str());
            return;
        }

        const std::string finalName = newName.empty() ? source->m_name.ToString() : newName;
		auto* cloned = dynamic_cast<Entity*>(Object::Instantiate(source, finalName));
        if (!cloned)
        {
            std::printf("[CLI] 복제 실패: %s\n", sourceName.c_str());
            return;
        }

        // Object::Instantiate가 newName을 반영하지만(Object.cpp:113) 씬 편입이
        // 고유 이름 생성으로 그것을 덮는다("Orig" -> "Orig (1)"). 회귀가 이름으로
        // 대상을 집으므로 반환 뒤에 한 번 더 지정한다.
        if (!newName.empty())
        {
            cloned->m_name = newName;
        }

        Debug->LogWarning("[CLI] 복제: " + sourceName + " -> " + cloned->m_name.ToString());
        std::printf("[CLI] 복제: %s -> %s (index=%d)\n",
            sourceName.c_str(), cloned->m_name.ToString().c_str(),
            static_cast<int>(cloned->m_index));
    }

    static void Cmd_object_parent(const ConsoleCommandContext& ctx)
    {
        const std::vector<std::string>& parts = ctx.parts;
        const std::string& cmd = ctx.cmd;

        // object.parent <자식> <부모>
        //
        // 계층을 가진 자산을 CLI로 저작하기 위한 명령이다. 이것이 없으면
        // object.create가 만드는 것은 전부 루트라, 회귀 게이트가 쓰는 픽스처를
        // 저작 자산 없이 만들 수 없었다(SceneGraphRedesignPlan §0.05).
        //
        // 부모를 "-"로 주면 씬 루트로 되돌린다.
        if (parts.size() < 3)
        {
            std::printf("[CLI] 사용법: object.parent <자식> <부모 | ->\n"
                "       부모에 -를 주면 씬 루트로 올린다\n");
            return;
        }

        Scene* scene = SceneManagers->GetActiveScene();
        if (!scene) { std::printf("[CLI] 활성 씬 없음\n"); return; }

        // 이름에 공백이 흔하므로(엔진이 "Prim_Cube (1)"처럼 번호를 붙인다)
        // 부모를 마지막 토큰으로 보고 그 앞 전체를 자식으로 본다
        // — object.rename·prefab.create와 같은 규칙이다.
        //
        // LC2: 예전에는 원문을 `rfind` 로 잘랐다. 그래서 따옴표를 쓴 입력
        // (`object.parent "Big Boss" "Main Characters"`)에서 자식 이름에
        // **따옴표가 남았고** 씬에서 영영 못 찾았다. 이제 토큰만 본다.
        const auto names = CommandCore::SplitTrailingName(parts, 1);
        const std::string& childName  = names.leading;
        const std::string& parentName = names.trailing;

        auto child = scene->GetEntity(childName);
        if (!child)
        {
            Debug->LogError("[CLI] 자식 오브젝트를 찾을 수 없음: " + childName);
            std::printf("[CLI] 자식 오브젝트를 찾을 수 없음: %s\n", childName.c_str());
            return;
        }

		Entity* parent =
            ("-" == parentName) ? scene->GetRootEntity() : scene->GetEntity(parentName);
        if (!parent)
        {
            Debug->LogError("[CLI] 부모 오브젝트를 찾을 수 없음: " + parentName);
            std::printf("[CLI] 부모 오브젝트를 찾을 수 없음: %s\n", parentName.c_str());
            return;
        }

        if (parent->m_index == child->m_index)
        {
            std::printf("[CLI] 자기 자신을 부모로 삼을 수 없음: %s\n", childName.c_str());
            return;
        }

        // 순환을 막는다 — 자기 자손을 부모로 삼으면 순회가 무한이 된다.
        for (auto ancestor = parent; ancestor; )
        {
            if (ancestor->m_index == child->m_index)
            {
                Debug->LogError("[CLI] 순환 계층 거부: " + childName + " <- " + parentName);
                std::printf("[CLI] 순환이 된다(부모가 자식의 자손): %s\n", parentName.c_str());
                return;
            }
            const Entity::Index ancestorParent = ancestor->GetParentIndex();
            if (Entity::INVALID_INDEX == ancestorParent) break;
            auto next = scene->GetEntity(ancestorParent);
            if (next == ancestor) break;
            ancestor = next;
        }

        // 부모 인덱스·Transform 부모 ID·자식 목록을 한 점에서 함께 옮긴다.
		parent->AddChild(child);

        Debug->LogWarning("[CLI] 부모 지정: " + childName + " -> " + parentName);
        std::printf("[CLI] 부모 지정: %s -> %s\n", childName.c_str(), parentName.c_str());
    }

    // H2 root-reference 회귀용. m_rootIndex는 일반 parent와 별개의 same-scene
    // 참조라서 DDOL/Prefab 슬롯 재배정 때 따로 remap해야 한다. 모델 자산 없이
    // 그 경계를 태울 수 있도록 설정과 조회를 한 명령에 둔다.

    static void Cmd_object_rootref(const ConsoleCommandContext& ctx)
    {
        if (ctx.parts.size() < 2)
        {
            std::printf("[CLI] 사용법: object.rootref <오브젝트> [루트오브젝트 | -]\n");
            return;
        }

        Scene* scene = SceneManagers->GetActiveScene();
        if (!scene) { std::printf("[CLI] 활성 씬 없음\n"); return; }

        Entity* object = scene->GetEntity(ctx.parts[1]);
        if (!object)
        {
            std::printf("[CLI] rootref 대상 없음: %s\n", ctx.parts[1].c_str());
            return;
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
                    return;
                }
                object->SetRootIndex(root->m_index);
            }
        }

        const Entity::Index rootIndex = object->GetRootIndex();
        Entity* root = scene->TryGetEntity(rootIndex);
        std::printf("[object.rootref] %s root=%d name=%s\n",
            object->GetHashedName().ToString().c_str(), static_cast<int>(rootIndex),
            root ? root->GetHashedName().ToString().c_str() : "<invalid>");
    }

    static void Cmd_object_transform(const ConsoleCommandContext& ctx)
    {
        const std::vector<std::string>& parts = ctx.parts;

        // object.transform <이름> <px> <py> <pz> [rx ry rz] [sx sy sz]
        // 회전은 오일러 각(도)이다. 라디안을 쓰면 스크립트를 읽을 때 값이
        // 무슨 뜻인지 바로 안 보인다.
        if (parts.size() < 5)
        {
            std::printf("[CLI] 사용법: object.transform <이름> <px> <py> <pz>"
                " [rx ry rz] [sx sy sz]\n"
                "       이름에 공백이 있으면 따옴표로 묶는다: \"Main Camera\"\n");
            return;
        }

        Scene* scene = SceneManagers->GetActiveScene();
        if (!scene) { std::printf("[CLI] 활성 씬 없음\n"); return; }

        auto object = scene->GetEntity(parts[1]);
        if (!object)
        {
            std::printf("[CLI] 오브젝트를 찾을 수 없음: %s\n", parts[1].c_str());
            return;
        }

        const auto number = [&](size_t index, float fallback) -> float
        {
            return (parts.size() > index)
                ? static_cast<float>(std::atof(parts[index].c_str())) : fallback;
        };

        const math::vector3 position{ number(2, 0.f), number(3, 0.f), number(4, 0.f) };
        const math::vector3 euler{ number(5, 0.f), number(6, 0.f), number(7, 0.f) };
        const math::vector3 scale{ number(8, 1.f), number(9, 1.f), number(10, 1.f) };

        object->Transform_().SetPosition(position);
        object->Transform_().SetRotation(math::quaternion_from_pitch_yaw_roll(
            math::radians(euler.x), math::radians(euler.y), math::radians(euler.z)));
        object->Transform_().SetScale(scale);
        object->Transform_().UpdateWorldMatrix();

        // ★ 이 경로도 오버라이드로 기록한다 (SceneGraphRedesignPlan P-write S4).
        //
        // object.property와 달리 여기는 리플렉션 세터(ApplyReflectedProperty)를
        // 지나지 않고 Transform의 세터를 직접 부른다 — 실측으로 확인한, CLI에서
        // Property::setter를 우회하는 유일한 다른 쓰기 경로다. 이 슬라이스를 빼면
        // object.transform으로 만든 로컬 수정은 S3 이후에도 여전히 조용히 유실된다.
        //
        // Transform은 S1-b+S3에서 컴포넌트로 승격됐으므로 다른 컴포넌트와 똑같이
        // 다룬다(순번도 ComputeComponentSlot이 센다). 세 필드는 Transform::reflect()
        // 의 이름 그대로다 — 이름이 어긋나면 RecordPropertyOverride가 프로퍼티 노드를
        // 못 찾고 조용히 아무것도 안 남기므로, 필드명을 바꿀 때 여기도 함께 본다.
        PrefabUtility::RecordPropertyOverride(*object, object->Transform_(), "position");
        PrefabUtility::RecordPropertyOverride(*object, object->Transform_(), "rotation");
        PrefabUtility::RecordPropertyOverride(*object, object->Transform_(), "scale");

        char message[192]{};
        std::snprintf(message, sizeof(message),
            "[CLI] 변환 설정: %s pos(%.2f %.2f %.2f) rot(%.1f %.1f %.1f) scale(%.2f %.2f %.2f)",
            parts[1].c_str(), position.x, position.y, position.z,
            euler.x, euler.y, euler.z, scale.x, scale.y, scale.z);
        Debug->LogWarning(message);
        std::printf("%s\n", message);
    }

    static void Cmd_object_property(const ConsoleCommandContext& ctx)
    {
        const std::vector<std::string>& parts = ctx.parts;
        const std::string& cmd = ctx.cmd;

        // object.property <오브젝트> <컴포넌트> <필드> <값...>
        //
        // 리플렉션으로 설정한다. 컴포넌트마다 전용 명령을 만들면 종류가 늘 때마다
        // CLI가 같이 늘고, 그건 인스펙터가 이미 하는 일을 두 번 하는 것이다 —
        // 인스펙터도 같은 프로퍼티 목록을 훑는다.
        if (parts.size() < 5)
        {
            std::printf("[CLI] 사용법: object.property <오브젝트> <컴포넌트> <필드> <값...>\n");
            return;
        }

        Scene* scene = SceneManagers->GetActiveScene();
        if (!scene) { std::printf("[CLI] 활성 씬 없음\n"); return; }

        auto object = scene->GetEntity(parts[1]);
        if (!object)
        {
            std::printf("[CLI] 오브젝트를 찾을 수 없음: %s\n", parts[1].c_str());
            return;
        }

        const Meta::Type* requestedType = Meta::Find(parts[2]);
        Component* target = nullptr;
        for (const auto& component : object->m_components)
        {
            if (component && (component->ToString() == parts[2]
                || (requestedType && component->GetTypeID() == requestedType->typeID)))
            {
                target = component.get();
                break;
            }
        }
        if (nullptr == target)
        {
            Debug->LogError("[CLI] 컴포넌트를 찾을 수 없음: " + parts[1] + "." + parts[2]);
            std::printf("[CLI] 컴포넌트를 찾을 수 없음: %s\n", parts[2].c_str());
            return;
        }

        // 값에 쉼표로 구분한 성분이 들어올 수 있다(벡터·색). 필드 이름 뒤 전체.
        std::string rest = CommandCore::JoinFrom(parts, 1);
        for (size_t i = 1; i <= 3; ++i) rest = TrimLine(rest.substr(rest.find(parts[i]) + parts[i].size()));
        const std::string rawValue = rest;

        if (!ApplyReflectedProperty(target, parts[3], rawValue))
        {
            // 실패를 로그에도 남긴다. 콘솔 출력은 스크립트 실행에서 리다이렉트되지
            // 않아 보이지 않고, 그러면 '설정한 줄 알았는데 안 된' 씬이 저장된다.
            Debug->LogError("[CLI] 프로퍼티 설정 실패: " + parts[2] + "." + parts[3]
                + " = " + rawValue);
            std::printf("[CLI] 프로퍼티 설정 실패: %s.%s\n", parts[2].c_str(), parts[3].c_str());
            return;
        }

        // ★ 저작 의도의 기록 지점 (SceneGraphRedesignPlan P-write S3).
        //
        // 프리팹 인스턴스라면 이 수정이 "로컬 수정"이고, 기록해 두지 않으면 다음
        // 프리팹 갱신이 에러도 로그도 없이 덮어쓴다. 값은 넘기지 않는다 —
        // RecordPropertyOverride가 방금 쓰인 컴포넌트 상태에서 직접 뽑는다.
        //
        // 부수 효과 하나를 알고 쓴다: 이 목록이 비어 있지 않게 되는 순간
        // UpdateInstances의 과도기 시딩(m_prefabOverrides.empty() 조건)이 더 이상
        // 돌지 않는다. 그건 손실이 아니라 이득이다 — 그 시딩은 m_name·m_instanceID·
        // m_index·m_parentIndex 같은 **엔진 장부까지 사용자 수정으로 오기록**한다
        // (실측: 8건 중 7건). 정본이 서면 추론은 물러나는 것이 맞다.
        PrefabUtility::RecordPropertyOverride(*object, *target, parts[3]);

        Debug->LogWarning("[CLI] 프로퍼티 설정: " + parts[1] + "." + parts[2] + "."
            + parts[3] + " = " + rawValue);
        std::printf("[CLI] 프로퍼티 설정: %s.%s = %s\n", parts[2].c_str(), parts[3].c_str(),
            rawValue.c_str());
    }

    static CommandCore::CommandResult Cmd_scene_select(const ConsoleCommandContext& ctx)
    {
        const std::vector<std::string>& parts = ctx.parts;
        const std::string& cmd = ctx.cmd;

        if (parts.size() < 2)
        {
            std::printf("[CLI] 사용법: scene.select <오브젝트 이름>\n");
            return CommandCore::InvalidArguments("scene.select: <오브젝트 이름> 이 필요하다");
        }

        Scene* scene = SceneManagers->GetActiveScene();
        if (!scene)
        {
            std::printf("[CLI] 활성 씬 없음\n");
            return CommandCore::PreconditionFailed("scene.none", "활성 씬이 없다");
        }

        const std::string objectName = CommandCore::JoinFrom(parts, 1);
        auto object = scene->GetEntity(objectName);
        if (!object)
        {
            std::printf("[CLI] 오브젝트를 찾을 수 없음: %s\n", objectName.c_str());
            return CommandCore::Fail("object.not_found",
                "오브젝트를 찾을 수 없다: " + objectName);
        }

        // 인스펙터가 보는 선택 상태를 그대로 바꾼다(에디터에서 클릭한 것과 같은 효과).
		scene->m_selectedEntity = object;
        Debug->LogWarning("[CLI] 선택: " + objectName);
        std::printf("[CLI] 선택: %s\n", objectName.c_str());

        CommandCore::CommandData data = CommandCore::CommandData::Object();
        data.Set("name", CommandCore::CommandData::String(objectName));
        return CommandCore::Ok("선택: " + objectName, std::move(data));
    }

    static void Cmd_component_add(const ConsoleCommandContext& ctx)
    {
        const std::vector<std::string>& parts = ctx.parts;
        const std::string& cmd = ctx.cmd;

        if (parts.size() < 3)
        {
            std::printf("[CLI] 사용법: component.add <오브젝트 이름> <컴포넌트 타입>\n");
            return;
        }

        Scene* scene = SceneManagers->GetActiveScene();
        if (!scene) { std::printf("[CLI] 활성 씬 없음\n"); return; }

        // 오브젝트 이름에 공백이 흔하므로 컴포넌트 타입을 마지막 토큰으로 본다.
        const auto names = CommandCore::SplitTrailingName(parts, 1);
        const std::string& objectName = names.leading;
        const std::string& typeName = names.trailing;

        auto object = scene->GetEntity(objectName);
        if (!object)
        {
            std::printf("[CLI] 오브젝트를 찾을 수 없음: %s\n", objectName.c_str());
            return;
        }

        // 에디터의 Add Component 메뉴와 같은 경로.
        auto found = ComponentFactorys->m_componentTypes.find(typeName);
        if (found == ComponentFactorys->m_componentTypes.end() || nullptr == found->second)
        {
            std::printf("[CLI] 알 수 없는 컴포넌트 타입: %s\n", typeName.c_str());
            return;
        }

        auto component = object->AddComponent(*found->second);
        if (!component)
        {
            std::printf("[CLI] 컴포넌트 추가 실패\n");
            return;
        }

        if (auto* initializable = dynamic_cast<System::IInitializable*>(component))
        {
            initializable->Initialize();
        }

        Debug->LogWarning("[CLI] 컴포넌트 추가: " + objectName + " <- " + typeName);
        std::printf("[CLI] 컴포넌트 추가: %s <- %s\n", objectName.c_str(), typeName.c_str());
    }

    static void Cmd_prefab_instantiate(const ConsoleCommandContext& ctx)
    {
        const std::vector<std::string>& parts = ctx.parts;

        // 실제 게임 콘텐츠(프리팹)를 씬에 소환한다. UI 프리팹의 지연 연결 검증에
        // 필요해 추가했지만, 콘텐츠가 걸린 회귀라면 어디든 쓸 수 있다.
        if (parts.size() < 2)
        {
            std::printf("[CLI] 사용법: prefab.instantiate <프리팹 이름> [인스턴스 이름]\n");
            return;
        }

        Prefab* prefab = PrefabUtilitys->LoadPrefab(parts[1]);
        if (nullptr == prefab)
        {
            Debug->LogError("[CLI] 프리팹을 찾을 수 없음: " + parts[1]);
            std::printf("[CLI] 프리팹을 찾을 수 없음: %s\n", parts[1].c_str());
            return;
        }

        const std::string instanceName = (parts.size() > 2) ? parts[2] : parts[1];
        Entity* instance = PrefabUtilitys->InstantiatePrefab(prefab, instanceName);
        if (nullptr == instance)
        {
            std::printf("[CLI] 인스턴스 생성 실패: %s\n", parts[1].c_str());
            return;
        }

        Debug->LogWarning("[CLI] 프리팹 소환: " + parts[1] + " -> " + instanceName);
        std::printf("[CLI] 프리팹 소환: %s -> %s (index=%d)\n",
            parts[1].c_str(), instanceName.c_str(), static_cast<int>(instance->m_index));
    }

    static void Cmd_prefab_overrides(const ConsoleCommandContext& ctx)
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
            return;
        }

        Scene* scene = SceneManagers->GetActiveScene();
        if (!scene) { std::printf("[CLI] 활성 씬 없음\n"); return; }

        const std::string objectName = CommandCore::JoinFrom(parts, 1);
        auto object = scene->GetEntity(objectName);
        if (!object)
        {
            Debug->LogError("[CLI] 오브젝트를 찾을 수 없음: " + objectName);
            std::printf("[CLI] 오브젝트를 찾을 수 없음: %s\n", objectName.c_str());
            return;
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
    }

    static void Cmd_prefab_update(const ConsoleCommandContext& ctx)
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
            return;
        }

        Scene* scene = SceneManagers->GetActiveScene();
        if (!scene) { std::printf("[CLI] 활성 씬 없음\n"); return; }

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
            return;
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
            return;
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
            return;
        }

		Prefab* prefab = PrefabUtilitys->CreatePrefab(source, prefabName);
        if (!prefab)
        {
            std::printf("[CLI] 프리팹 정의 생성 실패: %s\n", prefabName.c_str());
            return;
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
            return;
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
    }

    static void Cmd_prefab_status(const ConsoleCommandContext& ctx)
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
    }

	static void Cmd_prefab_corpus_digest(const ConsoleCommandContext& ctx)
	{
		// D2-d: 호출자가 지정한 9개 프리팹의 인스턴스 루트 identity와 활성 씬의
		// 전체 prefab identity/override multiset을 저장 전후 비교 가능한 digest로
		// 출력한다. instanceID나 scene index처럼 재로드에서 바뀔 수 있는 값은 섞지
		// 않는다.
		if (ctx.parts.size() < 3)
		{
			std::printf("[CLI] 사용법: prefab.corpus.digest <라벨> <프리팹 이름>...\n");
			EngineBootstrap::SetExitCode(6);
			return;
		}

		Scene* scene = SceneManagers->GetActiveScene();
		if (!scene)
		{
			std::printf("[CLI] 활성 씬 없음\n");
			EngineBootstrap::SetExitCode(6);
			return;
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
		if (!passed) EngineBootstrap::SetExitCode(6);
	}

    static void Cmd_camera_editor(const ConsoleCommandContext& ctx)
    {
        const std::vector<std::string>& parts = ctx.parts;

        // camera.editor match|follow on|follow off|status
        //
        // 씬 뷰와 게임 뷰가 서로 다른 시점이면 두 그림의 차이가 시점 탓인지
        // 렌더 탓인지 갈리지 않는다. 시점을 통일해 두면 남는 차이가 곧
        // 렌더 경로의 차이다.
        const std::string action = (parts.size() >= 2) ? parts[1] : "status";

        if (action == "match")
        {
            if (ConsoleCommandSystem::MatchEditorCameraToGameCamera())
            {
                std::printf("[CLI] camera.editor match — 게임 카메라 자세로 맞춤\n");
            }
            else
            {
                std::printf("[CLI] camera.editor match 실패: 게임 카메라가 없다\n");
            }
        }
        else if (action == "follow")
        {
            const std::string mode = (parts.size() >= 3) ? parts[2] : "on";
            ConsoleCommandSystem::SetEditorCameraFollowing(mode != "off" && mode != "0");
            // 켤 때 한 번 맞춰 둔다 — 다음 프레임을 기다리지 않고 바로 보인다.
            if (ConsoleCommandSystem::IsEditorCameraFollowing())
            {
                ConsoleCommandSystem::MatchEditorCameraToGameCamera();
            }
            std::printf("[CLI] camera.editor follow %s\n",
                ConsoleCommandSystem::IsEditorCameraFollowing() ? "on" : "off");
        }
        else
        {
            const Camera* editorCamera = EditorSessionState::Get().EditorCamera();
            Scene* activeScene = SceneManagers->GetActiveScene();
            CameraComponent* gameCamera = (nullptr != activeScene)
                ? activeScene->Cameras().GetPrimaryCamera() : nullptr;

            const auto describe = [](const char* label, uint64_t viewId,
                const FrameCameraSnapshot* snapshot)
            {
                if (nullptr == snapshot) { std::printf("  %s: 없음\n", label); return; }
                std::printf("  %s: view %llu · pos(%.3f %.3f %.3f)"
                    " · forward(%.3f %.3f %.3f) · fov %.1f\n",
                    label, static_cast<unsigned long long>(viewId),
                    snapshot->eyePosition.x, snapshot->eyePosition.y, snapshot->eyePosition.z,
                    snapshot->forward.x, snapshot->forward.y, snapshot->forward.z, snapshot->fov);
            };

            std::printf("[CLI] camera.editor status (follow %s)\n",
                ConsoleCommandSystem::IsEditorCameraFollowing() ? "on" : "off");
            const FrameCameraSnapshot editorSnapshot = (nullptr != editorCamera)
                ? editorCamera->CaptureFrameSnapshot() : FrameCameraSnapshot{};
            const FrameCameraSnapshot gameSnapshot = (nullptr != gameCamera)
                ? gameCamera->CaptureFrameSnapshot() : FrameCameraSnapshot{};
            describe("에디터", kEnhancedEditorViewId,
                nullptr != editorCamera ? &editorSnapshot : nullptr);
            describe("게임  ", nullptr != gameCamera ? gameCamera->GetInstanceID() : 0,
                nullptr != gameCamera ? &gameSnapshot : nullptr);
        }
    }

    static void Cmd_component_list(const ConsoleCommandContext& ctx)
    {
        const std::vector<std::string>& parts = ctx.parts;

        // 붙일 수 있는 컴포넌트 타입을 훑어본다(콜라이더 이름 확인용).
        const std::string filter = (parts.size() > 1) ? parts[1] : std::string{};

        int count = 0;
        for (const auto& [typeName, type] : ComponentFactorys->m_componentTypes)
        {
            if (!filter.empty() && typeName.find(filter) == std::string::npos) continue;

            Debug->LogWarning("[CLI]   " + typeName);
            ++count;
        }
        std::printf("[CLI] 컴포넌트 타입 %d개 기록\n", count);
    }

    static void Cmd_prefab_create(const ConsoleCommandContext& ctx)
    {
        const std::vector<std::string>& parts = ctx.parts;
        const std::string& cmd = ctx.cmd;

        if (parts.size() < 3)
        {
            std::printf("[CLI] 사용법: prefab.create <오브젝트 이름> <프리팹 이름>\n");
            return;
        }

        Scene* scene = SceneManagers->GetActiveScene();
        if (!scene) { std::printf("[CLI] 활성 씬 없음\n"); return; }

        // 오브젝트 이름에 공백이 흔하므로 프리팹 이름을 마지막 토큰으로 본다.
        // LC2: 원문 재해석 제거.
        const auto names = CommandCore::SplitTrailingName(parts, 1);
        const std::string& objectName = names.leading;
        const std::string& prefabName = names.trailing;

        auto object = scene->GetEntity(objectName);
        if (!object)
        {
            std::printf("[CLI] 오브젝트를 찾을 수 없음: %s\n", objectName.c_str());
            return;
        }

		Prefab* prefab = PrefabUtilitys->CreatePrefab(object, prefabName);
        if (!prefab)
        {
            std::printf("[CLI] 프리팹 생성 실패\n");
            return;
        }

        const file::path path = PathFinder::Relative("Prefabs\\") / (prefabName + ".prefab");
        if (!PrefabUtilitys->SavePrefab(prefab, path.string()))
        {
            std::printf("[CLI] 프리팹 저장 실패: %s\n", path.string().c_str());
            return;
        }

        Debug->LogWarning("[CLI] 프리팹 생성: " + prefabName + " <- " + objectName);
        std::printf("[CLI] 프리팹 생성: %s\n", path.string().c_str());
    }

    static void Cmd_play(const ConsoleCommandContext& ctx)
    {
        const std::string& cmd = ctx.cmd;

        // 에디터의 재생/정지 버튼과 같은 경로(SceneManager::Editor가 다음 프레임에 처리).
        SceneManagers->SetGameStart(cmd == "play");
        std::printf("[CLI] %s 요청\n", cmd.c_str());
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

    static void Cmd_undo_redo(const ConsoleCommandContext& ctx)
    {
        if (ctx.cmd == "undo") Meta::UndoManager::GetInstance()->Undo();
        else                   Meta::UndoManager::GetInstance()->Redo();
        std::printf("[CLI] %s 실행\n", ctx.cmd.c_str());
    }

    // 기존 object.create는 Undo 스택을 건드리지 않는다. 그 성질에 이미 여러 게이트가
    // 기대고 있으므로 바꾸지 않고, 이력을 쌓는 별도 명령을 둔다.

    static void Cmd_object_create_undoable(const ConsoleCommandContext& ctx)
    {
        const std::vector<std::string>& parts = ctx.parts;
        if (parts.size() < 2)
        {
            std::printf("[CLI] 사용법: object.create.undoable <이름>\n");
            return;
        }

        Scene* scene = SceneManagers->GetActiveScene();
        if (!scene) { std::printf("[CLI] 활성 씬 없음\n"); return; }

        Meta::UndoManager::GetInstance()->Execute(
            std::make_unique<Meta::CreateEntityCommand>(
                scene, parts[1], GameObjectType::Empty));
        std::printf("[CLI] undo 기록과 함께 생성: %s\n", parts[1].c_str());
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
        reg.Legacy({ "ai.status" }, &Cmd_ai_status);
        reg.Result({ "scene.save" }, &Cmd_scene_save);
        reg.Legacy({ "object.create" }, &Cmd_object_create);
        reg.Legacy({ "object.rename" }, &Cmd_object_rename);
        reg.Legacy({ "object.transform" }, &Cmd_object_transform);
        reg.Legacy({ "object.parent" }, &Cmd_object_parent);
        reg.Legacy({ "object.rootref" }, &Cmd_object_rootref);
        reg.Legacy({ "object.duplicate" }, &Cmd_object_duplicate);
        reg.Result({ "scene.hierarchycheck" }, &Cmd_scene_hierarchycheck);
        reg.Legacy({ "object.property" }, &Cmd_object_property);
        reg.Result({ "scene.select" }, &Cmd_scene_select);
        reg.Legacy({ "component.add" }, &Cmd_component_add);
        reg.Legacy({ "prefab.instantiate" }, &Cmd_prefab_instantiate);
        reg.Legacy({ "prefab.status" }, &Cmd_prefab_status);
        reg.Legacy({ "prefab.corpus.digest" }, &Cmd_prefab_corpus_digest);
        reg.Legacy({ "prefab.overrides" }, &Cmd_prefab_overrides);
        reg.Legacy({ "prefab.update" }, &Cmd_prefab_update);
        reg.Legacy({ "camera.editor" }, &Cmd_camera_editor);
        reg.Legacy({ "component.list" }, &Cmd_component_list);
        reg.Legacy({ "prefab.create" }, &Cmd_prefab_create);
        reg.Legacy({ "play", "stop" }, &Cmd_play);
        reg.Result({ "play.state" }, &Cmd_play_state);
        reg.Result({ "undo.state" }, &Cmd_undo_state);
        reg.Legacy({ "undo", "redo" }, &Cmd_undo_redo);
        reg.Legacy({ "object.create.undoable" }, &Cmd_object_create_undoable);
        reg.Result({ "scene.selection" }, &Cmd_scene_selection);
        reg.Result({ "scene.dump" }, &Cmd_scene_dump);
    }
}
