#include "../EditorModelPlacement.h"
#include <DirectXTex.h>
#include <stb_image.h>
#include "Texture.h"
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
#include "Assets/ModelAssetAuthoringTransaction.h" // MBC11: 모델 저작 트랜잭션
#include "RHI/IRHIDeviceResources.h"                // MBC11: VRAM 계측
#include "LifecycleTrace.h"
#include "LifecycleRegistry.h"
#include "Animator.h"
#include "Socket.h" // X7 transform bulk probe
#include "BoneRegion.h" // MAX_BONES
#include "Experiment/Model.h" // I5: Experiment 모델 패리티
#include "RenderScene.h"      // I5-D4e-1: GetAnimationJob
#include "AvatarMask.h"       // I5: AvatarMask A/B 대조
#include "FoliageComponent.h"      // I5: Foliage 게이트
#include "Terrain.h"               // D4 Terrain YAML authoring round-trip
#include "Experiment/MaterialInstance.h"      // I5: Experiment MaterialInstance
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
#include "BoneComponent.h" // E7-b: 본 마커 보유 수 진단
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
#include "ExperimentParity/ExperimentVertexLayoutSelfTest.h"
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
#include <psapi.h> // MBC11: peak working set
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

    static CommandCore::CommandResult Cmd_model_async(const ConsoleCommandContext& ctx)
    {
        using namespace CommandCore;
        if (ctx.parts.size() == 3 && ctx.parts[1] == "probe")
        {
            const bool passed = RenderTest::RunIncrementalModelCancellationSelfTest(ctx.parts[2]);
            auto data = CommandData::Object();
            data.Set("passed", CommandData::Bool(passed));
            return passed ? Ok({}, std::move(data)) : Fail("model.async.probe_failed", "Cancellation/slot reuse failed", std::move(data));
        }
        if (ctx.parts.size() > 2) return InvalidArguments("model.async <path>|status|wait|probe <guard-path>");
        if (ctx.parts.size() == 2 && ctx.parts[1] == "wait")
        {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
            ctx.system.WaitForResult([deadline]() -> std::optional<CommandResult>
            {
                if (Editor::ModelPlacement::Get().IsIdle()) return Ok("Model placement queue is idle");
                if (std::chrono::steady_clock::now() < deadline) return std::nullopt;
                return CommandResult{ CommandStatus::TimedOut, "model.async.timeout", "Model placement did not become idle" };
            });
            return Ok(); // The command system publishes only the eventual result.
        }
        if (ctx.parts.size() < 2 || ctx.parts[1] == "status")
        {
            Editor::ModelPlacement::Get().PrintStatus();
            auto data = CommandData::Object();
            data.Set("idle", CommandData::Bool(Editor::ModelPlacement::Get().IsIdle()));
            return Ok({}, std::move(data));
        }
        Scene* scene = SceneManagers->GetActiveScene();
        if (!scene) return PreconditionFailed("scene.not_found", "No active scene");
        Editor::ModelPlacement::Get().Execute(scene->GetSceneId(), ctx.parts[1]);
        std::printf("[model.async] queued %s\n", ctx.parts[1].c_str());
        auto data = CommandData::Object();
        data.Set("queued", CommandData::Bool(true));
        return Ok("Model placement queued; wait checks completion", std::move(data));
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

    // I5-D5a — Foliage 메시의 experiment 핸들 합류 게이트. 코퍼스에 Foliage
    // 저작분이 0이라(착수 정찰 실측) 합성으로 판정한다: seed가 씬에
    // FoliageComponent+타입(Gunner)+인스턴스를 저작 경로(AddFoliageType —
    // 바인딩 지점) 그대로 심고 foliage 자산을 게시하며, 저장·재로드 뒤 verify가
    // ①postLoad 재해석 경로의 바인딩 ②프록시 DrawSource의 핸들 반영
    // (CaptureDrawSources — 실물 함수) ③뷰 완비(stableKey)를 잰다. 렌더러
    // poolFoliage 분기는 헤드리스 관측 밖(라이브 렌더 0프레임 + dx12.scene
    // 하네스에 Foliage 대칭 구성 없음) — 계획서 한계 기록.

    // I5-D4e-3 — 본 이름 해석 창구의 전수 A/B 대조. Scene 본 전파가 쓰는
    // 실물 창구(Animator::ResolveBoneIndex)를 BoneComponent 전수에 태우고,
    // legacy FindBone 직접 해석과 인덱스를 대조한다(1:1 계약 실증). 경로
    // 계수(viaExperiment)는 창구 내부의 실분기 관측이다 — 조건 재현이 아니라서
    // experiment 분기 소실이 legacy 폴백으로 조용히 덮여도 여기서 갈린다.

    // I5-D4e-3 — AvatarMask 트리 생성의 A/B 대조. 실물 창구
    // (BuildAvatarBoneMasks — experiment 단일 패스+스택 DFS)와 legacy
    // MakeBoneMask 재귀를 같은 스켈레톤에 돌려 m_BoneMasks의 크기·순서
    // (boneName 열)·자식 계수를 대조한다 — 순서가 저장분 인덱스 대응
    // (ReCreateMask)이라 순서 재현이 곧 저작 호환이다.

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

    static CommandCore::CommandResult Cmd_assets_decodeab(const ConsoleCommandContext& ctx)
    {
        using namespace CommandCore;
        if (ctx.parts.size() > 3) return InvalidArguments("assets.decodeab [root] [limit]");
        int parsedLimit = 0;
        if (ctx.parts.size() > 2 && (!ParseNumber(ctx.parts[2], parsedLimit) || parsedLimit < 0)) return InvalidArguments("limit must be a nonnegative integer");
        // 인자: [루트] [상한]. 루트를 '-' 로 주면 저작 자산 트리.
        //
        // ★ 기본값을 저장소 루트로 둔다. 디코더를 태우는 PNG 는 저작 자산
        //   (Dynamic_CPP/Assets)만이 아니다 — generation 이 뽑아 둔 모델
        //   임베디드 텍스처와 에디터 아이콘도 같은 로더를 지난다.
        const file::path root = (ctx.parts.size() > 1 && ctx.parts[1] != "-")
            ? file::path(ctx.parts[1]) : PathFinder::Relative();
        const int limit = ctx.parts.size() > 2
            ? parsedLimit : 0;   // 0 = 전수

        std::vector<file::path> sources;
        std::error_code walkError;
        for (auto it = file::recursive_directory_iterator(root, walkError);
            it != file::recursive_directory_iterator(); it.increment(walkError))
        {
            if (walkError) break;
            if (!it->is_regular_file(walkError)) continue;
            std::string ext = it->path().extension().string();
            for (char& c : ext) c = static_cast<char>(std::tolower(c));
            if (ext == ".png") sources.push_back(it->path());
            if (0 != limit && static_cast<int>(sources.size()) >= limit) break;
        }

        uint32_t compared = 0, identical = 0, sizeMismatch = 0, wicWasBgra = 0;
        uint32_t wicFailed = 0, stbFailed = 0, differing = 0;
        uint64_t totalDiffBytes = 0;
        uint32_t maxDelta = 0;
        uint32_t channelDiff[4] = { 0, 0, 0, 0 };
        std::vector<std::string> samples;

        for (const file::path& path : sources)
        {
            std::ifstream stream(path, std::ios::binary | std::ios::ate);
            if (!stream) continue;
            const std::streamsize size = stream.tellg();
            stream.seekg(0);
            std::vector<char> raw(static_cast<size_t>(size));
            stream.read(raw.data(), size);
            stream.close();
            const auto* bytes = reinterpret_cast<const uint8_t*>(raw.data());

            // ── A: DirectXTex(WIC) → RGBA8 정규화 ──
            DirectX::ScratchImage wic;
            DirectX::TexMetadata meta{};
            if (FAILED(DirectX::LoadFromWICMemory(bytes, raw.size(),
                DirectX::WIC_FLAGS_IGNORE_SRGB, &meta, wic))) { ++wicFailed; continue; }

            DirectX::ScratchImage converted;
            const DirectX::ScratchImage* wicFinal = &wic;
            if (DXGI_FORMAT_R8G8B8A8_UNORM != meta.format)
            {
                // ★ 이 계수가 0 이면 아래 Convert 가 한 번도 안 돌았다는 뜻이고,
                //   그러면 대조는 "WIC 원본 vs stb" 그대로다. 0 이 아니면
                //   "WIC→Convert vs stb" 이므로 Convert 가 채널 순서만 바꾼다는
                //   전제가 결과에 섞인다.
                ++wicWasBgra;
                // BGRA8_UNORM -> RGBA8_UNORM 은 채널 순서만 바꾼다(둘 다 IsSRGB
                // 가 false 라 DirectXTex 가 전달 함수에 손대지 않는다).
                if (FAILED(DirectX::Convert(wic.GetImages(), wic.GetImageCount(),
                    meta, DXGI_FORMAT_R8G8B8A8_UNORM, DirectX::TEX_FILTER_DEFAULT,
                    DirectX::TEX_THRESHOLD_DEFAULT, converted))) { ++wicFailed; continue; }
                wicFinal = &converted;
            }
            const DirectX::Image* a = wicFinal->GetImage(0, 0, 0);
            if (nullptr == a || nullptr == a->pixels) { ++wicFailed; continue; }

            // ── B: stb_image → RGBA8 ──
            int w = 0, h = 0, channelsInFile = 0;
            stbi_uc* b = stbi_load_from_memory(bytes, static_cast<int>(raw.size()),
                &w, &h, &channelsInFile, 4);
            if (nullptr == b) { ++stbFailed; continue; }

            ++compared;
            if (static_cast<size_t>(w) != a->width || static_cast<size_t>(h) != a->height)
            {
                ++sizeMismatch;
                if (samples.size() < 5)
                    samples.push_back(path.filename().string() + " 치수 "
                        + std::to_string(a->width) + "x" + std::to_string(a->height)
                        + " vs " + std::to_string(w) + "x" + std::to_string(h));
                stbi_image_free(b);
                continue;
            }

            // 행 간격이 다를 수 있다(WIC 쪽은 정렬). 행 단위로 센다.
            uint64_t diffBytes = 0;
            uint32_t localMax = 0;
            const size_t rowBytes = static_cast<size_t>(w) * 4u;
            for (int y = 0; y < h; ++y)
            {
                const uint8_t* ra = a->pixels + static_cast<size_t>(y) * a->rowPitch;
                const uint8_t* rb = b + static_cast<size_t>(y) * rowBytes;
                for (size_t x = 0; x < rowBytes; ++x)
                {
                    if (ra[x] == rb[x]) continue;
                    ++diffBytes;
                    const uint32_t delta = static_cast<uint32_t>(
                        std::abs(static_cast<int>(ra[x]) - static_cast<int>(rb[x])));
                    if (delta > localMax) localMax = delta;
                    ++channelDiff[x % 4];
                }
            }
            stbi_image_free(b);

            if (0 == diffBytes) { ++identical; continue; }
            ++differing;
            totalDiffBytes += diffBytes;
            if (localMax > maxDelta) maxDelta = localMax;
            if (samples.size() < 5)
            {
                char line[256]{};
                std::snprintf(line, sizeof(line), "%s — 다른 바이트 %llu / %llu (최대 편차 %u)",
                    path.filename().string().c_str(),
                    static_cast<unsigned long long>(diffBytes),
                    static_cast<unsigned long long>(rowBytes * h), localMax);
                samples.push_back(line);
            }
        }

        std::printf("[CLI] assets.decodeab PNG %zu장 중 %u장 대조\n",
            sources.size(), compared);
        std::printf("  완전 일치      %u\n", identical);
        std::printf("  픽셀 불일치    %u  (다른 바이트 누적 %llu · 최대 편차 %u)\n",
            differing, static_cast<unsigned long long>(totalDiffBytes), maxDelta);
        std::printf("  치수 불일치    %u\n", sizeMismatch);
        std::printf("  WIC 실패 %u · stb 실패 %u\n", wicFailed, stbFailed);
        std::printf("  WIC 이 BGRA 로 낸 장수 %u  (0 이면 정규화 없이 원본끼리 대조한 것)\n",
            wicWasBgra);
        if (0 != differing)
        {
            std::printf("  채널별 불일치 R %u · G %u · B %u · A %u\n",
                channelDiff[0], channelDiff[1], channelDiff[2], channelDiff[3]);
        }
        for (const std::string& sample : samples)
            std::printf("    %s\n", sample.c_str());
        std::printf("[CLI] assets.decodeab %s\n",
            (0 == differing && 0 == sizeMismatch && 0 == wicFailed && 0 == stbFailed)
                ? "통과" : "차이 있음");
        auto data = CommandData::Object();
        data.Set("found", CommandData::Int(sources.size()));
        data.Set("compared", CommandData::Int(compared));
        data.Set("identical", CommandData::Int(identical));
        data.Set("differing", CommandData::Int(differing));
        data.Set("sizeMismatch", CommandData::Int(sizeMismatch));
        data.Set("wicFailed", CommandData::Int(wicFailed));
        data.Set("stbFailed", CommandData::Int(stbFailed));
        data.Set("totalDiffBytes", CommandData::Int(totalDiffBytes));
        data.Set("maxDelta", CommandData::Int(maxDelta));
        if (!(!walkError && !sources.empty() && compared == sources.size() && identical == compared && !wicFailed && !stbFailed)) return Fail("texture.validation_failed", "Texture measurement incomplete or mismatched", std::move(data));
        return Ok("Texture measurement completed", std::move(data));

    }

    static CommandCore::CommandResult Cmd_assets_decodeabhdr(const ConsoleCommandContext& ctx)
    {
        using namespace CommandCore;
        if (ctx.parts.size() > 2) return InvalidArguments("assets.decodeabhdr [root]");
        const file::path root = (ctx.parts.size() > 1 && ctx.parts[1] != "-")
            ? file::path(ctx.parts[1]) : PathFinder::Relative();

        std::vector<file::path> sources;
        std::error_code walkError;
        for (auto it = file::recursive_directory_iterator(root, walkError);
            it != file::recursive_directory_iterator(); it.increment(walkError))
        {
            if (walkError) break;
            if (!it->is_regular_file(walkError)) continue;
            std::string ext = it->path().extension().string();
            for (char& c : ext) c = static_cast<char>(std::tolower(c));
            if (ext == ".hdr") sources.push_back(it->path());
        }

        uint32_t compared = 0, identical = 0, differing = 0, failed = 0;
        uint32_t stbChannels = 0;
        double maxDelta = 0.0;
        uint64_t diffSamples = 0;
        std::vector<std::string> samples;

        for (const file::path& path : sources)
        {
            std::ifstream stream(path, std::ios::binary | std::ios::ate);
            if (!stream) continue;
            const std::streamsize size = stream.tellg();
            stream.seekg(0);
            std::vector<char> raw(static_cast<size_t>(size));
            stream.read(raw.data(), size);
            stream.close();
            const auto* bytes = reinterpret_cast<const uint8_t*>(raw.data());

            DirectX::ScratchImage hdr;
            DirectX::TexMetadata meta{};
            if (FAILED(DirectX::LoadFromHDRMemory(bytes, raw.size(), &meta, hdr)))
            { ++failed; continue; }
            const DirectX::Image* a = hdr.GetImage(0, 0, 0);
            if (nullptr == a || DXGI_FORMAT_R32G32B32A32_FLOAT != meta.format)
            { ++failed; continue; }

            int w = 0, h = 0, channelsInFile = 0;
            float* b = stbi_loadf_from_memory(bytes, static_cast<int>(raw.size()),
                &w, &h, &channelsInFile, 4);
            if (nullptr == b) { ++failed; continue; }
            stbChannels = static_cast<uint32_t>(channelsInFile);

            if (static_cast<size_t>(w) != a->width || static_cast<size_t>(h) != a->height)
            { ++failed; stbi_image_free(b); continue; }
            ++compared;
            double localMax = 0.0;
            uint64_t localDiff = 0;
            if (static_cast<size_t>(w) == a->width && static_cast<size_t>(h) == a->height)
            {
                for (int y = 0; y < h; ++y)
                {
                    const auto* ra = reinterpret_cast<const float*>(
                        a->pixels + static_cast<size_t>(y) * a->rowPitch);
                    const float* rb = b + static_cast<size_t>(y) * w * 4;
                    for (int x = 0; x < w * 4; ++x)
                    {
                        const double delta = std::abs(
                            static_cast<double>(ra[x]) - static_cast<double>(rb[x]));
                        if (!std::isfinite(delta) || delta > 0.0) { ++localDiff; if (delta > localMax) localMax = delta; }
                    }
                }
            }
            // ★ 첫 불일치 자산에서 실제 값을 찍는다. "다르다"와 "몇 배
            //   다르다"는 대응이 갈린다 — 비율이 일정하면 스케일 규약 차이이고
            //   보정 가능하지만, 들쭉날쭉하면 디코드 자체가 다른 것이다.
            if (0 != localDiff && 0 == differing)
            {
                const auto* ra = reinterpret_cast<const float*>(a->pixels);
                for (int s = 0; s < (std::min)(4, w * h); ++s)
                {
                    const int o = s * 4;
                    std::printf("    표본%d WIC(%.6f %.6f %.6f %.3f) stb(%.6f %.6f %.6f %.3f) 비율 %.5f\n",
                        s, ra[o], ra[o + 1], ra[o + 2], ra[o + 3],
                        b[o], b[o + 1], b[o + 2], b[o + 3],
                        (0.0f != b[o]) ? (ra[o] / b[o]) : 0.0f);
                }
            }
            stbi_image_free(b);

            if (0 == localDiff) { ++identical; continue; }
            ++differing;
            diffSamples += localDiff;
            if (localMax > maxDelta) maxDelta = localMax;
            if (samples.size() < 5)
            {
                char line[256]{};
                std::snprintf(line, sizeof(line), "%s — 다른 표본 %llu (최대 편차 %.6f)",
                    path.filename().string().c_str(),
                    static_cast<unsigned long long>(localDiff), localMax);
                samples.push_back(line);
            }
        }

        std::printf("[CLI] assets.decodeabhdr HDR %zu장 중 %u장 대조\n",
            sources.size(), compared);
        std::printf("  완전 일치      %u\n", identical);
        std::printf("  값 불일치      %u  (다른 표본 %llu · 최대 편차 %.6f)\n",
            differing, static_cast<unsigned long long>(diffSamples), maxDelta);
        std::printf("  실패           %u\n", failed);
        std::printf("  파일의 채널 수 %u  (stbi_loadf 는 4로 강제 요청했다)\n", stbChannels);
        for (const std::string& sample : samples)
            std::printf("    %s\n", sample.c_str());
        std::printf("[CLI] assets.decodeabhdr %s\n",
            (0 == differing && 0 == failed) ? "통과" : "차이 있음");
        auto data = CommandData::Object();
        data.Set("found", CommandData::Int(sources.size()));
        data.Set("compared", CommandData::Int(compared));
        data.Set("identical", CommandData::Int(identical));
        data.Set("differing", CommandData::Int(differing));
        data.Set("failed", CommandData::Int(failed));
        data.Set("diffSamples", CommandData::Int(diffSamples));
        data.Set("maxDelta", CommandData::Double(maxDelta));
        if (!(!walkError && !sources.empty() && compared == sources.size() && identical == compared && !failed)) return Fail("texture.validation_failed", "Texture measurement incomplete or mismatched", std::move(data));
        return Ok("Texture measurement completed", std::move(data));

    }

    static CommandCore::CommandResult Cmd_assets_texturebench(const ConsoleCommandContext& ctx)
    {
        using namespace CommandCore;
        int parsedLimit = 64;
        if (ctx.parts.size() > 2 || (ctx.parts.size() > 1 && (!ParseNumber(ctx.parts[1], parsedLimit) || parsedLimit < 1))) return InvalidArguments("assets.texturebench [positive limit]");
        namespace chrono = std::chrono;
        const int limit = ctx.parts.size() > 1
            ? parsedLimit : 64;
        const file::path root = PathFinder::Relative();

        std::vector<file::path> sources;
        std::error_code walkError;
        for (auto it = file::recursive_directory_iterator(root, walkError);
            it != file::recursive_directory_iterator(); it.increment(walkError))
        {
            if (walkError) break;
            if (!it->is_regular_file(walkError)) continue;
            std::string ext = it->path().extension().string();
            for (char& c : ext) c = static_cast<char>(std::tolower(c));
            if (ext == ".png") sources.push_back(it->path());
            if (static_cast<int>(sources.size()) >= limit) break;
        }
        if (sources.empty())
        {
            std::printf("[CLI] assets.texturebench fail PNG 를 찾지 못했다: %s\n",
                root.string().c_str());
            return PreconditionFailed("texture.corpus_missing", "No PNG input found");
        }

        double readMs = 0.0, decodeMs = 0.0, mipMs = 0.0, compressMs = 0.0, legacyMs = 0.0;
        uint64_t sourceBytes = 0, decodedBytes = 0, mippedBytes = 0, compressedBytes = 0;
        uint32_t decoded = 0, mipped = 0, compressed = 0;

        for (const file::path& path : sources)
        {
            // ① 파일 바이트 읽기 — cooked artifact 로드의 하한이다.
            //    (artifact 는 헤더 파싱 + mmap 이므로 디코드가 0 이다)
            auto t0 = chrono::steady_clock::now();
            std::ifstream stream(path, std::ios::binary | std::ios::ate);
            if (!stream) continue;
            const std::streamsize size = stream.tellg();
            stream.seekg(0);
            std::vector<char> raw(static_cast<size_t>(size));
            stream.read(raw.data(), size);
            stream.close();
            auto t1 = chrono::steady_clock::now();
            readMs += chrono::duration<double, std::milli>(t1 - t0).count();
            sourceBytes += static_cast<uint64_t>(size);

            // ② 디코드 — 지금 런타임이 로드마다 하는 일.
            DirectX::ScratchImage image;
            DirectX::TexMetadata metadata{};
            t0 = chrono::steady_clock::now();
            HRESULT hr = DirectX::LoadFromWICMemory(
                reinterpret_cast<const uint8_t*>(raw.data()), raw.size(),
                DirectX::WIC_FLAGS_IGNORE_SRGB, &metadata, image);
            t1 = chrono::steady_clock::now();
            if (FAILED(hr)) continue;
            decodeMs += chrono::duration<double, std::milli>(t1 - t0).count();
            decodedBytes += image.GetPixelsSize();
            ++decoded;

            // ③ 밉 생성 — 지금 파이프라인에 아예 없는 단계.
            DirectX::ScratchImage mips;
            t0 = chrono::steady_clock::now();
            hr = DirectX::GenerateMipMaps(image.GetImages(), image.GetImageCount(),
                metadata, DirectX::TEX_FILTER_DEFAULT, 0, mips);
            t1 = chrono::steady_clock::now();
            const bool hasMips = SUCCEEDED(hr) && 0 != mips.GetImageCount();
            if (hasMips)
            {
                mipMs += chrono::duration<double, std::milli>(t1 - t0).count();
                mippedBytes += mips.GetPixelsSize();
                ++mipped;
            }

            // ④ BC1 압축 — 지금은 baseColorMap 만, 그것도 런타임에.
            const DirectX::ScratchImage& compressSource = hasMips ? mips : image;
            DirectX::ScratchImage block;
            t0 = chrono::steady_clock::now();
            hr = DirectX::Compress(compressSource.GetImages(),
                compressSource.GetImageCount(), compressSource.GetMetadata(),
                DXGI_FORMAT_BC1_UNORM_SRGB,
                DirectX::TEX_COMPRESS_SRGB | DirectX::TEX_COMPRESS_UNIFORM,
                0.5f, block);
            t1 = chrono::steady_clock::now();
            if (SUCCEEDED(hr))
            {
                compressMs += chrono::duration<double, std::milli>(t1 - t0).count();
                compressedBytes += block.GetPixelsSize();
                ++compressed;
            }

            // ⑤ 지금의 런타임 로드 경로 전체(디코드 + 중립 이미지 감싸기).
            t0 = chrono::steady_clock::now();
            std::shared_ptr<Texture> texture = Texture::LoadSharedFromPath(path);
            t1 = chrono::steady_clock::now();
            if (texture) legacyMs += chrono::duration<double, std::milli>(t1 - t0).count();
        }

        const double mb = 1024.0 * 1024.0;
        std::printf("[CLI] assets.texturebench %u장\n", decoded);
        std::printf("  파일 읽기      %8.1f ms  (%.1f MB)   <- cooked artifact 로드 하한\n",
            readMs, static_cast<double>(sourceBytes) / mb);
        std::printf("  디코드         %8.1f ms  (%.1f MB)   <- 런타임이 매번 하는 일\n",
            decodeMs, static_cast<double>(decodedBytes) / mb);
        std::printf("  밉 생성        %8.1f ms  (%.1f MB, %u장)\n",
            mipMs, static_cast<double>(mippedBytes) / mb, mipped);
        std::printf("  BC1 압축       %8.1f ms  (%.1f MB, %u장)\n",
            compressMs, static_cast<double>(compressedBytes) / mb, compressed);
        std::printf("  cook 합계      %8.1f ms  (디코드+밉+압축)\n",
            decodeMs + mipMs + compressMs);
        std::printf("  현재 로드 경로 %8.1f ms  (LoadSharedFromPath)\n", legacyMs);
        std::printf("  장당 평균 — 디코드 %.2f ms · cook %.2f ms · 읽기 %.2f ms\n",
            decodeMs / (std::max)(1u, decoded),
            (decodeMs + mipMs + compressMs) / (std::max)(1u, decoded),
            readMs / (std::max)(1u, decoded));
        auto data = CommandData::Object();
        data.Set("readMs", CommandData::Double(readMs));
        data.Set("decodeMs", CommandData::Double(decodeMs));
        data.Set("mipMs", CommandData::Double(mipMs));
        data.Set("compressMs", CommandData::Double(compressMs));
        data.Set("legacyMs", CommandData::Double(legacyMs));
        data.Set("decoded", CommandData::Int(decoded));
        data.Set("mipped", CommandData::Int(mipped));
        data.Set("compressed", CommandData::Int(compressed));
        data.Set("sourceBytes", CommandData::Int(sourceBytes));
        data.Set("decodedBytes", CommandData::Int(decodedBytes));
        data.Set("mippedBytes", CommandData::Int(mippedBytes));
        data.Set("compressedBytes", CommandData::Int(compressedBytes));
        data.Set("found", CommandData::Int(sources.size()));
        if (!(!walkError && decoded == sources.size() && mipped == decoded && compressed == decoded)) return Fail("texture.validation_failed", "Texture measurement incomplete or mismatched", std::move(data));
        return Ok("Texture measurement completed", std::move(data));

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

    // I7-C1 — cooked catalog 관측·마운트. 굽는 쪽(AssetCooker → Derived/ +
    // CEMF → pak)은 D5-b2c에서 다 섰는데 읽는 쪽이 이어져 있지 않아 cooked
    // 경로가 제품에서 한 번도 돌지 않았다(실측 texCooked=0). 이 명령이
    //   ① 기동 마운트 상태를 보이고
    //   ② 게이트가 임시 Derived 트리를 명시적으로 마운트할 창구가 된다.
    // 저작 트리에는 Derived가 없으므로 인자 없는 실행은 미게시(skip)가 정상이다.

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
        reg.Result({ "assets.decodeab" }, &Cmd_assets_decodeab);
        reg.Result({ "assets.decodeabhdr" }, &Cmd_assets_decodeabhdr);
        reg.Result({ "assets.texturebench" }, &Cmd_assets_texturebench);
        reg.Result({ "collisionmatrix.authoring.probe" },
                   &Cmd_collisionmatrix_authoring_probe);
        reg.Result({ "model.load" }, &Cmd_model_load);
        reg.Result({ "terrain.authoring.probe" }, &Cmd_terrain_authoring_probe);
        reg.Result({ "foliage.authoring.probe" }, &Cmd_foliage_authoring_probe);
        reg.Result({ "blackboard.authoring.probe" }, &Cmd_blackboard_authoring_probe);
        reg.Result({ "material.corpus.probe" }, &Cmd_material_corpus_probe);
        reg.Result({ "tag.list" }, &Cmd_tag_list);
        reg.Result({ "tag.has" }, &Cmd_tag_has);
        reg.Result({ "tag.add" }, &Cmd_tag_add);
        reg.Result({ "tag.remove" }, &Cmd_tag_remove);
        reg.Result({ "inputmap.authoring.probe" }, &Cmd_inputmap_authoring_probe);
        reg.Result({ "inputmap.corpus.probe" }, &Cmd_inputmap_corpus_probe);
        reg.Result({ "model.loadcached" }, &Cmd_model_loadcached);
        reg.Result({ "model.place" }, &Cmd_model_place);
        reg.Result({ "model.async" }, &Cmd_model_async);
        reg.Result({ "assets.scenemodel" }, &Cmd_assets_scenemodel);
        reg.Result({ "assets.modeldiag" }, &Cmd_assets_modeldiag);
        reg.Result({ "animator.status" }, &Cmd_animator_status);
        reg.Result({ "experiment.cooked" }, &Cmd_experiment_cooked);
        reg.Result({ "assets.unload" }, &Cmd_assets_unload);
        reg.Result({ "bt.status", "bt.reset" }, &Cmd_bt_status);
    }
}
