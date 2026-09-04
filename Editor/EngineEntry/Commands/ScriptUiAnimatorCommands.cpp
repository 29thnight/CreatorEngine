// LC6 (PHASE 14.5) — Script·UI·Animator 도메인 명령.
//
// `script.*` · `ui.*` · `animator.*`. 스크립트 리로드, UI 트리 조회·조작,
// 애니메이터 파라미터·상태 전이를 다룬다.
//
// §9 분류로는 Shared engine service 에 가깝다 — GUI 와 low-level 결과만 같으면
// 되고, Undo·selection 까지 같아야 하는 Shared Editor operation 은 아니다.
// 다만 그 표기는 descriptor 작업에서 전수로 한다.
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

namespace ConsoleCmd
{
	static void Cmd_animator_scene_probe(const ConsoleCommandContext&)
	{
		Animator source;
		source.AddParameter<float>("Speed", 1.5f, ValueType::Float);
		const auto controller = source.CreateController_UINoAni();
		controller->name = "Base Layer";
		AnimationState* anyState =
			controller->CreateState("Any State", -1, true);
		controller->CreateState("Idle", 0);
		controller->CreateState("Run", 1);
		controller->SetCurState("Idle");

		AniTransition* transition =
			controller->CreateTransition("Idle", "Run");
		if (transition)
		{
			transition->exitTime = 0.25f;
			transition->blendTime = 0.15f;
			transition->hasExitTime = true;
			TransCondition condition{
				0.5f, ConditionType::Greater, ValueType::Float };
			condition.valueName = "Speed";
			condition.valueParameter = source.Parameters.front();
			condition.m_ownerController = controller.get();
			transition->conditions.push_back(condition);
		}

		Authoring::WriteDocument first = Meta::SerializeDocument(&source);
		const std::string yaml = first.Dump();
		std::string parseError;
		auto parsed = Authoring::WriteDocument::ParseText(yaml, &parseError);

		bool stable = false;
		bool links = false;
		std::size_t states = 0;
		std::size_t transitions = 0;
		std::size_t conditions = 0;
		if (parsed)
		{
			Animator roundTrip;
			const Authoring::ReadNode roundTripNode = parsed->Root().Read();
			Meta::Deserialize(&roundTrip, roundTripNode);
			// ComponentFactory와 같은 순서: typed fields 뒤 component post-load.
			roundTrip.OnDeserialized(
				Authoring::NodeViewAccess::Make(roundTripNode));
			Authoring::WriteDocument second = Meta::SerializeDocument(&roundTrip);
			stable = Authoring::NodesEqual(
				first.Root().Read(), second.Root().Read());

			if (roundTrip.m_animationControllers.size() == 1
				&& roundTrip.Parameters.size() == 1)
			{
				const auto& restored = roundTrip.m_animationControllers.front();
				states = restored->StateVec.size();
				for (const auto& state : restored->StateVec)
				{
					transitions += state->Transitions.size();
					for (const auto& restoredTransition : state->Transitions)
						conditions += restoredTransition->conditions.size();
				}

				AnimationState* idle = restored->FindState("Idle");
				links = restored->m_owner == &roundTrip
					&& restored->m_curState == idle
					&& restored->m_anyState
					&& restored->m_anyState->m_isAny
					&& restored->m_anyState->m_name == "Any State"
					&& idle && idle->Transitions.size() == 1
					&& idle->Transitions.front()->curState == idle
					&& idle->Transitions.front()->nextState == restored->FindState("Run")
					&& idle->Transitions.front()->conditions.size() == 1
					&& idle->Transitions.front()->conditions.front().valueParameter
						== roundTrip.Parameters.front();
			}
		}

		const bool passed = parsed.has_value() && stable && links
			&& nullptr != anyState && controller->m_anyState.get() == anyState
			&& states == 3 && transitions == 1 && conditions == 1
			&& !yaml.empty();
		std::printf(
			"[animator.scene.probe] controllers=%zu parameters=%zu states=%zu "
			"transitions=%zu conditions=%zu yamlBytes=%zu stable=%d links=%d "
			"selfcheck=%s\n",
			source.m_animationControllers.size(), source.Parameters.size(), states,
			transitions, conditions, yaml.size(), stable ? 1 : 0, links ? 1 : 0,
			passed ? "pass" : "fail");
		if (!passed) EngineBootstrap::SetExitCode(5);
	}

	// 입력 액션맵은 맵마다 YAML `.inputmap` 하나다. 이름에 '.'이 든 맵과 실제
	// action/key payload가 저장·재기동 후 그대로 복원되는지를 함께 본다.
	// LC1 대표 selftest.
	//
	// 이 핸들러 하나에 §5.4 의 네 결과가 전부 들어 있어 이행 본보기로 골랐다 —
	// 사용법 오류(2) · 선행조건 불충족(3) · 판정 실패(4) · 성공(0). 예전에는
	// 넷 중 셋이 `printf` 뒤 `return` 이었고 판정 실패만 `SetExitCode(5)` 로
	// infrastructure 오류인 척했다.

