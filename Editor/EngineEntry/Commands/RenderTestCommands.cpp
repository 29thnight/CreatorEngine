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
        Debug->LogWarning(std::string("[dx12.selftest] ") + (passed ? "통과" : "실패") + "\n" + log);
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
        Debug->LogWarning(std::string("[vk.grid] ") + (passed ? "통과" : "실패") + "\n" + log);
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
        Debug->LogWarning(std::string("[vk.texturecodec] ") +
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
        Debug->LogWarning(std::string("[vk.shadow] ") +
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
        Debug->LogWarning(std::string("[vk.gbuffer] ") +
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
        Debug->LogWarning(std::string("[vk.forward] ") +
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
        Debug->LogWarning(std::string("[vk.deferred] ") +
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
        Debug->LogWarning(std::string("[vk.decal] ") +
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
        Debug->LogWarning("[dx12.forward] " + verdict + "\n" + log);
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
        Debug->LogWarning("[dx12.forwardshade] " + verdict + "\n" + log);
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
        Debug->LogWarning("[dx12.post] " + verdict + "\n" + log);
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
        Debug->LogWarning("[dx12.ui] " + verdict + "\n" + log);
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
        Debug->LogWarning("[dx12.gizmoscene] " + verdict + "\n" + log);
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
        Debug->LogWarning("[dx12.shadowquality] " + verdict + "\n" + log);
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
        Debug->LogWarning("[dx12.skinning] " + verdict + "\n" + log);
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
        Debug->LogWarning(std::string("[render.livecheck] ") +
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
        Debug->LogWarning("[dx12.scene] " + verdict + "\n" + log);
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
        Debug->LogWarning("[dx12.gbuffer] " + verdict + "\n" + log);
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
        Debug->LogWarning(std::string("[render.pbr.parity] ") + result);
        std::printf("%s[CLI] render.pbr.parity %s\n", result.c_str(), passed ? "PASS" : "FAIL");
        auto data = CommandCore::CommandData::Object();
        data.Set("passed", CommandCore::CommandData::Bool(passed));
        data.Set("log", CommandCore::CommandData::String(result));
        return passed ? CommandCore::Ok({}, std::move(data))
            : CommandCore::Fail("render.pbr.parity.failed", "PBR verification failed", std::move(data));
    }

    static CommandCore::CommandResult Cmd_render_pbr_coverage(const ConsoleCommandContext& ctx)
    {
        if (ctx.parts.size() != 1) return CommandCore::InvalidArguments("This PBR verification accepts no arguments");
        std::string result;
        const bool passed = RunPbrCoverageTest(result);
        Debug->LogWarning(std::string("[render.pbr.coverage] ") + result);
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
        Debug->LogWarning(std::string("[render.pbr.occlusion] ") + result);
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
        Debug->LogWarning(std::string("[render.pbr.emission] ") + result);
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
        Debug->LogWarning(std::string("[render.pbr.transform] ") + result);
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
        Debug->LogWarning(std::string("[render.pbr.uv] ") + result);
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
        Debug->LogWarning(std::string("[render.pbr.mip] ") + result);
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
        if (ctx.parts.size() < 2 || ctx.parts.size() > 3
            || (ctx.parts.size() == 3 && ctx.parts[2] != "game" && ctx.parts[2] != "editor"))
            return InvalidArguments("render.pbr.capture <new-absolute-directory> [game|editor]");
        std::string error;
        const auto target = ctx.parts.size() == 3 && ctx.parts[2] == "editor"
            ? EnhancedLiveDisplayTarget::Editor : EnhancedLiveDisplayTarget::Game;
        if (!EnhancedSceneRenderer::RequestLivePbrCapture(ctx.parts[1], target, error))
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
    }
}
