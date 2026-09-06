#include "../EditorDiagnostics.h"
#include "../EditorProjectOperations.h"
// LC6 (PHASE 14.5) — AssetAuthoring 도메인 명령.
//
// `assets.*` · `asset.*` · `material.*` · `model.*` · `terrain.*` · `foliage.*` ·
// `blackboard.*` · `bt.*` · `tag.*` · `inputmap.*` · `collisionmatrix.*` ·
// `experiment.*`. 자산을 만들고 굽고 신원을 검증한다.
//
// ★ `experiment.*` 30 개가 여기 들어온다. §12 의 도메인 목록에는 그 이름이
//   없지만, 하는 일이 자산 cook·identity 검증이라 자리는 여기다. 이름이 아니라
//   하는 일로 가른다 — RenderTest 를 접두사로 묶었다가 라이브 조정 명령이
//   격리 프로브와 같은 파일에 앉았던 것이 이 슬라이스의 교훈이다.
//
// ★★ `*.authoring.probe` 계열은 §9 의 **Raw fixture authoring** 이다.
//   회귀 fixture 를 만들려고 Undo 를 의도적으로 우회한다. 그 우회가 표기되지
//   않으면 일반 회귀와 섞여, GUI 동등성을 검사해야 할 것과 검사하면 안 되는
//   것을 가릴 수 없다. 표기는 descriptor 작업에서 전수로 한다.
//
// ── 이 이동에서 바꾸지 않은 것 ──────────────────────────────────────────
//
// 핸들러 본문과 서명 그대로다. 이동의 증거는 골든이 한 글자도 안 변하는 것
// 하나뿐이다(§12.3).
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
#include "AssetIdentity/AssetIdentitySelfTest.h"
#include "AssetIdentity/AssetSidecarSchemaSelfTest.h"
#include "AssetIdentity/ModelAssetGenerationSelfTest.h"
#include "AssetIdentity/SceneModelGenerationSelfTest.h"
#include "ExperimentParity/ExperimentVertexLayoutSelfTest.h" // RunModelRenderWiringSelfTest — assets.modelrender 가 쓴다
#include "ExperimentParity/ExperimentCookedSelfTest.h"
#include "ShaderMeta.h"
#include "ExperimentParity/ExperimentMaterialCookSelfTest.h"
#include "ExperimentParity/ExperimentSceneCookSelfTest.h"
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
    static CommandCore::CommandResult Cmd_model_load(const ConsoleCommandContext& ctx)
    {
        using namespace CommandCore;
        const std::vector<std::string>& parts = ctx.parts;
        const std::string& cmd = ctx.cmd;

        if (parts.size() < 2)
        {
            std::printf("[CLI] 사용법: model.load <모델 경로>\n");
            return InvalidArguments("model.load requires a model path");
        }

		// 경로에 공백이 들어갈 수 있으므로 명령어 뒤 전체를 경로로 본다.
		const std::string path = CommandCore::JoinFrom(parts, 1);
		const std::string modelName = file::path(path).stem().string();
		const std::shared_ptr<const assets::ModelAssetGeneration> previousGeneration =
			DataSystems->FindModelAssetGenerationByStem(modelName);
		const file::path imported = EditorAssetDatabase::Get().ImportSourceAsset(
			path, EditorAssetDatabase::ImportKind::Model);
		if (imported.empty())
		{
			std::printf("[CLI] 모델 임포트 실패: %s\n", path.c_str());
			return Fail("model.import_failed", "Model import failed: " + path);
		}
		const std::shared_ptr<const assets::ModelAssetGeneration> loadedGeneration =
			DataSystems->LoadModelAssetGenerationByPath(imported.string());
		if (!loadedGeneration)
		{
			std::printf("[CLI] 모델 generation 로드 실패: %s\n", imported.string().c_str());
			return Fail("model.load_failed", "Model generation load failed: " + imported.string());
		}
		const char* cacheResult = previousGeneration &&
			previousGeneration != loadedGeneration ? "reloaded" : "loaded";
		std::printf("[CLI] 모델 임포트 및 로드 요청: %s (runtime-cache=%s)\n",
			imported.string().c_str(), cacheResult);
        auto data = CommandData::Object();
        data.Set("path", CommandData::String(imported.string()));
        data.Set("cache", CommandData::String(cacheResult));
        return Ok({}, std::move(data));
    }

    static CommandCore::CommandResult Cmd_terrain_authoring_probe(const ConsoleCommandContext& ctx)
    {
        using namespace CommandCore;
        if (ctx.parts.size() != 3) return InvalidArguments("terrain.authoring.probe <name> <texture|->");
        if (ctx.parts.size() < 3)
        {
            std::printf("[terrain.authoring.probe] usage: <name> <texture|->\n");
            return InvalidArguments("terrain.authoring.probe <name> <texture|->");
        }

        TerrainAuthoringRequest request{};
        request.destinationDirectory = PathFinder::Relative("Terrain");
        request.name = StringToWstring(ctx.parts[1]);
        request.terrainId = 73;
        request.width = 2;
        request.height = 2;
        request.minHeight = -4.0f;
        request.maxHeight = 8.0f;
        request.heightMap = { -4.0f, 0.5f, 3.0f, 8.0f };

        TerrainAuthoringLayerSnapshot layer{};
        layer.layerId = 0;
        layer.name = "ProbeLayer";
        layer.diffuseTextureSource = ctx.parts[2] == "-"
            ? request.destinationDirectory / "__missing_terrain_probe__.png"
            : file::path(ctx.parts[2]);
        layer.tiling = 2.0f;
        layer.splatWeights = { 0.0f, 0.25f, 0.75f, 1.0f };
        request.layers.push_back(std::move(layer));

        TerrainAuthoringResult result{};
        const bool written = AssetAuthoringPort::WriteTerrain(request, result);
        bool roundTrip = false;
        std::size_t layers = 0;
        if (written)
        {
            TerrainComponent restored;
            roundTrip = restored.Load(result.descriptorPath.wstring());
            layers = restored.GetLayerCount().size();
            const float* heights = restored.GetHeightMap();
            roundTrip = roundTrip && restored.GetWidth() == 2
                && restored.GetHeight() == 2 && layers == 1 && heights
                && heights[0] == -4.0f && heights[1] == 0.5f
                && heights[2] == 3.0f && heights[3] == 8.0f
                && restored.m_trrainAssetGuid == result.guid;
        }
        std::printf(
            "[terrain.authoring.probe] %s path=%s guid=%s roundtrip=%s "
            "width=2 height=2 layers=%zu\n",
            written ? "committed" : "rejected",
            result.descriptorPath.string().c_str(),
            result.guid.ToString().c_str(), roundTrip ? "PASS" : "FAIL", layers);

        auto data = CommandData::Object();
        data.Set("written", CommandData::Bool(written));
        data.Set("roundTrip", CommandData::Bool(roundTrip));
        data.Set("path", CommandData::String(result.descriptorPath.string()));
        data.Set("layers", CommandData::Int(layers));
        return (ctx.parts[2] == "-" ? !written : written && roundTrip) ? Ok({}, std::move(data)) : Fail("terrain.authoring.failed", "Commandlet verification failed", std::move(data));
    }

	// Foliage 저작 트랜잭션을 실행 중인 Editor에서 그대로 태운다. escape 인자는
	// 목적지가 Foliage 루트를 벗어났을 때 거부되는지 보는 음성 경로다.

    static CommandCore::CommandResult Cmd_foliage_authoring_probe(const ConsoleCommandContext& ctx)
    {
        using namespace CommandCore;
        if (ctx.parts.size() < 2 || ctx.parts.size() > 3 || (ctx.parts.size() == 3 && ctx.parts[2] != "escape")) return InvalidArguments("foliage.authoring.probe <name> [escape]");
        if (ctx.parts.size() < 2)
        {
            std::printf("[foliage.authoring.probe] usage: <name> [escape]\n");
            return InvalidArguments("foliage.authoring.probe <name> [escape]");
        }

        const bool escape = ctx.parts.size() >= 3 && ctx.parts[2] == "escape";

        TextAssetAuthoringRequest request{};
        request.destinationDirectory = escape
            ? PathFinder::Relative("Terrain") : PathFinder::Relative("Foliage");
        request.name = StringToWstring(ctx.parts[1]);

        FoliageInstance source{};
        source.m_position = { 12.5f, 3.25f, -8.75f };
        source.m_rotation = { 15.f, 90.f, 270.f };
        source.m_scale = { 0.5f, 1.25f, 2.f };
        source.m_foliageTypeID = 7;
        source.m_isCulled = true;
        source.RebuildWorldMatrix();

        Authoring::WriteDocument assetDocument;
        const Authoring::WriteNode foliageAsset =
            assetDocument.Root().Child("FoliageAsset");
        foliageAsset.Child("Types").SetSequence();
        const Authoring::WriteNode instances = foliageAsset.Child("Instances");
        instances.SetSequence();
        Meta::SerializeInto(&source, Meta::TypeOf<FoliageInstance>(),
            instances.Append());
        request.payload = assetDocument.Dump();

        TextAssetAuthoringResult result{};
        const bool written = AssetAuthoringPort::WriteFoliage(request, result);
        bool schemaStable = false;
        bool roundTrip = false;
        bool derivedWorld = false;
        if (written)
        {
            try
            {
                std::string parseError;
                Authoring::ParsedDocument published =
                    Authoring::ParsedDocument::ParseFile(result.assetPath.string(), parseError);
                if (!published)
                    throw std::runtime_error(parseError);
                const Authoring::ReadNode publishedInstances =
                    published.Root()["FoliageAsset"]["Instances"];
                if (publishedInstances.IsSequence() && 1 == publishedInstances.Size())
                {
                    const Authoring::ReadNode instanceNode = publishedInstances.At(0);
                    schemaStable = 4 == instanceNode.Size() &&
                        instanceNode["m_position"] && instanceNode["m_rotation"] &&
                        instanceNode["m_scale"] && instanceNode["m_foliageTypeID"] &&
                        !instanceNode["m_isCulled"] && !instanceNode["m_worldMatrix"];

                    FoliageInstance loaded{};
                    Meta::Deserialize(&loaded, instanceNode);
                    roundTrip = loaded.m_position == source.m_position &&
                        loaded.m_rotation == source.m_rotation &&
                        loaded.m_scale == source.m_scale &&
                        loaded.m_foliageTypeID == source.m_foliageTypeID &&
                        !loaded.m_isCulled &&
                        loaded.m_worldMatrix == math::matrix4x4::identity();
                    loaded.RebuildWorldMatrix();
                    derivedWorld = math::near_equal(
                        loaded.m_worldMatrix, source.m_worldMatrix);
                }
            }
            catch (const std::exception&)
            {
                schemaStable = false;
                roundTrip = false;
                derivedWorld = false;
            }
        }

        const bool verified = written && schemaStable && roundTrip && derivedWorld;
        std::printf("[foliage.authoring.probe] %s path=%s guid=%s fields=%s "
            "roundtrip=%s derived=%s\n",
            written ? (verified ? "committed" : "invalid") : "rejected",
            result.assetPath.string().c_str(),
            result.guid.ToString().c_str(),
            schemaStable ? "4-runtime-absent" : "invalid",
            roundTrip ? "PASS" : "FAIL",
            derivedWorld ? "PASS" : "FAIL");

        auto data = CommandData::Object();
        data.Set("written", CommandData::Bool(written));
        data.Set("roundTrip", CommandData::Bool(roundTrip));
        data.Set("schemaStable", CommandData::Bool(schemaStable));
        data.Set("derivedWorld", CommandData::Bool(derivedWorld));
        data.Set("path", CommandData::String(result.assetPath.string()));
        return (escape ? !written : verified) ? Ok({}, std::move(data)) : Fail("foliage.authoring.failed", "Commandlet verification failed", std::move(data));
    }


	// D4: Animator controller graph의 유일한 영속 경로인 scene reflection YAML을
	// 실물 그래프로 왕복한다. JSON 파일이나 별도 controller writer는 관여하지 않는다.

	static CommandCore::CommandResult Cmd_inputmap_authoring_probe(const ConsoleCommandContext& ctx)
	{
		if (ctx.parts.size() < 2)
		{
			std::printf("[inputmap.authoring.probe] usage: <save|verify> <name>\n");
			return CommandCore::InvalidArguments(
				"inputmap.authoring.probe: <save|verify> <name> 가 필요하다",
				"inputmap.usage");
		}

		const std::string& action = ctx.parts[1];
		const std::string name = ctx.parts.size() >= 3 ? ctx.parts[2] : std::string{};

		if (action == "save")
		{
			ActionMap* map = InputActionManagers->AddActionMap(name);
			if (nullptr == map)
			{
				std::printf("[inputmap.authoring.probe] rejected no-map\n");
				return CommandCore::PreconditionFailed(
					"inputmap.map_unavailable",
					"inputmap.authoring.probe: action map 을 만들 수 없다: " + name);
			}
			InputAction* probeAction = map->AddAction();
			probeAction->actionName = "ProbeMove";
			probeAction->inputType = InputType::KeyBoard;
			probeAction->actionType = ActionType::Value;
			probeAction->keystate = KeyState::Released;
			probeAction->key = {
				static_cast<std::size_t>(KeyBoard::LeftArrow),
				static_cast<std::size_t>(KeyBoard::RightArrow),
				static_cast<std::size_t>(KeyBoard::DownArrow),
				static_cast<std::size_t>(KeyBoard::UpArrow) };
			probeAction->m_scriptName = "ProbeScript";
			probeAction->funName = "ProbeFunction";
			const bool saved = InputActionManagers->SaveMap(map);
			std::printf("[inputmap.authoring.probe] save=%s\n",
				saved ? "ok" : "failed");

			CommandCore::CommandData data = CommandCore::CommandData::Object();
			data.Set("action", CommandCore::CommandData::String("save"));
			data.Set("name",   CommandCore::CommandData::String(name));
			data.Set("saved",  CommandCore::CommandData::Bool(saved));
			if (!saved)
			{
				return CommandCore::Fail("inputmap.save_failed",
					"inputmap.authoring.probe: 저장 실패: " + name, std::move(data));
			}
			return CommandCore::Ok("inputmap 저장", std::move(data));
		}

		if (action == "verify")
		{
			InputActionManagers->LoadManager();
			size_t found = 0;
			size_t actionCount = 0;
			bool stable = false;
			for (ActionMap* map : InputActionManagers->m_actionMaps)
			{
				if (!map || map->m_name != name) continue;
				++found;
				actionCount = map->m_actions.size();
				if (actionCount == 1 && map->m_actions.front())
				{
					const InputAction* restored = map->m_actions.front();
					stable = restored->actionName == "ProbeMove"
						&& restored->inputType == InputType::KeyBoard
						&& restored->actionType == ActionType::Value
						&& restored->keystate == KeyState::Released
						&& restored->key == std::vector<std::size_t>{
							static_cast<std::size_t>(KeyBoard::LeftArrow),
							static_cast<std::size_t>(KeyBoard::RightArrow),
							static_cast<std::size_t>(KeyBoard::DownArrow),
							static_cast<std::size_t>(KeyBoard::UpArrow) }
						&& restored->m_scriptName == "ProbeScript"
						&& restored->funName == "ProbeFunction";
				}
			}
			std::printf(
				"[inputmap.authoring.probe] verify found=%zu actions=%zu stable=%d\n",
				found, actionCount, stable ? 1 : 0);

			CommandCore::CommandData data = CommandCore::CommandData::Object();
			data.Set("action",  CommandCore::CommandData::String("verify"));
			data.Set("name",    CommandCore::CommandData::String(name));
			data.Set("found",   CommandCore::CommandData::Int(static_cast<int64_t>(found)));
			data.Set("actions", CommandCore::CommandData::Int(static_cast<int64_t>(actionCount)));
			data.Set("stable",  CommandCore::CommandData::Bool(stable));

			// ★ 예전에는 `SetExitCode(5)` 였다.
			//
			//   5 는 §5.4 에서 infrastructure 오류다. 검사가 정직하게 "저장한
			//   것과 읽은 것이 다르다"를 판정한 것과 디스크가 죽은 것을 같은
			//   숫자로 알리면, 자동화가 재시도해서는 안 될 것을 재시도한다.
			//   판정 실패는 `Failed`(exit 4)다.
			if (found != 1 || !stable)
			{
				return CommandCore::Fail("inputmap.roundtrip_mismatch",
					"inputmap.authoring.probe: 왕복 결과가 저장한 것과 다르다",
					std::move(data));
			}
			return CommandCore::Ok("inputmap 왕복 일치", std::move(data));
		}

		std::printf("[inputmap.authoring.probe] unknown action %s\n", action.c_str());
		return CommandCore::InvalidArguments(
			"inputmap.authoring.probe: 알 수 없는 동작: " + action, "inputmap.unknown_action");
	}

    static CommandCore::CommandResult Cmd_inputmap_corpus_probe(const ConsoleCommandContext& ctx)
    {
        using namespace CommandCore;
        if (ctx.parts.size() != 1) return InvalidArguments("inputmap.corpus.probe");
        InputActionManagers->LoadManager();
        std::size_t maps = 0;
        std::size_t actions = 0;
        std::size_t keys = 0;
        std::size_t keyboard = 0;
        std::size_t gamepad = 0;
        std::size_t buttons = 0;
        std::size_t values = 0;
        std::size_t invalid = 0;
        std::unordered_set<std::string> names;
        for (const ActionMap* map : InputActionManagers->m_actionMaps)
        {
            if (!map || map->m_name.empty() || !names.insert(map->m_name).second)
            {
                ++invalid;
                continue;
            }
            ++maps;
            for (const InputAction* action : map->m_actions)
            {
                if (!action || action->actionName.empty() || action->key.empty()
                    || action->key.size() > 4)
                {
                    ++invalid;
                    continue;
                }
                ++actions;
                keys += action->key.size();
                if (action->inputType == InputType::KeyBoard) ++keyboard;
                else if (action->inputType == InputType::GamePad) ++gamepad;
                else ++invalid;
                if (action->actionType == ActionType::Button) ++buttons;
                else if (action->actionType == ActionType::Value) ++values;
                else ++invalid;
            }
        }

        const bool passed = maps > 0 && actions > 0 && keys > 0 && invalid == 0;
        std::printf(
            "[inputmap.corpus.probe] maps=%zu actions=%zu keys=%zu keyboard=%zu "
            "gamepad=%zu buttons=%zu values=%zu invalid=%zu selfcheck=%s\n",
            maps, actions, keys, keyboard, gamepad, buttons, values, invalid,
            passed ? "pass" : "fail");
        auto data = CommandData::Object();
        data.Set("maps", CommandData::Int(maps));
        data.Set("actions", CommandData::Int(actions));
        data.Set("keys", CommandData::Int(keys));
        data.Set("keyboard", CommandData::Int(keyboard));
        data.Set("gamepad", CommandData::Int(gamepad));
        data.Set("buttons", CommandData::Int(buttons));
        data.Set("values", CommandData::Int(values));
        data.Set("invalid", CommandData::Int(invalid));
        return passed ? Ok({}, std::move(data)) : Fail("inputmap.corpus.failed", "Commandlet verification failed", std::move(data));
    }

	// 태그 저작은 편집이 아니라 **종료 시 Finalize**가 디스크에 반영한다. 그 저장이
	// authoring handler 수명 창 안에서 일어나는지는 "추가하고 정상 종료 → 다시 켜서
	// 확인"으로만 증명된다 — 한 프로세스 안에서는 메모리 상태만 보게 된다.
	// SerializationPlan D3-b-L — ShaderMeta **읽기 경로**를 관측 가능하게 만든다.
	//
	// ★ 왜 새로 만들었나. 이 파서의 계약은 `dx12.selftest` 안에만 있다. 그런데 그것은
	//   회귀 세트(run-all)에 **없고**, 자기 하네스(`Invoke-DX12Validation.ps1`)는
	//   vcpkg baseline preflight에 막혀 지금 이 기계에서 돌지 않는다.
	//
	//   변이로 확인했다 — `ValidateMap`의 unknown-field 거부를 무력화하면
	//   `dx12.selftest`는 빨개지지만 `verify-experiment-asset-cooker`는 **초록이다.**
	//   즉 정기적으로 도는 게이트 중 이 경로를 지키는 것이 하나도 없다. ryml 이식
	//   **전에** 자를 먼저 세운다. TagManager에서 같은 순서를 놓쳐 저작 자산을 잃었다.
	//
	// ★ 두 방향을 함께 낸다. 실자산이 파싱되는지(수용)와 잘못된 문서가 거부되는지
	//   (거절). 저작 코퍼스는 전부 유효하므로 수용만 재면 **느슨해지는 이식**을
	//   원리적으로 못 잡는다 — 그리고 그것이 backend 교체에서 가장 흔한 실패다.
	//
	// ★ 거절 사례에는 ryml 고유 위험을 넣었다. `operator[]`는 없는 키에서 **abort**
	//   하므로(어댑터가 `find_child`로 흡수한다) "필수 키 누락"이 조용한 실패가 아니라
	//   프로세스 사망이 될 수 있다. 그 경계를 여기서 상시로 밟는다.

    static CommandCore::CommandResult Cmd_tag_list(const ConsoleCommandContext& ctx)
    {
        if (ctx.parts.size() != 1) return CommandCore::InvalidArguments("tag.list accepts no arguments");
        return EditorProjectOperations::Tags();
    }
    static CommandCore::CommandResult Cmd_tag_add(const ConsoleCommandContext& ctx)
    {
        if (ctx.parts.size() != 2) return CommandCore::InvalidArguments("tag.add <name>");
        return EditorProjectOperations::AddTag(ctx.parts[1]);
    }
    static CommandCore::CommandResult Cmd_tag_has(const ConsoleCommandContext& ctx)
    {
        if (ctx.parts.size() != 2) return CommandCore::InvalidArguments("tag.has <name>");
        return EditorProjectOperations::HasTag(ctx.parts[1]);
    }
    static CommandCore::CommandResult Cmd_tag_remove(const ConsoleCommandContext& ctx)
    {
        if (ctx.parts.size() != 2) return CommandCore::InvalidArguments("tag.remove <name>");
        return EditorProjectOperations::RemoveTag(ctx.parts[1]);
    }


	// 충돌 행렬은 프로젝트 설정 자산이라 meta를 만들지 않는다. 저장 후 다시 읽어
	// 값이 돌아오는지 보고, escape 인자로 설정 루트 밖 목적지가 거부되는지 본다.

    static CommandCore::CommandResult Cmd_collisionmatrix_authoring_probe(const ConsoleCommandContext& ctx)
    {
        using namespace CommandCore;
        if (ctx.parts.size() > 2 || (ctx.parts.size() == 2 && ctx.parts[1] != "escape")) return InvalidArguments("collisionmatrix.authoring.probe [escape]");
        const bool escape = ctx.parts.size() >= 2 && ctx.parts[1] == "escape";
        if (escape)
        {
            UncatalogedAuthoringRequest request{};
            request.destinationPath =
                PathFinder::Relative("Foliage") / "CollisionMatrix.asset";
            request.payload = "0:\n  0: true\n";
            const bool written = AssetAuthoringPort::WriteCollisionMatrix(request);
            std::printf("[collisionmatrix.authoring.probe] %s\n",
                written ? "committed" : "rejected");
            auto data = CommandData::Object(); data.Set("written", CommandData::Bool(written));
            return !written ? Ok("Escaping path rejected", std::move(data)) : Fail("authoring.escape_accepted", "Escaping path was accepted", std::move(data));
        }

        auto matrix = PhysicsManagers->GetCollisionMatrix();
        if (matrix.size() < 2 || matrix[0].size() < 2)
        {
            std::printf("[collisionmatrix.authoring.probe] rejected matrix=%zu\n",
                matrix.size());
            return PreconditionFailed("collisionmatrix.coverage_missing", "Collision matrix requires at least two layers");
        }

        const uint8_t before = matrix[0][1];
        matrix[0][1] = before ? 0 : 1;
        PhysicsManagers->SetCollisionMatrix(matrix);
        if (!PhysicsManagers->SaveCollisionMatrix())
        {
            std::printf("[collisionmatrix.authoring.probe] rejected\n");
            matrix[0][1] = before; PhysicsManagers->SetCollisionMatrix(matrix);
            return Fail("collisionmatrix.save_failed", "Collision matrix save failed");
        }

        // 메모리를 되돌린 뒤 파일에서 다시 읽는다. 디스크를 실제로 거치지 않았다면
        // 여기서 뒤집힌 값이 돌아오지 않는다.
        matrix[0][1] = before;
        PhysicsManagers->SetCollisionMatrix(matrix);
        PhysicsManagers->LoadCollisionMatrix();
        const uint8_t reloaded = PhysicsManagers->GetCollisionMatrix()[0][1];

        // 저장소의 CollisionMatrix.asset을 원래대로 돌려놓는다.
        matrix[0][1] = before;
        PhysicsManagers->SetCollisionMatrix(matrix);
        const bool restored = PhysicsManagers->SaveCollisionMatrix();

        std::printf("[collisionmatrix.authoring.probe] committed roundtrip=%s restored=%s\n",
            reloaded != before ? "ok" : "mismatch", restored ? "ok" : "failed");
        auto data = CommandData::Object();
        data.Set("roundTrip", CommandData::Bool(reloaded != before));
        data.Set("restored", CommandData::Bool(restored));
        return reloaded != before && restored ? Ok({}, std::move(data)) : Fail("collisionmatrix.authoring.failed", "Commandlet verification failed", std::move(data));
    }

	// Blackboard는 Foliage와 달리 실제 runtime 타입의 직렬화 경로를 그대로 태운다.
	// key 하나를 넣고 저장한 뒤 같은 이름으로 다시 읽어 값이 살아 돌아오는지 본다.

    static CommandCore::CommandResult Cmd_blackboard_authoring_probe(const ConsoleCommandContext& ctx)
    {
        using namespace CommandCore;
        if (ctx.parts.size() < 2 || ctx.parts.size() > 3 || (ctx.parts.size() == 3 && ctx.parts[2] != "empty" && ctx.parts[2] != "noname")) return InvalidArguments("blackboard.authoring.probe <name> [empty|noname]");
        if (ctx.parts.size() < 2)
        {
            std::printf("[blackboard.authoring.probe] usage: <name> [empty|noname]\n");
            return InvalidArguments("blackboard.authoring.probe requires a name");
        }

        const std::string mode = ctx.parts.size() >= 3 ? ctx.parts[2] : "";
        const bool empty = mode == "empty";
        const bool noName = mode == "noname";

        BlackBoard board;
        if (!empty)
        {
            board.SetValueAsInt("ProbeKey", 4177);
        }

        if (!board.Serialize(noName ? std::string_view{} : ctx.parts[1]))
        {
            std::printf("[blackboard.authoring.probe] rejected\n");
            return (empty || noName) ? Ok("Invalid board rejected") : Fail("blackboard.write_failed", "Board serialization failed");
        }

        BlackBoard reloaded;
        int roundTrip = 0;
        try
        {
            reloaded.Deserialize(ctx.parts[1]);
            if (reloaded.HasKey("ProbeKey"))
                roundTrip = reloaded.GetValueAsInt("ProbeKey");
        }
        catch (const std::exception& exception)
        {
            std::printf("[blackboard.authoring.probe] reload-failed %s\n",
                exception.what());
            return Fail("blackboard.reload_failed", exception.what());
        }

        std::printf("[blackboard.authoring.probe] committed keys=%zu roundtrip=%d\n",
            reloaded.GetValues().size(), roundTrip);
        auto data = CommandData::Object();
        data.Set("keys", CommandData::Int(reloaded.GetValues().size()));
        data.Set("roundTrip", CommandData::Int(roundTrip));
        return !empty && !noName && roundTrip == 4177 ? Ok({}, std::move(data)) : Fail("blackboard.authoring.failed", "Commandlet verification failed", std::move(data));
    }

    static CommandCore::CommandResult Cmd_asset_guid_rename_probe(const ConsoleCommandContext& ctx)
    {
        using namespace CommandCore;
        if (ctx.parts.size() != 1) return InvalidArguments("asset.guid.rename.probe");
        // D2-c: 새 material payload와 sidecar가 같은 UUIDv4를 갖고, target+meta
        // rename 뒤에도 catalog의 GUID->path 참조가 그대로 새 경로를 가리키는지
        // 한 transaction으로 확인한다. 고정 이름을 쓰지 않아 이전 실패 잔재나
        // 병렬 실행과 충돌하지 않는다.
        const FileGuid requestedGuid = FileGuid::CreateRandomV4();
        std::string suffix = requestedGuid.ToString();
        std::erase(suffix, '-');
        const std::string sourceName = "D2GuidRenameProbe_" + suffix;
        const std::string destinationName = sourceName + "_Renamed";
        const file::path sourcePath = PathFinder::Relative("Materials\\") /
            (sourceName + ".asset");
        const file::path destinationPath = PathFinder::Relative("Materials\\") /
            (destinationName + ".asset");
        const file::path sourceMeta = sourcePath.string() + ".meta";
        const file::path destinationMeta = destinationPath.string() + ".meta";

        auto cleanup = [](const file::path& assetPath)
        {
            DataSystems->ApplyAssetChange({ RuntimeAssetChangeKind::Removed,
                RuntimeAssetType::Material, {}, assetPath });
            std::error_code ignored;
            file::remove(assetPath, ignored);
            ignored.clear();
            file::remove(assetPath.string() + ".meta", ignored);
        };

        Material authored;
        authored.m_name = sourceName;
        authored.m_fileGuid = requestedGuid;
        authored.m_materialInfo.m_roughness = 0.375f;

        bool saved = false;
        bool renamed = false;
        bool identityPreserved = false;
        bool materialRoundTrip = false;
        FileGuid canonicalGuid{};
        try
        {
            saved = EditorAssetDatabase::Get().SaveMaterial(&authored);
            canonicalGuid = DataSystems->GetFileGuid(sourcePath);
            if (saved && canonicalGuid == requestedGuid)
            {
                const FileGuid movedGuid = EditorAssetDatabase::Get().RenameAsset(
                    sourcePath, destinationPath);
                renamed = movedGuid == canonicalGuid;
                identityPreserved = renamed
                    && !file::exists(sourcePath) && !file::exists(sourceMeta)
                    && file::is_regular_file(destinationPath)
                    && file::is_regular_file(destinationMeta)
                    && DataSystems->GetFileGuid(sourcePath) == FileGuid{}
                    && DataSystems->GetFileGuid(destinationPath) == canonicalGuid
                    && DataSystems->GetFilePath(canonicalGuid).lexically_normal()
                        == destinationPath.lexically_normal();

                if (identityPreserved)
                {
                    std::string parseError;
                    const Authoring::ParsedDocument persistedDocument =
                        Authoring::ParsedDocument::ParseFile(
                            destinationPath.string(), parseError);
                    const Authoring::ReadNode persisted =
                        persistedDocument.Root();
                    Material decoded;
                    Authoring::WriteDocument reserializedDocument;
                    materialRoundTrip = persistedDocument
                        && DataSystems->DeserializeMaterialPayload(
                        decoded, Authoring::NodeViewAccess::Make(persisted))
                        && decoded.m_fileGuid == canonicalGuid
                        && decoded.m_materialInfo.m_roughness
                            == authored.m_materialInfo.m_roughness
                        && DataSystems->SerializeMaterialPayload(
                            decoded, reserializedDocument.Root())
                        && persisted.Dump() == reserializedDocument.Dump();
                }
            }
        }
        catch (const std::exception& exception)
        {
            Debug->LogError("[asset.guid.rename] probe exception: "
                + std::string(exception.what()));
        }

        cleanup(sourcePath);
        cleanup(destinationPath);
        const bool passed = saved && renamed && identityPreserved
            && materialRoundTrip
            && canonicalGuid.IsRandomV4();
        std::printf("[asset.guid.rename] %s guid=%s save=%s move=%s "
            "identity=%s material-roundtrip=%s cleanup=%s\n",
            passed ? "pass" : "fail", canonicalGuid.ToString().c_str(),
            saved ? "yes" : "no", renamed ? "yes" : "no",
            identityPreserved ? "yes" : "no",
            materialRoundTrip ? "yes" : "no",
            (!file::exists(sourcePath) && !file::exists(sourceMeta)
                && !file::exists(destinationPath) && !file::exists(destinationMeta))
                ? "yes" : "no");
        auto data = CommandData::Object();
        data.Set("saved", CommandData::Bool(saved));
        data.Set("renamed", CommandData::Bool(renamed));
        data.Set("identityPreserved", CommandData::Bool(identityPreserved));
        data.Set("materialRoundTrip", CommandData::Bool(materialRoundTrip));
        data.Set("guid", CommandData::String(canonicalGuid.ToString()));
        return passed ? Ok({}, std::move(data)) : Fail("asset.guid.rename.failed", "Commandlet verification failed", std::move(data));
    }

    static CommandCore::CommandResult Cmd_material_corpus_probe(const ConsoleCommandContext& ctx)
    {
        using namespace CommandCore;
        // D2-d: 실제 standalone material corpus를 파일 수정 없이 메모리에서 두 번
        // 왕복한다. UUID version은 전역 strict gate의 책임이고, 여기서는 sidecar
        // 정본과 payload mirror가 같은 identity인지 및 asset reference가 보존되는지만
        // 판정한다.
        if (ctx.parts.size() < 2)
        {
            std::printf("[CLI] 사용법: material.corpus.probe <머티리얼 이름>...\n");
            return InvalidArguments("material.corpus.probe requires material names");
        }

        auto captureReferences = [](const Material& material)
        {
            std::vector<std::string> rows;
            rows.push_back("shader|" + material.m_shaderMetaGuid.ToString());
            for (const MaterialPropertyValue& property : material.m_propertyValues)
            {
                if (property.m_textureGuid == FileGuid{}) continue;
                rows.push_back("texture|" + property.m_name + "|"
                    + property.m_textureGuid.ToString());
            }
            std::ranges::sort(rows);
            return rows;
        };

        size_t passedCount = 0;
        size_t textureReferenceCount = 0;
        for (size_t index = 1; index < ctx.parts.size(); ++index)
        {
            const std::string& name = ctx.parts[index];
            const file::path assetPath = PathFinder::Relative("Materials\\") /
                (name + ".asset");
            bool decoded = false;
            bool identity = false;
            bool shader = false;
            bool textures = false;
            bool stable = false;
            size_t materialTextureReferences = 0;
            try
            {
                const FileGuid catalogGuid = DataSystems->GetFileGuid(assetPath);
                std::string parseError;
                const Authoring::ParsedDocument sourceDocument =
                    Authoring::ParsedDocument::ParseFile(
                        assetPath.string(), parseError);
                const Authoring::ReadNode source = sourceDocument.Root();
                Material first;
                decoded = sourceDocument
                    && DataSystems->DeserializeMaterialPayload(
                    first, Authoring::NodeViewAccess::Make(source));
                identity = decoded && catalogGuid != FileGuid{}
                    && first.m_fileGuid == catalogGuid;

                const file::path shaderPath = decoded
                    ? DataSystems->GetFilePath(first.m_shaderMetaGuid) : file::path{};
                shader = decoded && first.m_shaderMetaGuid != FileGuid{}
                    && !shaderPath.empty() && file::is_regular_file(shaderPath)
                    && shaderPath.extension() == ".shadermeta";

                textures = decoded;
                if (decoded)
                {
                    for (const MaterialPropertyValue& property : first.m_propertyValues)
                    {
                        if (property.m_textureGuid == FileGuid{}) continue;
                        ++materialTextureReferences;
                        const file::path texturePath = DataSystems->GetFilePath(
                            property.m_textureGuid);
                        if (texturePath.empty() || !file::is_regular_file(texturePath))
                            textures = false;
                    }
                }

                if (decoded)
                {
                    Authoring::WriteDocument firstCanonicalDocument;
                    const bool firstCanonicalWritten =
                        DataSystems->SerializeMaterialPayload(
                            first, firstCanonicalDocument.Root());
                    const Authoring::ReadNode firstCanonical =
                        firstCanonicalDocument.Root().Read();
                    Material second;
                    const bool decodedAgain = firstCanonicalWritten
                        && DataSystems->DeserializeMaterialPayload(
                        second, Authoring::NodeViewAccess::Make(firstCanonical));
                    Authoring::WriteDocument secondCanonicalDocument;
                    const bool secondCanonicalWritten = decodedAgain
                        && DataSystems->SerializeMaterialPayload(
                            second, secondCanonicalDocument.Root());
                    stable = decodedAgain
                        && secondCanonicalWritten
                        && second.m_fileGuid == first.m_fileGuid
                        && captureReferences(second) == captureReferences(first)
                        && secondCanonicalDocument.Dump()
                            == firstCanonicalDocument.Dump();
                }
            }
            catch (const std::exception& exception)
            {
                Debug->LogError("[material.corpus] " + name + ": "
                    + exception.what());
            }

            const bool passed = decoded && identity && shader && textures && stable;
            if (passed) ++passedCount;
            textureReferenceCount += materialTextureReferences;
            std::printf("[material.corpus] %s %s identity=%s shader=%s "
                "textures=%zu/%s stable=%s\n",
                name.c_str(), passed ? "pass" : "fail",
                identity ? "yes" : "no", shader ? "yes" : "no",
                materialTextureReferences, textures ? "valid" : "invalid",
                stable ? "yes" : "no");
        }

        const size_t total = ctx.parts.size() - 1;
        const bool passed = passedCount == total;
        std::printf("[material.corpus] %s materials=%zu/%zu textureRefs=%zu\n",
            passed ? "pass" : "fail", passedCount, total, textureReferenceCount);
        auto data = CommandData::Object();
        data.Set("passed", CommandData::Int(passedCount));
        data.Set("total", CommandData::Int(total));
        data.Set("textureReferences", CommandData::Int(textureReferenceCount));
        return passed ? Ok({}, std::move(data)) : Fail("material.corpus.failed", "Commandlet verification failed", std::move(data));
    }

    // I6-B4b 후속 — **에디터 드롭 경로**를 CLI로 연다. 콘텐츠 브라우저에서
    // 씬으로 끌어다 놓을 때 도는 것은 model.load(LoadModel)가 아니라
    // DataSystems->LoadCachedModelShared다(HierarchyWindow·SceneViewWindow).
    // 그 둘이 서로 다른 로더라는 것이 이 게이트 세트의 구멍이었다.

    static CommandCore::CommandResult Cmd_model_loadcached(const ConsoleCommandContext& ctx)
    {
        using namespace CommandCore;
        const std::vector<std::string>& parts = ctx.parts;
        if (parts.size() != 2)
        {
            std::printf("[CLI] 사용법: model.loadcached <모델 경로>\n");
            return InvalidArguments("model.loadcached requires one model path");
        }
        const auto generation = DataSystems->LoadModelAssetGenerationByPath(parts[1]);
        std::printf("[CLI] model.loadcached %s: %s\n",
            generation ? "ok" : "fail", parts[1].c_str());
        auto data = CommandData::Object();
        data.Set("path", CommandData::String(parts[1]));
        return generation ? Ok({}, std::move(data)) : Fail("model.load_failed", "Model generation load failed", std::move(data));
    }

    static CommandCore::CommandResult Cmd_model_place(const ConsoleCommandContext& ctx)
    {
        using namespace CommandCore;
        if (ctx.parts.size() != 2 || ctx.parts[1].empty()) return InvalidArguments("model.place requires a model name");
        Scene* scene = SceneManagers->GetActiveScene();
        const auto generation = DataSystems->FindModelAssetGenerationByStem(ctx.parts[1]);
        if (!scene || !generation) return PreconditionFailed("model.not_found", "Scene or imported model is unavailable");
        Entity* root = nullptr;
        Meta::UndoManager::GetInstance()->Execute(std::make_unique<Meta::LoadModelToSceneObjCommand>(scene, generation, &root));
        if (!root) return Fail("model.place.failed", "Model could not be instantiated");
        std::printf("[CLI] 씬에 배치: %s (ok)\n", ctx.parts[1].c_str());
        auto result = EditorObjectOperations::Describe(scene->HandleOf(root->m_index));
        result.data.Set("changed", CommandData::Bool(true));
        return result;
    }

    // I5-D4e-2 — 이벤트·루프 오버라이드의 소유 이관 게이트. 코퍼스에 저작분이
    // 0이라 실자산 게이트는 원리적으로 초록이므로([[plan-target-may-be-already-
    // dead]]의 그 함정) 합성으로 판정한다: seed가 합성 오버라이드(루프 false·
    // 이벤트 2)를 주입하고, 저장·재로드 뒤 verify가 ①왕복(Animator 소유로
    // 살아남았는가) ②비오염(공유 자산 m_animations가 불변인가 — 재주입 청산
    // 실증) ③발화 매칭(구간·되감김 규칙이 오버라이드·IsClipLooping을 소비
    // 하는가)을 잰다. 이관은 A/B 스위치와 무관한 무조건 경로라 on/off 대조군
    // 양쪽에서 같은 판정이어야 한다.

    static CommandCore::CommandResult Cmd_experiment_animevent(const ConsoleCommandContext& ctx)
    {
        using namespace CommandCore;
        if (ctx.parts.size() != 2 || (ctx.parts[1] != "seed" && ctx.parts[1] != "verify")) return InvalidArguments("experiment.animevent seed|verify");
        Scene* scene = SceneManagers->GetActiveScene();
        if (nullptr == scene || ctx.parts.size() < 2)
        {
            std::printf("[CLI] 사용법: experiment.animevent seed|verify\n");
            return PreconditionFailed("scene.not_found", "No active scene");
        }
        const bool isSeed = "seed" == ctx.parts[1];

        Animator* animator = nullptr;
        for (const auto& object : scene->m_Entities)
        {
            if (!object || object->IsDestroyMark()) continue;
            // I6-B3 — 후보 선별이 창구를 탄다. legacy 객체 존재를 관문으로
            // 쓰면 대입을 끊는 순간 이 게이트가 "animators=0 skip"으로 조용히
            // 비어 버린다(초록인 채로). 값 대조 arm은 아래에 그대로 둔다 —
            // 그것은 은퇴가 아니라 대조군이고, B4에서 함께 죽는다.
            Animator* candidate = object->GetComponent<Animator>();
            if (nullptr == candidate || 0 == candidate->GetSkeletonSerial())
                continue;
            if (0 == candidate->GetClipCount()) continue;
            animator = candidate;
            break;
        }
        if (nullptr == animator)
        {
            std::printf("[CLI] experiment.animevent %s skip animators=0\n",
                ctx.parts[1].c_str());
            return PreconditionFailed("animation.coverage_missing", "No typed Animator fixture");
        }

        if (isSeed)
        {
            animator->SetClipLooping(0, false);
            AnimatorClipOverride& clipOverride = animator->EnsureClipOverride(0);
            clipOverride.events.clear();
            KeyFrameEvent early;
            early.m_eventName = "gateEvent25";
            early.m_scriptName = "GateScript";
            early.m_funName = "GateFun25";
            early.key = 0.25f;
            early.frameKey = 25;
            KeyFrameEvent late;
            late.m_eventName = "gateEvent75";
            late.m_scriptName = "GateScript";
            late.m_funName = "GateFun75";
            late.key = 0.75f;
            late.frameKey = 75;
            clipOverride.events.push_back(early);
            clipOverride.events.push_back(late);
            std::printf("[CLI] experiment.animevent seed done clip=0 "
                "loop=false events=2\n");
            return Ok("Animation event fixture seeded");
        }

        std::vector<std::string> failures;
        // ① 왕복 — 저장·재로드가 Animator 소유 오버라이드를 보존했는가.
        const AnimatorClipOverride* clipOverride = animator->FindClipOverride(0);
        if (nullptr == clipOverride)
        {
            failures.push_back("왕복: 클립 0 오버라이드 부재");
        }
        else
        {
            if (!clipOverride->loopOverride.has_value()
                || false != *clipOverride->loopOverride)
            {
                failures.push_back("왕복: loop 오버라이드 소실");
            }
            if (2 != clipOverride->events.size()
                || "gateEvent25" != clipOverride->events[0].m_eventName
                || "GateFun25" != clipOverride->events[0].m_funName
                || std::abs(clipOverride->events[0].key - 0.25f) > 1e-6f
                || "gateEvent75" != clipOverride->events[1].m_eventName)
            {
                failures.push_back("왕복: 이벤트 필드 소실("
                    + std::to_string(clipOverride->events.size()) + "개)");
            }
        }
        // ② 비오염 — 공유 자산이 불변인가(재주입 청산 실증). 자산 원본 루프는
        // experiment 자산값과 동치여야 한다(둘 다 임포터 산물).
        //
        // ★ I6-B3 — 이 축은 **legacy 대조군**이라 legacy 자산이 없으면 잴 것이
        //   없다. 예전에는 후보 선별이 m_Skeleton 널을 걸러 줘서 여기가
        //   무방비로 역참조했고, B4 예행(대입 절단)에서 정확히 그 자리가
        //   ACCESS_VIOLATION으로 죽었다 — 관문을 창구로 옮기면 그 암묵 보장이
        //   사라진다. 이제 없으면 건너뛰되 **출력 토큰으로 드러낸다**(n/a).
        //   조용히 건너뛰면 대조군이 사라진 채로 초록이 나온다.
        // MBC9 — 공유 자산은 immutable ModelAnimationAsset이고 이벤트 필드가 아예
        // 없다(이벤트는 씬 소유). 오염은 구조적으로 불가능하므로 자산 루프값이 원본
        // 그대로인지(오버라이드가 자산을 안 건드렸는지)만 확인한다.
        const char* contaminationAxis = "immutable";
        if (const assets::ModelAnimationAsset* clip = animator->TypedClip(0))
        {
            const AnimatorClipOverride* probe = animator->FindClipOverride(0);
            if (probe && probe->loopOverride.has_value()
                && *probe->loopOverride == clip->looping)
            {
                // 오버라이드 값이 자산값과 같으면 "정본이 오버라이드"임을 가를 수 없다 —
                // seed는 false를 심고 Gunner 자산은 loop=true라 같지 않아야 한다.
                failures.push_back("비오염: 자산 loop가 오버라이드와 같아 판별 불가");
            }
        }
        else
        {
            contaminationAxis = "n/a";
        }
        // ③ 발화 매칭 — 구간·되감김 규칙이 오버라이드와 IsClipLooping을 본다.
        if (1 != animator->InvokeClipEvents(0, 0.3f, 0.2f))
        {
            failures.push_back("발화: 일반 구간(0.2→0.3) 매칭≠1");
        }
        if (1 != animator->InvokeClipEvents(0, 0.8f, 0.7f))
        {
            failures.push_back("발화: 일반 구간(0.7→0.8) 매칭≠1");
        }
        if (0 != animator->InvokeClipEvents(0, 0.1f, 0.7f))
        {
            failures.push_back("발화: loop=false 되감김이 발화됨");
        }
        animator->SetClipLooping(0, true);
        if (1 != animator->InvokeClipEvents(0, 0.1f, 0.7f))
        {
            failures.push_back("발화: loop=true 되감김(0.75) 매칭≠1");
        }
        animator->SetClipLooping(0, false);
        // ④ 루프 판정 폴백 — 오버라이드 없는 클립은 자산값.
        if (false != animator->IsClipLooping(0))
        {
            failures.push_back("루프: 오버라이드가 정본이 아님");
        }

        if (failures.empty())
        {
            std::printf("[CLI] experiment.animevent verify pass "
                "roundtrip=ok contamination=%s firing=ok\n",
                contaminationAxis);
        }
        else
        {
            std::string joined;
            for (const std::string& failure : failures)
            {
                joined += " [" + failure + "]";
            }
            std::printf("[CLI] experiment.animevent verify fail%s\n",
                joined.c_str());
        }
        auto data = CommandData::Object();
        data.Set("failures", CommandData::Int(failures.size()));
        data.Set("contamination", CommandData::String(contaminationAxis));
        return failures.empty() && animator->TypedClip(0) != nullptr ? Ok({}, std::move(data)) : Fail("experiment.animevent.failed", "Commandlet verification failed", std::move(data));
    }

    // I5-D5a — Foliage 메시의 experiment 핸들 합류 게이트. 코퍼스에 Foliage
    // 저작분이 0이라(착수 정찰 실측) 합성으로 판정한다: seed가 씬에
    // FoliageComponent+타입(Gunner)+인스턴스를 저작 경로(AddFoliageType —
    // 바인딩 지점) 그대로 심고 foliage 자산을 게시하며, 저장·재로드 뒤 verify가
    // ①postLoad 재해석 경로의 바인딩 ②프록시 DrawSource의 핸들 반영
    // (CaptureDrawSources — 실물 함수) ③뷰 완비(stableKey)를 잰다. 렌더러
    // poolFoliage 분기는 헤드리스 관측 밖(라이브 렌더 0프레임 + dx12.scene
    // 하네스에 Foliage 대칭 구성 없음) — 계획서 한계 기록.

    static CommandCore::CommandResult Cmd_experiment_foliage(const ConsoleCommandContext& ctx)
    {
        if (ctx.parts.size() < 2 || (ctx.parts[1] != "seed" && ctx.parts[1] != "verify"))
            return CommandCore::InvalidArguments("experiment.foliage seed <asset-directory> <model-path> | verify");
        if (ctx.parts[1] == "verify" && ctx.parts.size() != 2)
            return CommandCore::InvalidArguments("experiment.foliage verify accepts no other arguments");
        Scene* scene = SceneManagers->GetActiveScene();
        if (nullptr == scene || ctx.parts.size() < 2)
        {
            std::printf("[CLI] 사용법: experiment.foliage seed <asset-directory> <model-path> | verify\n");
            return CommandCore::PreconditionFailed("scene.none", "No active scene");
        }

        if ("seed" == ctx.parts[1])
        {
            if (ctx.parts.size() != 4)
            {
                std::printf("[CLI] experiment.foliage seed <asset-directory> <model-path>\n");
                return CommandCore::InvalidArguments("experiment.foliage seed <asset-directory> <model-path>");
            }
            const auto generation = DataSystems->LoadModelAssetGenerationByPath(ctx.parts[3]);
            if (!generation)
            {
                std::printf("[CLI] experiment.foliage seed fail 모델 없음\n");
                return CommandCore::PreconditionFailed("model.not_found", "Cannot load the supplied foliage fixture model");
            }
            Entity* entity = scene->CreateEntity("GateFoliage",
                GameObjectType::Mesh, 0);
            FoliageComponent* foliage = entity->AddComponent<FoliageComponent>();
            // MBC9 — Foliage 자산은 모델을 이름(stem)으로만 적는다. 바인딩은
            // AddFoliageType(BindModelGeneration)이 이름 → generation으로 잇는다.
            FoliageType type(generation->SourcePath().stem().string(), true);
            foliage->AddFoliageType(type);
            FoliageInstance instance;
            instance.m_position = { 3.0f, 0.0f, 3.0f };
            instance.m_scale = { 0.1f, 0.1f, 0.1f };
            foliage->AddFoliageInstance(instance);
            foliage->SaveFoliageAsset(file::path(ctx.parts[2]), L"gate_foliage");
            const bool published = FileGuid{} != foliage->m_foliageAssetGuid;
            std::printf("[CLI] experiment.foliage seed %s types=%zu "
                "assetGuid=%s\n", published ? "done" : "fail",
                foliage->GetFoliageTypes().size(),
                published ? "ok" : "nil");
            return published ? CommandCore::Ok("Foliage fixture published") : CommandCore::Fail("foliage.publish_failed", "Cannot publish the foliage fixture");
        }

        FoliageComponent* foliage = nullptr;
        for (const auto& object : scene->m_Entities)
        {
            if (!object || object->IsDestroyMark()) continue;
            if (FoliageComponent* candidate =
                object->GetComponent<FoliageComponent>())
            {
                foliage = candidate;
                break;
            }
        }
        if (nullptr == foliage)
        {
            std::printf("[CLI] experiment.foliage verify skip components=0\n");
            return CommandCore::PreconditionFailed("foliage.missing", "No foliage component to verify");
        }

        std::vector<std::string> failures;
        const auto& types = foliage->GetFoliageTypes();
        if (types.empty()) failures.push_back("왕복: 타입 0(자산 재로드 실패)");
        std::size_t authoredMaterialTypes = 0, authoredMaterialDraws = 0;
        std::size_t generationTypes = 0, generationViews = 0, materialTypes = 0;
        for (const FoliageType& type : types)
        {
            if (type.m_modelGeneration) ++generationTypes;
            if (type.m_material) ++materialTypes;
            // I5-D5c4(S2c-2c) — 재질 저작 정본도 같은 generation에서 잇는다.
            if (type.m_authoredMaterial) ++authoredMaterialTypes;
        }
        if (generationTypes != types.size())
        {
            failures.push_back("typed: generation 바인딩 "
                + std::to_string(generationTypes) + "/" + std::to_string(types.size()));
        }
        if (materialTypes != types.size())
        {
            failures.push_back("재질: runtime 재질 "
                + std::to_string(materialTypes) + "/" + std::to_string(types.size()));
        }
        if (authoredMaterialTypes != types.size())
        {
            failures.push_back("재질: 저작 정본 "
                + std::to_string(authoredMaterialTypes) + "/"
                + std::to_string(types.size()));
        }

        // 실물 프록시 사슬 — 생성자·색인·DrawSource 캡처를 제품 함수 그대로.
        FoliageRenderProxy proxy(foliage);
        proxy.RebuildInstanceMap();
        const auto draws = proxy.CaptureDrawSources();
        if (draws.empty()) failures.push_back("프록시: DrawSource 0");
        for (const auto& draw : draws)
        {
            if (draw.authoredMaterial) ++authoredMaterialDraws;
            if (draw.modelGeneration)
            {
                RHIModelMeshView typedView{};
                if (BuildRHIModelMeshView(*draw.modelGeneration, draw.modelMeshIndex, typedView)
                    && typedView.IsComplete())
                {
                    ++generationViews;
                }
            }
        }
        // DrawSource가 재질 정본을 나르는가 — 컴포넌트 바인딩만 보면 프록시
        // 복사 누락에 눈멀다.
        if (authoredMaterialDraws != draws.size())
        {
            failures.push_back("재질 운반: "
                + std::to_string(authoredMaterialDraws) + "/"
                + std::to_string(draws.size()));
        }
        if (generationViews != draws.size())
        {
            failures.push_back("typed: RHIModelMeshView 완비 "
                + std::to_string(generationViews) + "/" + std::to_string(draws.size()));
        }
        if (failures.empty())
        {
            std::printf("[CLI] experiment.foliage verify pass "
                "types=%zu draws=%zu authoredMat=%zu authoredMatDraws=%zu "
                "generationTypes=%zu generationViews=%zu\n",
                types.size(), draws.size(), authoredMaterialTypes, authoredMaterialDraws,
                generationTypes, generationViews);
        }
        else
        {
            std::string joined;
            for (const std::string& failure : failures)
                joined += " [" + failure + "]";
            std::printf("[CLI] experiment.foliage verify fail%s\n", joined.c_str());
        }
        CommandCore::CommandData data = CommandCore::CommandData::Object();
        data.Set("types", CommandCore::CommandData::Int(types.size()));
        data.Set("draws", CommandCore::CommandData::Int(draws.size()));
        data.Set("generationTypes", CommandCore::CommandData::Int(generationTypes));
        data.Set("generationViews", CommandCore::CommandData::Int(generationViews));
        return failures.empty() ? CommandCore::Ok("Foliage contract", std::move(data))
            : CommandCore::Fail("foliage.contract_failed", "Foliage contract failed", std::move(data));
    }

    // I5-D4e-3 — 본 이름 해석 창구의 전수 A/B 대조. Scene 본 전파가 쓰는
    // 실물 창구(Animator::ResolveBoneIndex)를 BoneComponent 전수에 태우고,
    // legacy FindBone 직접 해석과 인덱스를 대조한다(1:1 계약 실증). 경로
    // 계수(viaExperiment)는 창구 내부의 실분기 관측이다 — 조건 재현이 아니라서
    // experiment 분기 소실이 legacy 폴백으로 조용히 덮여도 여기서 갈린다.

    static CommandCore::CommandResult Cmd_experiment_boneresolve(const ConsoleCommandContext& ctx)
    {
        using namespace CommandCore;
        if (ctx.parts.size() != 1) return InvalidArguments("experiment.boneresolve");
        Scene* scene = SceneManagers->GetActiveScene();
        if (nullptr == scene)
        {
            std::printf("[CLI] experiment.boneresolve fail 활성 씬 없음\n");
            return PreconditionFailed("scene.not_found", "No active scene or render scene");
        }
        // MBC9 — 본 이름 해석 창구(Animator::ResolveBoneIndex)를 BoneComponent 전수에
        // 태운다. 독립 유도 대조는 name→index→name 왕복이고, 경로·신원 계수는 창구
        // 내부의 실분기 관측이다(typed generation 하나여야 한다).
        std::size_t boneCount = 0, viaGenerationCount = 0, serialGenerationCount = 0;
        std::size_t unresolvedCount = 0, roundtripMismatch = 0;
        for (const auto& object : scene->m_Entities)
        {
            if (!object || object->IsDestroyMark()) continue;
            if (nullptr == object->GetComponent<BoneComponent>()) continue;
            const auto& rootObject = scene->TryGetEntity(object->GetRootIndex());
            if (!rootObject) continue;
            Animator* animator = rootObject->GetComponent<Animator>();
            AnimatorDataPath serialPath = AnimatorDataPath::None;
            if (nullptr == animator
                || 0 == animator->GetSkeletonSerial(nullptr, &serialPath))
            {
                continue;
            }

            const std::string boneName = object->RemoveSuffixNumberTag();
            AnimatorDataPath resolvePath = AnimatorDataPath::None;
            const int resolved = animator->ResolveBoneIndex(boneName, nullptr, &resolvePath);

            ++boneCount;
            if (AnimatorDataPath::Generation == resolvePath) ++viaGenerationCount;
            if (AnimatorDataPath::Generation == serialPath) ++serialGenerationCount;
            if (resolved < 0) ++unresolvedCount;
            else if (animator->GetBoneName(resolved) != boneName) ++roundtripMismatch;
        }
        const bool passed = boneCount > 0 && 0 == unresolvedCount
            && 0 == roundtripMismatch && viaGenerationCount == boneCount
            && serialGenerationCount == boneCount;
        std::printf("[CLI] experiment.boneresolve %s bones=%zu generation=%zu "
            "unresolved=%zu serialGeneration=%zu roundtrip=%zu\n",
            passed ? "pass" : (boneCount == 0 ? "skip" : "fail"),
            boneCount, viaGenerationCount, unresolvedCount,
            serialGenerationCount, roundtripMismatch);
        auto data = CommandData::Object();
        data.Set("bones", CommandData::Int(boneCount));
        data.Set("generation", CommandData::Int(viaGenerationCount));
        data.Set("unresolved", CommandData::Int(unresolvedCount));
        data.Set("serialGeneration", CommandData::Int(serialGenerationCount));
        data.Set("roundtrip", CommandData::Int(roundtripMismatch));
        if (boneCount == 0) { auto result = PreconditionFailed("animation.coverage_missing", "Typed model fixture is absent"); result.data = std::move(data); return result; }
        return passed ? Ok({}, std::move(data)) : Fail("experiment.boneresolve.failed", "Commandlet verification failed", std::move(data));
    }

    // I5-D4e-3 — AvatarMask 트리 생성의 A/B 대조. 실물 창구
    // (BuildAvatarBoneMasks — experiment 단일 패스+스택 DFS)와 legacy
    // MakeBoneMask 재귀를 같은 스켈레톤에 돌려 m_BoneMasks의 크기·순서
    // (boneName 열)·자식 계수를 대조한다 — 순서가 저장분 인덱스 대응
    // (ReCreateMask)이라 순서 재현이 곧 저작 호환이다.

    static CommandCore::CommandResult Cmd_experiment_animmask(const ConsoleCommandContext& ctx)
    {
        using namespace CommandCore;
        if (ctx.parts.size() != 1) return InvalidArguments("experiment.animmask");
        Scene* scene = SceneManagers->GetActiveScene();
        if (nullptr == scene)
        {
            std::printf("[CLI] experiment.animmask fail 활성 씬 없음\n");
            return PreconditionFailed("scene.not_found", "No active scene");
        }
        Animator* animator = nullptr;
        for (const auto& object : scene->m_Entities)
        {
            if (!object || object->IsDestroyMark()) continue;
            Animator* candidate = object->GetComponent<Animator>();
            if (nullptr == candidate || 0 == candidate->GetSkeletonSerial()) continue;
            animator = candidate;
            break;
        }
        if (nullptr == animator)
        {
            std::printf("[CLI] experiment.animmask skip animators=0\n");
            return PreconditionFailed("animation.coverage_missing", "No typed Animator fixture");
        }

        AvatarMask channelMask; // 실물 창구 산출
        AnimatorDataPath maskPath = AnimatorDataPath::None;
        BoneMask* channelRoot =
            animator->BuildAvatarBoneMasks(channelMask, nullptr, &maskPath);
        const bool viaGeneration = AnimatorDataPath::Generation == maskPath;

        std::vector<std::string> failures;
        if (nullptr == channelRoot) failures.push_back("루트 마스크 생성 실패");

        // I6-B4a — **독립 유도** 대조. 마스크 트리를 스켈레톤 원자료(부모 인덱스)와
        // 직접 맞춘다: ① 마스크 하나당 뼈 하나 ② 마스크 트리의 부모 관계가
        // 스켈레톤의 parent와 같다 ③ 부모가 자식보다 앞에 온다(저장분 인덱스
        // 대응이 전제하는 preorder). DFS를 다시 구현해 순서를 맞추지 않는다 —
        // 같은 규약을 두 번 쓰면 둘이 함께 틀려도 초록이다.
        const char* structureAxis = "n/a";
        std::vector<std::pair<std::string, std::size_t>> structureBones; // (name, parent or npos)
        if (const assets::ModelSkeletonAsset* typedSkeleton = animator->TypedSkeleton())
        {
            for (const assets::ModelBoneAsset& bone : typedSkeleton->bones)
            {
                structureBones.emplace_back(bone.name,
                    bone.parent == assets::kInvalidModelAssetIndex
                    ? std::string::npos : static_cast<std::size_t>(bone.parent));
            }
        }
        if (!structureBones.empty())
        {
            std::unordered_map<std::string, std::size_t> boneIndexOf;
            for (std::size_t index = 0; index < structureBones.size(); ++index)
            {
                boneIndexOf.emplace(structureBones[index].first, index);
            }
            std::unordered_map<const BoneMask*, std::size_t> position;
            for (std::size_t index = 0;
                index < channelMask.m_BoneMasks.size(); ++index)
            {
                position.emplace(channelMask.m_BoneMasks[index], index);
            }

            bool structureOk =
                channelMask.m_BoneMasks.size() == structureBones.size();
            if (!structureOk)
            {
                failures.push_back("구조: 마스크 " +
                    std::to_string(channelMask.m_BoneMasks.size()) + " vs 뼈 " +
                    std::to_string(structureBones.size()));
            }
            for (std::size_t index = 0;
                structureOk && index < channelMask.m_BoneMasks.size(); ++index)
            {
                const BoneMask* mask = channelMask.m_BoneMasks[index];
                const auto self = boneIndexOf.find(mask->boneName);
                if (self == boneIndexOf.end())
                {
                    failures.push_back("구조: 스켈레톤에 없는 이름 "
                        + mask->boneName);
                    structureOk = false;
                    break;
                }
                for (const BoneMask* child : mask->m_children)
                {
                    const auto childIndex = boneIndexOf.find(child->boneName);
                    if (childIndex == boneIndexOf.end()
                        || structureBones[childIndex->second].second != self->second)
                    {
                        failures.push_back("구조: 부모 관계 어긋남 "
                            + mask->boneName + " -> " + child->boneName);
                        structureOk = false;
                        break;
                    }
                    const auto childPos = position.find(child);
                    if (childPos == position.end() || childPos->second <= index)
                    {
                        failures.push_back("구조: 자식이 부모보다 앞에 온다 "
                            + child->boneName);
                        structureOk = false;
                        break;
                    }
                }
            }
            structureAxis = structureOk ? "ok" : "fail";
        }
        else
        {
            failures.push_back("구조: typed skeleton 원자료 없음");
        }
        if (!viaGeneration) failures.push_back("경로: generation 아님");

        if (failures.empty())
        {
            std::printf("[CLI] experiment.animmask pass masks=%zu "
                "viaGeneration=%d structure=%s\n",
                channelMask.m_BoneMasks.size(), viaGeneration ? 1 : 0, structureAxis);
        }
        else
        {
            std::string joined;
            for (const std::string& failure : failures)
                joined += " [" + failure + "]";
            std::printf("[CLI] experiment.animmask fail viaGeneration=%d%s\n",
                viaGeneration ? 1 : 0, joined.c_str());
        }
        auto data = CommandData::Object();
        data.Set("masks", CommandData::Int(channelMask.m_BoneMasks.size()));
        data.Set("viaGeneration", CommandData::Bool(viaGeneration));
        data.Set("structure", CommandData::String(structureAxis));
        data.Set("failures", CommandData::Int(failures.size()));
        return failures.empty() ? Ok({}, std::move(data)) : Fail("experiment.animmask.failed", "Commandlet verification failed", std::move(data));
    }

    // I5-D5b — 에디터 실소비 창구의 전수 A/B. 에디터 UI 자체는 헤드리스
    // 관측 밖이라(--script 라이브는 렌더 0프레임) UI를 재지 않고, UI가
    // 지나게 된 **창구 두 개**를 씬 전수에 태워 legacy 직소비와 대조한다.
    //
    //   clip 축  — Animator::GetClipCount/GetClipName vs
    //              m_Skeleton->m_animations(size·m_name). 개수만 재면 순서가
    //              뒤집혀도 초록이라 **이름을 인덱스별로** 맞춘다: 편집
    //              정본(D4e-2 오버라이드)이 인덱스 축이므로 순서가 어긋나면
    //              다른 클립을 편집하게 된다.
    //   mesh 축  — MeshRenderer::HasRenderableMesh vs (bool)m_Mesh. D4f
    //              이전에는 둘이 동치여야 한다(역브리지가 항상 짝을 만든다).
    //
    // 경로 계수(viaExperiment)는 창구 내부의 실분기 관측이다 — 조건 재현이
    // 아니라서 experiment 분기가 소실돼 legacy 폴백으로 조용히 덮여도 여기서
    // 갈린다.

    static CommandCore::CommandResult Cmd_experiment_editorsurface(const ConsoleCommandContext& ctx)
    {
        using namespace CommandCore;
        if (ctx.parts.size() != 1) return InvalidArguments("experiment.editorsurface");
        Scene* scene = SceneManagers->GetActiveScene();
        if (nullptr == scene)
        {
            std::printf("[CLI] experiment.editorsurface fail 활성 씬 없음\n");
            return PreconditionFailed("scene.not_found", "No active scene");
        }

        // MBC9 — 창구(GetClipCount/GetClipName/GetClipFrameCount·HasRenderableMesh)가
        // typed generation 원자료와 인덱스별로 맞는지 잰다. legacy 대조군은 은퇴했다.
        std::size_t animators = 0, clipViaGeneration = 0;
        std::size_t clipsChecked = 0, clipCountMismatch = 0, clipNameMismatch = 0;
        std::size_t clipFrameMismatch = 0;
        std::size_t renderers = 0, meshPresent = 0, meshGuardMismatch = 0;
        std::string firstMismatch;

        for (const auto& object : scene->m_Entities)
        {
            if (!object || object->IsDestroyMark()) continue;

            Animator* animator = object->GetComponent<Animator>();
            if (nullptr != animator && 0 != animator->GetSkeletonSerial())
            {
                ++animators;
                AnimatorDataPath clipPath = AnimatorDataPath::None;
                const std::size_t windowCount = animator->GetClipCount(nullptr, &clipPath);
                if (AnimatorDataPath::Generation == clipPath) ++clipViaGeneration;

                const auto clips = animator->m_modelGeneration
                    ? animator->m_modelGeneration->Animations()
                    : std::span<const assets::ModelAnimationAsset>{};
                if (windowCount != clips.size())
                {
                    ++clipCountMismatch;
                    if (firstMismatch.empty())
                    {
                        firstMismatch = "clipCount " + std::to_string(windowCount)
                            + "/" + std::to_string(clips.size());
                    }
                }
                const std::size_t common = (std::min)(windowCount, clips.size());
                for (std::size_t clip = 0; clip < common; ++clip)
                {
                    ++clipsChecked;
                    const std::string windowName = animator->GetClipName(static_cast<int>(clip));
                    if (windowName != clips[clip].name)
                    {
                        ++clipNameMismatch;
                        if (firstMismatch.empty())
                        {
                            firstMismatch = "clipName[" + std::to_string(clip) + "] "
                                + windowName + "/" + clips[clip].name;
                        }
                    }
                    // frame 축 — 유니크 키 시각 수(이벤트 저작의 frameKey 상한).
                    const std::size_t windowFrames =
                        animator->GetClipFrameCount(static_cast<int>(clip));
                    const std::size_t assetFrames =
                        assets::animation::CountUniqueKeyTimes(clips[clip]);
                    if (windowFrames != assetFrames)
                    {
                        ++clipFrameMismatch;
                        if (firstMismatch.empty())
                        {
                            firstMismatch = "clipFrames[" + std::to_string(clip) + "] "
                                + std::to_string(windowFrames) + "/"
                                + std::to_string(assetFrames);
                        }
                    }
                }
            }

            MeshRenderer* renderer = object->GetComponent<MeshRenderer>();
            if (nullptr != renderer)
            {
                ++renderers;
                const bool windowHas = renderer->HasRenderableMesh();
                RHIModelMeshView view{};
                const bool typedHas = renderer->m_modelGeneration
                    && BuildRHIModelMeshView(*renderer->m_modelGeneration,
                        renderer->m_modelMeshIndex, view) && view.IsComplete();
                if (windowHas) ++meshPresent;
                if (windowHas != typedHas)
                {
                    ++meshGuardMismatch;
                    if (firstMismatch.empty())
                    {
                        firstMismatch = std::string("meshGuard ") +
                            (windowHas ? "1/0" : "0/1") + " " +
                            object->m_name.ToString();
                    }
                }
            }
        }

        const bool covered = animators > 0 && renderers > 0 && clipsChecked > 0;
        const bool passed = covered && 0 == clipCountMismatch
            && 0 == clipNameMismatch && 0 == clipFrameMismatch
            && 0 == meshGuardMismatch && clipViaGeneration == animators;
        std::printf("[CLI] experiment.editorsurface %s animators=%zu "
            "clipGeneration=%zu clips=%zu countMismatch=%zu "
            "nameMismatch=%zu frameMismatch=%zu renderers=%zu "
            "meshPresent=%zu guardMismatch=%zu%s%s\n",
            passed ? "pass" : (covered ? "fail" : "skip"),
            animators, clipViaGeneration, clipsChecked,
            clipCountMismatch, clipNameMismatch, clipFrameMismatch,
            renderers, meshPresent, meshGuardMismatch,
            firstMismatch.empty() ? "" : " first=",
            firstMismatch.c_str());
        auto data = CommandData::Object();
        data.Set("animators", CommandData::Int(animators));
        data.Set("clipViaGeneration", CommandData::Int(clipViaGeneration));
        data.Set("clipsChecked", CommandData::Int(clipsChecked));
        data.Set("clipCountMismatch", CommandData::Int(clipCountMismatch));
        data.Set("clipNameMismatch", CommandData::Int(clipNameMismatch));
        data.Set("clipFrameMismatch", CommandData::Int(clipFrameMismatch));
        data.Set("renderers", CommandData::Int(renderers));
        data.Set("meshPresent", CommandData::Int(meshPresent));
        data.Set("meshGuardMismatch", CommandData::Int(meshGuardMismatch));
        data.Set("firstMismatch", CommandData::String(firstMismatch));
        if (!(covered)) { auto result = PreconditionFailed("model.coverage_missing", "Typed model fixture coverage is missing"); result.data = std::move(data); return result; }
        return passed ? Ok({}, std::move(data)) : Fail("experiment.editorsurface.failed", "Commandlet verification failed", std::move(data));
    }

    // I6-B4b — 재생 팔레트 골든. D4e-1은 legacy 재귀와 experiment 단일 순회를
    // 같은 입력으로 돌려 원소 단위로 대조했는데, B4b가 legacy 재귀를 걷어
    // **대조할 팔이 없어졌다**. 남은 것은 제품 포즈 함수를 결정적 표본으로
    // 태워 접은 digest이고, 그것이 재는 것은 정확성이 아니라 **안정성**이다.
    //
    // ★ 이 강등을 축소해 적지 않는다. "맞다"를 말하던 축이 "어제와 같다"로
    //   내려온 것이고, 그 인수인계는 B4a가 legacy가 살아 있는 구간에서
    //   변이로 증명해 두었다(8c·9b·6b).
    //
    // ★ 표본은 클립 구간 5점 × 전 클립이다. 라이브 틱은 실시간이라 비결정이라
    //   골든이 성립하지 않는다 — 이 명령이 결정적 입력을 만든다.
    struct AnimtickAxisResult final
    {
        std::size_t clipCount{};
        std::size_t sampleCount{};
        std::size_t failedEvaluations{};
        // 1/4096 양자화 FNV. 양자화에도 **엄격하다** — 512뼈×16원소×50표본이면
        // 어떤 값 하나는 늘 반올림 경계 근처라, 양자 미만 섭동(0.0001)에도
        // 값이 바뀐다(B4a 실측). x64 Debug 이 툴체인 고정 골든이다.
        std::uint32_t poseDigest{ 2166136261u };
    };

    static void FoldPoseDigest(const std::vector<math::matrix4x4>& pose,
        std::uint32_t& digest)
    {
        for (std::size_t bone = 0; bone < MAX_BONES; ++bone)
        {
            for (int element = 0; element < 16; ++element)
            {
                const float value = (&pose[bone].m[0][0])[element];
                const std::int32_t quantized = static_cast<std::int32_t>(
                    std::lround(static_cast<double>(value) * 4096.0));
                std::uint32_t bits = static_cast<std::uint32_t>(quantized);
                for (int byte = 0; byte < 4; ++byte)
                {
                    digest ^= (bits >> (byte * 8)) & 0xFFu;
                    digest *= 16777619u;
                }
            }
        }
    }

    // PHASE 3.75 MBC8 — typed 표본. 같은 자산이면 experiment 판과 같은 digest를
    // 내야 한다 — 골든(6b)이 typed 샘플러의 정확성 게이트다.
    static void MeasureAnimtickAxisTyped(AnimationJob& job, Animator& animator,
        const assets::ModelAssetGeneration& generation, AnimtickAxisResult& result)
    {
        static constexpr float kSampleFractions[]{ 0.f, 0.25f, 0.5f, 0.75f, 0.95f };
        std::vector<math::matrix4x4> pose(MAX_BONES);
        const auto clips = generation.Animations();
        for (std::size_t clip = 0; clip < clips.size(); ++clip)
        {
            ++result.clipCount;
            const float duration = static_cast<float>(clips[clip].durationTicks);
            for (const float fraction : kSampleFractions)
            {
                if (!job.EvaluateGenerationPose(animator, generation,
                    static_cast<int>(clip), duration * fraction, pose.data()))
                {
                    ++result.failedEvaluations;
                    continue;
                }
                ++result.sampleCount;
                FoldPoseDigest(pose, result.poseDigest);
            }
        }
    }

    // 스킨 기하 축 — experiment.skinbounds
    //
    // ★ 왜 CPU에서 다시 푸는가: 헤드리스는 라이브 팔레트로 그린 그림을 픽셀로
    //   못 잰다(--script 는 렌더 0프레임). 그래서 그림 대신 **그림의 입력**을
    //   잰다: 팔레트 × 정점 스킨을 CPU에서 풀어 결과 기하가 성한지 본다.
    //
    // ★ 독립 유도다. 렌더 경로의 산술을 재구현하는 것이 아니라 스키닝 정의
    //   자체(가중합)를 쓰고, 판정은 **바인드 포즈 대비 크기 비율**이다 —
    //   팔레트 규약이 어긋나거나 인덱스가 밀리면 기하가 폭발하거나 접힌다.
    //   B4b가 깨뜨릴 수 있는 것이 정확히 그것이다.
    //
    // ★ 크기 비율은 자다(2026-09-02 정정). 처음엔 7,115배가 나와 "밖에서 팔레트
    //   규약을 못 맞춘다"고 관측값으로만 남겼는데, 그것은 규약의 문제가 아니라
    //   **팔레트가 실제로 폭발해 있던 것**이었다 — glTF 임포터가 inverseBind
    //   를 전치된 채 게시했고(GltfImporter.cpp 참조), dx12.scene 이 포화한
    //   것도 하네스 한계가 아니라 그 폭발을 옳게 그린 결과였다. 아래 식은
    //   셰이더(mul(v, P) 행 벡터)와 같은 산술이라 비율이 곧 화면의 크기다.
    //   그래서 유한성·인덱스 범위에 **크기 상한**을 더한다.

    static CommandCore::CommandResult Cmd_experiment_skinbounds(const ConsoleCommandContext& ctx)
    {
        using namespace CommandCore;
        if (ctx.parts.size() != 1) return InvalidArguments("experiment.skinbounds");
        Scene* scene = SceneManagers->GetActiveScene();
        if (nullptr == scene)
        {
            std::printf("[CLI] experiment.skinbounds fail 활성 씬 없음\n");
            return PreconditionFailed("scene.not_found", "No active scene");
        }

        std::size_t meshes = 0, nonFinite = 0, emptyWeights = 0, outOfRange = 0;
        // 왜 걸러졌는지 세지 않으면 meshes=0이 "대상 없음"인지 "조회 실패"인지
        // 구별되지 않는다 — skip을 통과로 읽는 실수를 막는 계수다.
        std::size_t renderers = 0, withModel = 0, withSkin = 0, withAnimator = 0;
        double worstRatio = 0.0;
        std::string worstMesh;
        std::uint32_t digest = 2166136261u;

        for (const auto& object : scene->m_Entities)
        {
            if (!object || object->IsDestroyMark()) continue;
            MeshRenderer* renderer = object->GetComponent<MeshRenderer>();
            if (nullptr == renderer) continue;
            ++renderers;
            if (!renderer->m_modelGeneration
                || renderer->m_modelMeshIndex >= renderer->m_modelGeneration->Meshes().size())
            {
                continue;
            }
            ++withModel;
            // MBC9 — typed generation의 packed 정점을 레이아웃 표(ModelVertexLayout)로
            // 디코드한다. 스킨 속성이 없으면 대상이 아니다.
            const assets::ModelMeshAsset* mesh =
                &renderer->m_modelGeneration->Meshes()[renderer->m_modelMeshIndex];
            const assets::VertexAttributeMask layout = mesh->vertexAttributeMask;
            if (!assets::Has(layout, assets::VertexAttribute::BoneIndices)
                || !assets::Has(layout, assets::VertexAttribute::BoneWeights)
                || !assets::Has(layout, assets::VertexAttribute::Position)
                || 0 == mesh->vertexStride)
            {
                continue;
            }
            ++withSkin;
            const std::uint32_t positionOffset =
                assets::OffsetOf(layout, assets::VertexAttribute::Position);
            const std::uint32_t boneIndexOffset =
                assets::OffsetOf(layout, assets::VertexAttribute::BoneIndices);
            const std::uint32_t boneWeightOffset =
                assets::OffsetOf(layout, assets::VertexAttribute::BoneWeights);

            // ★ 메시 엔티티는 GetRootIndex를 안 채운다(SetRootIndex는 본
            //   오브젝트에만 걸린다). 프록시가 쓰는 것과 같은 규칙으로 부모 사슬을
            //   거슬러 찾는다(ProxyCommand::FindEnabledAnimator).
            Animator* animator = nullptr;
            for (Entity::Index parent = object->GetParentIndex();
                parent != Entity::INVALID_INDEX; )
            {
                Entity* owner = object->OwnerSceneFindIndex(parent);
                if (nullptr == owner) break;
                if (Animator* candidate = owner->GetComponent<Animator>())
                {
                    animator = candidate;
                    break;
                }
                parent = owner->GetParentIndex();
            }
            if (nullptr == animator || 0 == animator->GetSkeletonSerial()) continue;
            ++withAnimator;

            ++meshes;
            const std::size_t boneCount = animator->GetBoneCount();
            float skinnedMin[3] = { FLT_MAX, FLT_MAX, FLT_MAX };
            float skinnedMax[3] = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
            float bindMin[3] = { FLT_MAX, FLT_MAX, FLT_MAX };
            float bindMax[3] = { -FLT_MAX, -FLT_MAX, -FLT_MAX };

            const std::size_t vertexCount = mesh->vertexBytes.size() / mesh->vertexStride;
            const std::byte* base = mesh->vertexBytes.data();
            for (std::size_t index = 0; index < vertexCount; ++index)
            {
                const std::byte* vertex = base + index * mesh->vertexStride;
                float position[3]{};
                float boneWeights[4]{};
                std::uint8_t boneIndices[4]{};
                std::memcpy(position, vertex + positionOffset, sizeof(position));
                std::memcpy(boneWeights, vertex + boneWeightOffset, sizeof(boneWeights));
                std::memcpy(boneIndices, vertex + boneIndexOffset, sizeof(boneIndices));
                float weightSum = 0.f;
                float skinned[3] = { 0.f, 0.f, 0.f };
                for (std::size_t lane = 0; lane < 4; ++lane)
                {
                    const float weight = boneWeights[lane];
                    if (weight <= 0.f) continue;
                    const std::size_t bone = boneIndices[lane];
                    if (bone >= boneCount || bone >= MAX_BONES)
                    {
                        ++outOfRange;
                        continue;
                    }
                    weightSum += weight;
                    const math::matrix4x4& m = animator->m_FinalTransforms[bone];
                    const float px = position[0];
                    const float py = position[1];
                    const float pz = position[2];
                    // 행 벡터 규약 v·M — GBuffer.hlsl 의 mul(v, gBones[i]) 과
                    // 같은 산술이다(패스가 transpose 해 올리고 HLSL 이 열 우선
                    // 으로 읽어 되돌린다). 이동 성분은 m[3][0..2].
                    skinned[0] += weight * (m.m[0][0] * px + m.m[1][0] * py
                        + m.m[2][0] * pz + m.m[3][0]);
                    skinned[1] += weight * (m.m[0][1] * px + m.m[1][1] * py
                        + m.m[2][1] * pz + m.m[3][1]);
                    skinned[2] += weight * (m.m[0][2] * px + m.m[1][2] * py
                        + m.m[2][2] * pz + m.m[3][2]);
                }
                if (weightSum <= 0.f) { ++emptyWeights; continue; }
                if (!std::isfinite(skinned[0]) || !std::isfinite(skinned[1])
                    || !std::isfinite(skinned[2]))
                {
                    ++nonFinite;
                    continue;
                }
                const float bind[3] = { position[0], position[1], position[2] };
                for (int axis = 0; axis < 3; ++axis)
                {
                    skinnedMin[axis] = (std::min)(skinnedMin[axis], skinned[axis]);
                    skinnedMax[axis] = (std::max)(skinnedMax[axis], skinned[axis]);
                    bindMin[axis] = (std::min)(bindMin[axis], bind[axis]);
                    bindMax[axis] = (std::max)(bindMax[axis], bind[axis]);
                    const std::int32_t q = static_cast<std::int32_t>(
                        std::lround(static_cast<double>(skinned[axis]) * 256.0));
                    std::uint32_t bits = static_cast<std::uint32_t>(q);
                    for (int byte = 0; byte < 4; ++byte)
                    {
                        digest ^= (bits >> (byte * 8)) & 0xFFu;
                        digest *= 16777619u;
                    }
                }
            }

            double skinnedExtent = 0.0, bindExtent = 0.0;
            for (int axis = 0; axis < 3; ++axis)
            {
                skinnedExtent = (std::max)(skinnedExtent,
                    (double)(skinnedMax[axis] - skinnedMin[axis]));
                bindExtent = (std::max)(bindExtent,
                    (double)(bindMax[axis] - bindMin[axis]));
            }
            const double ratio = bindExtent > 0.0
                ? skinnedExtent / bindExtent : 0.0;
            if (ratio > worstRatio) { worstRatio = ratio; worstMesh = mesh->name; }
        }

        // 판정 셋 — 유한성·인덱스 범위·크기 상한. 상한은 "포즈가 바인드의
        // 몇 배까지 커질 수 있나"의 넉넉한 값이다: 정상 포즈는 1배 근방이고
        // (실측 Gunner 0.0/0.5/0.9 전부 1.0x 대), 규약이 어긋나면 수천 배다.
        // 그 사이 어디에 둬도 같은 판정이라 4배로 잡는다. digest는 포즈별
        // 골든으로 쓴다.
        constexpr double kMaxSkinnedExtentRatio = 4.0;
        const bool passed = meshes > 0 && 0 == nonFinite && 0 == outOfRange
            && worstRatio <= kMaxSkinnedExtentRatio;
        std::printf("[CLI] experiment.skinbounds %s meshes=%zu nonFinite=%zu "
            "outOfRange=%zu emptyWeights=%zu worstRatio=%.4f worst=%s "
            "digest=%08X renderers=%zu withModel=%zu withSkin=%zu "
            "withAnimator=%zu\n",
            passed ? "pass" : (meshes == 0 ? "skip" : "fail"),
            meshes, nonFinite, outOfRange, emptyWeights, worstRatio,
            worstMesh.empty() ? "-" : worstMesh.c_str(), digest,
            renderers, withModel, withSkin, withAnimator);
        auto data = CommandData::Object();
        data.Set("meshes", CommandData::Int(meshes));
        data.Set("nonFinite", CommandData::Int(nonFinite));
        data.Set("emptyWeights", CommandData::Int(emptyWeights));
        data.Set("outOfRange", CommandData::Int(outOfRange));
        data.Set("renderers", CommandData::Int(renderers));
        data.Set("withModel", CommandData::Int(withModel));
        data.Set("withSkin", CommandData::Int(withSkin));
        data.Set("withAnimator", CommandData::Int(withAnimator));
        data.Set("digest", CommandData::Int(digest));
        data.Set("worstRatio", CommandData::Double(worstRatio));
        data.Set("worstMesh", CommandData::String(worstMesh));
        if (!(meshes > 0)) { auto result = PreconditionFailed("model.coverage_missing", "Typed model fixture coverage is missing"); result.data = std::move(data); return result; }
        return passed ? Ok({}, std::move(data)) : Fail("experiment.skinbounds.failed", "Commandlet verification failed", std::move(data));
    }

    // 결정적 포즈 고정 — experiment.animpose <fraction>
    //
    // ★ 왜 필요한가: 라이브 스키닝의 **그림**을 재려면 포즈가 결정적이어야
    //   한다. 기존 라이브 게이트는 그 비결정성을 Animator를 **꺼서** 피했고,
    //   그래서 재는 것이 바인드 포즈였다 — "스키닝 산술의 시각 판정은 이
    //   게이트 밖"이라고 시나리오 주석이 스스로 적어 뒀다. 그 공백에서
    //   B4b가 두 번 깨졌다.
    //
    // ★ 어떻게 고정하는가: 편집 모드는 GameLogic(0.0f) — delta 0이라 시간이
    //   안 흐른다(EditorMain 주석). 그 상태에서 m_TimeElapsed를 한 번 박으면
    //   틱이 매 프레임 **같은 시각의 포즈를 다시 계산**하므로 결과가 안정적이다.
    //   애니메이터를 끄지 않으므로 프록시가 팔레트를 계속 나른다.

    static CommandCore::CommandResult Cmd_experiment_animpose(const ConsoleCommandContext& ctx)
    {
        using namespace CommandCore;
        if (ctx.parts.size() < 2 || ctx.parts.size() > 3) return InvalidArguments("experiment.animpose <0..1> [clip]");
        const std::vector<std::string>& parts = ctx.parts;
        if (parts.size() < 2)
        {
            std::printf("[CLI] 사용법: experiment.animpose <0..1> [클립]\n");
            return InvalidArguments("experiment.animpose <0..1> [clip]");
        }
        Scene* scene = SceneManagers->GetActiveScene();
        if (nullptr == scene)
        {
            std::printf("[CLI] experiment.animpose fail 활성 씬 없음\n");
            return PreconditionFailed("scene.not_found", "No active scene");
        }
        double fraction{}; int clip{};
        if (!ParseNumber(parts[1], fraction) || fraction < 0 || fraction > 1 ||
            (parts.size() == 3 && (!ParseNumber(parts[2], clip) || clip < 0)))
            return InvalidArguments("Expected fraction 0..1 and non-negative clip index");

        std::size_t posed = 0;
        for (const auto& object : scene->m_Entities)
        {
            if (!object || object->IsDestroyMark()) continue;
            Animator* animator = object->GetComponent<Animator>();
            if (nullptr == animator || 0 == animator->GetSkeletonSerial()) continue;
            const double duration = animator->GetClipDuration(clip);
            if (duration <= 0.0) continue;

            animator->m_AnimIndexChosen = static_cast<uint32_t>(clip);
            animator->m_TimeElapsed =
                static_cast<float>(duration * fraction);
            ++posed;
            std::printf("[CLI] experiment.animpose %s clip=%d elapsed=%.4f "
                "duration=%.4f\n",
                object->GetHashedName().ToString().c_str(), clip,
                animator->m_TimeElapsed, duration);
        }
        std::printf("[CLI] experiment.animpose done posed=%zu fraction=%.4f\n",
            posed, fraction);
        auto data = CommandData::Object(); data.Set("posed", CommandData::Int(posed));
        data.Set("fraction", CommandData::Double(fraction)); data.Set("clip", CommandData::Int(clip));
        if (posed == 0) { auto result = PreconditionFailed("animation.coverage_missing", "No matching typed Animator clip"); result.data = std::move(data); return result; }
        return Ok({}, std::move(data));
    }

    // 라이브 재생 관측 — animator.status
    //
    // ★ 이 저장소에는 **살아 있는 애니메이터가 실제로 도는가**를 재는 축이
    //   없었다(헤드리스는 프레임을 완성하지 않고, 라이브 게이트는 저장 직전에
    //   Animator를 꺼 버리며, animtick은 포즈를 다시 계산할 뿐 라이브 팔레트를
    //   읽지 않는다). B4b 되돌림의 직접 사유가 그 공백이라, 그 자리를 메우는
    //   가장 작은 관측부터 세운다.
    //
    // 재는 것: 애니메이터별 경로·선택 클립·경과 시간과 **라이브 팔레트
    // (m_FinalTransforms)의 digest**. 시간을 두고 두 번 부르면 digest가
    // 달라져야 "재생 중"이다 — 같으면 팔레트가 굳은 것이고, 그것이 화면에서
    // 안 움직이는 것의 직접 원인이다.

    static CommandCore::CommandResult Cmd_animator_status(const ConsoleCommandContext& ctx)
    {
        if (ctx.parts.size() != 1) return CommandCore::InvalidArguments("animator.status accepts no arguments");
        return EditorDiagnostics::AnimatorStatus(SceneManagers->GetActiveScene());
    }

    static CommandCore::CommandResult Cmd_experiment_animtick(const ConsoleCommandContext& ctx)
    {
        using namespace CommandCore;
        if (ctx.parts.size() != 1) return InvalidArguments("experiment.animtick");
        Scene* scene = SceneManagers->GetActiveScene();
        RenderScene* renderScene = SceneManagers->GetRenderScene();
        if (nullptr == scene || nullptr == renderScene)
        {
            std::printf("[CLI] experiment.animtick fail 활성 씬/렌더 씬 없음\n");
            return PreconditionFailed("scene.not_found", "No active scene or render scene");
        }
        AnimationJob& job = renderScene->GetAnimationJob();

        std::size_t animatorCount = 0;
        std::size_t typedAnimators = 0;
        AnimtickAxisResult axis{};

        for (const auto& object : scene->m_Entities)
        {
            if (!object || object->IsDestroyMark()) continue;
            Animator* animator = object->GetComponent<Animator>();
            if (nullptr == animator) continue;
            // MBC9 — typed 정본 하나다.
            if (!animator->m_modelGeneration || nullptr == animator->TypedSkeleton()) continue;
            ++animatorCount;
            ++typedAnimators;
            MeasureAnimtickAxisTyped(job, *animator, *animator->m_modelGeneration, axis);
        }

        const bool passed = animatorCount > 0 && axis.sampleCount > 0
            && 0 == axis.failedEvaluations;
        std::printf("[CLI] experiment.animtick %s animators=%zu clips=%zu "
            "samples=%zu failedEval=%zu poseDigest=%08X path=%s typed=%zu\n",
            passed ? "pass" : (animatorCount == 0 ? "skip" : "fail"),
            animatorCount, axis.clipCount, axis.sampleCount,
            axis.failedEvaluations, axis.poseDigest,
            animatorCount > 0 ? "generation" : "none",
            typedAnimators);
        auto data = CommandData::Object();
        data.Set("animators", CommandData::Int(animatorCount));
        data.Set("typed", CommandData::Int(typedAnimators));
        data.Set("clips", CommandData::Int(axis.clipCount));
        data.Set("samples", CommandData::Int(axis.sampleCount));
        data.Set("failedEval", CommandData::Int(axis.failedEvaluations));
        data.Set("poseDigest", CommandData::Int(axis.poseDigest));
        if (animatorCount == 0) { auto result = PreconditionFailed("animation.coverage_missing", "Typed model fixture is absent"); result.data = std::move(data); return result; }
        return passed ? Ok({}, std::move(data)) : Fail("experiment.animtick.failed", "Commandlet verification failed", std::move(data));
    }

    static CommandCore::CommandResult Cmd_assets_identity(const ConsoleCommandContext& ctx)
    {
        using namespace CommandCore;
        if (ctx.parts.size() != 1) return InvalidArguments("assets.identity");
        // MBC1 — ce.uuidv8.sha256.v1 신원 프로필·충돌 registry 합성 검사. CPU 전용.
        // `assets.` 접두는 experiment가 아닌 정식 자산 계층(§5.1)의 명령이다.
        RenderTest::AssetIdentityReport report;
        std::string log;
        const bool passed = RenderTest::RunAssetIdentitySelfTest(log, &report);
        std::printf("%s", log.c_str());
        if (passed)
        {
            Debug->LogWarning(std::string("[assets.identity] 통과\n") + log);
        }
        else
        {
            Debug->LogError(std::string("[assets.identity] 실패\n") + log);
        }
        std::printf("[CLI] assets.identity %s\n", passed ? "통과" : "실패");
        auto data = CommandData::Object();
        data.Set("passed", CommandData::Int(report.passed));
        data.Set("failed", CommandData::Int(report.failed));
        data.Set("bcryptMatched", CommandData::Int(report.bcryptMatched));
        data.Set("bcryptTotal", CommandData::Int(report.bcryptTotal));
        data.Set("assertions", CommandData::Int(report.passed + report.failed));
        auto items = CommandData::Array();
        for (const auto& row : report.vectors)
        {
            auto item = CommandData::Object();
            item.Set("name", CommandData::String(row.name));
            item.Set("uuid", CommandData::String(row.uuid));
            items.Append(std::move(item));
        }
        data.Set("vectors", std::move(items));
        data.Set("log", CommandData::String(std::move(log)));
        return passed ? Ok({}, std::move(data)) : Fail("assets.identity.failed", "Commandlet verification failed", std::move(data));
    }

    static CommandCore::CommandResult Cmd_assets_sidecar(const ConsoleCommandContext& ctx)
    {
        using namespace CommandCore;
        if (ctx.parts.size() > 2) return InvalidArguments("assets.sidecar [asset-root]");
        // MBC2 — epoch header·stable key·sidecar schema v2. 인자로 asset root를 주면
        // 실자산 corpus를 임포트해 폐포를 검증한다(디스크에 쓰지 않는다 — 쓰기는 MBC3).
        const std::string assetRoot = ctx.parts.size() > 1 ? ctx.parts[1] : std::string();
        RenderTest::AssetSidecarReport report;
        std::string log;
        const bool passed = RenderTest::RunAssetSidecarSchemaSelfTest(assetRoot, log, &report);
        std::printf("%s", log.c_str());
        if (passed)
        {
            Debug->LogWarning(std::string("[assets.sidecar] 통과\n") + log);
        }
        else
        {
            Debug->LogError(std::string("[assets.sidecar] 실패\n") + log);
        }
        std::printf("[CLI] assets.sidecar %s\n", passed ? "통과" : "실패");
        auto data = CommandData::Object();
        data.Set("passed", CommandData::Int(report.passed));
        data.Set("failed", CommandData::Int(report.failed));
        data.Set("models", CommandData::Int(report.models));
        data.Set("modelsPassed", CommandData::Int(report.modelsPassed));
        data.Set("subAssets", CommandData::Int(report.subAssets));
        data.Set("registry", CommandData::Int(report.registry));
        data.Set("assertions", CommandData::Int(report.passed + report.failed));
        auto items = CommandData::Array();
        for (const auto& row : report.corpus)
        {
            auto item = CommandData::Object();
            item.Set("name", CommandData::String(row.name));
            item.Set("ok", CommandData::Bool(row.ok));
            item.Set("mat", CommandData::Int(row.mat));
            item.Set("matSem", CommandData::Int(row.matSem));
            item.Set("matAuth", CommandData::Int(row.matAuth));
            item.Set("tex", CommandData::Int(row.tex));
            item.Set("texSem", CommandData::Int(row.texSem));
            item.Set("texAuth", CommandData::Int(row.texAuth));
            item.Set("mesh", CommandData::Int(row.mesh));
            item.Set("skel", CommandData::Int(row.skel));
            item.Set("anim", CommandData::Int(row.anim));
            items.Append(std::move(item));
        }
        data.Set("corpus", std::move(items));
        data.Set("log", CommandData::String(std::move(log)));
        return passed ? Ok({}, std::move(data)) : Fail("assets.sidecar.failed", "Commandlet verification failed", std::move(data));
    }

    static CommandCore::CommandResult Cmd_assets_generation(const ConsoleCommandContext& ctx)
    {
        using namespace CommandCore;
        if (ctx.parts.size() != 2) return InvalidArguments("assets.generation <project-root>");
        if (ctx.parts.size() < 2)
        {
            Debug->LogWarning("[assets.generation] 사용법: assets.generation <fixture project root>");
            std::printf("[CLI] assets.generation 사용법: assets.generation <project root>\n");
            return InvalidArguments("assets.generation <project-root>");
        }
        std::string log;
        const bool passed = RenderTest::RunModelAssetGenerationSelfTest(
            ctx.parts[1], log);
        std::printf("%s", log.c_str());
        if (passed)
            Debug->LogWarning(std::string("[assets.generation] 통과\n") + log);
        else
            Debug->LogError(std::string("[assets.generation] 실패\n") + log);
        std::printf("[CLI] assets.generation %s\n", passed ? "PASS" : "FAIL");
        auto data = CommandData::Object();
        data.Set("log", CommandData::String(std::move(log)));
        return passed ? Ok({}, std::move(data)) : Fail("assets.generation.failed", "Commandlet verification failed", std::move(data));
    }

    static CommandCore::CommandResult Cmd_assets_generationcorpus(const ConsoleCommandContext& ctx)
    {
        using namespace CommandCore;
        if (ctx.parts.size() != 2) return InvalidArguments("assets.generationcorpus <content-root>");
        if (ctx.parts.size() < 2)
        {
            Debug->LogWarning("[assets.generationcorpus] 사용법: assets.generationcorpus <runtime content root>");
            std::printf("[CLI] assets.generationcorpus 사용법: assets.generationcorpus <content root>\n");
            return InvalidArguments("assets.generationcorpus <content-root>");
        }
        std::string log;
        const bool passed = RenderTest::RunModelAssetGenerationCorpusSelfTest(
            ctx.parts[1], log);
        std::printf("%s", log.c_str());
        if (passed)
            Debug->LogWarning(std::string("[assets.generationcorpus] 통과\n") + log);
        else
            Debug->LogError(std::string("[assets.generationcorpus] 실패\n") + log);
        std::printf("[CLI] assets.generationcorpus %s\n",
            passed ? "PASS" : "FAIL");
        auto data = CommandData::Object();
        data.Set("log", CommandData::String(std::move(log)));
        return passed ? Ok({}, std::move(data)) : Fail("assets.generationcorpus.failed", "Commandlet verification failed", std::move(data));
    }

    static CommandCore::CommandResult Cmd_assets_modelrender(const ConsoleCommandContext& ctx)
    {
        using namespace CommandCore;
        if (ctx.parts.size() != 1) return InvalidArguments("assets.modelrender");
        std::string log;
        const bool passed = RenderTest::RunModelRenderWiringSelfTest(log);
        std::printf("%s", log.c_str());
        if (passed)
            Debug->LogWarning(std::string("[assets.modelrender] 통과\n") + log);
        else
            Debug->LogError(std::string("[assets.modelrender] 실패\n") + log);
        std::printf("[CLI] assets.modelrender %s\n", passed ? "PASS" : "FAIL");
        auto data = CommandData::Object();
        data.Set("log", CommandData::String(std::move(log)));
        return passed ? Ok({}, std::move(data)) : Fail("assets.modelrender.failed", "Commandlet verification failed", std::move(data));
    }

    // PHASE 3.75 MBC7 — 활성 씬의 MeshRenderer가 typed generation handle·closure
    // 텍스처로 서 있는가(씬 전수). `assets.scenemodel reload <모델 이름>`은 reimport
    // 뒤 이전 texture generation이 재사용되지 않는가를 같은 프로세스에서 잰다.
    // PHASE 3.75 MBC10 — 읽기 전용 모델 소비 스냅샷. 제품 경로(MeshRenderer 해석·씬
    // 인스턴스화·Animator 틱)는 stdout 토큰 대신 계수만 올리고, 이 명령이 그것을 읽는다.
    // 아무 상태도 바꾸지 않는다(리셋도 없다).

    static CommandCore::CommandResult Cmd_assets_modeldiag(const ConsoleCommandContext& ctx)
    {
        using namespace CommandCore;
        if (ctx.parts.size() != 1) return InvalidArguments("This command takes no arguments");
        const ModelConsumptionSnapshot snapshot = ModelConsumptionDiagnostics::Snapshot();
        const DataSystem::ModelGenerationSourceSnapshot sources =
            DataSystems->SnapshotModelGenerationSources();
        std::printf("[CLI] assets.modeldiag meshResolveGeneration=%llu meshResolveFailed=%llu "
            "instantiateGeneration=%llu instantiateRejected=%llu tickGeneration=%llu "
            "tickNone=%llu lastInstantiated=%s generationFromCatalog=%llu "
            "generationFromLibrary=%llu generationLoadFailed=%llu\n",
            (unsigned long long)snapshot.meshResolveGeneration,
            (unsigned long long)snapshot.meshResolveFailed,
            (unsigned long long)snapshot.instantiateGeneration,
            (unsigned long long)snapshot.instantiateRejected,
            (unsigned long long)snapshot.tickGeneration,
            (unsigned long long)snapshot.tickNone,
            snapshot.lastInstantiated.empty() ? "-" : snapshot.lastInstantiated.c_str(),
            (unsigned long long)sources.fromCatalog,
            (unsigned long long)sources.fromLibrary,
            (unsigned long long)sources.failed);
        auto data = CommandData::Object();
        data.Set("meshResolveGeneration", CommandData::Int(snapshot.meshResolveGeneration));
        data.Set("meshResolveFailed", CommandData::Int(snapshot.meshResolveFailed));
        data.Set("instantiateGeneration", CommandData::Int(snapshot.instantiateGeneration));
        data.Set("instantiateRejected", CommandData::Int(snapshot.instantiateRejected));
        data.Set("tickGeneration", CommandData::Int(snapshot.tickGeneration));
        data.Set("tickNone", CommandData::Int(snapshot.tickNone));
        data.Set("lastInstantiated", CommandData::String(snapshot.lastInstantiated));
        data.Set("generationFromCatalog", CommandData::Int(sources.fromCatalog));
        data.Set("generationFromLibrary", CommandData::Int(sources.fromLibrary));
        data.Set("generationLoadFailed", CommandData::Int(sources.failed));
        return Ok({}, std::move(data));
    }

    // PHASE 3.75 MBC11 — §8.4 B1/B2/B5/B6 계측.
    //   assets.modelbench <dir|-> <iterations> [cooked|author]
    //   cooked: 모델마다 generation을 은퇴(ContentReload)시킨 뒤 cooked generation 로드를
    //           N회 재고(min/avg ms) — 관측 밖의 상태 변경은 캐시 은퇴·재게시뿐이다.
    //   author: <dir>이 <project>/Assets/<sub>인 **사본 프로젝트**에서 authoring transaction
    //           (소스 디코드 → sidecar → generation 게시)을 N회 재고(min/avg ms). 작업
    //           트리의 sidecar를 건드리지 않도록 사본에서만 부른다(게이트가 사본을 만든다).
    //   끝에 프로세스 peak working set과 VRAM 사용량을 찍는다.

    static CommandCore::CommandResult Cmd_assets_modelbench(const ConsoleCommandContext& ctx)
    {
        using namespace CommandCore;
        if (ctx.parts.size() > 4 || (ctx.parts.size() == 4 && ctx.parts[3] != "author" && ctx.parts[3] != "cooked")) return InvalidArguments("assets.modelbench [root|-] [iterations] [author|cooked]");
        int iterations = 5;
        if (ctx.parts.size() > 2 && (!ParseNumber(ctx.parts[2], iterations) || iterations < 1 || iterations > 10000))
            return InvalidArguments("iterations must be 1..10000");
        auto rows = CommandData::Array();
        const auto phasesData = [](const assets::ModelAssetPhaseTimeline& phases) {
            auto result = CommandData::Array();
            for (const auto& phase : phases) { auto item = CommandData::Object(); item.Set("phase", CommandData::String(phase.phase)); item.Set("milliseconds", CommandData::Double(phase.milliseconds)); result.Append(std::move(item)); }
            return result;
        };
        const bool authorMode = ctx.parts.size() > 3 && ctx.parts[3] == "author";
        file::path root = (ctx.parts.size() > 1 && ctx.parts[1] != "-")
            ? file::path(ctx.parts[1]) : file::path(PathFinder::ModelSourcePath());
        std::error_code error;
        if (!file::is_directory(root, error))
        {
            std::printf("[CLI] assets.modelbench fail 디렉터리가 아니다: %s\n",
                root.string().c_str());
            return PreconditionFailed("model.corpus_missing", "Model directory is unavailable");
        }
        std::vector<file::path> models;
        for (file::recursive_directory_iterator it(root, error), end; !error && it != end;
            it.increment(error))
        {
            if (!it->is_regular_file(error)) continue;
            std::string ext = it->path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (ext == ".glb" || ext == ".gltf" || ext == ".fbx") models.push_back(it->path());
        }
        if (error) return Fail("model.corpus_read_failed", error.message());
        std::sort(models.begin(), models.end());

        std::size_t failed = 0, measured = 0, skipped = 0;
        if (authorMode)
        {
            // 사본 프로젝트 배치: <project>/Assets/<sub>/<model>.
            const file::path assetRoot = root.parent_path();
            const file::path projectRoot = assetRoot.parent_path();
            const file::path generationRoot = projectRoot / "Library" / "ModelAssetGenerations";
            const file::path identityHeader = projectRoot / "ProjectSetting" / "AssetIdentity.asset";
            if (assetRoot.filename() != file::path("Assets") || !file::exists(identityHeader, error))
            {
                std::printf("[CLI] assets.modelbench fail author 모드는 <project>/Assets/<sub> 사본과 "
                    "ProjectSetting/AssetIdentity.asset이 필요하다: %s\n", root.string().c_str());
                return InvalidArguments("Author mode requires a fixture project with Assets and AssetIdentity.asset");
            }
            const auto formatPhases = [](const assets::ModelAssetPhaseTimeline& phases)
                {
                    std::string text;
                    for (const assets::ModelAssetPhaseMs& phase : phases)
                    {
                        char value[32];
                        std::snprintf(value, sizeof(value), "%.3f", phase.milliseconds);
                        if (!text.empty()) text += ';';
                        text += phase.phase; text += ':'; text += value;
                    }
                    return text.empty() ? std::string("-") : text;
                };
            for (const file::path& model : models)
            {
                double minMs = 1e30, sumMs = 0.0;
                assets::ModelAssetPhaseTimeline minPhases;
                bool ok = true;
                std::size_t subAssets = 0;
                for (int i = 0; i < iterations; ++i)
                {
                    assets::ModelAssetAuthoringRequest request;
                    request.assetRoot = assetRoot;
                    request.sourcePath = model;
                    request.identityHeaderPath = identityHeader;
                    request.generationRoot = generationRoot;
                    const auto begin = std::chrono::steady_clock::now();
                    const assets::ModelAssetAuthoringResult result = assets::AuthorModelAsset(request);
                    const auto finish = std::chrono::steady_clock::now();
                    if (!result.Succeeded()) { ok = false; break; }
                    const double ms = std::chrono::duration<double, std::milli>(finish - begin).count();
                    if (ms < minMs) { minMs = ms; minPhases = result.phases; }
                    sumMs += ms;
                    subAssets = result.subAssetCount;
                }
                if (!ok)
                {
                    ++failed;
                    std::printf("[CLI] assets.modelbench model=%s fail reason=authoring\n",
                        model.stem().string().c_str());
                    continue;
                }
                // 같은 사본에서 방금 게시한 generation을 런타임 리더로 N회 읽는다 — B2
                // (cooked generation load)를 작업 트리 Library 상태와 무관하게 잰다.
                double loadMinMs = 1e30, loadSumMs = 0.0;
                assets::ModelAssetPhaseTimeline loadPhases;
                std::size_t meshes = 0; unsigned long long vertices = 0;
                file::path sidecarPath = model; sidecarPath += ".meta";
                for (int i = 0; i < iterations && ok; ++i)
                {
                    assets::ModelAssetGenerationLoadRequest load;
                    load.identityHeaderPath = identityHeader;
                    load.generationRoot = generationRoot;
                    load.canonicalSidecarPath = sidecarPath;
                    const auto begin = std::chrono::steady_clock::now();
                    const assets::ModelAssetGenerationLoadResult loaded =
                        assets::LoadModelAssetGeneration(load);
                    const auto finish = std::chrono::steady_clock::now();
                    if (!loaded.Succeeded()) { ok = false; break; }
                    const double ms = std::chrono::duration<double, std::milli>(finish - begin).count();
                    if (ms < loadMinMs) { loadMinMs = ms; loadPhases = loaded.phases; }
                    loadSumMs += ms;
                    meshes = loaded.generation->Meshes().size(); vertices = 0;
                    for (const assets::ModelMeshAsset& mesh : loaded.generation->Meshes())
                        if (mesh.vertexStride) vertices += mesh.vertexBytes.size() / mesh.vertexStride;
                }
                if (!ok)
                {
                    ++failed;
                    std::printf("[CLI] assets.modelbench model=%s fail reason=generation-load\n",
                        model.stem().string().c_str());
                    continue;
                }
                ++measured;
            auto row = CommandData::Object();
            row.Set("model", CommandData::String(model.stem().string()));
            row.Set("meshes", CommandData::Int(meshes)); row.Set("vertices", CommandData::Int(vertices));
                row.Set("authorMinMs", CommandData::Double(minMs)); row.Set("authorAvgMs", CommandData::Double(sumMs / iterations));
                row.Set("cookedMinMs", CommandData::Double(loadMinMs)); row.Set("cookedAvgMs", CommandData::Double(loadSumMs / iterations));
                row.Set("authorPhases", phasesData(minPhases)); row.Set("cookedPhases", phasesData(loadPhases));
                row.Set("subAssets", CommandData::Int(subAssets)); rows.Append(std::move(row));
                std::printf("[CLI] assets.modelbench model=%s authorMinMs=%.3f authorAvgMs=%.3f "
                    "subAssets=%zu cookedMinMs=%.3f cookedAvgMs=%.3f meshes=%zu vertices=%llu "
                    "authorPhases=%s cookedPhases=%s\n",
                    model.stem().string().c_str(), minMs, sumMs / iterations, subAssets,
                    loadMinMs, loadSumMs / iterations, meshes, vertices,
                    formatPhases(minPhases).c_str(), formatPhases(loadPhases).c_str());
            }
            models.clear(); // 아래 cooked 루프를 건너뛴다(done 줄은 공통).
        }
        for (const file::path& model : models)
        {
            const FileGuid guid = DataSystems->GetFilenameToGuid(model.filename().string());
            if (FileGuid{} == guid || !assets::IsUuidV8(guid.m_guid))
            {
                ++skipped;
                std::printf("[CLI] assets.modelbench model=%s skip reason=no-v8-identity\n",
                    model.stem().string().c_str());
                continue;
            }
            double minMs = 1e30, sumMs = 0.0;
            std::size_t meshes = 0; unsigned long long vertices = 0;
            bool ok = true;
            for (int i = 0; i < iterations; ++i)
            {
                RuntimeAssetChange change;
                change.kind = RuntimeAssetChangeKind::ContentReload;
                change.assetType = RuntimeAssetType::Model;
                change.guid = guid;
                change.path = model;
                DataSystems->ApplyAssetChange(change);
                const auto begin = std::chrono::steady_clock::now();
                const auto generation = DataSystems->LoadModelAssetGeneration(guid);
                const auto finish = std::chrono::steady_clock::now();
                if (!generation) { ok = false; break; }
                const double ms = std::chrono::duration<double, std::milli>(finish - begin).count();
                minMs = (std::min)(minMs, ms); sumMs += ms;
                meshes = generation->Meshes().size(); vertices = 0;
                for (const assets::ModelMeshAsset& mesh : generation->Meshes())
                {
                    if (mesh.vertexStride) vertices += mesh.vertexBytes.size() / mesh.vertexStride;
                }
            }
            if (!ok)
            {
                ++failed;
                std::printf("[CLI] assets.modelbench model=%s fail reason=generation-load\n",
                    model.stem().string().c_str());
                continue;
            }
            ++measured;
            auto row = CommandData::Object();
            row.Set("model", CommandData::String(model.stem().string()));
            row.Set("meshes", CommandData::Int(meshes)); row.Set("vertices", CommandData::Int(vertices));
            row.Set("cookedMinMs", CommandData::Double(minMs)); row.Set("cookedAvgMs", CommandData::Double(sumMs / iterations)); rows.Append(std::move(row));
            std::printf("[CLI] assets.modelbench model=%s cookedMinMs=%.3f cookedAvgMs=%.3f "
                "meshes=%zu vertices=%llu\n", model.stem().string().c_str(), minMs,
                sumMs / iterations, meshes, vertices);
        }
        PROCESS_MEMORY_COUNTERS counters{};
        counters.cb = sizeof(counters);
        double peakMb = 0.0;
        if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)))
            peakMb = static_cast<double>(counters.PeakWorkingSetSize) / (1024.0 * 1024.0);
        unsigned long long vramUsedMb = 0, vramBudgetMb = 0;
        if (IRHIDeviceResources* device = GetDiagnosticsDeviceResources())
        {
            const RHIVideoMemoryInfo memory = device->QueryVideoMemory();
            vramUsedMb = memory.usedMB; vramBudgetMb = memory.budgetMB;
        }
        std::printf("[CLI] assets.modelbench done mode=%s measured=%zu failed=%zu "
            "iterations=%d peakWorkingSetMB=%.1f vramUsedMB=%llu vramBudgetMB=%llu\n",
            authorMode ? "author" : "cooked", measured, failed,
            iterations, peakMb, vramUsedMb, vramBudgetMb);
        auto data = CommandData::Object(); data.Set("models", std::move(rows));
        data.Set("mode", CommandData::String(authorMode ? "author" : "cooked"));
        data.Set("measured", CommandData::Int(measured)); data.Set("failed", CommandData::Int(failed)); data.Set("skipped", CommandData::Int(skipped));
        data.Set("iterations", CommandData::Int(iterations)); data.Set("peakWorkingSetMB", CommandData::Double(peakMb));
        data.Set("vramUsedMB", CommandData::Int(vramUsedMb)); data.Set("vramBudgetMB", CommandData::Int(vramBudgetMb));
        if (failed) return Fail("model.benchmark_failed", "Model benchmark failed", std::move(data));
        if (!measured) { auto result = PreconditionFailed("model.coverage_missing", "No typed model measured"); result.data = std::move(data); return result; }
        return Ok({}, std::move(data));
    }

    static CommandCore::CommandResult Cmd_assets_scenemodel(const ConsoleCommandContext& ctx)
    {
        using namespace CommandCore;
        if (ctx.parts.size() != 1 && (ctx.parts.size() != 3 || ctx.parts[1] != "reload"))
            return InvalidArguments("assets.scenemodel [reload <model-name>]");
        RenderTest::SceneModelReport report;
        std::string log;
        bool passed = false;
        if (ctx.parts.size() >= 3 && "reload" == ctx.parts[1])
        {
            passed = RenderTest::RunSceneModelGenerationReloadSelfTest(ctx.parts[2], log, &report);
        }
        else
        {
            passed = RenderTest::RunSceneModelGenerationSelfTest(log, &report);
        }
        if (passed)
            Debug->LogWarning(std::string("[assets.scenemodel] 통과\n") + log);
        else
            Debug->LogError(std::string("[assets.scenemodel] 실패\n") + log);
        auto data = CommandData::Object();
        data.Set("renderers", CommandData::Int(report.renderers));
        data.Set("generationBound", CommandData::Int(report.generationBound));
        data.Set("unbound", CommandData::Int(report.unbound));
        data.Set("handleInvalid", CommandData::Int(report.handleInvalid));
        data.Set("rhiView", CommandData::Int(report.rhiView));
        data.Set("meshIdPersisted", CommandData::Int(report.meshIdPersisted));
        data.Set("textureProps", CommandData::Int(report.textureProps));
        data.Set("embeddedProps", CommandData::Int(report.embeddedProps));
        data.Set("generationTextures", CommandData::Int(report.generationTextures));
        data.Set("otherTextures", CommandData::Int(report.otherTextures));
        data.Set("missingTextures", CommandData::Int(report.missingTextures));
        data.Set("gunnerRenderers", CommandData::Int(report.gunnerRenderers));
        data.Set("gunnerEmbedded", CommandData::Int(report.gunnerEmbedded));
        data.Set("textures", CommandData::Int(report.textures));
        data.Set("reused", CommandData::Int(report.reused));
        data.Set("created", CommandData::Int(report.created));
        data.Set("missing", CommandData::Int(report.missing));
        data.Set("retired", CommandData::Int(report.retired));
        data.Set("reload", CommandData::Bool(report.reload));
        data.Set("sameAggregate", CommandData::Bool(report.sameAggregate));

        data.Set("log", CommandData::String(std::move(log)));
        return passed ? Ok({}, std::move(data)) : Fail("scenemodel.failed", "Scene model generation verification failed", std::move(data));
    }

    static CommandCore::CommandResult Cmd_experiment_cooked(const ConsoleCommandContext& ctx)
    {
        using namespace CommandCore;
        if (ctx.parts.size() > 2) return InvalidArguments("experiment.cooked [asset]");
        // 인자가 없으면 합성 검사, 있으면 그 자산으로 실자산 왕복까지 돌다.
        std::string log;
        bool passed = RenderTest::RunExperimentCookedSelfTest(log);
        if (ctx.parts.size() > 1)
        {
            // ★ && 로 이어 붙이지 않는다 — 단축 평가로 두 번째가 안 돌면
            //   "합성만 돌고 통과"가 실자산 통과처럼 보인다.
            const bool roundTrip =
                RenderTest::RunExperimentCookedRoundTrip(ctx.parts[1], log);
            passed = passed && roundTrip;
        }

        std::printf("%s", log.c_str());
        if (passed)
        {
            Debug->LogWarning(std::string("[experiment.cooked] 통과\n") + log);
        }
        else
        {
            Debug->LogError(std::string("[experiment.cooked] 실패\n") + log);
        }
        std::printf("[CLI] experiment.cooked %s\n", passed ? "통과" : "실패");
        auto data = CommandData::Object();
        data.Set("log", CommandData::String(std::move(log)));
        return passed ? Ok({}, std::move(data)) : Fail("experiment.cooked.failed", "Commandlet verification failed", std::move(data));
    }

    static CommandCore::CommandResult Cmd_experiment_matcook(const ConsoleCommandContext& ctx)
    {
        using namespace CommandCore;
        if (ctx.parts.size() == 2 || ctx.parts.size() > 4) return InvalidArguments("experiment.matcook [asset-root material [model]]");
        // 인자 없으면 합성. <assetRoot> <material> [model] 이면 실자산까지.
        std::string log;
        bool passed = RenderTest::RunExperimentMaterialCookSelfTest(log);
        if (ctx.parts.size() > 2)
        {
            const bool real = RenderTest::RunExperimentMaterialCookReal(
                ctx.parts[1], ctx.parts[2], log);
            passed = passed && real;
        }
        if (ctx.parts.size() > 3)
        {
            // ★ 모델 쪽이 b2c-3 의 본체다 — 재질 의존과 임베디드 추출.
            const bool model = RenderTest::RunExperimentModelDependencyReal(
                ctx.parts[1], ctx.parts[3], log);
            passed = passed && model;
        }

        std::printf("%s", log.c_str());
        if (passed)
        {
            Debug->LogWarning(std::string("[experiment.matcook] 통과\n") + log);
        }
        else
        {
            Debug->LogError(std::string("[experiment.matcook] 실패\n") + log);
        }
        std::printf("[CLI] experiment.matcook %s\n", passed ? "통과" : "실패");
        auto data = CommandData::Object();
        data.Set("log", CommandData::String(std::move(log)));
        return passed ? Ok({}, std::move(data)) : Fail("experiment.matcook.failed", "Commandlet verification failed", std::move(data));
    }

    // I5-D5c1 — 재질 런타임 병행 표현의 전수 A/B, 그리고 **왕복 손실의 실측**.
    //
    // 지금 제품은 저작 문서를 experiment로 읽어 legacy 객체를 만들고, sealing이
    // 매 프레임 그 legacy를 experiment로 되돌린다(experiment→legacy→experiment).
    // D5-c1은 저작 원본을 버리지 않고 MeshRenderer에 병행 보관한다. 이 게이트가
    // 재는 것은 두 가지다:
    //
    //   A/B 동등 — 저작 원본+override로 합성한 재질(B)이 legacy 왕복 결과(A)와
    //     같은가. 값 비교는 코덱 인코딩 텍스트로 한다(수학 타입에 operator==가
    //     없다 — S2c-2a의 diff writer와 같은 규약).
    //   왕복 손실 — 같지 않다면 **어디서** 갈리는가. onlyAuthored는 저작 원본에만
    //     있는 항목이다: 변환기 헤더가 예고한 손실(legacy에 표현이 없는 string
    //     property·texture colorSpace)이 여기 숫자로 나온다. 이 계수가 0이 아니면
    //     D5-c2의 sealing 직행이 **화면을 바꾼다**는 뜻이므로, 그 값을 모르고
    //     넘어가면 c2의 픽셀 차이를 선재 손실과 구분할 수 없다.
    //
    // 이 슬라이스에서 병행 표현의 소비자는 이 게이트뿐이다(렌더는 아직 legacy).
    // I5-D5c2-1 — packing 직전 논리 값의 A/B를 **바이트로** 재기 위한 합성
    // layout. 실제 layout은 셰이더 reflection 산물이라(EnsureShaderMetaVariant —
    // 렌더 패스 컨텍스트 필요) 헤드리스 CLI에서 얻을 수 없다. 그래서 meta 선언
    // 순서대로 offset을 순차 배치한 layout을 만든다.
    //
    // ★ 한계(정직): 이 offset은 제품 GPU 레이아웃이 **아니다**. 이 축이 재는
    //   것은 "두 경로가 같은 논리 값을 packing하는가"이지 "같은 자리에 올리는가"가
    //   아니다. 자리 판정은 D34 계열 픽셀 게이트의 몫이다.
    [[nodiscard]] static bool BuildSyntheticBindingLayout(const ShaderMeta& meta,
        ShaderMetaBindingLayout& outLayout, std::string& outError)
    {
        outLayout = ShaderMetaBindingLayout{};
        outLayout.constantBufferName = "SyntheticMaterialCB";
        std::uint32_t offset = 0;
        for (const ShaderPropertyDesc& desc : meta.properties)
        {
            ShaderMetaPropertyBinding binding;
            binding.name = desc.name;
            binding.propertyType = desc.type;
            if (ShaderPropertyType::Texture2D == desc.type)
            {
                binding.resourceKind = RHIShaderResourceKind::Texture;
                outLayout.properties.push_back(std::move(binding));
                continue;
            }
            const std::size_t size =
                MaterialPropertyPacker::LogicalByteSize(desc.type);
            if (0 == size)
            {
                outError = "논리 크기 0인 property: " + desc.name;
                return false;
            }
            binding.resourceKind = RHIShaderResourceKind::ConstantBuffer;
            binding.byteOffset = offset;
            binding.byteSize = static_cast<std::uint32_t>(size);
            offset += static_cast<std::uint32_t>(size);
            outLayout.properties.push_back(std::move(binding));
        }
        outLayout.constantBufferByteSize = offset;
        return offset > 0;
    }

    // I5-D5c1 — 합성 seed. **코퍼스에 새 정본 저작분이 0이다**(실측: 씬·프리팹의
    // shaderAssetId 0건, ref 표기 0건, standalone 재질 2개 전부 legacy 표기).
    // S2b writer는 ShaderMeta를 아는 재질만 새 정본으로 쓰는데 코퍼스 재질의
    // m_shaderMetaGuid가 전부 nil이라, S2c-2a가 만든 base 참조 저작 경로가
    // 실자산에서 한 번도 돈 적이 없다 — 실자산 게이트는 이 슬라이스에 판별력이
    // 0이고 합성이 필수다(D5-a Foliage와 같은 결론).
    //
    // seed는 저작 경로 그대로 간다: 새 정본 재질 자산을 게시하고(저작 루트
    // 가드를 지나는 WriteTextAssetWithMeta — GUID 발급 포함), 씬 renderer를 그
    // base에 링크한 뒤 override 하나를 얹는다. 저장이 ref 표기를 내고, 재로드가
    // 병행 표현을 채운다.
    static bool SeedAuthoredMaterial(Scene& scene, const file::path& directory,
        const file::path& texturePath, std::string& outError)
    {
        // ShaderMeta는 실물을 쓴다 — 지어낸 GUID로는 keywords 정규화도 property
        // 검증도 돌지 않아 seed가 검사 대상을 비껴간다.
        const file::path metaPath = PathFinder::Relative("Shaders\\")
            / "DefaultPassShader" / "GBuffer.shadermeta";
        const FileGuid shaderGuid = DataSystems->GetFileGuid(metaPath);
        if (FileGuid{} == shaderGuid)
        {
            outError = "GBuffer.shadermeta GUID 미해석: " + metaPath.string();
            return false;
        }
        std::string error;
        const ShaderMetaHandle handle =
            DataSystems->LoadShaderMetaHandle(shaderGuid, error);
        const std::shared_ptr<const ShaderMeta> meta =
            DataSystems->ResolveShaderMeta(handle);
        if (!meta) { outError = "ShaderMeta 로드 실패: " + error; return false; }

        // base 저작본 — meta가 아는 숫자 property만 싣는다(모르는 이름은
        // 변환기가 나르지 않아 대조가 무의미해진다).
        // 게시 GUID를 먼저 만든다 — 저작 루트 가드가 preferredGuid의
        // canonical UUIDv4를 요구하고(nil은 거부), 같은 값을 저작본 assetId에도
        // 실어 자산 신원과 문서 신원을 일치시킨다.
        const FileGuid preferredGuid = FileGuid::CreateRandomV4();
        experiment::Material authored;
        authored.name = "GateAuthoredMat";
        authored.assetId.value = preferredGuid.m_guid;
        authored.shaderAssetId.value = shaderGuid.m_guid;
        authored.blendMode = experiment::MaterialBlendMode::Opaque;
        for (const ShaderPropertyDesc& desc : meta->properties)
        {
            if (desc.type != ShaderPropertyType::Float) continue;
            experiment::MaterialProperty property;
            property.name = desc.name;
            property.value = 0.25f;
            authored.properties.push_back(std::move(property));
            if (authored.properties.size() >= 2) break;
        }
        if (authored.properties.empty())
        {
            outError = "meta에 float property가 없다 — seed가 diff를 만들 수 없다";
            return false;
        }

        // ★ texture도 실어야 c3-2의 owner 축이 실제로 무언가를 비교한다.
        //   싣지 않으면 legacy 맵도 resolver도 nullptr을 주고 "동일"로 통과한다
        //   (nullptr끼리 비교하는 눈먼 초록 — 실제로 한 번 그렇게 나왔다).
        const FileGuid textureGuid = DataSystems->GetFileGuid(texturePath);
        if (FileGuid{} == textureGuid)
        {
            outError = "seed 텍스처 GUID 미해석: " + texturePath.string();
            return false;
        }
        for (const ShaderPropertyDesc& desc : meta->properties)
        {
            if (ShaderPropertyType::Texture2D != desc.type) continue;
            experiment::MaterialProperty property;
            property.name = desc.name;
            experiment::TextureReference reference;
            reference.assetId.value = textureGuid.m_guid;
            property.value = std::move(reference);
            authored.properties.push_back(std::move(property));
            // I5-D5c5 — 슬롯을 **전부** 싣는다. 한 슬롯(baseColorMap)만
            // 실으면 normalMap이 비어 useNormalMap 유도가 늘 0이 되고, 그
            // 축은 "0과 0을 비교해 통과"가 된다(c3-2가 owner에서 겪은 눈먼
            // 초록의 같은 형태). legacy 사본은 이 저작본을 변환해 만들므로
            // 두 경로가 같은 텍스처를 갖고, owner 대조는 그대로 성립한다.
        }

        // 링크 대상은 씬의 첫 MeshRenderer다. legacy 사본은 base를 변환해
        // 만든다 — 저작 경계의 ref 읽기와 같은 규약(base 소유 사본+override).
        MeshRenderer* target = nullptr;
        for (const auto& object : scene.m_Entities)
        {
            if (!object || object->IsDestroyMark()) continue;
            if (MeshRenderer* candidate = object->GetComponent<MeshRenderer>())
            {
                target = candidate;
                break;
            }
        }
        if (nullptr == target) { outError = "씬에 MeshRenderer가 없다"; return false; }

		Authoring::WriteDocument document;
		if (!experiment::SerializeMaterialAuthoring(
			authored, document.Root(), error))
		{
			outError = "저작 인코딩 실패: " + error;
			return false;
		}
		const file::path destination =
			directory / (authored.name + ".asset");
		const FileGuid baseGuid = AssetAuthoringPort::WriteTextAssetWithMeta(
			destination, document.Dump(), preferredGuid);
        if (FileGuid{} == baseGuid)
        {
            outError = "자산 게시 거부(저작 루트 가드): " + destination.string();
            return false;
        }

        auto owned = std::make_shared<Material>();
        if (!ExperimentMaterialMigration::ConvertToLegacyMaterial(authored,
            meta.get(), *owned, error))
        {
            outError = "base legacy 변환 실패: " + error;
            return false;
        }
        // override 하나 — 이것이 씬에 diff로 남아야 ref 표기가 성립한다.
        experiment::MaterialProperty override;
        override.name = authored.properties.front().name;
        override.value = 0.75f;
        if (!ExperimentMaterialMigration::ApplyPropertyToLegacy(*owned,
            override, error))
        {
            outError = "override 적용 실패: " + error;
            return false;
        }
        owned->m_name = authored.name;
        DataSystems->FinalizeMaterialRuntime(*owned);
        target->SetMaterial(std::move(owned));
        target->m_materialBaseGuid = baseGuid;
        std::printf("[CLI] experiment.matruntime seed done base=%s "
            "property=%s owner=%s\n", authored.name.c_str(),
            override.name.c_str(),
            target->GetOwner() ? target->GetOwner()->m_name.ToString().c_str()
                : "?");
        return true;
    }

    static CommandCore::CommandResult Cmd_experiment_matruntime(const ConsoleCommandContext& ctx)
    {
        using namespace CommandCore;
        const bool seed = ctx.parts.size() >= 2 && ctx.parts[1] == "seed";
        const bool edit = ctx.parts.size() == 2 && ctx.parts[1] == "edit";
        if ((seed && ctx.parts.size() != 4) || (!seed && !edit && ctx.parts.size() != 1))
            return InvalidArguments("experiment.matruntime [edit | seed <asset-directory> <texture-path>]");
        Scene* scene = SceneManagers->GetActiveScene();
        if (nullptr == scene)
        {
            std::printf("[CLI] experiment.matruntime fail 활성 씬 없음\n");
            return PreconditionFailed("scene.not_loaded", "No active scene");
        }

        if (seed)
        {
            std::string error;
            if (!SeedAuthoredMaterial(*scene, file::path(ctx.parts[2]), file::path(ctx.parts[3]), error))
                return Fail("material.seed_failed", error);
            return Ok("Authored material fixture published");
        }

        // I5-D5c3 — 편집 반영 축. `MaterialScriptBinding.h`의 계약은 "논리 값
        // 갱신이 곧 화면 갱신"이다(M4 이후 sealing이 매 프레임 논리 값에서 CB
        // bytes를 다시 pack하므로). c2-2가 저작 정본 직행을 넣으면서 그 계약이
        // 저작 재질에서 깨졌다: 편집은 legacy를 바꾸는데 sealing은 인스턴스에서
        // 합성한 값을 쓴다. 이 축이 그 간극을 잰다 — 실물 편집 창구를 태우고
        // 인스턴스 합성 결과가 따라오는지 본다.
        if (ctx.parts.size() >= 2 && "edit" == ctx.parts[1])
        {
            MeshRenderer* target = nullptr;
            for (const auto& object : scene->m_Entities)
            {
                if (!object || object->IsDestroyMark()) continue;
                MeshRenderer* candidate = object->GetComponent<MeshRenderer>();
                if (nullptr != candidate && candidate->m_Material
                    && nullptr != candidate->GetMaterialInstance())
                {
                    target = candidate;
                    break;
                }
            }
            if (nullptr == target)
            {
                std::printf("[CLI] experiment.matruntime edit skip "
                    "저작 정본 보유 renderer 0\n");
                return PreconditionFailed("material.no_instance", "No renderer with an authored material instance");
            }

            std::string error;
            const ShaderMetaHandle handle = DataSystems->LoadShaderMetaHandle(
                target->m_Material->m_shaderMetaGuid, error);
            const std::shared_ptr<const ShaderMeta> meta =
                DataSystems->ResolveShaderMeta(handle);
            if (!meta)
            {
                std::printf("[CLI] experiment.matruntime edit fail meta=%s\n",
                    error.c_str());
                return Fail("material.meta_failed", error);
            }
            std::string propertyName;
            for (const ShaderPropertyDesc& desc : meta->properties)
            {
                if (ShaderPropertyType::Float != desc.type) continue;
                propertyName = desc.name;
                break;
            }
            if (propertyName.empty())
            {
                std::printf("[CLI] experiment.matruntime edit skip "
                    "float property 0\n");
                return PreconditionFailed("material.no_float", "No float property to edit");
            }

            // 실물 편집 창구를 그대로 태운다 — 재구현하면 게이트가 제품이
            // 아니라 자기 사본을 재게 된다.
            constexpr float kEdited = 0.4242f;
            const bool applied = MaterialScriptBinding::SetFloat(
                *target->m_Material, propertyName, kEdited,
                target->GetMaterialInstance());
            const float legacyAfter = MaterialScriptBinding::GetFloat(
                *target->m_Material, propertyName, -1.0f);

            experiment::Material effective;
            float instanceAfter = -1.0f;
            if (target->GetMaterialInstance()->BuildEffectiveMaterial(
                effective, error))
            {
                for (const experiment::MaterialProperty& property
                    : effective.properties)
                {
                    if (property.name != propertyName) continue;
                    if (const float* value =
                        std::get_if<float>(&property.value))
                    {
                        instanceAfter = *value;
                    }
                    break;
                }
            }

            // 편집의 종착점은 프록시다 — 인스턴스만 따라오고 프록시가 옛
            // 스냅샷을 들고 있으면 화면은 여전히 안 바뀐다(c2-2가 만든 그
            // 비대칭). 갱신 커맨드를 실제로 태워 세대 기반 재스냅샷을 잰다.
            RenderScene* renderScene = SceneManagers->GetRenderScene();
            renderScene->UpdateCommand(target);
            // 큐는 렌더 스레드가 비우지만 헤드리스에는 그 틱이 없다 — 게이트가
            // 같은 소비 창구를 직접 태운다(제품과 다른 경로를 만들지 않는다).
            ProxyCommandQueue->Execute(*renderScene,
                renderScene->GetSceneEpoch());
            float proxyAfter = -1.0f;
            for (const auto& proxy :
                SceneManagers->GetRenderScene()->GetPrimitiveProxySnapshot())
            {
                auto* meshProxy = dynamic_cast<MeshRenderProxy*>(proxy.get());
                if (nullptr == meshProxy
                    || meshProxy->m_instancedID != target->GetInstanceID()
                    || !meshProxy->m_authoredMaterial)
                {
                    continue;
                }
                for (const experiment::MaterialProperty& property
                    : meshProxy->m_authoredMaterial->properties)
                {
                    if (property.name != propertyName) continue;
                    if (const float* value = std::get_if<float>(&property.value))
                    {
                        proxyAfter = *value;
                    }
                    break;
                }
                break;
            }

            const bool instanceFollowed =
                std::abs(instanceAfter - kEdited) < 1e-5f;
            const bool proxyFollowed =
                std::abs(proxyAfter - kEdited) < 1e-5f;
            std::printf("[CLI] experiment.matruntime edit %s property=%s "
                "applied=%d legacy=%.4f instance=%.4f proxy=%.4f\n",
                (applied && instanceFollowed && proxyFollowed)
                    ? "pass" : "fail",
                propertyName.c_str(), applied ? 1 : 0, legacyAfter,
                instanceAfter, proxyAfter);
            return (applied && instanceFollowed && proxyFollowed) ? Ok("Material edit reached instance and proxy") : Fail("material.edit_mismatch", "Material edit did not reach instance and proxy");
        }



        const auto encodeValue = [](const std::string& name,
            const experiment::MaterialPropertyValue& value) -> std::string
        {
            experiment::MaterialProperty property;
            property.name = name;
            property.value = value;
			Authoring::WriteDocument document;
			std::string error;
			if (!experiment::SerializeMaterialPropertyValue(
				property, document.Root(), error))
			{
				return "<encode-fail:" + error + ">";
			}
			return document.Dump();
		};

        std::size_t renderers = 0, withMaterial = 0, withInstance = 0;
        std::size_t compared = 0, metaMissing = 0, buildFailed = 0;
        std::size_t valueMismatch = 0, onlyAuthored = 0, onlyLegacy = 0;
        std::size_t blendMismatch = 0;
        std::size_t sealCompared = 0, sealByteMismatch = 0;
        std::size_t sealLayoutFailed = 0, sealBuildFailed = 0;
        std::string firstSealMismatch;
        std::size_t meshProxies = 0, proxyAuthored = 0, proxyValueMismatch = 0;
        std::size_t texResolved = 0, texOwnerMismatch = 0, texResolveFailed = 0;
        std::size_t texResolvedOwners = 0;
        std::size_t texCooked = 0, texSourceFallback = 0;
        std::string firstTexMismatch;
        // I5-D5c5 — 저작 단독 시공 축. 기존 2단계(legacy 시공 → 덮어쓰기)와
        // 신규 1단계(BuildSealSourceFromAuthored)가 같은 산출을 내는가.
        std::size_t sealAuthoredBuilt = 0, sealAuthoredFail = 0;
        std::size_t sealAuthoredTexMismatch = 0, sealNormalMapDerived = 0;
        std::size_t sealDeadChannelDelta = 0;
        std::string firstAuthoredSeal;
        std::string firstMismatch;

        for (const auto& object : scene->m_Entities)
        {
            if (!object || object->IsDestroyMark()) continue;
            MeshRenderer* renderer = object->GetComponent<MeshRenderer>();
            if (nullptr == renderer) continue;
            ++renderers;
            if (!renderer->m_Material) continue;
            ++withMaterial;

            experiment::MaterialInstance* instance =
                renderer->GetMaterialInstance();
            if (nullptr == instance) continue;
            ++withInstance;

            // legacy 축은 실물 변환 정본을 그대로 태운다 — 재구현하지 않는다.
            std::string error;
            const ShaderMetaHandle handle = DataSystems->LoadShaderMetaHandle(
                renderer->m_Material->m_shaderMetaGuid, error);
            const std::shared_ptr<const ShaderMeta> meta =
                DataSystems->ResolveShaderMeta(handle);
            if (!meta) { ++metaMissing; continue; }

            experiment::Material legacyRoundTrip;
            experiment::Material authoredEffective;
            if (!ExperimentMaterialMigration::ConvertLegacyMaterial(
                    *renderer->m_Material, *meta, legacyRoundTrip, error)
                || !instance->BuildEffectiveMaterial(authoredEffective, error))
            {
                ++buildFailed;
                if (firstMismatch.empty()) firstMismatch = "build:" + error;
                continue;
            }
            ++compared;

            if (legacyRoundTrip.blendMode != authoredEffective.blendMode)
            {
                ++blendMismatch;
                if (firstMismatch.empty())
                {
                    firstMismatch = "blend " + object->m_name.ToString();
                }
            }

            std::unordered_map<std::string, std::string> legacyValues;
            for (const experiment::MaterialProperty& property
                : legacyRoundTrip.properties)
            {
                legacyValues[property.name] =
                    encodeValue(property.name, property.value);
            }
            for (const experiment::MaterialProperty& property
                : authoredEffective.properties)
            {
                const auto it = legacyValues.find(property.name);
                if (it == legacyValues.end())
                {
                    ++onlyAuthored;
                    if (firstMismatch.empty())
                    {
                        firstMismatch = "onlyAuthored " + property.name;
                    }
                    continue;
                }
                if (it->second != encodeValue(property.name, property.value))
                {
                    ++valueMismatch;
                    if (firstMismatch.empty())
                    {
                        firstMismatch = "value " + property.name;
                    }
                }
                legacyValues.erase(it);
            }
            onlyLegacy += legacyValues.size();
            if (!legacyValues.empty() && firstMismatch.empty())
            {
                firstMismatch = "onlyLegacy " + legacyValues.begin()->first;
            }

            // ── D5-c2-1: packing 직전 논리 값의 바이트 A/B ──
            //
            // 이름 집합 대조(위)는 "저작에만/legacy에만"을 세지만, 누락 property가
            // ApplyDefault(ShaderMeta 기본값)로 채워지므로 **그것만으로는 c2가
            // 화면을 바꾸는지 알 수 없다**. legacy 왕복이 주입한 값과 meta
            // 기본값이 같다면 바이트는 동일하고 c2는 무해하다. 그 판정이 이
            // 축이다.
            ShaderMetaBindingLayout syntheticLayout;
            std::string layoutError;
            if (!BuildSyntheticBindingLayout(*meta, syntheticLayout, layoutError))
            {
                ++sealLayoutFailed;
                continue;
            }
            std::vector<std::uint8_t> legacyBytes, authoredBytes;
            if (!experiment::BuildMaterialPropertyBlock(legacyRoundTrip, *meta,
                    syntheticLayout, legacyBytes, error)
                || !experiment::BuildMaterialPropertyBlock(authoredEffective,
                    *meta, syntheticLayout, authoredBytes, error))
            {
                ++sealBuildFailed;
                if (firstMismatch.empty()) firstMismatch = "sealBuild:" + error;
                continue;
            }
            ++sealCompared;
            if (legacyBytes != authoredBytes)
            {
                ++sealByteMismatch;
                std::size_t firstByte = 0;
                while (firstByte < legacyBytes.size()
                    && firstByte < authoredBytes.size()
                    && legacyBytes[firstByte] == authoredBytes[firstByte])
                {
                    ++firstByte;
                }
                // 어느 property의 자리인지 되짚는다 — 숫자만으로는 c2의 위험
                // 크기를 판단할 수 없다.
                std::string culprit = "?";
                for (const ShaderMetaPropertyBinding& binding
                    : syntheticLayout.properties)
                {
                    if (RHIShaderResourceKind::ConstantBuffer
                        != binding.resourceKind) continue;
                    if (firstByte >= binding.byteOffset
                        && firstByte < binding.byteOffset + binding.byteSize)
                    {
                        culprit = binding.name;
                        break;
                    }
                }
                if (firstSealMismatch.empty())
                {
                    firstSealMismatch = culprit + "@"
                        + std::to_string(firstByte);
                }
            }

            // ── D5-c3-2: texture owner 축 ──
            //
            // sealing이 texture generation owner를 legacy 이름 맵 대신 저작
            // GUID에서(M2 resolver) 얻도록 바꿨다. 이 전환이 **그림을 바꾸지
            // 않는가**를 재는 유일한 방법은 두 경로가 같은 owner를 주는지
            // 보는 것이다 — 다른 텍스처가 오면 화면이 조용히 달라진다.
            ExperimentMaterialSealing::SealSource legacySeal;
            if (ExperimentMaterialSealing::BuildSealSourceFromLegacy(
                *renderer->m_Material, *meta, legacySeal, error))
            {
                ExperimentMaterialSealing::SealSource authoredSeal = legacySeal;
                ExperimentMaterialSealing::ApplyAuthoredMaterial(authoredSeal,
                    authoredEffective);
                std::size_t cooked = 0, sourceFallback = 0;
                // MBC7 — 제품 sealing과 같은 축: renderer가 붙든 모델 generation
                // closure가 embedded texture의 첫 해석 축이다(경로 해석 없음).
                if (!ExperimentMaterialSealing::ApplyAuthoredTextures(
                    authoredSeal, *meta, error, &cooked, &sourceFallback,
                    renderer->m_modelGeneration.get()))
                {
                    ++texResolveFailed;
                    if (firstTexMismatch.empty())
                    {
                        firstTexMismatch = "resolve:" + error;
                    }
                }
                else
                {
                    texCooked += cooked;
                    texSourceFallback += sourceFallback;
                    for (const auto& legacyTexture : legacySeal.textures)
                    {
                        const auto found = std::find_if(
                            authoredSeal.textures.begin(),
                            authoredSeal.textures.end(),
                            [&](const auto& candidate)
                            {
                                return candidate.propertyName
                                    == legacyTexture.propertyName;
                            });
                        ++texResolved;
                        if (found != authoredSeal.textures.end()
                            && nullptr != found->owner)
                        {
                            ++texResolvedOwners;
                        }
                        if (found == authoredSeal.textures.end()
                            || found->owner != legacyTexture.owner)
                        {
                            ++texOwnerMismatch;
                            if (firstTexMismatch.empty())
                            {
                                firstTexMismatch = legacyTexture.propertyName;
                            }
                        }
                    }
                }

                // ── D5-c5: 저작 단독 시공 ──
                //
                // 제품 경로는 이제 legacy를 읽지 않고 저작본만으로 seal을
                // 짓는다. 그것이 **기존 2단계와 같은 산출**인지를 여기서
                // 잰다 — 갈리면 화면이 조용히 달라진다.
                ExperimentMaterialSealing::SealSource directSeal;
                std::string directError;
                if (!ExperimentMaterialSealing::BuildSealSourceFromAuthored(
                    authoredEffective, *meta, directSeal, directError,
                    renderer->m_modelGeneration.get()))
                {
                    ++sealAuthoredFail;
                    if (firstAuthoredSeal.empty())
                        firstAuthoredSeal = "build:" + directError;
                }
                else
                {
                    ++sealAuthoredBuilt;
                    // texture owner는 두 경로가 **반드시** 같아야 한다 —
                    // 같은 resolver를 부르므로 갈리면 시공 순서 결함이다.
                    if (directSeal.textures.size() != authoredSeal.textures.size())
                    {
                        ++sealAuthoredTexMismatch;
                        if (firstAuthoredSeal.empty())
                            firstAuthoredSeal = "texCount";
                    }
                    else
                    {
                        for (std::size_t slot = 0;
                            slot < directSeal.textures.size(); ++slot)
                        {
                            if (directSeal.textures[slot].propertyName
                                    == authoredSeal.textures[slot].propertyName
                                && directSeal.textures[slot].owner
                                    == authoredSeal.textures[slot].owner)
                            {
                                continue;
                            }
                            ++sealAuthoredTexMismatch;
                            if (firstAuthoredSeal.empty())
                            {
                                firstAuthoredSeal = "tex:" +
                                    directSeal.textures[slot].propertyName;
                            }
                        }
                    }
                    // useNormalMap — 인스턴스 채널의 유일한 실소비다. 저작
                    // 유도가 실제로 1을 내야 이 축이 공허하지 않다(seed가
                    // normalMap을 싣는 이유).
                    if (0 != directSeal.useNormalMap) ++sealNormalMapDerived;
                    // 죽은 채널(baseColorFactor/metallic/roughness/flow)은
                    // **판정하지 않고 보고한다** — 제품 셰이더가 CB를 우선하므로
                    // (usePropertyBlock·useLegacyInstanceMaterial) legacy 값과
                    // 갈려도 그림이 바뀌지 않는다. 이 계수는 "legacy 값을
                    // 잃었다"는 사실의 크기다.
                    if (directSeal.baseColorFactor != legacySeal.baseColorFactor
                        || directSeal.metallic != legacySeal.metallic
                        || directSeal.roughness != legacySeal.roughness
                        || directSeal.flow.windVector != legacySeal.flow.windVector
                        || directSeal.flow.uvScroll != legacySeal.flow.uvScroll)
                    {
                        ++sealDeadChannelDelta;
                    }
                }
            }
        }

        // ── D5-c2-2: 프록시 축 ──
        //
        // sealing 직행 자체는 헤드리스 관측 밖이다: --script 라이브는 렌더
        // 0프레임이고 dx12.scene 하네스는 sealing 경로를 타지 않는다(자체 그리기).
        // 그래서 **프록시가 저작 정본을 나르는가**까지를 잰다 — 그 뒤 4줄
        // (poolMesh 반입 → ApplyAuthoredMaterial)은 코드가 미러이고, 값의 동등은
        // 위 바이트 축이 이미 증명했다. 남는 간극은 계획서에 한계로 적는다.
        //
        // 프록시가 나르는 것이 컴포넌트 인스턴스와 **같은 값**인지도 본다 —
        // 프록시는 값 스냅샷이라 인스턴스를 가리키지 않는다(렌더 스레드 안전).
        if (RenderScene* renderScene = SceneManagers->GetRenderScene())
        {
            for (const auto& proxy : renderScene->GetPrimitiveProxySnapshot())
            {
                auto* meshProxy = dynamic_cast<MeshRenderProxy*>(proxy.get());
                if (nullptr == meshProxy) continue;
                ++meshProxies;
                if (!meshProxy->m_authoredMaterial) continue;
                ++proxyAuthored;

                // 같은 renderer의 인스턴스를 찾아 값 대조한다.
                MeshRenderer* owner = nullptr;
                for (const auto& object : scene->m_Entities)
                {
                    if (!object || object->IsDestroyMark()) continue;
                    MeshRenderer* candidate = object->GetComponent<MeshRenderer>();
                    if (nullptr != candidate
                        && candidate->GetInstanceID() == meshProxy->m_instancedID)
                    {
                        owner = candidate;
                        break;
                    }
                }
                experiment::MaterialInstance* instance =
                    (nullptr != owner) ? owner->GetMaterialInstance() : nullptr;
                if (nullptr == instance) { ++proxyValueMismatch; continue; }

                experiment::Material expected;
                std::string error;
                if (!instance->BuildEffectiveMaterial(expected, error))
                {
                    ++proxyValueMismatch;
                    continue;
                }
                bool same = expected.properties.size()
                    == meshProxy->m_authoredMaterial->properties.size()
                    && expected.blendMode
                        == meshProxy->m_authoredMaterial->blendMode;
                if (same)
                {
                    for (const experiment::MaterialProperty& property
                        : expected.properties)
                    {
                        const auto it = std::find_if(
                            meshProxy->m_authoredMaterial->properties.begin(),
                            meshProxy->m_authoredMaterial->properties.end(),
                            [&](const experiment::MaterialProperty& candidate)
                            {
                                return candidate.name == property.name;
                            });
                        if (it == meshProxy->m_authoredMaterial->properties.end()
                            || encodeValue(it->name, it->value)
                                != encodeValue(property.name, property.value))
                        {
                            same = false;
                            break;
                        }
                    }
                }
                if (!same) ++proxyValueMismatch;
            }
        }

        // 판정은 **동등 축만** 한다. onlyAuthored/onlyLegacy는 왕복 손실의
        // 실측이라 여기서 붉히지 않고 계수로 보고한다 — 이 슬라이스는 손실을
        // 없애는 것이 아니라 크기를 아는 것이 목적이다(그 처방은 c2다).
        const bool covered = withInstance > 0 && compared > 0;
        // sealByteMismatch는 **판정하지 않는다** — c2가 화면을 바꾸는 폭의
        // 실측이지 결함이 아니다(단정을 걸면 c2가 고칠 때 거꾸로 붉어진다).
        // 반면 layout/build 실패는 축이 돌지 않았다는 뜻이라 붉힌다.
        const bool passed = covered && 0 == valueMismatch && 0 == blendMismatch
            && 0 == buildFailed && 0 == sealLayoutFailed && 0 == sealBuildFailed
            && sealCompared > 0 && 0 == proxyValueMismatch
            && 0 == texOwnerMismatch && 0 == texResolveFailed
            // D5-c5: 저작 단독 시공이 기존 2단계와 갈리면 실패다.
            && 0 == sealAuthoredFail && 0 == sealAuthoredTexMismatch;
        std::printf("[CLI] experiment.matruntime %s renderers=%zu "
            "withMaterial=%zu withInstance=%zu compared=%zu metaMissing=%zu "
            "buildFailed=%zu valueMismatch=%zu blendMismatch=%zu "
            "onlyAuthored=%zu onlyLegacy=%zu sealCompared=%zu "
            "sealByteMismatch=%zu sealLayoutFailed=%zu sealBuildFailed=%zu "
            "meshProxies=%zu proxyAuthored=%zu proxyValueMismatch=%zu "
            "texResolved=%zu texOwnerMismatch=%zu texResolveFailed=%zu "
            "texResolvedOwners=%zu texCooked=%zu texSourceFallback=%zu "
            "sealAuthored=%zu sealAuthoredFail=%zu sealAuthoredTexMismatch=%zu "
            "normalMapDerived=%zu deadChannelDelta=%zu%s%s%s%s%s%s%s%s\n",
            passed ? "pass" : (covered ? "fail" : "skip"),
            renderers, withMaterial, withInstance, compared, metaMissing,
            buildFailed, valueMismatch, blendMismatch, onlyAuthored, onlyLegacy,
            sealCompared, sealByteMismatch, sealLayoutFailed, sealBuildFailed,
            meshProxies, proxyAuthored, proxyValueMismatch,
            texResolved, texOwnerMismatch, texResolveFailed, texResolvedOwners,
            texCooked, texSourceFallback,
            sealAuthoredBuilt, sealAuthoredFail, sealAuthoredTexMismatch,
            sealNormalMapDerived, sealDeadChannelDelta,
            firstMismatch.empty() ? "" : " first=",
            firstMismatch.c_str(),
            firstSealMismatch.empty() ? "" : " firstSeal=",
            firstSealMismatch.c_str(),
            firstTexMismatch.empty() ? "" : " firstTex=",
            firstTexMismatch.c_str(),
            firstAuthoredSeal.empty() ? "" : " firstAuthoredSeal=",
            firstAuthoredSeal.c_str());
        CommandData data = CommandData::Object();
        data.Set("renderers", CommandData::Int(static_cast<std::int64_t>(renderers)));
        data.Set("compared", CommandData::Int(static_cast<std::int64_t>(compared)));
        data.Set("passed", CommandData::Bool(passed));
        if (!covered) return PreconditionFailed("material.no_coverage", "No authored material instance was compared");
        return passed ? Ok("Material runtime contract passed", std::move(data))
            : Fail("material.runtime_mismatch", "Material runtime contract failed", std::move(data));
    }

    // I7-C1 — cooked catalog 관측·마운트. 굽는 쪽(AssetCooker → Derived/ +
    // CEMF → pak)은 D5-b2c에서 다 섰는데 읽는 쪽이 이어져 있지 않아 cooked
    // 경로가 제품에서 한 번도 돌지 않았다(실측 texCooked=0). 이 명령이
    //   ① 기동 마운트 상태를 보이고
    //   ② 게이트가 임시 Derived 트리를 명시적으로 마운트할 창구가 된다.
    // 저작 트리에는 Derived가 없으므로 인자 없는 실행은 미게시(skip)가 정상이다.

    static CommandCore::CommandResult Cmd_experiment_catalog(const ConsoleCommandContext& ctx)
    {
        using namespace CommandCore;
        if (ctx.parts.size() != 1 && (ctx.parts.size() < 3 || (ctx.parts[1] != "mount" && ctx.parts[1] != "probe"))) return InvalidArguments("experiment.catalog [mount <root>|probe <texture>]");
        namespace ck = experiment::cooked;
        // 경로에 공백이 있을 수 있다. 예전에는 그래서 원문에서 잘라 썼는데,
        // 그 방식은 `find("mount")` 가 **경로 안의 "mount" 를 먼저 만나면**
        // 엉뚱한 곳을 자른다(`experiment.catalog mount D:/mount/x`). 토큰을
        // 이어 붙이면 그 함정이 없고 따옴표도 이미 벗겨져 있다(LC2).
        if (ctx.parts.size() >= 3 && ctx.parts[1] == "mount")
        {
            const file::path root =
                std::filesystem::path(CommandCore::JoinFrom(ctx.parts, 2));
            std::string error;
            const bool mounted = DataSystems->MountCookedCatalog(root, error);
            std::printf("[CLI] experiment.catalog mount %s root=%s entries=%zu sources=%zu stale=%zu%s%s\n",
                mounted ? "pass" : "fail", root.string().c_str(),
                DataSystems->CookedCatalogEntryCount(),
                DataSystems->CookedCatalogSourceAssetCount(),
                DataSystems->CookedCatalogStaleCount(),
                error.empty() ? "" : " error=", error.c_str());
            auto data = CommandData::Object(); data.Set("root", CommandData::String(root.string()));
            data.Set("entries", CommandData::Int(DataSystems->CookedCatalogEntryCount()));
            data.Set("sources", CommandData::Int(DataSystems->CookedCatalogSourceAssetCount())); data.Set("stale", CommandData::Int(DataSystems->CookedCatalogStaleCount()));
            return mounted ? Ok({}, std::move(data)) : Fail("catalog.mount_failed", error, std::move(data));
        }

        // I7-C1 — 제품 바인딩 소비 프로브. "표가 섰다"와 "resolver가 cooked를
        // 골랐다"는 다르다. 실제 texture 자산 경로를 받아 제품 서비스
        // (MakeDataSystemMaterialResolveServices + 마운트된 catalog)로 해석하고
        // cookedTextures 계수를 낸다 — sealing이 매 프레임 타는 그 경로다.
        if (ctx.parts.size() >= 3 && ctx.parts[1] == "probe")
        {
            const auto mounted = DataSystems->GetCookedCatalog();
            // LC2: mount 분기와 같은 이유로 토큰을 잇는다.
            const std::string pathText = CommandCore::JoinFrom(ctx.parts, 2);
            const FileGuid textureGuid =
                DataSystems->GetFileGuid(std::filesystem::path(pathText));
            const file::path metaPath = PathFinder::Relative("Shaders\\")
                / "DefaultPassShader" / "GBuffer.shadermeta";
            const FileGuid shaderGuid = DataSystems->GetFileGuid(metaPath);
            if (FileGuid{} == textureGuid || FileGuid{} == shaderGuid)
            {
                std::printf("[CLI] experiment.catalog probe fail GUID 미해석 "
                    "texture=%d shader=%d\n",
                    FileGuid{} != textureGuid, FileGuid{} != shaderGuid);
                return PreconditionFailed("catalog.fixture_missing", "Texture or shader GUID is unavailable");
            }

            std::string metaError;
            const ShaderMetaHandle metaHandle =
                DataSystems->LoadShaderMetaHandle(shaderGuid, metaError);
            const std::shared_ptr<const ShaderMeta> meta =
                DataSystems->ResolveShaderMeta(metaHandle);
            if (!meta)
            {
                std::printf("[CLI] experiment.catalog probe fail meta %s\n",
                    metaError.c_str());
                return Fail("catalog.meta_failed", metaError);
            }

            experiment::Material material;
            material.name = "CookedProbe";
            material.shaderAssetId.value = shaderGuid.m_guid;
            experiment::MaterialProperty property;
            property.name = std::string(
                standard_material::property::BaseColorMap);
            experiment::TextureReference reference;
            reference.assetId.value = textureGuid.m_guid;
            property.value = std::move(reference);
            material.properties.push_back(std::move(property));

            // ★ 제품 sealing이 타는 **그 함수**를 부른다. resolver를 직접
            //   부르면 "binding이 catalog를 쓴다"까지만 증명되고, sealing이
            //   그것을 넘기는지는 안 재진다 — 여기가 실제 소비 지점이다.
            ExperimentMaterialSealing::SealSource source;
            source.material = std::move(material);
            std::size_t cooked = 0, sourceFallback = 0;
            std::string error;
            const bool ok = ExperimentMaterialSealing::ApplyAuthoredTextures(
                source, *meta, error, &cooked, &sourceFallback);
            std::size_t owners = 0;
            for (const auto& texture : source.textures)
            {
                if (nullptr != texture.owner) ++owners;
            }
            std::printf("[CLI] experiment.catalog probe %s catalog=%d "
                "textures=%zu cooked=%zu sourceFallback=%zu%s%s\n",
                (ok && owners > 0) ? "pass" : "fail",
                nullptr != mounted, owners, cooked, sourceFallback,
                error.empty() ? "" : " error=", error.c_str());
            auto data = CommandData::Object(); data.Set("catalog", CommandData::Bool(mounted != nullptr));
            data.Set("textures", CommandData::Int(owners)); data.Set("cooked", CommandData::Int(cooked)); data.Set("sourceFallback", CommandData::Int(sourceFallback));
            return ok && owners > 0 ? Ok({}, std::move(data)) : Fail("catalog.probe_failed", error, std::move(data));
        }

        const auto catalog = DataSystems->GetCookedCatalog();
        if (!catalog)
        {
            // 미게시는 결함이 아니다 — 저작 트리의 정상 상태다.
            std::printf("[CLI] experiment.catalog skip 미게시(Derived 없음)\n");
            return PreconditionFailed("catalog.not_mounted", "No cooked catalog is mounted");
        }

        // 씬의 모델 GUID가 실제로 cooked artifact로 해석되는가 — 표가 서 있어도
        // 제품 신원과 맞물리지 않으면 cookedPath는 여전히 빈 경로다.
        std::size_t modelsProbed = 0, modelsResolved = 0;
        if (Scene* scene = SceneManagers->GetActiveScene())
        {
            std::unordered_set<std::uint64_t> seen;
            for (const auto& object : scene->m_Entities)
            {
                if (!object || object->IsDestroyMark()) continue;
                MeshRenderer* renderer = object->GetComponent<MeshRenderer>();
                if (nullptr == renderer) continue;
                if (FileGuid{} == renderer->m_modelGuid) continue;
                if (!seen.insert(renderer->m_modelGuid.m_guid.data[0]).second)
                {
                    continue;
                }
                ++modelsProbed;
                experiment::AssetId id;
                id.value = renderer->m_modelGuid.m_guid;
                if (!catalog->ResolveArtifactPath(id).empty()) ++modelsResolved;
            }
        }

        std::printf("[CLI] experiment.catalog pass entries=%zu sources=%zu models=%zu "
            "materials=%zu textures=%zu shaderMetas=%zu scenes=%zu prefabs=%zu "
            "modelsProbed=%zu modelsResolved=%zu stale=%zu root=%s\n",
            catalog->Size(),
            catalog->SourceAssetCount(),
            catalog->CountOfKind(ck::CookedAssetKind::Model),
            catalog->CountOfKind(ck::CookedAssetKind::Material),
            catalog->CountOfKind(ck::CookedAssetKind::Texture),
            catalog->CountOfKind(ck::CookedAssetKind::ShaderMeta),
            catalog->CountOfKind(ck::CookedAssetKind::Scene),
            catalog->CountOfKind(ck::CookedAssetKind::Prefab),
            modelsProbed, modelsResolved, DataSystems->CookedCatalogStaleCount(),
            catalog->DerivedRoot().string().c_str());
        auto data = CommandData::Object();
        data.Set("entries", CommandData::Int(catalog->Size())); data.Set("sources", CommandData::Int(catalog->SourceAssetCount()));
        data.Set("modelsProbed", CommandData::Int(modelsProbed)); data.Set("modelsResolved", CommandData::Int(modelsResolved));
        data.Set("stale", CommandData::Int(DataSystems->CookedCatalogStaleCount())); data.Set("root", CommandData::String(catalog->DerivedRoot().string()));
        return modelsProbed == modelsResolved ? Ok({}, std::move(data)) : Fail("catalog.resolve_failed", "Scene model resolution is incomplete", std::move(data));
    }

    static CommandCore::CommandResult Cmd_experiment_scenecook(const ConsoleCommandContext& ctx)
    {
        using namespace CommandCore;
        if (ctx.parts.size() == 2 || ctx.parts.size() > 3) return InvalidArguments("experiment.scenecook [asset-root scene]");
        std::string log;
        bool passed = RenderTest::RunExperimentSceneCookSelfTest(log);
        if (ctx.parts.size() > 2)
        {
            const bool real = RenderTest::RunExperimentSceneCookReal(
                ctx.parts[1], ctx.parts[2], log);
            passed = passed && real;
        }

        std::printf("%s", log.c_str());
        if (passed)
        {
            Debug->LogWarning(std::string("[experiment.scenecook] 통과\n") + log);
        }
        else
        {
            Debug->LogError(std::string("[experiment.scenecook] 실패\n") + log);
        }
        std::printf("[CLI] experiment.scenecook %s\n", passed ? "통과" : "실패");
        auto data = CommandData::Object();
        data.Set("log", CommandData::String(std::move(log)));
        return passed ? Ok({}, std::move(data)) : Fail("experiment.scenecook.failed", "Commandlet verification failed", std::move(data));
    }

    static CommandCore::CommandResult Cmd_assets_unload(const ConsoleCommandContext& ctx)
    {
        using namespace CommandCore;
        if (ctx.parts.size() != 1) return InvalidArguments("This command takes no arguments");
        DataSystems->UnloadUnusedAssets();
        std::printf("[CLI] 사용하지 않는 에셋 정리 요청\n");
        return Ok("Unused asset cleanup requested");
    }

    static CommandCore::CommandResult Cmd_bt_status(const ConsoleCommandContext& ctx)
    {
        using namespace CommandCore;
        if (ctx.parts.size() != 1) return InvalidArguments("This command takes no arguments");
        const std::string& cmd = ctx.cmd;

        // 행동 트리 진단(PHASE 9-8 완료 기준 1·3).
        //
        // 왜 필요한가: 트리 생성·틱은 실패할 때만 로그를 남긴다. 그래서 "트리가 안
        // 서서 AI가 가만히 있다"와 "정상 동작"이 밖에서 구분되지 않는다 — 둘 다
        // 무음이다. 회귀 세트도 BT를 쓰는 씬을 열지 않으므로, 전부 통과해도 BT
        // 코드는 한 줄도 실행되지 않은 채 통과할 수 있다. 즉 BT에 대한 양성 증거가
        // 없었고, 이 명령이 그 자리를 메운다.
        if (cmd == "bt.reset")
        {
            ClrHost::Get().ResetBehaviorTreeStats();
            ClrHost::Get().ResetAICrossings();
            std::printf("[CLI] BT 누계 초기화 (트리는 그대로)\n");
            return Ok("Behavior tree counters reset");
        }

        ClrHost::ScriptBTStats bt{};
        if (!ClrHost::Get().GetBehaviorTreeStats(bt))
        {
            // 조용히 0을 찍지 않는다 — "트리 0개"와 "지표를 못 읽었다"는 전혀 다른
            // 상황인데 같은 숫자로 보이면 진단이 거꾸로 간다.
            std::printf("[CLI] BT 지표 없음 — 스크립트 계층 비활성이거나 구 어셈블리\n");
            return PreconditionFailed("script.stats_unavailable", "Behavior tree stats unavailable");
        }

        const ClrHost::AICrossingCounters& x = ClrHost::Get().AICrossings();

        char line[512]{};
        std::snprintf(line, sizeof(line),
            "[bt.status] 트리 %d개 · 노드 타입 %d종 · 틱 %llu회 · 건너뜀 %llu회",
            bt.treeCount, bt.nodeTypeCount,
            static_cast<unsigned long long>(bt.tickCount),
            static_cast<unsigned long long>(bt.skippedCount));
        std::printf("[CLI] %s\n", line);
        Debug->LogWarning(line);

        // 크로싱 비율이 이 재설계의 핵심 주장이다 — 트리가 몇 개든 프레임당 1회.
        // 분모는 FlushAITicks 호출 수(= 흘려보낸 프레임 수)이고, 큐가 빈 프레임도
        // 포함한다. 그래서 비율은 1을 넘을 수 없고, 넘으면 배선이 깨진 것이다.
        char cross[512]{};
        const double perFrame = (0 == x.flushCalls)
            ? 0.0 : static_cast<double>(x.crossings) / static_cast<double>(x.flushCalls);
        const double perCrossing = (0 == x.crossings)
            ? 0.0 : static_cast<double>(x.ticksDelivered) / static_cast<double>(x.crossings);
        std::snprintf(cross, sizeof(cross),
            "[bt.status] 경계 통과 %llu회 / 프레임 %llu (프레임당 %.2f) · 전달 틱 %llu건 "
            "(크로싱당 %.1f · 최대 배치 %llu)",
            static_cast<unsigned long long>(x.crossings),
            static_cast<unsigned long long>(x.flushCalls), perFrame,
            static_cast<unsigned long long>(x.ticksDelivered), perCrossing,
            static_cast<unsigned long long>(x.maxBatch));
        std::printf("[CLI] %s\n", cross);
        Debug->LogWarning(cross);
        auto data = CommandData::Object();
        data.Set("treeCount", CommandData::Int(bt.treeCount));
        data.Set("nodeTypeCount", CommandData::Int(bt.nodeTypeCount));
        data.Set("tickCount", CommandData::Int(bt.tickCount));
        data.Set("skippedCount", CommandData::Int(bt.skippedCount));
        data.Set("flushCalls", CommandData::Int(x.flushCalls));
        data.Set("crossings", CommandData::Int(x.crossings));
        data.Set("ticksDelivered", CommandData::Int(x.ticksDelivered));
        data.Set("maxBatch", CommandData::Int(x.maxBatch));
        data.Set("perFrame", CommandData::Double(perFrame));
        data.Set("perCrossing", CommandData::Double(perCrossing));
        return Ok({}, std::move(data));
    }

    void RegisterAssetAuthoringCommands(Registrar& reg)
    {
        reg.Result({ "collisionmatrix.authoring.probe" },
                   &Cmd_collisionmatrix_authoring_probe);
        reg.Result({ "model.load" }, &Cmd_model_load);
        reg.Result({ "terrain.authoring.probe" }, &Cmd_terrain_authoring_probe);
        reg.Result({ "foliage.authoring.probe" }, &Cmd_foliage_authoring_probe);
        reg.Result({ "blackboard.authoring.probe" }, &Cmd_blackboard_authoring_probe);
        reg.Result({ "asset.guid.rename.probe" }, &Cmd_asset_guid_rename_probe);
        reg.Result({ "material.corpus.probe" }, &Cmd_material_corpus_probe);
        reg.Result({ "tag.list" }, &Cmd_tag_list);
        reg.Result({ "tag.has" }, &Cmd_tag_has);
        reg.Result({ "tag.add" }, &Cmd_tag_add);
        reg.Result({ "tag.remove" }, &Cmd_tag_remove);
        reg.Result({ "inputmap.authoring.probe" }, &Cmd_inputmap_authoring_probe);
        reg.Result({ "inputmap.corpus.probe" }, &Cmd_inputmap_corpus_probe);
        reg.Result({ "model.loadcached" }, &Cmd_model_loadcached);
        reg.Result({ "model.place" }, &Cmd_model_place);
        reg.Result({ "assets.identity" }, &Cmd_assets_identity);
        reg.Result({ "assets.sidecar" }, &Cmd_assets_sidecar);
        reg.Result({ "assets.generation" }, &Cmd_assets_generation);
        reg.Result({ "assets.generationcorpus" }, &Cmd_assets_generationcorpus);
        reg.Result({ "assets.modelrender" }, &Cmd_assets_modelrender);
        reg.Result({ "assets.scenemodel" }, &Cmd_assets_scenemodel);
        reg.Result({ "assets.modeldiag" }, &Cmd_assets_modeldiag);
        reg.Result({ "assets.modelbench" }, &Cmd_assets_modelbench);
        reg.Result({ "experiment.skinbounds" }, &Cmd_experiment_skinbounds);
        reg.Result({ "experiment.animpose" }, &Cmd_experiment_animpose);
        reg.Result({ "animator.status" }, &Cmd_animator_status);
        reg.Result({ "experiment.animtick" }, &Cmd_experiment_animtick);
        reg.Result({ "experiment.animevent" }, &Cmd_experiment_animevent);
        reg.Result({ "experiment.boneresolve" }, &Cmd_experiment_boneresolve);
        reg.Result({ "experiment.foliage" }, &Cmd_experiment_foliage);
        reg.Result({ "experiment.animmask" }, &Cmd_experiment_animmask);
        reg.Result({ "experiment.editorsurface" }, &Cmd_experiment_editorsurface);
        reg.Result({ "experiment.cooked" }, &Cmd_experiment_cooked);
        reg.Result({ "experiment.matcook" }, &Cmd_experiment_matcook);
        reg.Result({ "experiment.matruntime" }, &Cmd_experiment_matruntime);
        reg.Result({ "experiment.scenecook" }, &Cmd_experiment_scenecook);
        reg.Result({ "experiment.catalog" }, &Cmd_experiment_catalog);
        reg.Result({ "assets.unload" }, &Cmd_assets_unload);
        reg.Result({ "bt.status", "bt.reset" }, &Cmd_bt_status);
    }
}