    // ★ LC7: 결과형. 게이트가 "리로드 실패 뒤에도 스크립트가 붙는가"를 stdout 이
    //   아니라 값으로 물을 수 있어야 한다 — 서비스만 켠 실행에는 콘솔이 없다.
    static CommandCore::CommandResult Cmd_script_add(const ConsoleCommandContext& ctx)
    {
        const std::vector<std::string>& parts = ctx.parts;
        const std::string& cmd = ctx.cmd;

        if (parts.size() < 3)
        {
            std::printf("[CLI] 사용법: script.add <오브젝트 이름> <스크립트 타입>\n");
            return CommandCore::InvalidArguments(
                "script.add: <오브젝트 이름> <스크립트 타입> 이 필요하다");
        }

        Scene* scene = SceneManagers->GetActiveScene();
        if (!scene)
        {
            std::printf("[CLI] 활성 씬 없음\n");
            return CommandCore::PreconditionFailed("scene.none", "활성 씬이 없다");
        }

        // 오브젝트 이름에 공백이 흔하다("Main Camera"). 타입은 마지막 토큰으로 보고,
        // 그 앞 전체를 이름으로 취급한다.
        const auto names = CommandCore::SplitTrailingName(parts, 1);
        const std::string& objectName = names.leading;
        const std::string& typeName = names.trailing;

        auto object = scene->GetEntity(objectName);
        if (!object)
        {
            Debug->LogError("[스크립트] 오브젝트를 찾을 수 없음: " + objectName);
            std::printf("[CLI] 오브젝트를 찾을 수 없음: %s\n", objectName.c_str());
            return CommandCore::PreconditionFailed(
                "object.not_found", "오브젝트를 찾을 수 없다: " + objectName);
        }

        // 한 오브젝트에 스크립트를 여럿 붙일 수 있어야 하므로 중복 허용 경로를 쓴다.
        // ScriptComponent가 Awake에서 관리 인스턴스를 만든다.
        const Meta::Type* scriptType = Meta::Find("ScriptComponent");
        if (nullptr == scriptType)
        {
            std::printf("[CLI] ScriptComponent 타입을 찾을 수 없음\n");
            return CommandCore::InternalError(
                "script.component_type_missing", "ScriptComponent 타입을 찾을 수 없다");
        }

        // K2 스테이지 A: AddComponentAllowMultiple가 raw Component*를 돌려준다 —
        // dynamic_pointer_cast(shared_ptr 전용) 대신 dynamic_cast.
        auto* script = dynamic_cast<ScriptComponent*>(
            object->AddComponentAllowMultiple(*scriptType));
        if (!script)
        {
            std::printf("[CLI] ScriptComponent 추가 실패\n");
            return CommandCore::InternalError(
                "script.component_add_failed", "ScriptComponent 를 추가하지 못했다");
        }

        // m_scriptType은 드레인보다 먼저 세워야 한다 — OnInitialized가 이 값을 보고
        // CreateBehaviour를 부른다(비어 있으면 그냥 돌아간다. ScriptComponent.cpp).
        script->m_scriptType = typeName;

        // (C2-2) 예전에는 여기서 script->OnInitialized()를 직접 불렀다("씬의 초기화
        // 단계는 이미 지나갔을 수 있으므로 여기서 직접 깨운다"). 하지만
        // AddComponentAllowMultiple 안의 AttachComponentLifecycle이 이미 이 컴포넌트를
        // PendingAwake 큐에 넣어 뒀고(State_AwakeCalled 비트는 아직 서지 않은 채),
        // 직접 부르면 그 비트를 세우지 않으므로 다음 프레임 Scene::RegistryDrainAwakeAndStart가
        // 큐에 남은 같은 컴포넌트를 또 한 번 깨운다 — OnInitialized 이중 호출.
        // ScriptComponent::OnInitialized의 `if (HasInstance()) return;` 가드가 보통은
        // 이걸 조용히 삼키지만, 그건 설계가 아니라 우연이다.
        //
        // Api_Prefab_Instantiate(ClrHost.cpp)가 쓰는 것과 같은 관용구로 고친다 — 부착
        // 직후 scene->DrainPendingLifecycle()을 동기로 불러 정상 드레인 경로를 태운다.
        // 이미 깨운 컴포넌트는 State_AwakeCalled로 건너뛰므로 씬 전체를 다시 돌아도 안전하다.
        scene->DrainPendingLifecycle();

        if (!script->HasInstance())
        {
            Debug->LogError("[스크립트] 부착 실패 — 타입=" + typeName);
            std::printf("[CLI] 스크립트 부착 실패 (타입=%s)\n", typeName.c_str());
            return CommandCore::Fail("script.attach_failed",
                "스크립트 인스턴스를 만들지 못했다 — 타입=" + typeName
                + " (어셈블리가 올라와 있는지 script.status 로 확인할 것)");
        }

        const int id = script->GetInstanceId();
        Debug->LogWarning("[스크립트] " + objectName + " 에 " + typeName + " 부착 (id=" + std::to_string(id) + ")");
        std::printf("[CLI] 부착 완료: %s <- %s (id=%d)\n", objectName.c_str(), typeName.c_str(), id);

        CommandCore::CommandData data = CommandCore::CommandData::Object();
        data.Set("object", CommandCore::CommandData::String(objectName));
        data.Set("type",   CommandCore::CommandData::String(typeName));
        data.Set("instanceId", CommandCore::CommandData::Int(id));
        return CommandCore::Ok("부착 완료 " + objectName + " <- " + typeName,
                               std::move(data));
    }

    static void Cmd_script_fields(const ConsoleCommandContext& ctx)
    {
        const std::vector<std::string>& parts = ctx.parts;

        if (parts.size() < 2)
        {
            std::printf("[CLI] 사용법: script.fields <인스턴스 id>\n");
            return;
        }

        auto& clr = ClrHost::Get();
        const int id = std::atoi(parts[1].c_str());
        const int count = clr.GetFieldCount(id);

        Debug->LogWarning("[스크립트] 인스턴스 " + parts[1] + " 노출 필드 " + std::to_string(count) + "개");

        for (int i = 0; i < count; ++i)
        {
            const std::string name = clr.GetFieldName(id, i);
            const auto type = clr.GetFieldType(id, i);

            std::string value;
            switch (type)
            {
            case ClrHost::ScriptFieldType::Float:
                value = "float " + std::to_string(clr.GetFieldFloat(id, i));
                break;
            case ClrHost::ScriptFieldType::Int32:
                value = "int " + std::to_string(clr.GetFieldInt32(id, i));
                break;
            case ClrHost::ScriptFieldType::Bool:
                value = std::string("bool ") + (clr.GetFieldBool(id, i) ? "true" : "false");
                break;
            case ClrHost::ScriptFieldType::String:
                value = "string \"" + clr.GetFieldString(id, i) + "\"";
                break;
            case ClrHost::ScriptFieldType::Float2:
            {
                const auto v = clr.GetFieldFloat2(id, i);
                value = "float2 (" + std::to_string(v.x) + ", " + std::to_string(v.y) + ")";
                break;
            }
            case ClrHost::ScriptFieldType::Float3:
            {
                const auto v = clr.GetFieldFloat3(id, i);
                value = "float3 (" + std::to_string(v.x) + ", " + std::to_string(v.y) + ", " + std::to_string(v.z) + ")";
                break;
            }
            case ClrHost::ScriptFieldType::Object:
            {
                Entity* target = clr.GetFieldObject(id, i);
                value = std::string("object ") + (nullptr != target ? target->m_name.ToString() : "(없음)");
                break;
            }
            default:
                value = "(미지원 타입)";
                break;
            }

            Debug->LogWarning("[스크립트]   [" + std::to_string(i) + "] " + name + " = " + value);
        }
        std::printf("[CLI] 필드 %d개 기록\n", count);
    }

