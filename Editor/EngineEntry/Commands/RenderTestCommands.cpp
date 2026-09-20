// LC6 (PHASE 14.5) — RenderTest 도메인 명령.
//
// `dx12.*` · `vk.*` · `render.*` · `rhi.*` · `pipeline.*` 64 개. 전부 렌더러의
// 관측/격리 검사라 GUI 의미가 없다(§9 의 Test/diagnostic probe).
//
// ── 이 이동에서 바꾸지 않은 것 ──────────────────────────────────────────
//
// 핸들러 본문은 **한 글자도 손대지 않았다.** 서명도 그대로 legacy 64 개다.
// LC6 이 "이동하는 handler 를 result-bearing 으로 함께 바꾼다"고 적어 두었지만,
// §12 는 "파일 분리는 기능 변경과 한 덩어리로 하지 않는다"고 못 박는다. 둘을
// 같은 커밋에 넣으면 `verify-cli-registry-golden.ps1` 의 `result_bearing` 열이
// **정당하게** 바뀌고, 그 순간 "이동이 뭔가를 깨뜨렸나"와 "이행이 뭔가를
// 바꿨나"를 가를 수 없게 된다. 골든이 한 글자도 안 변하는 것이 이 이동의
// 유일한 증거라, 그 증거를 버리지 않는다. 이행은 별도로 간다.
//
// ── include 를 이 TU 가 직접 소유한다 ───────────────────────────────────
//
// 이 파일은 유니티 빌드에서 빠져 있다(`IncludeInUnityFile=false`). 그래서
// 다른 파일이 앞서 들여온 헤더에 기댈 수 없고, 기대면 **평상시 빌드에서**
// 바로 깨진다 — 아무도 안 돌리는 별도 빌드 모드에 맡기지 않는다.
//
// ★ 목록은 `ConsoleCommandSystem.cpp` 의 것을 통째로 물려받았다. 도메인이
//   일곱 개 다 나간 뒤에 남는 것을 보고 줄이는 것이 순서다 — 지금 줄이면
//   무엇이 어느 도메인의 것인지 모르는 채로 자르게 된다.

#include "CommandRegistrar.h"
#include "RHI/Vulkan/VulkanSelfTest.h"
#include "CommandSupport.h"