    static void Cmd_script_set(const ConsoleCommandContext& ctx)
    {
        const std::vector<std::string>& parts = ctx.parts;
        const std::string& cmd = ctx.cmd;

        if (parts.size() < 4)
        {
            std::printf("[CLI] 사용법: script.set <인스턴스 id> <필드 인덱스> <값>\n");
            return;
        }

        auto& clr = ClrHost::Get();
        const int id = std::atoi(parts[1].c_str());
        const int index = std::atoi(parts[2].c_str());

        // 값에 공백이 들어갈 수 있다(문자열·오브젝트 이름). 인덱스 뒤 전체를 값으로 본다.
        std::string rest = CommandCore::JoinFrom(parts, 1);
        rest = TrimLine(rest.substr(parts[1].size()));
        const std::string rawValue = TrimLine(rest.substr(parts[2].size()));

        switch (clr.GetFieldType(id, index))
        {
        case ClrHost::ScriptFieldType::Float:
            clr.SetFieldFloat(id, index, static_cast<float>(std::atof(rawValue.c_str())));
            break;
        case ClrHost::ScriptFieldType::Int32:
            clr.SetFieldInt32(id, index, std::atoi(rawValue.c_str()));
            break;
        case ClrHost::ScriptFieldType::Bool:
            clr.SetFieldBool(id, index, rawValue == "true" || rawValue == "1");
            break;
        case ClrHost::ScriptFieldType::String:
            clr.SetFieldString(id, index, rawValue);
            break;
        case ClrHost::ScriptFieldType::Float2:
        {
            ClrHost::ScriptFloat2 v{};
            sscanf_s(rawValue.c_str(), "%f,%f", &v.x, &v.y);
            clr.SetFieldFloat2(id, index, v);
            break;
        }
        case ClrHost::ScriptFieldType::Float3:
        {
            ClrHost::ScriptFloat3 v{};
            sscanf_s(rawValue.c_str(), "%f,%f,%f", &v.x, &v.y, &v.z);
            clr.SetFieldFloat3(id, index, v);
            break;
        }
        case ClrHost::ScriptFieldType::Object:
        {
            // 오브젝트 참조는 이름으로 지정한다("none"이면 비운다).
            Entity* target = nullptr;
            if (rawValue != "none" && !rawValue.empty())
            {
                if (Scene* activeScene = SceneManagers->GetActiveScene())
                {
                    auto found = activeScene->GetEntity(rawValue);
					target = found;
                }

                if (nullptr == target)
                {
                    std::printf("[CLI] 오브젝트를 찾을 수 없음: %s\n", rawValue.c_str());
                    return;
                }
            }
            clr.SetFieldObject(id, index, target);
            break;
        }
        default:
            std::printf("[CLI] 설정할 수 없는 필드입니다\n");
            return;
        }

        // 인스펙터 편집과 같은 취급 — 바뀐 값을 컴포넌트에 담아 직렬화 대상으로 만든다.
        if (Scene* scene = SceneManagers->GetActiveScene())
        {
            for (const auto& object : scene->m_Entities)
            {
                if (!object) continue;

                auto script = object->GetComponent<ScriptComponent>();
                if (nullptr != script && script->GetInstanceId() == id)
                {
                    script->CaptureFields();
                    break;
                }
            }
        }

        Debug->LogWarning("[스크립트] 필드 설정 — id=" + parts[1] + " [" + parts[2] + "] = " + parts[3]);
        std::printf("[CLI] 필드 설정 완료\n");
    }

    static void Cmd_ui_rect(const ConsoleCommandContext& ctx)
    {
        const std::vector<std::string>& parts = ctx.parts;

        // 오브젝트 이하 전체의 worldRect를 재귀로 찍는다. 이름 대신 *를 주면 씬의
        // 모든 RectTransform을 훑는다 — 해상도를 바꿔 가며, 또는 코드를 고치기
        // 전후로 같은 명령을 돌려 레이아웃 결과를 통째로 대조하기 위한 것이다(PHASE 7).
        if (parts.size() < 2)
        {
            std::printf("[CLI] 사용법: ui.rect <오브젝트 이름 | *>\n");
            return;
        }

        Scene* scene = SceneManagers->GetActiveScene();
        if (!scene) { std::printf("[CLI] 활성 씬 없음\n"); return; }

        // 재귀 깊이 상한 — 계층이 순환하더라도 CLI가 스택을 태우지 않게 한다.
        constexpr int kMaxDepth = 32;

        std::function<void(Entity*, int)> dump = [&](Entity* obj, int depth)
        {
            if (nullptr == obj || depth > kMaxDepth) return;

            if (auto* rect = obj->GetComponent<RectTransformComponent>())
            {
                const auto& world = rect->GetWorldRect();
                const auto& size = rect->GetSizeDelta();
                const auto& anchored = rect->GetAnchoredPosition();
                const auto screenPosition = rect->GetScreenPosition();
                const std::string line =
                    std::string(static_cast<size_t>(depth) * 2, ' ') + obj->m_name.ToString() +
                    " world(" + std::to_string(static_cast<int>(world.x)) + ", " +
                    std::to_string(static_cast<int>(world.y)) + ", " +
                    std::to_string(static_cast<int>(world.width)) + ", " +
                    std::to_string(static_cast<int>(world.height)) + ")" +
                    " sizeDelta(" + std::to_string(static_cast<int>(size.x)) + ", " +
                    std::to_string(static_cast<int>(size.y)) + ")" +
                    " anchor(" + std::to_string(rect->GetAnchorMin().x).substr(0, 4) + "," +
                    std::to_string(rect->GetAnchorMin().y).substr(0, 4) + "-" +
                    std::to_string(rect->GetAnchorMax().x).substr(0, 4) + "," +
                    std::to_string(rect->GetAnchorMax().y).substr(0, 4) + ")" +
                    " pos(" + std::to_string(static_cast<int>(anchored.x)) + ", " +
                    std::to_string(static_cast<int>(anchored.y)) + ")" +
                    " screen(" + std::to_string(static_cast<int>(screenPosition.x)) + ", " +
                    std::to_string(static_cast<int>(screenPosition.y)) + ")" +
                    " scale(" + std::to_string(rect->GetLayoutScale()).substr(0, 5) + ")";

                std::printf("[CLI] %s\n", line.c_str());
                Debug->LogWarning("[ui.rect] " + line);
            }

            for (auto childIndex : obj->GetChildrenIndices())
            {
                dump(obj->OwnerSceneFindIndex(childIndex), depth + 1);
            }
        };

        if (parts[1] == "*")
        {
            // 최상위는 "아무의 자식도 아닌 오브젝트"로 가린다. m_parentIndex만 보고
            // 판별했더니 프리팹 루트 밑의 캔버스가 최상위로도 잡혀 같은 서브트리가
            // 두 번 찍혔다 — 대조에서 개수가 정확히 두 배가 되어 드러났다.
            std::unordered_set<Entity::Index> childIndices;
            for (const auto& obj : scene->m_Entities)
            {
                if (!obj || obj->IsDestroyMark()) continue;
                for (auto childIndex : obj->GetChildrenIndices()) childIndices.insert(childIndex);
            }

            for (const auto& obj : scene->m_Entities)
            {
                if (!obj || obj->IsDestroyMark()) continue;
                if (childIndices.count(obj->m_index)) continue;
                dump(obj.get(), 0);
            }
        }
        else
        {
			Entity* target = scene->GetEntity(parts[1]);
            if (!target) { std::printf("[CLI] 오브젝트 없음: %s\n", parts[1].c_str()); return; }
            dump(target, 0);
        }
    }

    static void Cmd_ui_anchor(const ConsoleCommandContext& ctx)
    {
        const std::vector<std::string>& parts = ctx.parts;
        const std::string& cmd = ctx.cmd;

        // 스트레치 앵커처럼 저작 데이터에 없는 배치를 검증하려면 값을 직접 넣어 봐야 한다.
        // ui.anchor <오브젝트> <minX> <minY> <maxX> <maxY> / ui.size <오브젝트> <x> <y>
        // ui.pos <오브젝트> <x> <y>       — 앵커 기준 로컬 anchoredPosition
        // ui.screenpos <오브젝트> <x> <y> — 좌상단 원점 화면 픽셀 좌표
        const size_t needed = (cmd == "ui.anchor") ? 6 : 4;
        if (parts.size() < needed)
        {
            std::printf("[CLI] 사용법: ui.anchor <오브젝트> <minX> <minY> <maxX> <maxY>"
                " · ui.size <오브젝트> <x> <y> · ui.pos/ui.screenpos <오브젝트> <x> <y>\n");
            return;
        }

        Scene* scene = SceneManagers->GetActiveScene();
        if (!scene) { std::printf("[CLI] 활성 씬 없음\n"); return; }

		Entity* target = scene->GetEntity(parts[1]);
        if (!target) { std::printf("[CLI] 오브젝트 없음: %s\n", parts[1].c_str()); return; }

        auto* rect = target->GetComponent<RectTransformComponent>();
        if (!rect) { std::printf("[CLI] RectTransform 없음: %s\n", parts[1].c_str()); return; }

        if (cmd == "ui.anchor")
        {
            const math::vector2 min{ std::strtof(parts[2].c_str(), nullptr), std::strtof(parts[3].c_str(), nullptr) };
            const math::vector2 max{ std::strtof(parts[4].c_str(), nullptr), std::strtof(parts[5].c_str(), nullptr) };
            rect->SetAnchorMin(min);
            rect->SetAnchorMax(max);
            std::printf("[CLI] %s 앵커 = (%.2f,%.2f)-(%.2f,%.2f)\n",
                parts[1].c_str(), min.x, min.y, max.x, max.y);
        }
        else if (cmd == "ui.pos")
        {
            const math::vector2 pos{ std::strtof(parts[2].c_str(), nullptr),
                                      std::strtof(parts[3].c_str(), nullptr) };
            rect->SetAnchoredPosition(pos);
            std::printf("[CLI] %s anchoredPosition = (%.2f,%.2f)\n",
                parts[1].c_str(), pos.x, pos.y);
        }
        else if (cmd == "ui.screenpos")
        {
            const math::vector2 pos{ std::strtof(parts[2].c_str(), nullptr),
                                      std::strtof(parts[3].c_str(), nullptr) };
            rect->SetScreenPosition(pos);
            std::printf("[CLI] %s screenPosition = (%.2f,%.2f)\n",
                parts[1].c_str(), pos.x, pos.y);
        }
        else
        {
            const math::vector2 size{ std::strtof(parts[2].c_str(), nullptr), std::strtof(parts[3].c_str(), nullptr) };
            rect->SetSizeDelta(size);
            std::printf("[CLI] %s sizeDelta = (%.2f,%.2f)\n", parts[1].c_str(), size.x, size.y);
        }
    }

    static void Cmd_ui_hitbox(const ConsoleCommandContext& ctx)
    {

        // 버튼의 클릭 판정 상자를 rect와 나란히 찍는다. 두 값이 같아야 보이는 곳과
        // 눌리는 곳이 일치한다 — 해상도가 바뀌어도 유지되는지가 검증 대상이다(PHASE 7-7).
        Scene* scene = SceneManagers->GetActiveScene();
        if (!scene) { std::printf("[CLI] 활성 씬 없음\n"); return; }

        int reported = 0;
        for (const auto& owned : scene->m_Entities)
        {
            Entity* owner = owned.get();
            if (nullptr == owner || owner->IsDestroyMark()) continue;

            auto* button = owner->GetComponent<UIButton>();
            if (nullptr == button) continue;

            auto* rect = owner->GetComponent<RectTransformComponent>();
            if (nullptr == rect) continue;

            const auto& hitbox = button->GetHitbox();
            const auto& world = rect->GetWorldRect();
            const std::string line = owner->m_name.ToString() +
                " rect(" + std::to_string(static_cast<int>(world.x)) + ", " +
                std::to_string(static_cast<int>(world.y)) + ", " +
                std::to_string(static_cast<int>(world.width)) + ", " +
                std::to_string(static_cast<int>(world.height)) + ")" +
                " hitbox(" + std::to_string(static_cast<int>(hitbox.x)) + ", " +
                std::to_string(static_cast<int>(hitbox.y)) + ", " +
                std::to_string(static_cast<int>(hitbox.width)) + ", " +
                std::to_string(static_cast<int>(hitbox.height)) + ")";

            std::printf("[CLI] %s\n", line.c_str());
            Debug->LogWarning("[ui.hitbox] " + line);
            ++reported;
        }

        if (0 == reported) std::printf("[CLI] 버튼 없음\n");
    }