#include "CommandCore/CommandSession.h" // LC1: 결과 누적과 process exit code
#include "CommandCore/CommandParser.h"
#include "CommandCore/CommandRegistry.h"       // LC3: descriptor snapshot
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
#include "BoneRegion.h" // kMaxBones
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
    static CommandCore::CommandResult Cmd_dx12_selftest(const ConsoleCommandContext& ctx)
    {
        const std::vector<std::string>& parts = ctx.parts;
        if (parts.size() < 2 || parts.size() > 3) return CommandCore::InvalidArguments("dx12.selftest <texture-path> [output]");

        // EnhancedSceneRenderer 브링업 자가 검증(PHASE 3-3). 자체 디바이스·큐·펜스로
        // 돌므로 DX11 렌더 스레드와 충돌하지 않는다 — 게임 스레드에서 즉시 실행.
        const std::string outputPath = ResolveTestArtifactPath("DX12",
            (parts.size() > 2) ? parts[2] : std::string("dx12_selftest.png"));

        std::string log;
        const bool passed = DX12Test::RunSelfTest(outputPath, 6, parts[1], log);

        for (const auto& line : { log })
        {
            std::printf("%s", line.c_str());
        }
        Debug::PrintLog(spdlog::level::warn, std::string("[dx12.selftest] ") + (passed ? "통과" : "실패") + "\n" + log);
        std::printf("[CLI] dx12.selftest %s → %s\n", passed ? "통과" : "실패", outputPath.c_str());

        // LC6: 판정을 값으로 돌려준다. 위의 printf 는 그대로 둔다 —
        // 기존 하네스가 stdout 을 읽고 있고, 그 이주까지 같은 변경에 넣으면
        // 무엇이 깨졌는지 가를 수 없게 된다.
        CommandCore::CommandData data = CommandCore::CommandData::Object();
        data.Set("log", CommandCore::CommandData::String(log));
        data.Set("passed", CommandCore::CommandData::Bool(passed));
        if (!passed)
        {
            return CommandCore::Fail("rendertest.failed", "dx12.selftest 실패", std::move(data));
        }
        return CommandCore::Ok("dx12.selftest 통과", std::move(data));
    }

    static CommandCore::CommandResult Cmd_vk_grid(const ConsoleCommandContext& ctx)
    {
        // 그리드 패스를 Vulkan 으로 (5d). EnhancedGridPass 를 한 줄도 안 고치고
        // 돌려 dx12.grid 기준선과 픽셀 대조한다 — 지표 ②(공유 패스)와
        // ③(픽셀 대조)이 처음으로 0 을 벗어나는 검사다.
        std::string log;
        const bool passed = RunVulkanGridTest(log);

        std::printf("%s", log.c_str());
        Debug::PrintLog(spdlog::level::warn, std::string("[vk.grid] ") + (passed ? "통과" : "실패") + "\n" + log);
        std::printf("[CLI] vk.grid %s\n", passed ? "통과" : "실패");

        // LC6: 판정을 값으로 돌려준다. 위의 printf 는 그대로 둔다 —
        // 기존 하네스가 stdout 을 읽고 있고, 그 이주까지 같은 변경에 넣으면
        // 무엇이 깨졌는지 가를 수 없게 된다.
        CommandCore::CommandData data = CommandCore::CommandData::Object();
        data.Set("log", CommandCore::CommandData::String(log));
        data.Set("passed", CommandCore::CommandData::Bool(passed));
        if (!passed)
        {
            return CommandCore::Fail("rendertest.failed", "vk.grid 실패", std::move(data));
        }
        return CommandCore::Ok("vk.grid 통과", std::move(data));
    }

    static CommandCore::CommandResult Cmd_vk_texturecodec(const ConsoleCommandContext& ctx)
    {
        std::string log;
        const bool passed = RunVulkanTextureCodecTest(log);

        std::printf("%s", log.c_str());
        Debug::PrintLog(spdlog::level::warn, std::string("[vk.texturecodec] ") +
            (passed ? "통과" : "실패") + "\n" + log);
        std::printf("[CLI] vk.texturecodec %s\n", passed ? "통과" : "실패");

        // LC6: 판정을 값으로 돌려준다. 위의 printf 는 그대로 둔다 —
        // 기존 하네스가 stdout 을 읽고 있고, 그 이주까지 같은 변경에 넣으면
        // 무엇이 깨졌는지 가를 수 없게 된다.
        CommandCore::CommandData data = CommandCore::CommandData::Object();
        data.Set("log", CommandCore::CommandData::String(log));
        data.Set("passed", CommandCore::CommandData::Bool(passed));
        if (!passed)
        {
            return CommandCore::Fail("rendertest.failed", "vk.texturecodec 실패", std::move(data));
        }
        return CommandCore::Ok("vk.texturecodec 통과", std::move(data));
    }

    static CommandCore::CommandResult Cmd_vk_shadow(const ConsoleCommandContext& ctx)
    {
        std::string log;
        const bool passed = RunVulkanShadowTest(log);

        std::printf("%s", log.c_str());
        Debug::PrintLog(spdlog::level::warn, std::string("[vk.shadow] ") +
            (passed ? "통과" : "실패") + "\n" + log);
        std::printf("[CLI] vk.shadow %s\n", passed ? "통과" : "실패");

        // LC6: 판정을 값으로 돌려준다. 위의 printf 는 그대로 둔다 —
        // 기존 하네스가 stdout 을 읽고 있고, 그 이주까지 같은 변경에 넣으면
        // 무엇이 깨졌는지 가를 수 없게 된다.
        CommandCore::CommandData data = CommandCore::CommandData::Object();
        data.Set("log", CommandCore::CommandData::String(log));
        data.Set("passed", CommandCore::CommandData::Bool(passed));
        if (!passed)
        {
            return CommandCore::Fail("rendertest.failed", "vk.shadow 실패", std::move(data));
        }
        return CommandCore::Ok("vk.shadow 통과", std::move(data));
    }

    static CommandCore::CommandResult Cmd_vk_gbuffer(const ConsoleCommandContext& ctx)
    {
        std::string log;
        const bool passed = RunVulkanGBufferTest(log);

        std::printf("%s", log.c_str());
        Debug::PrintLog(spdlog::level::warn, std::string("[vk.gbuffer] ") +
            (passed ? "통과" : "실패") + "\n" + log);
        std::printf("[CLI] vk.gbuffer %s\n", passed ? "통과" : "실패");

        // LC6: 판정을 값으로 돌려준다. 위의 printf 는 그대로 둔다 —
        // 기존 하네스가 stdout 을 읽고 있고, 그 이주까지 같은 변경에 넣으면
        // 무엇이 깨졌는지 가를 수 없게 된다.
        CommandCore::CommandData data = CommandCore::CommandData::Object();
        data.Set("log", CommandCore::CommandData::String(log));
        data.Set("passed", CommandCore::CommandData::Bool(passed));
        if (!passed)
        {
            return CommandCore::Fail("rendertest.failed", "vk.gbuffer 실패", std::move(data));
        }
        return CommandCore::Ok("vk.gbuffer 통과", std::move(data));
    }

    static CommandCore::CommandResult Cmd_vk_forward(const ConsoleCommandContext& ctx)
    {
        std::string result;
        const bool passed = RunVulkanForwardTest(result);
        std::printf("%s", result.c_str());
        Debug::PrintLog(spdlog::level::warn, std::string("[vk.forward] ") +
            (passed ? "통과\n" : "실패\n") + result);
        std::printf("[CLI] vk.forward %s\n", passed ? "통과" : "실패");

        // LC6: 판정을 값으로 돌려준다. 위의 printf 는 그대로 둔다 —
        // 기존 하네스가 stdout 을 읽고 있고, 그 이주까지 같은 변경에 넣으면
        // 무엇이 깨졌는지 가를 수 없게 된다.
        CommandCore::CommandData data = CommandCore::CommandData::Object();
        data.Set("log", CommandCore::CommandData::String(result));
        data.Set("passed", CommandCore::CommandData::Bool(passed));
        if (!passed)
        {
            return CommandCore::Fail("rendertest.failed", "vk.forward 실패", std::move(data));
        }
        return CommandCore::Ok("vk.forward 통과", std::move(data));
    }

    static CommandCore::CommandResult Cmd_vk_deferred(const ConsoleCommandContext& ctx)
    {
        std::string result;
        const bool passed = RunVulkanDeferredTest(result);
        Debug::PrintLog(spdlog::level::warn, std::string("[vk.deferred] ") +
            (passed ? "통과\n" : "실패\n") + result);
        std::printf("[CLI] vk.deferred %s\n", passed ? "통과" : "실패");

        // LC6: 판정을 값으로 돌려준다. 위의 printf 는 그대로 둔다 —
        // 기존 하네스가 stdout 을 읽고 있고, 그 이주까지 같은 변경에 넣으면
        // 무엇이 깨졌는지 가를 수 없게 된다.
        CommandCore::CommandData data = CommandCore::CommandData::Object();
        data.Set("log", CommandCore::CommandData::String(result));
        data.Set("passed", CommandCore::CommandData::Bool(passed));
        if (!passed)
        {
            return CommandCore::Fail("rendertest.failed", "vk.deferred 실패", std::move(data));
        }
        return CommandCore::Ok("vk.deferred 통과", std::move(data));
    }

    static CommandCore::CommandResult Cmd_vk_decal(const ConsoleCommandContext& ctx)
    {
        std::string result;
        const bool passed = RunVulkanDecalTest(result);
        Debug::PrintLog(spdlog::level::warn, std::string("[vk.decal] ") +
            (passed ? "통과\n" : "실패\n") + result);
        std::printf("[CLI] vk.decal %s\n", passed ? "통과" : "실패");

        // LC6: 판정을 값으로 돌려준다. 위의 printf 는 그대로 둔다 —
        // 기존 하네스가 stdout 을 읽고 있고, 그 이주까지 같은 변경에 넣으면
        // 무엇이 깨졌는지 가를 수 없게 된다.
        CommandCore::CommandData data = CommandCore::CommandData::Object();
        data.Set("log", CommandCore::CommandData::String(result));
        data.Set("passed", CommandCore::CommandData::Bool(passed));
        if (!passed)
        {
            return CommandCore::Fail("rendertest.failed", "vk.decal 실패", std::move(data));
        }
        return CommandCore::Ok("vk.decal 통과", std::move(data));
    }

    static CommandCore::CommandResult Cmd_dx12_forward(const ConsoleCommandContext& ctx)
    {
        std::string log;
        const bool passed = DX12Test::RunForwardPlusTest(log);
        const std::string verdict = passed ? "통과" : "실패";

        std::printf("%s", log.c_str());
        Debug::PrintLog(spdlog::level::warn, "[dx12.forward] " + verdict + "\n" + log);
        std::printf("[CLI] dx12.forward %s\n", verdict.c_str());

        // LC6: 판정을 값으로 돌려준다. 위의 printf 는 그대로 둔다 —
        // 기존 하네스가 stdout 을 읽고 있고, 그 이주까지 같은 변경에 넣으면
        // 무엇이 깨졌는지 가를 수 없게 된다.
        CommandCore::CommandData data = CommandCore::CommandData::Object();
        data.Set("log", CommandCore::CommandData::String(log));
        data.Set("passed", CommandCore::CommandData::Bool(passed));
        if (!passed)
        {
            return CommandCore::Fail("rendertest.failed", "dx12.forward 실패", std::move(data));
        }
        return CommandCore::Ok("dx12.forward 통과", std::move(data));
    }

    static CommandCore::CommandResult Cmd_dx12_forwardshade(const ConsoleCommandContext& ctx)
    {
        std::string log;
        const bool passed = DX12Test::RunForwardPlusShadeTest(log);
        const std::string verdict = passed ? "통과" : "실패";

        std::printf("%s", log.c_str());
        Debug::PrintLog(spdlog::level::warn, "[dx12.forwardshade] " + verdict + "\n" + log);
        std::printf("[CLI] dx12.forwardshade %s\n", verdict.c_str());

        // LC6: 판정을 값으로 돌려준다. 위의 printf 는 그대로 둔다 —
        // 기존 하네스가 stdout 을 읽고 있고, 그 이주까지 같은 변경에 넣으면
        // 무엇이 깨졌는지 가를 수 없게 된다.
        CommandCore::CommandData data = CommandCore::CommandData::Object();
        data.Set("log", CommandCore::CommandData::String(log));
        data.Set("passed", CommandCore::CommandData::Bool(passed));
        if (!passed)
        {
            return CommandCore::Fail("rendertest.failed", "dx12.forwardshade 실패", std::move(data));
        }
        return CommandCore::Ok("dx12.forwardshade 통과", std::move(data));
    }





    static CommandCore::CommandResult Cmd_dx12_post(const ConsoleCommandContext& ctx)
    {
        std::string log;
        const bool passed = DX12Test::RunPostChainTest(log);
        const std::string verdict = passed ? "통과" : "실패";

        std::printf("%s", log.c_str());
        Debug::PrintLog(spdlog::level::warn, "[dx12.post] " + verdict + "\n" + log);
        std::printf("[CLI] dx12.post %s\n", verdict.c_str());

        // LC6: 판정을 값으로 돌려준다. 위의 printf 는 그대로 둔다 —
        // 기존 하네스가 stdout 을 읽고 있고, 그 이주까지 같은 변경에 넣으면
        // 무엇이 깨졌는지 가를 수 없게 된다.
        CommandCore::CommandData data = CommandCore::CommandData::Object();
        data.Set("log", CommandCore::CommandData::String(log));
        data.Set("passed", CommandCore::CommandData::Bool(passed));
        if (!passed)
        {
            return CommandCore::Fail("rendertest.failed", "dx12.post 실패", std::move(data));
        }
        return CommandCore::Ok("dx12.post 통과", std::move(data));
    }



    static CommandCore::CommandResult Cmd_dx12_ui(const ConsoleCommandContext& ctx)
    {
        std::string log;
        const bool passed = DX12Test::RunUITest(log);
        const std::string verdict = passed ? "통과" : "실패";

        std::printf("%s", log.c_str());
        Debug::PrintLog(spdlog::level::warn, "[dx12.ui] " + verdict + "\n" + log);
        std::printf("[CLI] dx12.ui %s\n", verdict.c_str());

        // LC6: 판정을 값으로 돌려준다. 위의 printf 는 그대로 둔다 —
        // 기존 하네스가 stdout 을 읽고 있고, 그 이주까지 같은 변경에 넣으면
        // 무엇이 깨졌는지 가를 수 없게 된다.
        CommandCore::CommandData data = CommandCore::CommandData::Object();
        data.Set("log", CommandCore::CommandData::String(log));
        data.Set("passed", CommandCore::CommandData::Bool(passed));
        if (!passed)
        {
            return CommandCore::Fail("rendertest.failed", "dx12.ui 실패", std::move(data));
        }
        return CommandCore::Ok("dx12.ui 통과", std::move(data));
    }

    static CommandCore::CommandResult Cmd_dx12_gizmoscene(const ConsoleCommandContext& ctx)
    {
        // Gizmo 계열 씬 연결 검증(PHASE 3-6, Gizmo 계열 5차 슬라이스).
        std::string log;
        const bool passed = DX12Test::RunGizmoSceneTest(log);
        const std::string verdict = passed ? "통과" : "실패";

        std::printf("%s", log.c_str());
        Debug::PrintLog(spdlog::level::warn, "[dx12.gizmoscene] " + verdict + "\n" + log);
        std::printf("[CLI] dx12.gizmoscene %s\n", verdict.c_str());

        // LC6: 판정을 값으로 돌려준다. 위의 printf 는 그대로 둔다 —
        // 기존 하네스가 stdout 을 읽고 있고, 그 이주까지 같은 변경에 넣으면
        // 무엇이 깨졌는지 가를 수 없게 된다.
        CommandCore::CommandData data = CommandCore::CommandData::Object();
        data.Set("log", CommandCore::CommandData::String(log));
        data.Set("passed", CommandCore::CommandData::Bool(passed));
        if (!passed)
        {
            return CommandCore::Fail("rendertest.failed", "dx12.gizmoscene 실패", std::move(data));
        }
        return CommandCore::Ok("dx12.gizmoscene 통과", std::move(data));
    }

    static CommandCore::CommandResult Cmd_dx12_shadowquality(const ConsoleCommandContext& ctx)
    {
        // 그림자 품질 검증(PHASE 3-6 — 경사 비례 편향·캐스케이드 경계 블렌딩).
        std::string log;
        const bool passed = DX12Test::RunShadowQualityTest(log);
        const std::string verdict = passed ? "통과" : "실패";

        std::printf("%s", log.c_str());
        Debug::PrintLog(spdlog::level::warn, "[dx12.shadowquality] " + verdict + "\n" + log);
        std::printf("[CLI] dx12.shadowquality %s\n", verdict.c_str());

        // LC6: 판정을 값으로 돌려준다. 위의 printf 는 그대로 둔다 —
        // 기존 하네스가 stdout 을 읽고 있고, 그 이주까지 같은 변경에 넣으면
        // 무엇이 깨졌는지 가를 수 없게 된다.
        CommandCore::CommandData data = CommandCore::CommandData::Object();
        data.Set("log", CommandCore::CommandData::String(log));
        data.Set("passed", CommandCore::CommandData::Bool(passed));
        if (!passed)
        {
            return CommandCore::Fail("rendertest.failed", "dx12.shadowquality 실패", std::move(data));
        }
        return CommandCore::Ok("dx12.shadowquality 통과", std::move(data));
    }

    static CommandCore::CommandResult Cmd_dx12_skinning(const ConsoleCommandContext& ctx)
    {
        // GBuffer 스키닝 검증(PHASE 3-6).
        std::string log;
        const bool passed = DX12Test::RunSkinningTest(log);
        const std::string verdict = passed ? "통과" : "실패";

        std::printf("%s", log.c_str());
        Debug::PrintLog(spdlog::level::warn, "[dx12.skinning] " + verdict + "\n" + log);
        std::printf("[CLI] dx12.skinning %s\n", verdict.c_str());

        // LC6: 판정을 값으로 돌려준다. 위의 printf 는 그대로 둔다 —
        // 기존 하네스가 stdout 을 읽고 있고, 그 이주까지 같은 변경에 넣으면
        // 무엇이 깨졌는지 가를 수 없게 된다.
        CommandCore::CommandData data = CommandCore::CommandData::Object();
        data.Set("log", CommandCore::CommandData::String(log));
        data.Set("passed", CommandCore::CommandData::Bool(passed));
        if (!passed)
        {
            return CommandCore::Fail("rendertest.failed", "dx12.skinning 실패", std::move(data));
        }
        return CommandCore::Ok("dx12.skinning 통과", std::move(data));
    }

    static CommandCore::CommandResult Cmd_render_livecheck(const ConsoleCommandContext& ctx)
    {
        const std::vector<std::string>& parts = ctx.parts;

        const uint32_t expectedWidth = (parts.size() >= 3)
            ? static_cast<uint32_t>((std::max)(0, std::atoi(parts[1].c_str())))
            : ScreenResizeBus::Get().GetWidth();
        const uint32_t expectedHeight = (parts.size() >= 3)
            ? static_cast<uint32_t>((std::max)(0, std::atoi(parts[2].c_str())))
            : ScreenResizeBus::Get().GetHeight();

		const RenderBackend configured = RuntimeSettings::Get().GetRenderBackend();
        const EnhancedLiveBackend scene = EnhancedSceneRenderer::GetLiveBackend();
        const bool backendMatch =
            ((RenderBackend::DX12 == configured && EnhancedLiveBackend::DX12 == scene) ||
             (RenderBackend::Vulkan == configured && EnhancedLiveBackend::Vulkan == scene)) &&
            ((RenderBackend::DX12 == configured &&
                0 == std::strcmp(GetImGuiHost().GetBackendName(), "DX12")) ||
             (RenderBackend::Vulkan == configured &&
                0 == std::strcmp(GetImGuiHost().GetBackendName(), "Vulkan")));

        std::string log;
        const bool displayPassed = EnhancedSceneRenderer::RunLiveDisplayRegression(
            expectedWidth, expectedHeight, log);
        const bool passed = backendMatch && displayPassed;
        std::printf("[render.livecheck] backend configured=%s scene=%s imgui=%s — %s\n",
            RenderBackendName(configured),
            EnhancedLiveBackend::Vulkan == scene ? "vulkan" : "dx12",
            GetImGuiHost().GetBackendName(), backendMatch ? "일치" : "불일치");
        std::printf("%s", log.c_str());
        std::printf("[CLI] render.livecheck %s\n", passed ? "통과" : "실패");
        Debug::PrintLog(spdlog::level::warn, std::string("[render.livecheck] ") +
            (passed ? "통과\n" : "실패\n") + log);

        // LC6: 판정을 값으로 돌려준다. 위의 printf 는 그대로 둔다 —
        // 기존 하네스가 stdout 을 읽고 있고, 그 이주까지 같은 변경에 넣으면
        // 무엇이 깨졌는지 가를 수 없게 된다.
        CommandCore::CommandData data = CommandCore::CommandData::Object();
        data.Set("log", CommandCore::CommandData::String(log));
        data.Set("passed", CommandCore::CommandData::Bool(passed));
        if (!passed)
        {
            return CommandCore::Fail("rendertest.failed", "render.livecheck 실패", std::move(data));
        }
        return CommandCore::Ok("render.livecheck 통과", std::move(data));
    }





    static CommandCore::CommandResult Cmd_dx12_scene(const ConsoleCommandContext& ctx)
    {
        // 씬 연결 검증(PHASE 3-6). 활성 씬의 카메라와 프록시를 DX12로 그린다.
        DX12Test::SceneBindingReport report;
        std::string log;
        const bool passed = DX12Test::RunSceneBindingTest(log, &report);
        const std::string verdict = passed ? "통과" : "실패";

        std::printf("%s", log.c_str());
        Debug::PrintLog(spdlog::level::warn, "[dx12.scene] " + verdict + "\n" + log);
        std::printf("[CLI] dx12.scene %s\n", verdict.c_str());

        // LC6: 판정을 값으로 돌려준다. 위의 printf 는 그대로 둔다 —
        // 기존 하네스가 stdout 을 읽고 있고, 그 이주까지 같은 변경에 넣으면
        // 무엇이 깨졌는지 가를 수 없게 된다.
        CommandCore::CommandData data = CommandCore::CommandData::Object();
        data.Set("log", CommandCore::CommandData::String(log));
        data.Set("drawCandidates", CommandCore::CommandData::Int(report.drawCandidates));
        data.Set("lights", CommandCore::CommandData::Int(report.lights));
        data.Set("draws", CommandCore::CommandData::Int(report.draws));
        data.Set("meshUploads", CommandCore::CommandData::Int(report.meshUploads));
        data.Set("generationUploads", CommandCore::CommandData::Int(report.generationUploads));
        data.Set("uploadKB", CommandCore::CommandData::Int(report.uploadKB));
        data.Set("coverage", CommandCore::CommandData::Int(report.coverage));
        data.Set("pixels", CommandCore::CommandData::Int(report.pixels));
        data.Set("texturedDraws", CommandCore::CommandData::Int(report.texturedDraws));
        data.Set("passed", CommandCore::CommandData::Bool(passed));
        if (!passed)
        {
            return CommandCore::Fail("rendertest.failed", "dx12.scene 실패", std::move(data));
        }
        return CommandCore::Ok("dx12.scene 통과", std::move(data));
    }

    static CommandCore::CommandResult Cmd_dx12_gbuffer(const ConsoleCommandContext& ctx)
    {
        // GBuffer 패스 검증(PHASE 3-6).
        std::string log;
        const bool passed = DX12Test::RunGBufferTest(log);
        const std::string verdict = passed ? "통과" : "실패";

        std::printf("%s", log.c_str());
        Debug::PrintLog(spdlog::level::warn, "[dx12.gbuffer] " + verdict + "\n" + log);
        std::printf("[CLI] dx12.gbuffer %s\n", verdict.c_str());

        // LC6: 판정을 값으로 돌려준다. 위의 printf 는 그대로 둔다 —
        // 기존 하네스가 stdout 을 읽고 있고, 그 이주까지 같은 변경에 넣으면
        // 무엇이 깨졌는지 가를 수 없게 된다.
        CommandCore::CommandData data = CommandCore::CommandData::Object();
        data.Set("log", CommandCore::CommandData::String(log));
        data.Set("passed", CommandCore::CommandData::Bool(passed));
        if (!passed)
        {
            return CommandCore::Fail("rendertest.failed", "dx12.gbuffer 실패", std::move(data));
        }
        return CommandCore::Ok("dx12.gbuffer 통과", std::move(data));
    }

    static CommandCore::CommandResult Cmd_render_pbr_parity(const ConsoleCommandContext& ctx)
    {
        if (ctx.parts.size() != 1) return CommandCore::InvalidArguments("This PBR verification accepts no arguments");
        std::string result;
        const bool passed = RunPbrShaderParityTest(result);
        Debug::PrintLog(spdlog::level::warn, std::string("[render.pbr.parity] ") + result);
        std::printf("%s[CLI] render.pbr.parity %s\n", result.c_str(), passed ? "PASS" : "FAIL");
        auto data = CommandCore::CommandData::Object();
        data.Set("passed", CommandCore::CommandData::Bool(passed));
        data.Set("log", CommandCore::CommandData::String(result));
        return passed ? CommandCore::Ok({}, std::move(data))
            : CommandCore::Fail("render.pbr.parity.failed", "PBR verification failed", std::move(data));
    }

    static CommandCore::CommandResult Cmd_render_pbr_sealstatus(const ConsoleCommandContext& ctx)
    {
        if (ctx.parts.size() != 1) return CommandCore::InvalidArguments("This command accepts no arguments");
        const auto seal = EnhancedSceneRenderer::GetLiveSealDiagnostics();
        auto data = CommandCore::CommandData::Object();
        data.Set("enabled", CommandCore::CommandData::Bool(seal.enabled));
        const auto count = [](auto value)
        {
            return CommandCore::CommandData::Int(static_cast<int64_t>(value));
        };
        data.Set("framesRendered", count(seal.framesRendered));
        data.Set("frameId", count(seal.frameId));
        data.Set("encoderDrops", count(seal.encoderDrops));
        data.Set("drawCount", count(seal.lastDrawCount));
        data.Set("batchCount", count(seal.lastBatchCount));
        data.Set("gbufferStamped", count(seal.gbufferStamped));
        data.Set("gbufferUnstamped", count(seal.gbufferUnstamped));
        data.Set("gbufferViolations", count(seal.gbufferViolations));
        data.Set("forwardStamped", count(seal.forwardStamped));
        data.Set("forwardUnstamped", count(seal.forwardUnstamped));
        data.Set("forwardViolations", count(seal.forwardViolations));
        data.Set("textureUploadFailures", count(seal.textureUploadFailures));
        data.Set("modelGenerationPairs", count(seal.modelGenerationPairs));
        data.Set("mixedGenerationModels", count(seal.mixedGenerationModels));
        data.Set("mixedNewestGeneration", count(seal.mixedNewestGeneration));
        // ★ 여기서 판정하지 않는다. 이 명령은 재는 자이고, 판정은 장시간
        //   검사가 표본 전체를 보고 한다 — 한 표본만 보고 실패로 끝내면
        //   나머지 표본을 못 모은다.
        return CommandCore::Ok({}, std::move(data));
    }

    static CommandCore::CommandResult Cmd_render_pbr_compare(const ConsoleCommandContext& ctx)
    {
        if (ctx.parts.size() < 3 || ctx.parts.size() > 4)
        {
            return CommandCore::InvalidArguments(
                "render.pbr.compare <left-capture-dir> <right-capture-dir> [output-json]");
        }
        const std::string output = ctx.parts.size() == 4 ? ctx.parts[3] : std::string{};
        std::string result;
        const bool passed = RunPbrCaptureCompare(ctx.parts[1], ctx.parts[2], output, result);
        Debug::PrintLog(spdlog::level::warn, std::string("[render.pbr.compare] ") + result);
        std::printf("%s[CLI] render.pbr.compare %s\n", result.c_str(), passed ? "PASS" : "FAIL");
        auto data = CommandCore::CommandData::Object();
        data.Set("passed", CommandCore::CommandData::Bool(passed));
        data.Set("log", CommandCore::CommandData::String(result));
        return passed ? CommandCore::Ok({}, std::move(data))
            : CommandCore::Fail("render.pbr.compare.failed",
                "PBR capture comparison failed", std::move(data));
    }

    static CommandCore::CommandResult Cmd_render_pbr_normalpair(const ConsoleCommandContext& ctx)
    {
        if (ctx.parts.size() != 2)
        {
            return CommandCore::InvalidArguments("render.pbr.normalpair <capture-dir>");
        }
        std::string result;
        const bool passed = RunPbrNormalPairVerification(ctx.parts[1], result);
        Debug::PrintLog(spdlog::level::warn, std::string("[render.pbr.normalpair] ") + result);
        std::printf("%s[CLI] render.pbr.normalpair %s\n", result.c_str(), passed ? "PASS" : "FAIL");
        auto data = CommandCore::CommandData::Object();
        data.Set("passed", CommandCore::CommandData::Bool(passed));
        data.Set("log", CommandCore::CommandData::String(result));
        return passed ? CommandCore::Ok({}, std::move(data))
            : CommandCore::Fail("render.pbr.normalpair.failed",
                "PBR normal-map pair verification failed", std::move(data));
    }

    static CommandCore::CommandResult Cmd_render_pbr_seal(const ConsoleCommandContext& ctx)
    {
        if (ctx.parts.size() != 1) return CommandCore::InvalidArguments("This PBR verification accepts no arguments");
        std::string result;
        const bool passed = RunPbrSealTest(result);
        Debug::PrintLog(spdlog::level::warn, std::string("[render.pbr.seal] ") + result);
        std::printf("%s[CLI] render.pbr.seal %s\n", result.c_str(), passed ? "PASS" : "FAIL");
        auto data = CommandCore::CommandData::Object();
        data.Set("passed", CommandCore::CommandData::Bool(passed));
        data.Set("log", CommandCore::CommandData::String(result));
        return passed ? CommandCore::Ok({}, std::move(data))
            : CommandCore::Fail("render.pbr.seal.failed", "PBR seal verification failed", std::move(data));
    }

    static CommandCore::CommandResult Cmd_render_pbr_coverage(const ConsoleCommandContext& ctx)
    {
        if (ctx.parts.size() != 1) return CommandCore::InvalidArguments("This PBR verification accepts no arguments");
        std::string result;
        const bool passed = RunPbrCoverageTest(result);
        Debug::PrintLog(spdlog::level::warn, std::string("[render.pbr.coverage] ") + result);
        std::printf("%s[CLI] render.pbr.coverage %s\n", result.c_str(), passed ? "PASS" : "FAIL");
        auto data = CommandCore::CommandData::Object();
        data.Set("passed", CommandCore::CommandData::Bool(passed));
        data.Set("log", CommandCore::CommandData::String(result));
        return passed ? CommandCore::Ok({}, std::move(data))
            : CommandCore::Fail("render.pbr.coverage.failed", "PBR verification failed", std::move(data));
    }

    static CommandCore::CommandResult Cmd_render_pbr_occlusion(const ConsoleCommandContext& ctx)
    {
        if (ctx.parts.size() != 1) return CommandCore::InvalidArguments("This PBR verification accepts no arguments");
        std::string result;
        const bool passed = RunPbrOcclusionTest(result);
        Debug::PrintLog(spdlog::level::warn, std::string("[render.pbr.occlusion] ") + result);
        std::printf("%s[CLI] render.pbr.occlusion %s\n", result.c_str(), passed ? "PASS" : "FAIL");
        auto data = CommandCore::CommandData::Object();
        data.Set("passed", CommandCore::CommandData::Bool(passed));
        data.Set("log", CommandCore::CommandData::String(result));
        return passed ? CommandCore::Ok({}, std::move(data))
            : CommandCore::Fail("render.pbr.occlusion.failed", "PBR verification failed", std::move(data));
    }

    static CommandCore::CommandResult Cmd_render_pbr_emission(const ConsoleCommandContext& ctx)
    {
        if (ctx.parts.size() != 1) return CommandCore::InvalidArguments("This PBR verification accepts no arguments");
        std::string result;
        const bool passed = RunPbrEmissionTest(result);
        Debug::PrintLog(spdlog::level::warn, std::string("[render.pbr.emission] ") + result);
        std::printf("%s[CLI] render.pbr.emission %s\n", result.c_str(), passed ? "PASS" : "FAIL");
        auto data = CommandCore::CommandData::Object();
        data.Set("passed", CommandCore::CommandData::Bool(passed));
        data.Set("log", CommandCore::CommandData::String(result));
        return passed ? CommandCore::Ok({}, std::move(data))
            : CommandCore::Fail("render.pbr.emission.failed", "PBR verification failed", std::move(data));
    }

    static CommandCore::CommandResult Cmd_render_pbr_transform(const ConsoleCommandContext& ctx)
    {
        if (ctx.parts.size() != 1) return CommandCore::InvalidArguments("This PBR verification accepts no arguments");
        std::string result;
        const bool passed = RunPbrTransformTest(result);
        Debug::PrintLog(spdlog::level::warn, std::string("[render.pbr.transform] ") + result);
        std::printf("%s[CLI] render.pbr.transform %s\n", result.c_str(), passed ? "PASS" : "FAIL");
        auto data = CommandCore::CommandData::Object();
        data.Set("passed", CommandCore::CommandData::Bool(passed));
        data.Set("log", CommandCore::CommandData::String(result));
        return passed ? CommandCore::Ok({}, std::move(data))
            : CommandCore::Fail("render.pbr.transform.failed", "PBR verification failed", std::move(data));
    }

    static CommandCore::CommandResult Cmd_render_pbr_uv(const ConsoleCommandContext& ctx)
    {
        if (ctx.parts.size() != 1) return CommandCore::InvalidArguments("This PBR verification accepts no arguments");
        std::string result;
        const bool passed = RunPbrUvTest(result);
        Debug::PrintLog(spdlog::level::warn, std::string("[render.pbr.uv] ") + result);
        std::printf("%s[CLI] render.pbr.uv %s\n", result.c_str(), passed ? "PASS" : "FAIL");
        auto data = CommandCore::CommandData::Object();
        data.Set("passed", CommandCore::CommandData::Bool(passed));
        data.Set("log", CommandCore::CommandData::String(result));
        return passed ? CommandCore::Ok({}, std::move(data))
            : CommandCore::Fail("render.pbr.uv.failed", "PBR verification failed", std::move(data));
    }

    static CommandCore::CommandResult Cmd_render_pbr_mip(const ConsoleCommandContext& ctx)
    {
        if (ctx.parts.size() != 1) return CommandCore::InvalidArguments("This PBR verification accepts no arguments");
        std::string result;
        const bool passed = RunPbrMipTest(result);
        Debug::PrintLog(spdlog::level::warn, std::string("[render.pbr.mip] ") + result);
        std::printf("%s[CLI] render.pbr.mip %s\n", result.c_str(), passed ? "PASS" : "FAIL");
        auto data = CommandCore::CommandData::Object();
        data.Set("passed", CommandCore::CommandData::Bool(passed));
        data.Set("log", CommandCore::CommandData::String(result));
        return passed ? CommandCore::Ok({}, std::move(data))
            : CommandCore::Fail("render.pbr.mip.failed", "PBR verification failed", std::move(data));
    }

    static CommandCore::CommandResult Cmd_render_pbr_capture(const ConsoleCommandContext& ctx)
    {
        using namespace CommandCore;
        if (ctx.parts.size() < 2 || ctx.parts.size() > 4
            || (ctx.parts.size() >= 3 && ctx.parts[2] != "game" && ctx.parts[2] != "editor")
            || (ctx.parts.size() == 4 && ctx.parts[3] != "controlled"))
            return InvalidArguments("render.pbr.capture <new-absolute-directory> [game|editor] [controlled]");
        std::string error;
        const auto target = ctx.parts.size() >= 3 && ctx.parts[2] == "editor"
            ? EnhancedLiveDisplayTarget::Editor : EnhancedLiveDisplayTarget::Game;
        if (!EnhancedSceneRenderer::RequestLivePbrCapture(ctx.parts[1], target, error,
                ctx.parts.size() == 4))
            return Fail("render.pbr.capture.rejected", error);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
        ctx.system.WaitForResult([deadline]() -> std::optional<CommandResult>
        {
            auto capture = EnhancedSceneRenderer::GetLivePbrCaptureStatus();
            if (capture.state == EnhancedPbrCaptureState::Pending && std::chrono::steady_clock::now() >= deadline)
            {
                EnhancedSceneRenderer::CancelLivePbrCapture();
                return CommandResult{ CommandStatus::TimedOut, "render.pbr.capture.timeout", "Product frame capture timed out" };
            }
            if (capture.state != EnhancedPbrCaptureState::Complete && capture.state != EnhancedPbrCaptureState::Failed)
                return std::nullopt;
            const bool passed = capture.state == EnhancedPbrCaptureState::Complete;
            std::printf("[CLI] render.pbr.capture %s frame=%llu path=%s error=%s\n",
                passed ? "PASS" : "FAIL", static_cast<unsigned long long>(capture.frameId),
                capture.directory.c_str(), capture.error.c_str());
            auto data = CommandData::Object();
            data.Set("frameId", CommandData::Int(capture.frameId));
            data.Set("directory", CommandData::String(capture.directory));
            return passed ? Ok({}, std::move(data)) : Fail("render.pbr.capture.failed", capture.error, std::move(data));
        });
        return Ok();
    }


    // ── 은퇴한 렌더 게이트 18종을 되살린다 (2026-09-14) ────────────────────
    //
    // 이 핸들러들은 `0ec60573`(2026-09-12, "닫힌·완료 계획 소속 Commandlet 68개
    // 은퇴")이 지운 것이다. 그 판정은 둘을 함께 요구했다 — ① 소속 계획이
    // 보관함에 있거나 폐쇄를 선언했고, ② **활성 계획 문서가 그 이름을 더 이상
    // 부르지 않는다.**
    //
    // ★ 되살리는 근거는 ②가 다시 참이 아니게 되었다는 것뿐이다. 셰이더 로직
    //   최적화는 픽셀로 판정하지 않으면 할 수 없는 작업이고, 그 픽셀을 재는
    //   자가 바로 이 열여덟이다. ①은 그대로다 — 계획은 여전히 닫혀 있다.
    //   그러니 이 되살림은 "은퇴가 틀렸다"가 아니라 "부르는 자가 새로 생겼다"
    //   이며, 셰이더 최적화가 끝나 부르는 자가 다시 없어지면 같은 기준으로
    //   다시 은퇴시키는 것이 맞다.
    //
    // ★ 본문은 한 글자도 새로 쓰지 않았다. `0ec60573^` 의 것을 그대로 옮겼다 —
    //   새로 쓰면 그것이 은퇴 전 검사와 같은 것인지 아무도 보증할 수 없고,
    //   그 순간 "되살린 게이트가 붉다"와 "내가 잘못 옮겼다"를 가를 수 없게 된다.
    //
    // 되살리지 않은 것: `rhi.uploadsegments`. 그것은 DX12/Vulkan 을 함께 부르는
    // 교차 백엔드 명령이고 인자 둘(<model-path> <texture-path>)을 요구해서
    // 인자 없이 도는 dx12 스윕의 일원이 될 수 없다. 셰이더 게이트도 아니다.

    static CommandCore::CommandResult Cmd_dx12_ssao(const ConsoleCommandContext& ctx)
    {
        std::string log;
        const bool passed = DX12Test::RunSSAOTest(log);
        const std::string verdict = passed ? "통과" : "실패";

        std::printf("%s", log.c_str());
        Debug::PrintLog(spdlog::level::warn, "[dx12.ssao] " + verdict + "\n" + log);
        std::printf("[CLI] dx12.ssao %s\n", verdict.c_str());

        // LC6: 판정을 값으로 돌려준다. 위의 printf 는 그대로 둔다 —
        // 기존 하네스가 stdout 을 읽고 있고, 그 이주까지 같은 변경에 넣으면
        // 무엇이 깨졌는지 가를 수 없게 된다.
        CommandCore::CommandData data = CommandCore::CommandData::Object();
        data.Set("log", CommandCore::CommandData::String(log));
        data.Set("passed", CommandCore::CommandData::Bool(passed));
        if (!passed)
        {
            return CommandCore::Fail("rendertest.failed", "dx12.ssao 실패", std::move(data));
        }
        return CommandCore::Ok("dx12.ssao 통과", std::move(data));
    }

    static CommandCore::CommandResult Cmd_dx12_ssgi(const ConsoleCommandContext& ctx)
    {
        std::string log;
        const bool passed = DX12Test::RunSSGITest(log);
        const std::string verdict = passed ? "통과" : "실패";

        std::printf("%s", log.c_str());
        Debug::PrintLog(spdlog::level::warn, "[dx12.ssgi] " + verdict + "\n" + log);
        std::printf("[CLI] dx12.ssgi %s\n", verdict.c_str());

        // LC6: 판정을 값으로 돌려준다. 위의 printf 는 그대로 둔다 —
        // 기존 하네스가 stdout 을 읽고 있고, 그 이주까지 같은 변경에 넣으면
        // 무엇이 깨졌는지 가를 수 없게 된다.
        CommandCore::CommandData data = CommandCore::CommandData::Object();
        data.Set("log", CommandCore::CommandData::String(log));
        data.Set("passed", CommandCore::CommandData::Bool(passed));
        if (!passed)
        {
            return CommandCore::Fail("rendertest.failed", "dx12.ssgi 실패", std::move(data));
        }
        return CommandCore::Ok("dx12.ssgi 통과", std::move(data));
    }

    static CommandCore::CommandResult Cmd_dx12_ssr(const ConsoleCommandContext& ctx)
    {
        // SSR 패스 검증(PHASE 3-6, 미구현 패스 이식 3차).
        std::string log;
        const bool passed = DX12Test::RunSSRTest(log);
        const std::string verdict = passed ? "통과" : "실패";

        std::printf("%s", log.c_str());
        Debug::PrintLog(spdlog::level::warn, "[dx12.ssr] " + verdict + "\n" + log);
        std::printf("[CLI] dx12.ssr %s\n", verdict.c_str());

        // LC6: 판정을 값으로 돌려준다. 위의 printf 는 그대로 둔다 —
        // 기존 하네스가 stdout 을 읽고 있고, 그 이주까지 같은 변경에 넣으면
        // 무엇이 깨졌는지 가를 수 없게 된다.
        CommandCore::CommandData data = CommandCore::CommandData::Object();
        data.Set("log", CommandCore::CommandData::String(log));
        data.Set("passed", CommandCore::CommandData::Bool(passed));
        if (!passed)
        {
            return CommandCore::Fail("rendertest.failed", "dx12.ssr 실패", std::move(data));
        }
        return CommandCore::Ok("dx12.ssr 통과", std::move(data));
    }

    static CommandCore::CommandResult Cmd_dx12_sss(const ConsoleCommandContext& ctx)
    {
        // SSS 패스 검증(PHASE 3-6, 미구현 패스 이식 1차).
        std::string log;
        const bool passed = DX12Test::RunSSSTest(log);
        const std::string verdict = passed ? "통과" : "실패";

        std::printf("%s", log.c_str());
        Debug::PrintLog(spdlog::level::warn, "[dx12.sss] " + verdict + "\n" + log);
        std::printf("[CLI] dx12.sss %s\n", verdict.c_str());

        // LC6: 판정을 값으로 돌려준다. 위의 printf 는 그대로 둔다 —
        // 기존 하네스가 stdout 을 읽고 있고, 그 이주까지 같은 변경에 넣으면
        // 무엇이 깨졌는지 가를 수 없게 된다.
        CommandCore::CommandData data = CommandCore::CommandData::Object();
        data.Set("log", CommandCore::CommandData::String(log));
        data.Set("passed", CommandCore::CommandData::Bool(passed));
        if (!passed)
        {
            return CommandCore::Fail("rendertest.failed", "dx12.sss 실패", std::move(data));
        }
        return CommandCore::Ok("dx12.sss 통과", std::move(data));
    }

    static CommandCore::CommandResult Cmd_dx12_fog(const ConsoleCommandContext& ctx)
    {
        // 볼류메트릭 포그 패스 검증(PHASE 3-6, 미구현 패스 이식 4차).
        std::string log;
        const bool passed = DX12Test::RunVolumetricFogTest(log);
        const std::string verdict = passed ? "통과" : "실패";

        std::printf("%s", log.c_str());
        Debug::PrintLog(spdlog::level::warn, "[dx12.fog] " + verdict + "\n" + log);
        std::printf("[CLI] dx12.fog %s\n", verdict.c_str());

        // LC6: 판정을 값으로 돌려준다. 위의 printf 는 그대로 둔다 —
        // 기존 하네스가 stdout 을 읽고 있고, 그 이주까지 같은 변경에 넣으면
        // 무엇이 깨졌는지 가를 수 없게 된다.
        CommandCore::CommandData data = CommandCore::CommandData::Object();
        data.Set("log", CommandCore::CommandData::String(log));
        data.Set("passed", CommandCore::CommandData::Bool(passed));
        if (!passed)
        {
            return CommandCore::Fail("rendertest.failed", "dx12.fog 실패", std::move(data));
        }
        return CommandCore::Ok("dx12.fog 통과", std::move(data));
    }

    static CommandCore::CommandResult Cmd_dx12_skybox(const ConsoleCommandContext& ctx)
    {
        // 스카이박스 패스 검증(PHASE 3-6).
        std::string log;
        const bool passed = DX12Test::RunSkyBoxTest(log);
        const std::string verdict = passed ? "통과" : "실패";

        std::printf("%s", log.c_str());
        Debug::PrintLog(spdlog::level::warn, "[dx12.skybox] " + verdict + "\n" + log);
        std::printf("[CLI] dx12.skybox %s\n", verdict.c_str());

        // LC6: 판정을 값으로 돌려준다. 위의 printf 는 그대로 둔다 —
        // 기존 하네스가 stdout 을 읽고 있고, 그 이주까지 같은 변경에 넣으면
        // 무엇이 깨졌는지 가를 수 없게 된다.
        CommandCore::CommandData data = CommandCore::CommandData::Object();
        data.Set("log", CommandCore::CommandData::String(log));
        data.Set("passed", CommandCore::CommandData::Bool(passed));
        if (!passed)
        {
            return CommandCore::Fail("rendertest.failed", "dx12.skybox 실패", std::move(data));
        }
        return CommandCore::Ok("dx12.skybox 통과", std::move(data));
    }

    static CommandCore::CommandResult Cmd_dx12_grid(const ConsoleCommandContext& ctx)
    {
        // 그리드 패스 검증(PHASE 3-6, Gizmo 계열 첫 슬라이스).
        std::string log;
        const bool passed = DX12Test::RunGridTest(log);
        const std::string verdict = passed ? "통과" : "실패";

        std::printf("%s", log.c_str());
        Debug::PrintLog(spdlog::level::warn, "[dx12.grid] " + verdict + "\n" + log);
        std::printf("[CLI] dx12.grid %s\n", verdict.c_str());

        // LC6: 판정을 값으로 돌려준다. 위의 printf 는 그대로 둔다 —
        // 기존 하네스가 stdout 을 읽고 있고, 그 이주까지 같은 변경에 넣으면
        // 무엇이 깨졌는지 가를 수 없게 된다.
        CommandCore::CommandData data = CommandCore::CommandData::Object();
        data.Set("log", CommandCore::CommandData::String(log));
        data.Set("passed", CommandCore::CommandData::Bool(passed));
        if (!passed)
        {
            return CommandCore::Fail("rendertest.failed", "dx12.grid 실패", std::move(data));
        }
        return CommandCore::Ok("dx12.grid 통과", std::move(data));
    }

    static CommandCore::CommandResult Cmd_dx12_wireframe(const ConsoleCommandContext& ctx)
    {
        // 와이어프레임 패스 검증(PHASE 3-6, Gizmo 계열 4차 슬라이스).
        std::string log;
        const bool passed = DX12Test::RunWireFrameTest(log);
        const std::string verdict = passed ? "통과" : "실패";

        std::printf("%s", log.c_str());
        Debug::PrintLog(spdlog::level::warn, "[dx12.wireframe] " + verdict + "\n" + log);
        std::printf("[CLI] dx12.wireframe %s\n", verdict.c_str());

        // LC6: 판정을 값으로 돌려준다. 위의 printf 는 그대로 둔다 —
        // 기존 하네스가 stdout 을 읽고 있고, 그 이주까지 같은 변경에 넣으면
        // 무엇이 깨졌는지 가를 수 없게 된다.
        CommandCore::CommandData data = CommandCore::CommandData::Object();
        data.Set("log", CommandCore::CommandData::String(log));
        data.Set("passed", CommandCore::CommandData::Bool(passed));
        if (!passed)
        {
            return CommandCore::Fail("rendertest.failed", "dx12.wireframe 실패", std::move(data));
        }
        return CommandCore::Ok("dx12.wireframe 통과", std::move(data));
    }

    static CommandCore::CommandResult Cmd_dx12_gizmoicon(const ConsoleCommandContext& ctx)
    {
        // 기즈모 아이콘 패스 검증(PHASE 3-6, Gizmo 계열 3차 슬라이스).
        std::string log;
        const bool passed = DX12Test::RunGizmoIconTest(log);
        const std::string verdict = passed ? "통과" : "실패";

        std::printf("%s", log.c_str());
        Debug::PrintLog(spdlog::level::warn, "[dx12.gizmoicon] " + verdict + "\n" + log);
        std::printf("[CLI] dx12.gizmoicon %s\n", verdict.c_str());

        // LC6: 판정을 값으로 돌려준다. 위의 printf 는 그대로 둔다 —
        // 기존 하네스가 stdout 을 읽고 있고, 그 이주까지 같은 변경에 넣으면
        // 무엇이 깨졌는지 가를 수 없게 된다.
        CommandCore::CommandData data = CommandCore::CommandData::Object();
        data.Set("log", CommandCore::CommandData::String(log));
        data.Set("passed", CommandCore::CommandData::Bool(passed));
        if (!passed)
        {
            return CommandCore::Fail("rendertest.failed", "dx12.gizmoicon 실패", std::move(data));
        }
        return CommandCore::Ok("dx12.gizmoicon 통과", std::move(data));
    }

    static CommandCore::CommandResult Cmd_dx12_gizmoline(const ConsoleCommandContext& ctx)
    {
        // 기즈모 라인 패스 검증(PHASE 3-6, Gizmo 계열 2차 슬라이스).
        std::string log;
        const bool passed = DX12Test::RunGizmoLineTest(log);
        const std::string verdict = passed ? "통과" : "실패";

        std::printf("%s", log.c_str());
        Debug::PrintLog(spdlog::level::warn, "[dx12.gizmoline] " + verdict + "\n" + log);
        std::printf("[CLI] dx12.gizmoline %s\n", verdict.c_str());

        // LC6: 판정을 값으로 돌려준다. 위의 printf 는 그대로 둔다 —
        // 기존 하네스가 stdout 을 읽고 있고, 그 이주까지 같은 변경에 넣으면
        // 무엇이 깨졌는지 가를 수 없게 된다.
        CommandCore::CommandData data = CommandCore::CommandData::Object();
        data.Set("log", CommandCore::CommandData::String(log));
        data.Set("passed", CommandCore::CommandData::Bool(passed));
        if (!passed)
        {
            return CommandCore::Fail("rendertest.failed", "dx12.gizmoline 실패", std::move(data));
        }
        return CommandCore::Ok("dx12.gizmoline 통과", std::move(data));
    }

    static CommandCore::CommandResult Cmd_dx12_decal(const ConsoleCommandContext& ctx)
    {
        // 데칼 패스 검증(PHASE 3-6, 미구현 패스 이식 2차).
        std::string log;
        const bool passed = DX12Test::RunDecalTest(log);
        const std::string verdict = passed ? "통과" : "실패";

        std::printf("%s", log.c_str());
        Debug::PrintLog(spdlog::level::warn, "[dx12.decal] " + verdict + "\n" + log);
        std::printf("[CLI] dx12.decal %s\n", verdict.c_str());

        // LC6: 판정을 값으로 돌려준다. 위의 printf 는 그대로 둔다 —
        // 기존 하네스가 stdout 을 읽고 있고, 그 이주까지 같은 변경에 넣으면
        // 무엇이 깨졌는지 가를 수 없게 된다.
        CommandCore::CommandData data = CommandCore::CommandData::Object();
        data.Set("log", CommandCore::CommandData::String(log));
        data.Set("passed", CommandCore::CommandData::Bool(passed));
        if (!passed)
        {
            return CommandCore::Fail("rendertest.failed", "dx12.decal 실패", std::move(data));
        }
        return CommandCore::Ok("dx12.decal 통과", std::move(data));
    }

    static CommandCore::CommandResult Cmd_dx12_ibl(const ConsoleCommandContext& ctx)
    {
        // IBL 생성 체인 검증(PHASE 3-6).
        std::string log;
        const bool passed = DX12Test::RunIBLTest(log);
        const std::string verdict = passed ? "통과" : "실패";

        std::printf("%s", log.c_str());
        Debug::PrintLog(spdlog::level::warn, "[dx12.ibl] " + verdict + "\n" + log);
        std::printf("[CLI] dx12.ibl %s\n", verdict.c_str());

        // LC6: 판정을 값으로 돌려준다. 위의 printf 는 그대로 둔다 —
        // 기존 하네스가 stdout 을 읽고 있고, 그 이주까지 같은 변경에 넣으면
        // 무엇이 깨졌는지 가를 수 없게 된다.
        CommandCore::CommandData data = CommandCore::CommandData::Object();
        data.Set("log", CommandCore::CommandData::String(log));
        data.Set("passed", CommandCore::CommandData::Bool(passed));
        if (!passed)
        {
            return CommandCore::Fail("rendertest.failed", "dx12.ibl 실패", std::move(data));
        }
        return CommandCore::Ok("dx12.ibl 통과", std::move(data));
    }

    static CommandCore::CommandResult Cmd_dx12_iblshade(const ConsoleCommandContext& ctx)
    {
        // IBL 앰비언트 소비 검증(PHASE 3-6).
        std::string log;
        const bool passed = DX12Test::RunIBLShadeTest(log);
        const std::string verdict = passed ? "통과" : "실패";

        std::printf("%s", log.c_str());
        Debug::PrintLog(spdlog::level::warn, "[dx12.iblshade] " + verdict + "\n" + log);
        std::printf("[CLI] dx12.iblshade %s\n", verdict.c_str());

        // LC6: 판정을 값으로 돌려준다. 위의 printf 는 그대로 둔다 —
        // 기존 하네스가 stdout 을 읽고 있고, 그 이주까지 같은 변경에 넣으면
        // 무엇이 깨졌는지 가를 수 없게 된다.
        CommandCore::CommandData data = CommandCore::CommandData::Object();
        data.Set("log", CommandCore::CommandData::String(log));
        data.Set("passed", CommandCore::CommandData::Bool(passed));
        if (!passed)
        {
            return CommandCore::Fail("rendertest.failed", "dx12.iblshade 실패", std::move(data));
        }
        return CommandCore::Ok("dx12.iblshade 통과", std::move(data));
    }

    static CommandCore::CommandResult Cmd_dx12_rendergraph(const ConsoleCommandContext& ctx)
    {
        // 렌더 그래프 자가 검증(PHASE 3-5).
        std::string log;
        const bool passed = DX12Test::RunRenderGraphTest(log);

        std::printf("%s", log.c_str());
        Debug::PrintLog(spdlog::level::warn, std::string("[dx12.rendergraph] ") + (passed ? "통과" : "실패") + "\n" + log);
        std::printf("[CLI] dx12.rendergraph %s\n", passed ? "통과" : "실패");

        // LC6: 판정을 값으로 돌려준다. 위의 printf 는 그대로 둔다 —
        // 기존 하네스가 stdout 을 읽고 있고, 그 이주까지 같은 변경에 넣으면
        // 무엇이 깨졌는지 가를 수 없게 된다.
        CommandCore::CommandData data = CommandCore::CommandData::Object();
        data.Set("log", CommandCore::CommandData::String(log));
        data.Set("passed", CommandCore::CommandData::Bool(passed));
        if (!passed)
        {
            return CommandCore::Fail("rendertest.failed", "dx12.rendergraph 실패", std::move(data));
        }
        return CommandCore::Ok("dx12.rendergraph 통과", std::move(data));
    }

    static CommandCore::CommandResult Cmd_vk_parallel(const ConsoleCommandContext&)
    {
        std::string log;
        const bool passed = RunVulkanParallelRecordingTest(log);
        std::printf("%s", log.c_str());
        auto data = CommandCore::CommandData::Object();
        data.Set("log", CommandCore::CommandData::String(std::move(log)));
        data.Set("passed", CommandCore::CommandData::Bool(passed));
        return passed ? CommandCore::Ok("vk.parallel 통과", std::move(data))
            : CommandCore::Fail("rendertest.failed", "vk.parallel 실패", std::move(data));
    }

    static CommandCore::CommandResult Cmd_dx12_parallel(const ConsoleCommandContext& ctx)
    {
        // 커맨드 기록 병렬화 검증(PHASE 3-6).
        std::string log;
        const bool passed = DX12Test::RunParallelRecordTest(log);
        const std::string verdict = passed ? "통과" : "실패";

        std::printf("%s", log.c_str());
        Debug::PrintLog(spdlog::level::warn, "[dx12.parallel] " + verdict + "\n" + log);
        std::printf("[CLI] dx12.parallel %s\n", verdict.c_str());

        // LC6: 판정을 값으로 돌려준다. 위의 printf 는 그대로 둔다 —
        // 기존 하네스가 stdout 을 읽고 있고, 그 이주까지 같은 변경에 넣으면
        // 무엇이 깨졌는지 가를 수 없게 된다.
        CommandCore::CommandData data = CommandCore::CommandData::Object();
        data.Set("log", CommandCore::CommandData::String(log));
        data.Set("passed", CommandCore::CommandData::Bool(passed));
        if (!passed)
        {
            return CommandCore::Fail("rendertest.failed", "dx12.parallel 실패", std::move(data));
        }
        return CommandCore::Ok("dx12.parallel 통과", std::move(data));
    }

    static CommandCore::CommandResult Cmd_dx12_descriptorheap(const ConsoleCommandContext& ctx)
    {
        // completion 기반 descriptor page recycler·샘플러 힙 자가 검증.
        std::string log;
        const bool passed = DX12Test::RunDescriptorHeapTest(log);

        std::printf("%s", log.c_str());
        Debug::PrintLog(spdlog::level::warn, std::string("[dx12.descriptorheap] ") + (passed ? "통과" : "실패") + "\n" + log);
        std::printf("[CLI] dx12.descriptorheap %s\n", passed ? "통과" : "실패");

        // LC6: 판정을 값으로 돌려준다. 위의 printf 는 그대로 둔다 —
        // 기존 하네스가 stdout 을 읽고 있고, 그 이주까지 같은 변경에 넣으면
        // 무엇이 깨졌는지 가를 수 없게 된다.
        CommandCore::CommandData data = CommandCore::CommandData::Object();
        data.Set("log", CommandCore::CommandData::String(log));
        data.Set("passed", CommandCore::CommandData::Bool(passed));
        if (!passed)
        {
            return CommandCore::Fail("rendertest.failed", "dx12.descriptorheap 실패", std::move(data));
        }
        return CommandCore::Ok("dx12.descriptorheap 통과", std::move(data));
    }

    static CommandCore::CommandResult Cmd_dx12_resize(const ConsoleCommandContext& ctx)
    {
        // 크기 추종 검증(해상도 슬라이스).
        std::string log;
        const bool passed = DX12Test::RunScreenResizeTest(log);
        const std::string verdict = passed ? "통과" : "실패";

        std::printf("%s", log.c_str());
        Debug::PrintLog(spdlog::level::warn, "[dx12.resize] " + verdict + "\n" + log);
        std::printf("[CLI] dx12.resize %s\n", verdict.c_str());

        // LC6: 판정을 값으로 돌려준다. 위의 printf 는 그대로 둔다 —
        // 기존 하네스가 stdout 을 읽고 있고, 그 이주까지 같은 변경에 넣으면
        // 무엇이 깨졌는지 가를 수 없게 된다.
        CommandCore::CommandData data = CommandCore::CommandData::Object();
        data.Set("log", CommandCore::CommandData::String(log));
        data.Set("passed", CommandCore::CommandData::Bool(passed));
        if (!passed)
        {
            return CommandCore::Fail("rendertest.failed", "dx12.resize 실패", std::move(data));
        }
        return CommandCore::Ok("dx12.resize 통과", std::move(data));
    }

    static CommandCore::CommandResult Cmd_dx12_psocache(const ConsoleCommandContext& ctx)
    {
        const std::vector<std::string>& parts = ctx.parts;

        // PSO 캐시 자가 검증(PHASE 3-4) — 매니저를 두 번 세워 캐시가 컴파일을
        // 실제로 없애는지 확인한다.
        const std::string cachePath = ResolveTestArtifactPath("DX12/Cache",
            (parts.size() > 1) ? parts[1] : std::string("dx12_pso.cache"));

        std::string log;
        const bool passed = DX12Test::RunPsoCacheTest(cachePath, log);

        std::printf("%s", log.c_str());
        Debug::PrintLog(spdlog::level::warn, std::string("[dx12.psocache] ") + (passed ? "통과" : "실패") + "\n" + log);
        std::printf("[CLI] dx12.psocache %s\n", passed ? "통과" : "실패");

        // LC6: 판정을 값으로 돌려준다. 위의 printf 는 그대로 둔다 —
        // 기존 하네스가 stdout 을 읽고 있고, 그 이주까지 같은 변경에 넣으면
        // 무엇이 깨졌는지 가를 수 없게 된다.
        CommandCore::CommandData data = CommandCore::CommandData::Object();
        data.Set("log", CommandCore::CommandData::String(log));
        data.Set("passed", CommandCore::CommandData::Bool(passed));
        if (!passed)
        {
            return CommandCore::Fail("rendertest.failed", "dx12.psocache 실패", std::move(data));
        }
        return CommandCore::Ok("dx12.psocache 통과", std::move(data));
    }

    void RegisterRenderTestCommands(Registrar& reg)
    {
        reg.Result({ "dx12.selftest" }, &Cmd_dx12_selftest);
        reg.Result({ "vk.grid" }, &Cmd_vk_grid);
        reg.Result({ "vk.texturecodec" }, &Cmd_vk_texturecodec);
        reg.Result({ "vk.shadow" }, &Cmd_vk_shadow);
        reg.Result({ "vk.gbuffer" }, &Cmd_vk_gbuffer);
        reg.Result({ "vk.forward" }, &Cmd_vk_forward);
        reg.Result({ "vk.deferred" }, &Cmd_vk_deferred);
        reg.Result({ "render.pbr.parity" }, &Cmd_render_pbr_parity);
        reg.Result({ "render.pbr.seal" }, &Cmd_render_pbr_seal);
        reg.Result({ "render.pbr.compare" }, &Cmd_render_pbr_compare);
        reg.Result({ "render.pbr.normalpair" }, &Cmd_render_pbr_normalpair);
        reg.Result({ "render.pbr.sealstatus" }, &Cmd_render_pbr_sealstatus);
        reg.Result({ "render.pbr.coverage" }, &Cmd_render_pbr_coverage);
        reg.Result({ "render.pbr.occlusion" }, &Cmd_render_pbr_occlusion);
        reg.Result({ "render.pbr.emission" }, &Cmd_render_pbr_emission);
        reg.Result({ "render.pbr.transform" }, &Cmd_render_pbr_transform);
        reg.Result({ "render.pbr.uv" }, &Cmd_render_pbr_uv);
        reg.Result({ "render.pbr.mip" }, &Cmd_render_pbr_mip);
        reg.Result({ "render.pbr.capture" }, &Cmd_render_pbr_capture);
        reg.Result({ "vk.decal" }, &Cmd_vk_decal);
        reg.Result({ "dx12.forward" }, &Cmd_dx12_forward);
        reg.Result({ "dx12.forwardshade" }, &Cmd_dx12_forwardshade);
        reg.Result({ "dx12.post" }, &Cmd_dx12_post);
        reg.Result({ "dx12.ui" }, &Cmd_dx12_ui);
        reg.Result({ "dx12.gizmoscene" }, &Cmd_dx12_gizmoscene);
        reg.Result({ "dx12.shadowquality" }, &Cmd_dx12_shadowquality);
        reg.Result({ "dx12.skinning" }, &Cmd_dx12_skinning);
        reg.Result({ "render.livecheck" }, &Cmd_render_livecheck);
        reg.Result({ "dx12.scene" }, &Cmd_dx12_scene);
        reg.Result({ "dx12.gbuffer" }, &Cmd_dx12_gbuffer);
        reg.Result({ "dx12.ssao" }, &Cmd_dx12_ssao);
        reg.Result({ "dx12.ssgi" }, &Cmd_dx12_ssgi);
        reg.Result({ "dx12.ssr" }, &Cmd_dx12_ssr);
        reg.Result({ "dx12.sss" }, &Cmd_dx12_sss);
        reg.Result({ "dx12.fog" }, &Cmd_dx12_fog);
        reg.Result({ "dx12.skybox" }, &Cmd_dx12_skybox);
        reg.Result({ "dx12.grid" }, &Cmd_dx12_grid);
        reg.Result({ "dx12.wireframe" }, &Cmd_dx12_wireframe);
        reg.Result({ "dx12.gizmoicon" }, &Cmd_dx12_gizmoicon);
        reg.Result({ "dx12.gizmoline" }, &Cmd_dx12_gizmoline);
        reg.Result({ "dx12.decal" }, &Cmd_dx12_decal);
        reg.Result({ "dx12.ibl" }, &Cmd_dx12_ibl);
        reg.Result({ "dx12.iblshade" }, &Cmd_dx12_iblshade);
        reg.Result({ "dx12.rendergraph" }, &Cmd_dx12_rendergraph);
        reg.Result({ "dx12.parallel" }, &Cmd_dx12_parallel);
        reg.Result({ "vk.parallel" }, &Cmd_vk_parallel);
        reg.Result({ "dx12.descriptorheap" }, &Cmd_dx12_descriptorheap);
        reg.Result({ "dx12.resize" }, &Cmd_dx12_resize);
        reg.Result({ "dx12.psocache" }, &Cmd_dx12_psocache);
    }
}