	static void Cmd_ui_navprobe(const ConsoleCommandContext&)
	{
		// U7/E7-c 전용 무자산 회귀. 형제+손자 경로를 가진 UI 프리팹을 메모리에서
		// 굽고 두 번 소환해, Navigation이 각 인스턴스 내부에서만 풀리는지와 UI도
		// 매번 새 instanceID를 받는지 함께 본다. 이어서 같은 데이터를 구 navObject
		// 형식으로 되돌려 인메모리 승격 경로도 태운다.
		Scene* scene = SceneManagers->GetActiveScene();
		if (!scene)
		{
			std::printf("[ui.navprobe] FAIL 활성 씬 없음\n");
			return;
		}

		auto authorRoot = scene->CreateEntity("__NavAuthorRoot", GameObjectType::Canvas);
		if (!authorRoot)
		{
			std::printf("[ui.navprobe] FAIL 저작 계층 생성 실패\n");
			return;
		}
		auto source = scene->CreateEntity("__NavSource", GameObjectType::UI, authorRoot->m_index);
		auto branch = scene->CreateEntity("__NavBranch", GameObjectType::UI, authorRoot->m_index);
		if (!source || !branch)
		{
			std::printf("[ui.navprobe] FAIL 저작 계층 생성 실패\n");
			return;
		}
		auto target = scene->CreateEntity("__NavTarget", GameObjectType::UI, branch->m_index);
		if (!target)
		{
			std::printf("[ui.navprobe] FAIL 저작 계층 생성 실패\n");
			return;
		}

		ImageComponent* sourceImage = source->AddComponent<ImageComponent>();
		target->AddComponent<ImageComponent>();
		if (!sourceImage)
		{
			std::printf("[ui.navprobe] FAIL ImageComponent 생성 실패\n");
			return;
		}
		sourceImage->SetNavi(Direction::Right, target);

		auto makeSequenceData = [](Prefab* prefab)
		{
			Authoring::WriteDocument data;
			const Authoring::ReadNode authored = prefab->GetPrefabData();
			if (authored.IsSequence())
				data.Root().Assign(authored);
			else
				data.Root().Append().Assign(authored);
			const Authoring::ReadNode dataView = data.Root().Read();
			prefab->SetPrefabData(Authoring::NodeViewAccess::Make(dataView));
		};

		Prefab* prefab = PrefabUtilitys->CreatePrefab(authorRoot, "__NavProbePrefab");
		if (!prefab)
		{
			std::printf("[ui.navprobe] FAIL 프리팹 생성 실패\n");
			return;
		}
		makeSequenceData(prefab);

		const std::string serialized = prefab->GetPrefabData().Dump();
		const bool schemaOk = serialized.find("navObject") == std::string::npos
			&& serialized.find("parentHops") != std::string::npos
			&& serialized.find("childOrdinals") != std::string::npos
			&& serialized.find("m_gameObjectType") == std::string::npos;

		auto resolveInstance = [scene](Entity* root, Entity*& outSource, Entity*& outTarget,
			ImageComponent*& outSourceImage) -> bool
		{
			outSource = nullptr;
			outTarget = nullptr;
			outSourceImage = nullptr;
			if (!root || root->GetChildrenIndices().size() < 2) return false;

			outSource = scene->TryGetEntity(root->GetChildrenIndices()[0]);
			Entity* instanceBranch = scene->TryGetEntity(root->GetChildrenIndices()[1]);
			if (!outSource || !instanceBranch || instanceBranch->GetChildrenIndices().empty()) return false;
			outTarget = scene->TryGetEntity(instanceBranch->GetChildrenIndices()[0]);
			outSourceImage = outSource ? outSource->GetComponent<ImageComponent>() : nullptr;
			if (!outTarget || !outSourceImage) return false;
			outSourceImage->DeserializeNavi();
			return outSourceImage->GetNextNavi(Direction::Right) == outTarget;
		};

		Entity* instanceA = prefab->Instantiate(scene, "__NavInstanceA");
		Entity* instanceB = prefab->Instantiate(scene, "__NavInstanceB");
		Entity* sourceA = nullptr; Entity* targetA = nullptr; ImageComponent* imageA = nullptr;
		Entity* sourceB = nullptr; Entity* targetB = nullptr; ImageComponent* imageB = nullptr;
		const bool instanceAOk = resolveInstance(instanceA, sourceA, targetA, imageA);
		const bool instanceBOk = resolveInstance(instanceB, sourceB, targetB, imageB);
		const bool isolated = instanceAOk && instanceBOk
			&& imageA->GetNextNavi(Direction::Right) == targetA
			&& imageB->GetNextNavi(Direction::Right) == targetB
			&& targetA != targetB;
		const bool freshIds = sourceA && sourceB && targetA && targetB
			&& sourceA->GetInstanceID() != sourceB->GetInstanceID()
			&& targetA->GetInstanceID() != targetB->GetInstanceID();
		const bool spatialComposition = instanceA
			&& instanceA->GetComponent<Transform>()
			&& instanceA->GetComponent<RectTransformComponent>()
			&& sourceA && !sourceA->GetComponent<Transform>()
			&& sourceA->GetComponent<RectTransformComponent>();

		// 구 navObject 파일 승격: 새 경로 필드를 지우고 저작 대상의 옛 ID를 넣는다.
		Authoring::WriteDocument legacyDocument;
		legacyDocument.Root().Assign(prefab->GetPrefabData());
		const Authoring::WriteNode sourceComponents = legacyDocument.Root().At(0)
			.Child("children").At(0).Child("m_components");
		bool legacyFixtureBuilt = false;
		for (std::size_t componentIndex = 0;
			componentIndex < sourceComponents.Size(); ++componentIndex)
		{
			const Authoring::WriteNode navs =
				sourceComponents.At(componentIndex).Child("navigations");
			if (!navs.Read().IsSequence() || navs.Size() == 0) continue;
			const Authoring::WriteNode navigation = navs.At(0);
			navigation.Child("navObject").SetScalar(target->GetInstanceID());
			navigation.RemoveChild("parentHops");
			navigation.RemoveChild("childOrdinals");
			legacyFixtureBuilt = true;
			break;
		}

		Prefab* legacyPrefab = PrefabUtilitys->CreatePrefab(authorRoot, "__NavLegacyProbePrefab");
		const Authoring::ReadNode legacyView = legacyDocument.Root().Read();
		if (legacyPrefab) legacyPrefab->SetPrefabData(
			Authoring::NodeViewAccess::Make(legacyView));
		Entity* legacyInstance = legacyPrefab ? legacyPrefab->Instantiate(scene, "__NavLegacyInstance") : nullptr;
		Entity* legacySource = nullptr; Entity* legacyTarget = nullptr; ImageComponent* legacyImage = nullptr;
		const bool legacyOk = legacyFixtureBuilt
			&& resolveInstance(legacyInstance, legacySource, legacyTarget, legacyImage);

		const bool passed = schemaOk && isolated && freshIds && spatialComposition && legacyOk;
		std::printf("[ui.navprobe] %s schema=%s isolated=%s freshIds=%s spatial=%s legacy=%s\n",
			passed ? "PASS" : "FAIL",
			schemaOk ? "PASS" : "FAIL",
			isolated ? "PASS" : "FAIL",
			freshIds ? "PASS" : "FAIL",
			spatialComposition ? "PASS" : "FAIL",
			legacyOk ? "PASS" : "FAIL");
	}

    static void Cmd_ui_status(const ConsoleCommandContext& ctx)
    {
        // 지연 연결 상태를 숫자로 본다. 레지스트리 등록 수와 그중 캔버스가 연결된 수,
        // 그리고 씬의 캔버스 목록 — 검증에서 눈으로 대조할 기준선이다.
        Scene* scene = SceneManagers->GetActiveScene();
        if (!scene) { std::printf("[CLI] 활성 씬 없음\n"); return; }

        int imageLinked = 0;
        for (auto* image : UIManagers->Images) { if (image && image->GetOwnerCanvas()) ++imageLinked; }
        int textLinked = 0;
        for (auto* text : UIManagers->Texts) { if (text && text->GetOwnerCanvas()) ++textLinked; }
        int spriteLinked = 0;
        for (auto* sprite : UIManagers->SpriteSheets) { if (sprite && sprite->GetOwnerCanvas()) ++spriteLinked; }

        // 캔버스별 소속 UI 수까지 보여 준다 — 오연결(엉뚱한 캔버스에 붙음)은
        // 총합만 봐서는 안 보이고, 캔버스별 분포가 어긋나야 드러난다.
        std::string canvasNames;
        for (const auto& canvasHandle : scene->GetCanvases())
        {
            // 캔버스 캐시는 핸들이다(트랙 E5-R2) — 씬에서 떠난 것은 여기서 걸러진다.
            if (Entity* canvas = scene->Resolve(canvasHandle))
            {
                if (!canvasNames.empty()) canvasNames += ", ";
                canvasNames += canvas->m_name.ToString();

                if (Canvas* canvasComponent = canvas->GetComponent<Canvas>())
                {
                    canvasNames += "(" + std::to_string(canvasComponent->UIObjs.size()) + ")";
                }
            }
        }

        char line[512]{};
        std::snprintf(line, sizeof(line),
            "[UI 상태] Image %d/%zu 연결 · Text %d/%zu 연결 · Sprite %d/%zu 연결 · 캔버스 %zu개 [%s]",
            imageLinked, UIManagers->Images.size(),
            textLinked, UIManagers->Texts.size(),
            spriteLinked, UIManagers->SpriteSheets.size(),
            scene->GetCanvases().size(), canvasNames.c_str());

        Debug->LogWarning(line);
        std::printf("%s\n", line);
    }

    static void Cmd_animator_state(const ConsoleCommandContext& ctx)
    {
        const std::vector<std::string>& parts = ctx.parts;
        const std::string& cmd = ctx.cmd;

        // 애니메이션 상태와 거기 붙는 스크립트는 원래 컨트롤러 편집기에서 만든다.
        // 상태 스크립트 바인딩을 검증할 방법이 없으므로 편집기가 하는 일을 CLI로 대신한다.
        if (parts.size() < 4)
        {
            std::printf("[CLI] 사용법: animator.state <오브젝트 이름> <상태 이름> <스크립트 타입>\n");
            return;
        }

        Scene* scene = SceneManagers->GetActiveScene();
        if (!scene) { std::printf("[CLI] 활성 씬 없음\n"); return; }

        const auto names = CommandCore::SplitTrailingName(parts, 1, 2);
        const std::string& objectName = names.leading;
        const std::string& stateName = parts[parts.size() - 2];
        const std::string& behaviourName = names.trailing;

        auto object = scene->GetEntity(objectName);
        if (!object) { std::printf("[CLI] 오브젝트를 찾을 수 없음: %s\n", objectName.c_str()); return; }

        Animator* animator = object->GetComponent<Animator>();
        if (nullptr == animator) { std::printf("[CLI] Animator가 없음: %s\n", objectName.c_str()); return; }

        if (animator->m_animationControllers.empty())
        {
            animator->CreateController("CliController");
        }

        auto controller = animator->m_animationControllers.front();
        if (!controller) { std::printf("[CLI] 컨트롤러 생성 실패\n"); return; }

        AnimationState* state = controller->CreateState(stateName, -1);
        if (nullptr == state) { std::printf("[CLI] 상태 생성 실패: %s\n", stateName.c_str()); return; }

        state->SetBehaviour(behaviourName);
        if (nullptr == state->behaviour)
        {
            Debug->LogError("[CLI] 상태 스크립트를 찾을 수 없음: " + behaviourName);
            std::printf("[CLI] 상태 스크립트를 찾을 수 없음: %s\n", behaviourName.c_str());
            return;
        }

        // 현재 상태로 만들어 두면 다음 프레임부터 Update가 돈다.
        // (전이 조건을 CLI로 짜기는 과하므로, 진입은 여기서 직접 흉내 낸다)
        controller->m_curState = state;
        state->behaviour->Enter();

        Debug->LogWarning("[CLI] 애니메이션 상태 추가: " + objectName + " · " + stateName + " <- " + behaviourName);
        std::printf("[CLI] 애니메이션 상태 추가: %s · %s <- %s\n",
            objectName.c_str(), stateName.c_str(), behaviourName.c_str());
    }

    static void Cmd_animator_exit(const ConsoleCommandContext& ctx)
    {
        const std::vector<std::string>& parts = ctx.parts;
        const std::string& cmd = ctx.cmd;

        // 상태에서 빠져나가는 것까지 확인하려면 전이 조건을 짜야 하는데 CLI로는 과하다.
        // 상태 머신이 전이 때 하는 일(Exit 호출 + 현재 상태 비우기)만 흉내 낸다.
        if (parts.size() < 2)
        {
            std::printf("[CLI] 사용법: animator.exit <오브젝트 이름>\n");
            return;
        }

        Scene* scene = SceneManagers->GetActiveScene();
        if (!scene) { std::printf("[CLI] 활성 씬 없음\n"); return; }

        const std::string objectName = CommandCore::JoinFrom(parts, 1);
        auto object = scene->GetEntity(objectName);
        if (!object) { std::printf("[CLI] 오브젝트를 찾을 수 없음: %s\n", objectName.c_str()); return; }

        Animator* animator = object->GetComponent<Animator>();
        if (nullptr == animator || animator->m_animationControllers.empty())
        {
            std::printf("[CLI] Animator 또는 컨트롤러가 없음: %s\n", objectName.c_str());
            return;
        }

        auto controller = animator->m_animationControllers.front();
        if (!controller || nullptr == controller->m_curState)
        {
            std::printf("[CLI] 현재 상태 없음\n");
            return;
        }

        if (controller->m_curState->behaviour) controller->m_curState->behaviour->Exit();
        controller->m_curState = nullptr;

        Debug->LogWarning("[CLI] 애니메이션 상태 종료: " + objectName);
        std::printf("[CLI] 애니메이션 상태 종료: %s\n", objectName.c_str());
    }

    static void Cmd_animator_param(const ConsoleCommandContext& ctx)
    {
        const std::vector<std::string>& parts = ctx.parts;
        const std::string& cmd = ctx.cmd;

        // 애니메이터 파라미터는 원래 컨트롤러 편집기에서 선언한다. 스크립트에서는 만들 수 없어
        // 바인딩을 검증할 방법이 없으므로, 편집기가 하는 일을 CLI로 대신한다.
        if (parts.size() < 4)
        {
            std::printf("[CLI] 사용법: animator.param <오브젝트 이름> <파라미터 이름> <bool|float|int|trigger>\n");
            return;
        }

        Scene* scene = SceneManagers->GetActiveScene();
        if (!scene) { std::printf("[CLI] 활성 씬 없음\n"); return; }

        // 오브젝트 이름에 공백이 흔하므로 뒤의 두 토큰을 파라미터 이름·타입으로 본다.
        const auto names = CommandCore::SplitTrailingName(parts, 1, 2);
        const std::string& objectName = names.leading;
        const std::string& paramName = parts[parts.size() - 2];
        const std::string& typeName = names.trailing;

        auto object = scene->GetEntity(objectName);
        if (!object)
        {
            std::printf("[CLI] 오브젝트를 찾을 수 없음: %s\n", objectName.c_str());
            return;
        }

        Animator* animator = object->GetComponent<Animator>();
        if (nullptr == animator)
        {
            std::printf("[CLI] Animator가 없음: %s\n", objectName.c_str());
            return;
        }

        if ("bool" == typeName)         animator->AddParameter(paramName, false, ValueType::Bool);
        else if ("float" == typeName)   animator->AddParameter(paramName, 0.f,   ValueType::Float);
        else if ("int" == typeName)     animator->AddParameter(paramName, 0,     ValueType::Int);
        else if ("trigger" == typeName) animator->AddParameter(paramName, false, ValueType::Trigger);
        else
        {
            std::printf("[CLI] 알 수 없는 파라미터 타입: %s\n", typeName.c_str());
            return;
        }

        Debug->LogWarning("[CLI] 애니메이터 파라미터 추가: " + objectName + " <- " + paramName + " (" + typeName + ")");
        std::printf("[CLI] 애니메이터 파라미터 추가: %s <- %s (%s)\n", objectName.c_str(), paramName.c_str(), typeName.c_str());
    }

    // ★ LC7: 결과를 값으로 낸다.
    //
    //   §10.2 가 요구하는 것은 "복원 수/전체, 실패 목록, 이전 컨텍스트 잔존" 이다.
    //   지금까지는 printf 한 줄이라, 라이브 코드 교체를 자동화하는 쪽이 성공했는지
    //   알려면 stdout 을 긁어야 했다 — 서비스만 켠 실행에는 콘솔이 없어 그마저도
    //   불가능했다.
    static CommandCore::CommandResult Cmd_script_reload(const ConsoleCommandContext& ctx)
    {
        auto& clr = ClrHost::Get();
        if (!clr.IsReady())
        {
            std::printf("[CLI] CLR이 준비되지 않았습니다\n");
            return CommandCore::PreconditionFailed(
                "script.clr_not_ready", "CLR 이 준비되지 않았다");
        }

        Scene* scene = SceneManagers->GetActiveScene();
        std::vector<ScriptComponent*> scripts;

        // 1) 값을 챙기고 인스턴스 참조를 끊는다. 하나라도 남으면 언로드가 실패한다.
        if (scene)
        {
            for (const auto& object : scene->m_Entities)
            {
                if (!object) continue;

                auto script = object->GetComponent<ScriptComponent>();
                if (nullptr != script)
                {
                    script->PrepareForReload();
                    scripts.push_back(script);
                }
            }
        }

        // 2) 어셈블리 교체
        if (!clr.ReloadScripts())
        {
            Debug->LogError("[스크립트] 리로드 실패");
            std::printf("[CLI] 리로드 실패\n");

            // ★ 실패해도 **이전 어셈블리는 그대로다**(LC7).
            //
            //   관리 쪽 `Reload()` 가 갈아 끼우기 전에 새 것을 버리는 컨텍스트에서
            //   검증한다. 예전에는 `Unload(); Load();` 라 실패하면 스크립트가 하나도
            //   남지 않았고, 그 상태를 호출자가 알 방법도 없었다. 이제 실패는
            //   실패로만 끝나고, 끊어 둔 인스턴스는 아래에서 되살린다.
            int recovered = 0;
            for (ScriptComponent* script : scripts)
            {
                script->OnInitialized();
                if (script->HasInstance()) ++recovered;
            }

            CommandCore::CommandData failData = CommandCore::CommandData::Object();
            failData.Set("restored", CommandCore::CommandData::Int(recovered));
            failData.Set("total", CommandCore::CommandData::Int(
                static_cast<int64_t>(scripts.size())));
            failData.Set("previousAssemblyKept", CommandCore::CommandData::Bool(true));
            return CommandCore::Fail("script.reload_failed",
                "새 어셈블리를 올리지 못했다 — 이전 어셈블리를 유지한다",
                std::move(failData));
        }

        // 3) 인스턴스를 다시 만들고 챙겨 둔 값을 되돌린다
        int restored = 0;
        CommandCore::CommandData failedList = CommandCore::CommandData::Array();
        for (ScriptComponent* script : scripts)
        {
            script->OnInitialized();
            if (script->HasInstance()) { ++restored; continue; }

            // 복원되지 못한 것을 **이름으로** 낸다. 수만 내면 무엇이 빠졌는지
            // 알 수 없고, 라이브 교체에서 알아야 할 것이 정확히 그것이다.
            const Entity* owner = script->GetOwner();
            failedList.Append(CommandCore::CommandData::String(
                (nullptr != owner) ? owner->GetHashedName().ToString()
                                   : std::string("(주인 없음)")));
        }

        // 언로드 완료 여부는 여기서 묻지 않는다. 리로드 호출 스택이 아직 살아 있어
        // 항상 "잔존"으로 나온다. 몇 프레임 뒤 script.status로 확인할 것.
        Debug->LogWarning("[스크립트] 리로드 완료 — 복원 " + std::to_string(restored) + "/" +
            std::to_string(scripts.size()));
        std::printf("[CLI] 리로드 완료: %d/%zu 복원 (언로드 확인은 script.status)\n",
            restored, scripts.size());

        CommandCore::CommandData data = CommandCore::CommandData::Object();
        data.Set("restored", CommandCore::CommandData::Int(restored));
        data.Set("total", CommandCore::CommandData::Int(
            static_cast<int64_t>(scripts.size())));
        data.Set("failed", std::move(failedList));

        // ★ 이전 컨텍스트 잔존 여부는 **여기서 내지 않는다.**
        //
        //   §10.2 는 그것을 `data` 로 내라고 했지만, 이 시점의 값은 뜻이 없다 —
        //   리로드를 부른 호출 스택이 아직 살아 있어 **항상 "잔존"** 이다. 뜻 없는
        //   값을 필드로 내면 소비자가 그것을 믿고 판단한다. 몇 프레임 뒤에 물을 수
        //   있게 `script.status` 가 그 값을 내고, 그쪽이 답할 수 있는 자리다.
        const bool allRestored = (restored == static_cast<int>(scripts.size()));
        if (!allRestored)
        {
            return CommandCore::Fail("script.reload_partial",
                "리로드는 됐으나 인스턴스 복원이 " + std::to_string(restored) + "/"
                + std::to_string(scripts.size()) + " 다", std::move(data));
        }
        return CommandCore::Ok("리로드 완료 " + std::to_string(restored) + "/"
            + std::to_string(scripts.size()), std::move(data));
    }

    // ★ LC7: 이전 컨텍스트 잔존 여부가 **여기서** 뜻을 갖는다.
    //
    //   리로드 직후에는 호출 스택이 살아 있어 항상 "잔존" 이다. 몇 프레임 지난
    //   뒤 이 명령으로 물어야 참이 판정된다. 그래서 §10.2 가 요구한 그 값을
    //   `script.reload` 가 아니라 이쪽이 낸다.
    static CommandCore::CommandResult Cmd_script_status(const ConsoleCommandContext& ctx)
    {
        auto& clr = ClrHost::Get();
        const bool stale = clr.IsPreviousContextAlive();

        Debug->LogWarning(std::string("[스크립트] CLR ") + (clr.IsReady() ? "준비됨" : "비활성") +
            " · 활성 스크립트 " + std::to_string(clr.LastActiveCount()) + "개" +
            " · 이전 어셈블리 " + (stale ? "잔존(참조 누수)" : "정리됨"));
        std::printf("[CLI] CLR %s, 활성 스크립트 %d개, 이전 어셈블리 %s\n",
            clr.IsReady() ? "준비됨" : "비활성", clr.LastActiveCount(),
            stale ? "잔존(참조 누수)" : "정리됨");

        CommandCore::CommandData data = CommandCore::CommandData::Object();
        data.Set("ready", CommandCore::CommandData::Bool(clr.IsReady()));
        data.Set("activeScripts", CommandCore::CommandData::Int(clr.LastActiveCount()));
        data.Set("previousContextAlive", CommandCore::CommandData::Bool(stale));
        return CommandCore::Ok("script status", std::move(data));
    }

    void RegisterScriptUiAnimatorCommands(Registrar& reg)
    {
        reg.Legacy({ "animator.scene.probe" }, &Cmd_animator_scene_probe);
        reg.Result({ "script.add" }, &Cmd_script_add);
        reg.Legacy({ "script.fields" }, &Cmd_script_fields);
        reg.Legacy({ "script.set" }, &Cmd_script_set);
        reg.Legacy({ "ui.rect" }, &Cmd_ui_rect);
        reg.Legacy({ "ui.anchor", "ui.size", "ui.pos", "ui.screenpos" }, &Cmd_ui_anchor);
        reg.Legacy({ "ui.hitbox" }, &Cmd_ui_hitbox);
        reg.Legacy({ "ui.navprobe" }, &Cmd_ui_navprobe);
        reg.Legacy({ "ui.status" }, &Cmd_ui_status);
        reg.Legacy({ "animator.state" }, &Cmd_animator_state);
        reg.Legacy({ "animator.exit" }, &Cmd_animator_exit);
        reg.Legacy({ "animator.param" }, &Cmd_animator_param);
        reg.Result({ "script.reload" }, &Cmd_script_reload);
        reg.Result({ "script.status" }, &Cmd_script_status);
    }
}
