// LC6 (PHASE 14.5) — Core 도메인 명령.
//
// `help` · `quit` · `wait` · `commands.*` · `cli.*` · `log.*` · `lifecycle.*` ·
// `window.*` · `game.*`. 명령 계층 자신에 대한 것과 실행 수명에 관한 것.
//
// ★ `commands.list`·`describe`·`selftest` 가 여기 있다. LC3 이 연 discovery 이고,
//   소비자가 C++ 소스를 긁지 않게 하는 창구다. `commands.selftest` 는 registry
//   무결성을 **양방향**으로 본다 — 등록된 것에 descriptor 가 있는가, 그리고
//   seed 는 있는데 등록이 사라지지 않았는가. 뒤쪽이 없으면 이 슬라이스처럼
//   핸들러를 파일 일곱 개로 옮기는 작업에서 등록 한 줄이 조용히 사라진다.
//
// ★★ `wait` 는 배치 전용이다. 서비스 세션에서는 400 으로 거절된다(LC5) —
//   전역 프레임 보류는 자기 요청만 늦추는 것이 아니라 다른 요청 전부의 지연이
//   된다.
//
// ── 이 이동에서 바꾸지 않은 것 ──────────────────────────────────────────
//
// 핸들러 본문과 서명 그대로다. 여기에는 이미 결과형인 것(help·commands.*·quit·
// wait)과 legacy 인 것이 섞여 있고, 그 구분도 그대로 옮긴다(§12.3).
//
// include 는 이 TU 가 직접 소유한다(유니티에서 빠져 있다).

#include "CommandRegistrar.h"
#include "EditorWorkspaceStore.h"
#include "ViewportHostWindow.h"
#include "EditorPanelCost.h"
#include "BrowserThumbnailCache.h"
#include "EditorClipContract.h"
#include "EditorStateContract.h"    // PHASE 21 W2-1: 키보드 탐색 계약
#include "EditorNavContract.h"    // PHASE 21 W2-1: 키보드 탐색 계약
#include "EditorPlayModeController.h"
#include "EditorWindowHost.h"       // PHASE 21 M4: editor.windows 덤프
#include "EditorWindowAudit.h"
#include "EditorWindowRegistry.h"   // W7-0: editor.window 요청 창구
#include "EditorWindowSelfTest.h"
#include "EditorMenuSelfTest.h"
#include "EditorMenuAudit.h"
#include "EditorChromeSnapshot.h"   // PHASE 21 W0: editor.dock/theme/layout
#include "EditorThemeSelfTest.h"

#include <span>
#include "RegisterEditorMenuManual.h"
#include "CommandSupport.h"

#include "CommandCore/CommandSession.h" // LC1: 결과 누적과 process exit code
#include "CommandCore/CommandParser.h"
#include "CommandCore/CommandRegistry.h"       // LC3: descriptor snapshot
#include "Commands/CommandRegistrar.h"          // LC6: 도메인 TU 등록 창구
#include "CommandCore/CommandDescriptorSeeds.h"
#include "EditorCommandServiceHost.h"        // LC4: 로컬 HTTP/JSON 서비스  // LC2: 토크나이저와 소유형 invocation
#include "EditorCameraRig.h"
#include "SceneViewportOverlay.h"
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
#include <cstdlib>
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
    static CommandCore::CommandResult Cmd_help(const ConsoleCommandContext& ctx)
    {
        const CommandCore::CommandRegistry& registry = CommandCore::CommandRegistry::Get();

        // `help <이름>` — 명령 하나의 상세.
        if (ctx.parts.size() > 1)
        {
            const std::string& name = ctx.parts[1];
            const CommandCore::CommandDescriptor* descriptor = registry.Find(name);
            if (nullptr == descriptor)
            {
                std::printf("[CLI] 알 수 없는 명령: %s\n", name.c_str());
                return CommandCore::InvalidArguments(
                    "알 수 없는 명령: " + name, "command.unknown");
            }
            std::fputs(CommandCore::RenderCommandDetail(*descriptor).c_str(), stdout);
            return CommandCore::Ok();
        }

        ctx.system.PrintHelp();
        return CommandCore::Ok();
    }

    /// commands.list [경로]
    ///
    /// discovery TSV. 소비자가 C++ 소스를 긁는 것을 여기서 끝낸다(§2.4) —
    /// `Invoke-Dx12Suite.ps1` 이 `cmd == "dx12.*"` 리터럴을 정규식으로 뽑다가
    /// 두 번 틀린(26/35 만 실행, 그리고 0/35 로 읽음) 그 방식이다.
    /// LC4 의 `GET /commands` 가 같은 snapshot 을 JSON 으로 낸다.

    static CommandCore::CommandResult Cmd_commands_list(const ConsoleCommandContext& ctx)
    {
        const CommandCore::CommandRegistry& registry = CommandCore::CommandRegistry::Get();
        const std::string tsv = CommandCore::RenderDiscoveryTsv(registry);

        if (ctx.parts.size() > 1)
        {
            const std::string path = ResolveTestArtifactPath("lc3", ctx.parts[1]);
            std::FILE* raw = nullptr;
            if (0 != fopen_s(&raw, path.c_str(), "wb") || nullptr == raw)
            {
                return CommandCore::InternalError("commands.write_failed",
                    "commands.list: " + path + " 를 쓸 수 없다");
            }
            // 닫기를 소유권에 묶는다. 지금 경로는 전부 닫지만, 사이에 조기
            // 반환이 하나만 끼어도 새는 형태였다.
            const std::unique_ptr<std::FILE, int(*)(std::FILE*)> out(raw, &std::fclose);

            std::fwrite(tsv.data(), 1, tsv.size(), out.get());
            const bool failed = (0 != std::ferror(out.get()));
            if (failed)
            {
                return CommandCore::InternalError("commands.write_failed",
                    "commands.list: " + path + " 를 온전히 쓰지 못했다");
            }
            std::printf("[CLI] commands.list 완료 명령=%zu 이름=%zu 경로=%s\n",
                        registry.CommandCount(), registry.NameCount(), path.c_str());
        }
        else
        {
            std::fputs(tsv.c_str(), stdout);
        }

        CommandCore::CommandData data = CommandCore::CommandData::Object();
        data.Set("commands", CommandCore::CommandData::Int(
            static_cast<int64_t>(registry.CommandCount())));
        data.Set("names", CommandCore::CommandData::Int(
            static_cast<int64_t>(registry.NameCount())));
        data.Set("problems", CommandCore::CommandData::Int(
            static_cast<int64_t>(registry.Problems().size())));
        return CommandCore::Ok("registry snapshot", std::move(data));
    }

    /// commands.describe <이름>

    static CommandCore::CommandResult Cmd_commands_describe(const ConsoleCommandContext& ctx)
    {
        if (ctx.parts.size() < 2)
        {
            return CommandCore::InvalidArguments(
                "commands.describe: 명령 이름이 필요하다", "commands.name_missing");
        }

        const CommandCore::CommandRegistry& registry = CommandCore::CommandRegistry::Get();
        const CommandCore::CommandDescriptor* descriptor = registry.Find(ctx.parts[1]);
        if (nullptr == descriptor)
        {
            return CommandCore::InvalidArguments(
                "알 수 없는 명령: " + ctx.parts[1], "command.unknown");
        }

        std::fputs(CommandCore::RenderCommandDetail(*descriptor).c_str(), stdout);

        CommandCore::CommandData data = CommandCore::CommandData::Object();
        data.Set("canonical", CommandCore::CommandData::String(descriptor->canonical));
        data.Set("cost", CommandCore::CommandData::String(
            std::string(CommandCore::ToString(descriptor->cost))));
        data.Set("roles", CommandCore::CommandData::String(
            std::string(CommandCore::ToString(descriptor->roles))));
        data.Set("class", CommandCore::CommandData::String(
            std::string(CommandCore::ToString(descriptor->cls))));
        data.Set("liveness", CommandCore::CommandData::String(
            std::string(CommandCore::ToString(descriptor->liveness))));
        data.Set("resultBearing", CommandCore::CommandData::Bool(descriptor->resultBearing));
        return CommandCore::Ok("descriptor", std::move(data));
    }

    /// commands.selftest
    ///
    /// registry 무결성. 이름 중복·요약 누락·descriptor 부재를 **실패로** 낸다.
    /// 예전에는 이름 중복이 `printf` 한 줄로 지나가고 그 뒤로 조용히 한쪽이 먹혔다.

    static CommandCore::CommandResult Cmd_commands_selftest(const ConsoleCommandContext& ctx)
    {
        (void)ctx;
        const CommandCore::CommandRegistry& registry = CommandCore::CommandRegistry::Get();

        // ★ **반대 방향을 본다: seed 는 있는데 등록이 사라진 명령.**
        //
        //   `Add()` 는 "등록하려는 이름에 seed 가 있나"만 본다. 그 방향만으로는
        //   등록 줄 하나가 통째로 빠진 경우를 못 본다 — 그 명령은 registry 에도
        //   help 에도 없으므로 **둘을 맞대 보는 검사는 전부 초록**이다. 실제로
        //   확인했다: `reg({"ai.status"}, ...)` 한 줄을 지우고 discovery 게이트를
        //   돌리니 "전체 통과"가 났고, 같은 출력에 `seed=212 명령=211` 이 이미
        //   찍혀 있었다 — 증거를 인쇄해 놓고 판정하지 않고 있었다.
        //
        //   LC6 은 핸들러 8,700 줄을 도메인 파일 일곱 개로 옮긴다. 등록 줄을
        //   빠뜨리는 것이 그 작업의 대표적 사고라, 표를 옮기기 전에 엔진이
        //   스스로 잡게 만든다.
        std::vector<std::string> problems = registry.Problems();
        const auto& commandletProblems = CommandCore::CommandRegistry::Commandlets().Problems();
        problems.insert(problems.end(), commandletProblems.begin(), commandletProblems.end());
        for (std::size_t i = 0; i < CommandCore::DescriptorSeedCount(); ++i)
        {
            const CommandCore::DescriptorSeed* seed = CommandCore::DescriptorSeedAt(i);
            if (nullptr == seed) continue;
            if (seed->commandlet)
            {
                if (!CommandCore::CommandRegistry::Commandlets().Find(seed->name))
                    problems.push_back(std::string("Unregistered commandlet: ") + seed->name);
                continue;
            }

            // ★ LC8: **이 호스트의 몫만 본다.**
            //
            //   seed 표는 Editor 와 Player 가 나눠 쓴다(§11.2). role 을 안 보면
            //   Player 전용 seed 가 여기서 "등록되지 않았다" 로 잡히고, 그것을
            //   피하려고 표를 둘로 가르면 schema 의 단일 정본이 깨진다(LC3 이
            //   없앤 바로 그 drift 다). 부재는 결함이 아니라 **설계**다 —
            //   `roles` 에 Editor 가 없는 명령은 여기 없는 것이 맞다.
            if (!CommandCore::HasRole(seed->roles, CommandCore::CommandRoles::Editor)) continue;

            if (nullptr == registry.Find(seed->name))
            {
                problems.push_back("seed 는 있는데 등록되지 않았다: "
                                   + std::string(seed->name));
            }
        }

        std::printf("[commands.selftest] 명령=%zu 이름=%zu seed=%zu 문제=%zu\n",
                    registry.CommandCount(), registry.NameCount(),
                    CommandCore::DescriptorSeedCount(), problems.size());
        for (const std::string& problem : problems)
        {
            std::printf("[commands.selftest] 문제: %s\n", problem.c_str());
        }

        CommandCore::CommandData data = CommandCore::CommandData::Object();
        data.Set("commands", CommandCore::CommandData::Int(
            static_cast<int64_t>(registry.CommandCount())));
        data.Set("problems", CommandCore::CommandData::Int(
            static_cast<int64_t>(problems.size())));

        if (!problems.empty())
        {
            return CommandCore::Fail("registry.integrity",
                "registry 무결성 위반 " + std::to_string(problems.size()) + "건",
                std::move(data));
        }
        return CommandCore::Ok("registry 무결성 통과", std::move(data));
    }

    static CommandCore::CommandResult Cmd_quit(const ConsoleCommandContext& ctx)
    {
        std::printf("[CLI] 종료 요청\n");
        ctx.system.RequestQuit();

        // ★ quit 이 성공을 낸다고 앞의 실패가 지워지지 않는다.
        //
        //   session 은 가장 심한 결과를 보존하므로(§3.1) 실패 뒤 quit 이 와도
        //   exit 은 내려가지 않는다. 이 성질이 없으면 시나리오 끝에 습관처럼
        //   붙는 quit 하나가 모든 판정을 지운다 — LC0 canary 의
        //   failure-then-quit 케이스가 정확히 그 상태를 고정해 두었다.
        return CommandCore::Ok("종료 요청");
    }

    /// cli.drain.budget [<시간ms> <개수>]
    ///
    /// 서비스 큐 드레인 예산을 읽거나 바꾼다(§7.2).
    ///
    /// ★ 있는 이유는 **게이트가 자기 이빨을 확인하기 위해서**다(§14.7).
    ///   예산을 0 으로 만들면 서비스 큐가 돌지 않고, 그 상태에서 SLO 게이트가
    ///   붉어져야 한다. 붉어지지 않으면 그 게이트는 아무것도 안 지키고 있다.

    static CommandCore::CommandResult Cmd_cli_drain_budget(const ConsoleCommandContext& ctx)
    {
        ConsoleCommandSystem::DrainBudget budget = ctx.system.GetDrainBudget();

        if (ctx.parts.size() >= 3)
        {
            try
            {
                budget.timeMs = std::stod(ctx.parts[1]);
                budget.count  = static_cast<std::size_t>(std::stoull(ctx.parts[2]));
            }
            catch (const std::exception&)
            {
                return CommandCore::InvalidArguments(
                    "cli.drain.budget: <시간ms> <개수> 가 숫자여야 한다", "drain.not_a_number");
            }
            if (budget.timeMs < 0.0)
            {
                return CommandCore::InvalidArguments(
                    "cli.drain.budget: 시간이 음수다", "drain.negative");
            }
            ctx.system.SetDrainBudget(budget);
        }

        std::printf("[CLI] cli.drain.budget time=%.3fms count=%zu\n", budget.timeMs, budget.count);

        CommandCore::CommandData data = CommandCore::CommandData::Object();
        data.Set("timeMs", CommandCore::CommandData::Double(budget.timeMs));
        data.Set("count",  CommandCore::CommandData::Int(static_cast<int64_t>(budget.count)));
        return CommandCore::Ok("드레인 예산", std::move(data));
    }

    /// cli.echo.args <아무 인자>
    ///
    /// tokenizer가 실제로 무엇을 만들었는지 되비춘다. 길이를 함께 찍는 이유는
    /// 따옴표·공백·빈 문자열이 눈으로 구분되지 않기 때문이다 — `<>`와 `<"">`는
    /// 화면에서 같아 보여도 다른 토큰이다.
    ///
    /// LC2가 라인 문법과 JSON `args` 배열이 같은 invocation을 만드는지 단정할 때
    /// 양쪽에서 이 명령을 부른다. 그때 비교 기준이 되려면 지금의 형상이 golden으로
    /// 고정돼 있어야 한다.

    static CommandCore::CommandResult Cmd_cli_echo_args(const ConsoleCommandContext& ctx)
    {
        using namespace CommandCore;
        std::printf("[CLI] cli.echo.args count=%zu line_len=%zu\n",
                    ctx.parts.size(), ctx.line.size());
        for (std::size_t i = 0; i < ctx.parts.size(); ++i)
        {
            std::printf("[CLI] cli.echo.args arg[%zu] len=%zu <<<%s>>>\n",
                        i, ctx.parts[i].size(), ctx.parts[i].c_str());
        }
        auto data = CommandData::Object();
        auto tokens = CommandData::Array();
        for (const auto& token : ctx.parts) tokens.Append(CommandData::String(token));
        data.Set("tokens", std::move(tokens));
        data.Set("count", CommandData::Int(ctx.parts.size()));
        data.Set("lineLength", CommandData::Int(ctx.line.size()));
        return Ok({}, std::move(data));
    }

    static CommandCore::CommandResult Cmd_game_pak(const ConsoleCommandContext& ctx)
    {
        (void)ctx;
        const bool buildOk = GameBuilderSystem::GetInstance()->BuildGame();
        std::printf("[CLI] game.pak Release Player 패키지 %s\n",
            buildOk ? "빌드·검증·게시 완료" : "실패");

        // 예전에는 여기서 SetExitCode(5) 를 직접 썼다. 의도는 옳았지만 —
        // "뒤의 quit/진단 명령은 계속 실행하되 최종 결과는 비-0" — 그 규약을
        // 이 핸들러 혼자 지켰고, 뒤에 오는 다른 직접 쓰기가 값을 덮을 수 있었다.
        // 이제 session 이 가장 심한 결과를 보존하므로 규약이 전역으로 성립한다.
        if (!buildOk)
        {
            return CommandCore::InternalError(
                "build.failed", "game.pak: Release Player 패키지 빌드 실패");
        }
        return CommandCore::Ok("Release Player 패키지 완료");
    }

    static CommandCore::CommandResult Cmd_wait(const ConsoleCommandContext& ctx)
    {
        const std::vector<std::string>& parts = ctx.parts;

        // ★ `atoi` 는 실패를 0 으로 돌려준다.
        //
        //   `wait abc` 가 조용히 `wait 0` 이 되어, 프레임 수로 시간을 재는
        //   시나리오가 대기 없이 지나가고도 성공으로 끝났다. §14.2 의
        //   "숫자 overflow/garbage → 조용한 0 변환 금지"가 이 자리다.
        //   `max(0, ...)` 도 같은 성격이다 — 음수를 오류가 아니라 0 으로 바꾼다.
        int frames = 1;
        if (parts.size() > 1)
        {
            const std::string& raw = parts[1];
            std::size_t consumed = 0;
            long long   parsed   = 0;
            try
            {
                parsed = std::stoll(raw, &consumed);
            }
            catch (const std::exception&)
            {
                return CommandCore::InvalidArguments(
                    "wait: 프레임 수가 숫자가 아니다: " + raw, "wait.not_a_number");
            }
            if (consumed != raw.size())
            {
                return CommandCore::InvalidArguments(
                    "wait: 프레임 수 뒤에 남는 문자가 있다: " + raw, "wait.trailing_garbage");
            }
            if (parsed < 0)
            {
                return CommandCore::InvalidArguments(
                    "wait: 프레임 수가 음수다: " + raw, "wait.negative");
            }
            if (parsed > static_cast<long long>(std::numeric_limits<int>::max()))
            {
                return CommandCore::InvalidArguments(
                    "wait: 프레임 수가 너무 크다: " + raw, "wait.too_large");
            }
            frames = static_cast<int>(parsed);
        }

        ctx.system.SetWaitFrames(frames);
        std::printf("[CLI] %d 프레임 대기\n", frames);

        CommandCore::CommandData data = CommandCore::CommandData::Object();
        data.Set("frames", CommandCore::CommandData::Int(frames));
        return CommandCore::Ok("프레임 대기", std::move(data));
    }

    static CommandCore::CommandResult Cmd_window_resize(const ConsoleCommandContext& ctx)
    {
        using namespace CommandCore;
        const std::vector<std::string>& parts = ctx.parts;

        // 해상도 독립 검증용(PHASE 7). 창을 실제로 리사이즈해 엔진의 리사이즈 경로를
        // 그대로 태운다 — g_ClientRect 갱신부터 UI 리플로우까지 실제 흐름을 검증해야
        // 의미가 있다.
        if (parts.size() != 3)
        {
            std::printf("[CLI] 사용법: window.resize <너비> <높이>\n");
            return InvalidArguments("window.resize requires width >= 320 and height >= 240");
        }

        int width{}, height{};
        if (!ParseNumber(parts[1], width) || !ParseNumber(parts[2], height) || width < 320 || height < 240 || width > 32767 || height > 32767)
        {
            std::printf("[CLI] 너무 작은 크기입니다: %dx%d\n", width, height);
            return InvalidArguments("window.resize requires width >= 320 and height >= 240");
        }

        // GetActiveWindow는 창이 포그라운드가 아니면 null을 준다. 스크립트 실행은
        // 대개 백그라운드라 이 경로가 실제로 걸리므로, 프로세스의 보이는 최상위 창을
        // 직접 찾아 대체한다.
        HWND hwnd = ::GetActiveWindow();
        if (nullptr == hwnd)
        {
            ::EnumWindows([](HWND candidate, LPARAM out) -> BOOL
            {
                DWORD pid = 0;
                ::GetWindowThreadProcessId(candidate, &pid);
                if (pid != ::GetCurrentProcessId()) return TRUE;
                if (!::IsWindowVisible(candidate)) return TRUE;
                if (::GetWindow(candidate, GW_OWNER) != nullptr) return TRUE;

                *reinterpret_cast<HWND*>(out) = candidate;
                return FALSE;
            }, reinterpret_cast<LPARAM>(&hwnd));
        }
        if (nullptr == hwnd) { std::printf("[CLI] 창 핸들 없음\n"); return PreconditionFailed("window.unavailable", "No editor window"); }

        // 클라이언트 영역이 요청 크기가 되도록 창 전체 크기를 역산한다.
        RECT desired{ 0, 0, width, height };
        const LONG style = ::GetWindowLong(hwnd, GWL_STYLE);
        const LONG exStyle = ::GetWindowLong(hwnd, GWL_EXSTYLE);
        if (!::AdjustWindowRectEx(&desired, style, FALSE, exStyle))
            return Fail("window.resize_failed", "AdjustWindowRectEx failed");

        if (!::SetWindowPos(hwnd, nullptr, 0, 0,
            desired.right - desired.left, desired.bottom - desired.top,
            SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE))
            return Fail("window.resize_failed", "SetWindowPos failed");

        // 요청한 크기가 그대로 적용되지 않을 수 있다(모니터보다 큰 창은 잘린다).
        // 요청값만 찍으면 검증에서 엉뚱한 기준을 잡게 되므로 실제 결과를 읽어 보고한다.
        RECT actual{};
        if (!::GetClientRect(hwnd, &actual))
            return Fail("window.query_failed", "GetClientRect failed after resize");
        const int actualWidth = actual.right - actual.left;
        const int actualHeight = actual.bottom - actual.top;

        const std::string message = (actualWidth == width && actualHeight == height)
            ? "[CLI] 창 크기 변경: " + std::to_string(actualWidth) + "x" + std::to_string(actualHeight)
            : "[CLI] 창 크기 변경(클램프됨): 요청 " + std::to_string(width) + "x" + std::to_string(height) +
              " -> 실제 " + std::to_string(actualWidth) + "x" + std::to_string(actualHeight);

        Debug::PrintLog(spdlog::level::warn, message);
        std::printf("%s\n", message.c_str());
        auto data = CommandData::Object();
        data.Set("requestedWidth", CommandData::Int(width));
        data.Set("requestedHeight", CommandData::Int(height));
        data.Set("width", CommandData::Int(actualWidth));
        data.Set("height", CommandData::Int(actualHeight));
        data.Set("clamped", CommandData::Bool(actualWidth != width || actualHeight != height));
        return Ok(message, std::move(data));
    }

    static CommandCore::CommandResult Cmd_window_info(const ConsoleCommandContext& ctx)
    {
        using namespace CommandCore;
        if (ctx.parts.size() != 1) return InvalidArguments("This command takes no arguments");
        // 엔진이 실제로 인식하는 클라이언트 크기. window.resize가 리사이즈 경로까지
        // 도달했는지를 UI 계산과 같은 출처(화면 크기 버스)로 확인한다.
        const auto client = ScreenResizeBus::Get().GetSizeSnapshot();
        const uint32_t clientW = client.width;
        const uint32_t clientH = client.height;
        std::printf("[CLI] 클라이언트 영역: %ux%u\n", clientW, clientH);
        Debug::PrintLog(spdlog::level::warn, "[CLI] 클라이언트 영역: " +
            std::to_string(clientW) + "x" + std::to_string(clientH));
        auto data = CommandData::Object();
        data.Set("width", CommandData::Int(clientW));
        data.Set("height", CommandData::Int(clientH));
        return Ok({}, std::move(data));
    }

    static CommandCore::CommandResult Cmd_lifecycle_trace(const ConsoleCommandContext& ctx)
    {
        using namespace CommandCore;
        const std::vector<std::string>& parts = ctx.parts;

        // 생명주기 호출 순서를 받아 적는다(PHASE 9-0).
        //
        // PHASE 9는 이 순서를 만들어 내는 기구를 통째로 바꾼다. 지금 순서는 델리게이트의
        // 우선순위 정렬과 등록 시점이 만드는 창발적 결과라 코드로는 알 수 없다 —
        // 교체 전에 받아 적어 두어야 교체 후 "동작이 같다"를 주장할 수 있다.
        const std::string mode = (parts.size() >= 2) ? parts[1] : "status";

        if ((mode != "on" && mode != "off" && mode != "clear" && mode != "status") ||
            parts.size() > (mode == "on" ? 3u : 2u))
            return InvalidArguments("lifecycle.trace on [frames] | off | clear | status");
        if (mode == "on")
        {
            // 틱 단계(Update·LateUpdate·FixedUpdate)를 적을 프레임 수.
            // 한 프레임 안의 순서가 알고 싶은 것이지 반복 횟수가 아니라서 예산을 둔다.
            int frames = 3;
            if (parts.size() == 3 && (!ParseNumber(parts[2], frames) || frames < 0))
                return InvalidArguments("frames must be a non-negative integer");
            Lifecycle::Trace::Enable(frames);
            std::printf("[CLI] lifecycle.trace on — 틱 %d프레임\n", frames);
        }
        else if (mode == "off")
        {
            Lifecycle::Trace::Disable();
            std::printf("[CLI] lifecycle.trace off — %zu건 보관\n", Lifecycle::Trace::Count());
        }
        else if (mode == "clear")
        {
            Lifecycle::Trace::Clear();
            std::printf("[CLI] lifecycle.trace clear\n");
        }
        else
        {
            std::printf("[CLI] lifecycle.trace — %s · %zu건 · 틱 잔여 %d프레임\n",
                Lifecycle::Trace::IsEnabled() ? "기록 중" : "정지",
                Lifecycle::Trace::Count(),
                Lifecycle::Trace::RemainingTickFrames());
        }
        auto data = CommandData::Object();
        data.Set("enabled", CommandData::Bool(Lifecycle::Trace::IsEnabled()));
        data.Set("count", CommandData::Int(Lifecycle::Trace::Count()));
        data.Set("remainingTickFrames", CommandData::Int(Lifecycle::Trace::RemainingTickFrames()));
        return Ok({}, std::move(data));
    }

    static CommandCore::CommandResult Cmd_lifecycle_registry(const ConsoleCommandContext& ctx)
    {
        using namespace CommandCore;
        if (ctx.parts.size() != 1) return InvalidArguments("This command takes no arguments");
        // 상태 조회만 남았다(PHASE 9-3에서 델리게이트를 철거해 경로가 하나다).
        // 진단 가치는 그대로다 — 각 단계 리스트의 크기가 곧 '무엇이 매 프레임 도는가'이고,
        // 마스크 표 크기는 등록 목록이 실제로 채워졌는지를 알려 준다.
        auto data = CommandData::Object();
        data.Set("maskTypes", CommandData::Int(Lifecycle::Registry::Count()));
        Scene* scene = SceneManagers->GetActiveScene();
        std::printf("[CLI] lifecycle — 마스크 표 %zu종\n", Lifecycle::Registry::Count());

        if (nullptr != scene)
        {
            const auto counts = scene->GetRegistryCounts();
            data.Set("pendingInitialize", CommandData::Int(counts.pendingInitialize));
            data.Set("pendingSimulation", CommandData::Int(counts.pendingSimulation));
            std::printf("[CLI]   pendingInitialize %zu · pendingSimulation %zu\n",
                counts.pendingInitialize, counts.pendingSimulation);

            // 트랙 L4 래칫 측정용 — 명시 구독(Schedule().Subscribe) 대 암묵 구독
            // (RegisterComponent 경유)의 잔존 수. 통합 단계에서 배선.
            const auto subCounts = scene->GetSubscriptionCounts();
            data.Set("implicitSubscriptions", CommandData::Int(subCounts.implicitCount));
            data.Set("explicitSubscriptions", CommandData::Int(subCounts.explicitCount));
            std::printf("[CLI]   구독 잔존 — 암묵 %zu · 명시 %zu\n",
                subCounts.implicitCount, subCounts.explicitCount);
        }
        data.Set("hasScene", CommandData::Bool(scene != nullptr));
        return Ok({}, std::move(data));
    }

    static CommandCore::CommandResult Cmd_lifecycle_dump(const ConsoleCommandContext& ctx)
    {
        using namespace CommandCore;
        const std::vector<std::string>& parts = ctx.parts;

        if (parts.size() > 2) return InvalidArguments("lifecycle.dump [path]");
        const std::string path = ResolveTestArtifactPath("Traces",
            (parts.size() >= 2) ? parts[1] : std::string("lifecycle_trace.tsv"));
        const size_t count = Lifecycle::Trace::Count();

        if (0 == count)
        {
            // 빈 파일을 성공으로 흘려보내면 "기준선을 떴다"고 착각한 채 다음으로 넘어간다.
            // 3-6에서 겪은 조용한 통과와 같은 부류라 여기서 실패로 못 박는다.
            std::printf("[CLI] lifecycle.dump 실패 — 기록 0건 (lifecycle.trace on 을 먼저 부를 것)\n");
            return PreconditionFailed("lifecycle.empty", "Enable lifecycle.trace before dumping");
        }

        if (Lifecycle::Trace::Dump(path))
        {
            auto data = CommandData::Object();
            data.Set("path", CommandData::String(path));
            data.Set("count", CommandData::Int(count));
            std::printf("[CLI] lifecycle.dump %s — %zu건\n", path.c_str(), count);
            return Ok({}, std::move(data));
        }
        else
        {
            std::printf("[CLI] lifecycle.dump 실패 — 파일을 열 수 없다: %s\n", path.c_str());
        }
        return Fail("lifecycle.dump_failed", "Unable to write trace: " + path);
    }

    static CommandCore::CommandResult Cmd_lifecycle_stress(const ConsoleCommandContext& ctx)
    {
        using namespace CommandCore;
        if (ctx.parts.size() < 2 || ctx.parts.size() > 3) return InvalidArguments("lifecycle.stress <mode> [count]");
        const std::vector<std::string>& parts = ctx.parts;

        // 파괴·생성을 몰아쳐 수명 경로를 흔든다(PHASE 9-0의 ASan 재현용).
        //
        // ★ 0ec60573(9-12)이 "닫힌 계획 소속" 으로 은퇴시켰지만, 살아 있는 게이트
        //   둘(verify-lifecycle-baseline · verify-ai-registry)이 시나리오에서 계속
        //   부르고 있었다. 이름이 표에서 빠지자 commandlet 모드의 미등록 명령이
        //   EditorCommandlets::Run 으로 흘러 "accepts no arguments" 로 거절됐고,
        //   기준선 TSV 의 씬 A 파괴 블록(21행)이 거꾸로 나왔다. 그래서 되살린다.
        //
        // 지금은 프레임 경계에서 파괴가 일어나는 경로만 흔든다. "순회 도중 파괴"와
        // "Update 안에서 AddComponent" 같은 재진입 재현은 reentrant 모드가 맡는다.
        const std::string mode = (parts.size() >= 2) ? parts[1] : "";
        int count = 8;
        if (parts.size() == 3 && (!ParseNumber(parts[2], count) || count < 1 || count > 100000)) return InvalidArguments("count must be 1..100000");
        if (mode != "destroy" && mode != "churn" && mode != "reentrant" && mode != "reentrant-destroy" && mode != "reentrant-add") return InvalidArguments("Unknown lifecycle stress mode");
        auto data = CommandData::Object(); data.Set("mode", CommandData::String(mode)); data.Set("requested", CommandData::Int(count));

        Scene* scene = SceneManagers->GetActiveScene();
        if (!scene) { std::printf("[CLI] 활성 씬 없음\n"); return PreconditionFailed("scene.not_found", "No active scene"); }

        if (mode == "destroy")
        {
            int marked = 0;
            // 루트(0번)는 건드리지 않는다. 씬 구조가 무너지면 이후 명령이 전부 의미를 잃는다.
            for (size_t i = 1; i < scene->m_Entities.size() && marked < count; ++i)
            {
                const auto& owned = scene->m_Entities[i];
                if (!owned || owned->IsDestroyMark()) continue;
                scene->DestroyEntity(owned.get());
                ++marked;
            }
            data.Set("marked", CommandData::Int(marked));
            std::printf("[CLI] lifecycle.stress destroy — %d개 파괴 표시\n", marked);
        }
        else if (mode == "churn")
        {
            // 파괴와 생성을 같은 프레임에 섞는다. 인덱스 재사용 경로가 여기서 드러난다.
            int marked = 0;
            for (size_t i = 1; i < scene->m_Entities.size() && marked < count; ++i)
            {
                const auto& owned = scene->m_Entities[i];
                if (!owned || owned->IsDestroyMark()) continue;
                scene->DestroyEntity(owned.get());
                ++marked;
            }
            for (int i = 0; i < count; ++i)
            {
                scene->CreateEntity("StressChurn_" + std::to_string(i));
            }
            data.Set("marked", CommandData::Int(marked)); data.Set("created", CommandData::Int(count));
            std::printf("[CLI] lifecycle.stress churn — 파괴 %d · 생성 %d\n", marked, count);
        }
        else if (mode == "reentrant" || mode == "reentrant-destroy" || mode == "reentrant-add")
        {
            // 순회 한복판에서 터뜨린다(PHASE 9-9).
            //
            // 위 destroy/churn은 프레임 경계에서 일어나므로 R1·R2를 시험하지 못한다 —
            // 그 둘은 "순회하는 도중에 대상이 죽으면?"이라는 질문이고, 답하려면
            // 실제로 순회 중이어야 한다.
            const auto kind =
                (mode == "reentrant-destroy") ? Scene::StressKind::Destroy :
                (mode == "reentrant-add")     ? Scene::StressKind::AddComponent :
                                                Scene::StressKind::Both;
            scene->ArmReentrancyStress(kind, count);
            data.Set("armed", CommandData::Bool(true));
            std::printf("[CLI] lifecycle.stress %s — 다음 Update 순회 한복판에서 %d건 발화\n",
                mode.c_str(), count);
        }
        return Ok("Lifecycle stress operation applied or armed", std::move(data));
    }

    static CommandCore::CommandResult Cmd_log_flush(const ConsoleCommandContext& ctx)
    {
        using namespace CommandCore;
        if (ctx.parts.size() != 1) return InvalidArguments("This command takes no arguments");
        Log::FlushNow();
        std::printf("[CLI] 로그 flush\n");
        return Ok("Logs flushed");
    }


    // ── editor:: 선언 배선 관측과 게이트 ────────────────────────────────
    //
    // 계획서 부록 B.3이 요구한 `editor.windows` 덤프다. 출력 형식은 이 가족의
    // 관례대로 TSV이고, 뒤에 감사 요약이 붙는다. 배선이 더러우면 실패로 낸다 —
    // 관측만 하고 판정하지 않으면 도는 세트에 넣어도 초록만 쌓인다.
    // PHASE 21 W7-0 — 창 하나를 열고 닫고 **앞으로 세운다**.
    //
    // `editor.windows` 는 표를 읽기만 한다. 여닫는 자리는 메뉴뿐이었고, 그래서
    // 도크 노드에 탭으로 겹친 창은 CLI 로 도달할 수 없었다 — 열려 있어도
    // 선택되지 않으면 본문이 돌지 않는다. Content Browser 가 AssetBundle 과 같은
    // `dock_slot::bottom` 이라 실제로 그랬고, W7 의 브라우저 비용이 181 프레임 중
    // **1 프레임**만 잡혔다.
    //
    // 요청만 걸고 UI 스레드가 프레임 머리에서 소비한다(`draw_windows`). 표를
    // 읽는 스레드가 그쪽 하나라, 적용도 그쪽이어야 읽기와 쓰기가 갈리지 않는다.
    static CommandCore::CommandResult Cmd_editor_window(const ConsoleCommandContext& ctx)
    {
        using namespace CommandCore;
        const auto& args = ctx.parts;
        if (3 != args.size()) return InvalidArguments("editor.window <stable-id> <open|close|focus>");

        ::editor::window_request request{};
        if ("open" == args[2])       request = ::editor::window_request::open;
        else if ("close" == args[2]) request = ::editor::window_request::close;
        else if ("focus" == args[2]) request = ::editor::window_request::focus;
        else return InvalidArguments("editor.window <stable-id> <open|close|focus>");

        auto data = CommandData::Object();
        data.Set("stableId", CommandData::String(args[1]));
        data.Set("request", CommandData::String(args[2]));
        if (!::editor::queue_window_request(args[1], request))
        {
            // 오타가 "아무 일도 일어나지 않음" 으로 보이지 않게 붉힌다.
            data.Set("declared", CommandData::Bool(false));
            return Fail("editor.window.undeclared",
                "선언되지 않은 창이다: " + args[1] + " (editor.windows 로 표를 보라)",
                std::move(data));
        }
        data.Set("declared", CommandData::Bool(true));
        // 적용은 **다음 UI 프레임**이다. 지금 상태를 돌려주면 거짓말이 된다.
        data.Set("queued", CommandData::Bool(true));
        return Ok("요청을 걸었다(다음 UI 프레임에 적용): " + args[1] + " " + args[2], std::move(data));
    }

    static CommandCore::CommandResult Cmd_editor_windows(const ConsoleCommandContext& ctx)
    {
        using namespace CommandCore;
        if (ctx.parts.size() != 1) return InvalidArguments("This command takes no arguments");

        const std::string table = ::editor::dump_declared_windows();
        const ::editor::window_audit audit = ::editor::audit_declared_windows();
        const std::string summary = ::editor::dump_window_audit();

        std::printf("%s%s", table.c_str(), summary.c_str());

        auto data = CommandData::Object();
        data.Set("declared", CommandData::Int(static_cast<int>(audit.declared)));
        data.Set("boundBodies", CommandData::Int(static_cast<int>(audit.bound)));
        data.Set("orphanBodies", CommandData::Int(static_cast<int>(audit.orphan_bodies.size())));
        data.Set("bodylessWindows", CommandData::Int(static_cast<int>(audit.bodyless_windows.size())));
        data.Set("duplicateIds", CommandData::Int(static_cast<int>(audit.duplicate_ids.size())));
        data.Set("emptyDockSlots", CommandData::Int(static_cast<int>(audit.empty_dock_slots.size())));
        data.Set("clean", CommandData::Bool(audit.clean()));

        if (!audit.clean())
        {
            return Fail("editor.windows.dirty", "창 배선 감사 실패: " + summary, std::move(data));
        }
        return Ok("창 " + std::to_string(audit.declared) + "개 배선 이상 없음", std::move(data));
    }

    // 계획서 부록 A.5 가 요구한 `editor.menu` 덤프다. `editor.windows` 와 같은
    // 모양이다 — TSV 뒤에 감사 요약, 더러우면 실패. 관측만 하고 판정하지 않으면
    // 도는 세트에 넣어도 초록만 쌓인다.
    //
    // 기대 선언자 이름은 중앙 목록이 내놓는 배열에서 온다(EDITOR_MENU_LIST 를
    // 두 번째로 소비한 것). 등록과 감사가 같은 출처를 쓰므로 이름이 두 벌이 되지
    // 않는다.
    static CommandCore::CommandResult Cmd_editor_menu(const ConsoleCommandContext& ctx)
    {
        using namespace CommandCore;
        if (ctx.parts.size() != 1) return InvalidArguments("This command takes no arguments");

        const std::span<const std::string_view> expected{
            ::editor::editor_menu_declarer_names.data(),
            ::editor::editor_menu_declarer_names.size() };

        const std::string table = ::editor::dump_menu_table();
        const ::editor::menu_audit audit = ::editor::audit_declared_menus(expected);
        const std::string summary = ::editor::dump_menu_audit(expected);

        std::printf("%s%s", table.c_str(), summary.c_str());

        auto data = CommandData::Object();
        data.Set("topItems", CommandData::Int(static_cast<int>(audit.top_items)));
        data.Set("popupItems", CommandData::Int(static_cast<int>(audit.popup_items)));
        data.Set("declarersSeen", CommandData::Int(static_cast<int>(audit.declarers_seen)));
        data.Set("declarersExpected", CommandData::Int(static_cast<int>(audit.declarers_expected)));
        data.Set("pathConflicts", CommandData::Int(static_cast<int>(audit.path_conflicts.size())));
        data.Set("unnamedItems", CommandData::Int(static_cast<int>(audit.unnamed_items.size())));
        data.Set("silentDeclarers", CommandData::Int(static_cast<int>(audit.silent_declarers.size())));
        data.Set("clean", CommandData::Bool(audit.clean()));

        if (!audit.clean())
        {
            return Fail("editor.menu.dirty", "메뉴 배선 감사 실패: " + summary, std::move(data));
        }
        return Ok("메뉴 항목 " + std::to_string(audit.top_items + audit.popup_items) +
            "개 배선 이상 없음", std::move(data));
    }


    // ── 크롬 관측 셋 (PHASE 21 W0 전반 · 계획서 §1.9) ─────────────────────
    //
    // `editor.windows`·`editor.menu` 는 **선언 표**를 읽는다. 이 셋은 살아 있는
    // ImGui 상태를 읽으므로 그리는 쪽이 프레임 끝에 게시한 스냅샷을 본다 —
    // 명령은 게임 스레드에서 돌고 ImGui 프레임은 PresentationThread 것이라
    // 직접 읽으면 경합이다(`EditorChromeSnapshot.h` 서두).
    //
    // 스냅샷이 없으면 **실패로 낸다.** 프레임이 한 번도 안 돌았다는 뜻이고,
    // 그것을 "항목 0" 으로 내면 빈 집합이 성공으로 읽힌다.
    static CommandCore::CommandResult Cmd_editor_dock(const ConsoleCommandContext& ctx)
    {
        using namespace CommandCore;
        if (ctx.parts.size() != 1) return InvalidArguments("This command takes no arguments");

        const ::editor::chrome_snapshot snapshot = ::editor::read_chrome_snapshot();
        // 읽은 뒤 다음 한 번을 요청한다 — 살아 있는 에디터에서 같은 명령을
        // 연달아 불러 값이 바뀌는지 볼 수 있어야 한다.
        ::editor::request_chrome_snapshot();
        if (!snapshot.valid)
        {
            return Fail("editor.dock.no_frame",
                "크롬 스냅샷이 없다 — 표시 프레임이 아직 한 번도 돌지 않았다");
        }

        const ::editor::dock_audit audit = ::editor::audit_dock_tree(snapshot);
        const std::string summary = ::editor::dump_dock_audit(snapshot);
        std::printf("%s%s", ::editor::dump_dock_tree(snapshot).c_str(), summary.c_str());

        auto data = CommandData::Object();
        data.Set("nodes", CommandData::Int(static_cast<int>(audit.nodes)));
        data.Set("leafNodes", CommandData::Int(static_cast<int>(audit.leaf_nodes)));
        data.Set("centralNodes", CommandData::Int(static_cast<int>(audit.central_nodes)));
        data.Set("dockedWindows", CommandData::Int(static_cast<int>(audit.docked_windows)));
        data.Set("undockedSlots", CommandData::Int(static_cast<int>(audit.undocked_slots.size())));
        data.Set("ghostTabs", CommandData::Int(static_cast<int>(audit.ghost_tabs.size())));
        data.Set("imguiVersionNum", CommandData::Int(audit.imgui_version_num));
        data.Set("versionKnown", CommandData::Bool(audit.internal_api_version_known));
        data.Set("imguiHeaderVersion", CommandData::String(snapshot.imgui_version));
        data.Set("imguiRuntimeVersion", CommandData::String(snapshot.imgui_runtime_version));
        data.Set("binaryMatchesHeader", CommandData::Bool(audit.imgui_binary_matches_header));
        data.Set("clean", CommandData::Bool(audit.clean()));

        // PHASE 21 W6 — preset 이 지켜야 하는 두 축을 밖으로 낸다.
        //
        // ① **가운데의 크기.** 비율만 쓰면 창이 작아질수록 가운데가 먼저
        //    사라진다. 빌더가 최소치를 지키는지는 노드의 실제 사각형으로만
        //    확인할 수 있다.
        // ② **화면 밖 떠 있는 패널.** 자리를 바꾸면 도킹돼 있던 창이 떠 버릴
        //    수 있고, 그 창이 화면 밖에 놓이면 사람이 되찾을 방법이 없다.
        //    "밖" 의 기준은 뿌리 도크 노드의 사각형이다 — 그것이 이 프레임의
        //    작업 영역이고, 같은 스냅샷에서 나온 값이라 두 벌이 되지 않는다.
        float centralWidth = 0.f, centralHeight = 0.f;
        float rootRect[4]{};
        bool haveRoot = false;
        for (const ::editor::dock_node_view& node : snapshot.nodes)
        {
            if (node.is_central)
            {
                centralWidth = node.rect[2] - node.rect[0];
                centralHeight = node.rect[3] - node.rect[1];
            }
            if (0 == node.parent && !haveRoot)
            {
                haveRoot = true;
                for (int i = 0; i < 4; ++i) rootRect[i] = node.rect[i];
            }
        }
        // ③ **접혀 버린 노드.** 폭이나 높이가 0 인 잎은 트리에는 멀쩡히 있고
        //    창도 붙어 있지만 사람에게는 패널이 사라진 것으로 보인다. 고아도
        //    유령도 아니라서 기존 감사가 통째로 못 보던 자리다.
        int degenerateNodes = 0;
        for (const ::editor::dock_node_view& node : snapshot.nodes)
        {
            if (!node.is_leaf || !node.is_visible) continue;
            if (node.rect[2] - node.rect[0] <= 1.f || node.rect[3] - node.rect[1] <= 1.f)
                ++degenerateNodes;
        }
        // ④ **아무도 안 사는 노드.** 자리를 갈라 놓고 그 자리에 들어올 창이
        //    없으면 빈 잎이 남는다 — 계획서가 금지한 orphan dock node 가 이것이다.
        //    ③ 과는 다른 축이다: 크기는 멀쩡하고 **탭이 0** 이다. 가운데 노드는
        //    빌더가 늘 비워 두었다가 Scene 이 들어오므로 여기서 세지 않는다
        //    (가운데가 비는 것은 `central` 단정이 따로 본다).
        //
        //    **`is_visible` 을 요구하면 안 된다.** 창이 하나도 없는 노드를 ImGui 는
        //    보이지 않는 것으로 표시하므로, 그 조건을 걸면 잡으려던 바로 그 경우가
        //    필터에서 빠진다. 변이로 확인했다 — `right_lower` 를 쓰지 않는 preset 에서
        //    그 자리를 억지로 가르면 노드가 7→9 · 잎이 4→5 로 늘어나는데 `is_visible`
        //    을 요구한 판은 0 을 냈다.
        int emptyLeafNodes = 0;
        for (const ::editor::dock_node_view& node : snapshot.nodes)
        {
            if (!node.is_leaf || node.is_central) continue;
            if (0 == node.tab_count) ++emptyLeafNodes;
        }
        int offscreenFloating = 0;
        if (haveRoot)
        {
            for (const ::editor::window_placement_view& placement : snapshot.placements)
            {
                if (0 != placement.dock_node || !placement.known_to_imgui) continue;
                if (placement.rect[2] <= placement.rect[0]) continue;   // 아직 크기가 없다
                const bool overlaps =
                    placement.rect[0] < rootRect[2] && placement.rect[2] > rootRect[0] &&
                    placement.rect[1] < rootRect[3] && placement.rect[3] > rootRect[1];
                if (!overlaps) ++offscreenFloating;
            }
        }
        data.Set("centralWidth", CommandData::Double(centralWidth));
        data.Set("centralHeight", CommandData::Double(centralHeight));
        data.Set("offscreenFloating", CommandData::Int(offscreenFloating));
        data.Set("degenerateNodes", CommandData::Int(degenerateNodes));
        data.Set("emptyLeafNodes", CommandData::Int(emptyLeafNodes));
        data.Set("rootWidth", CommandData::Double(haveRoot ? rootRect[2] - rootRect[0] : 0.0));
        data.Set("rootHeight", CommandData::Double(haveRoot ? rootRect[3] - rootRect[1] : 0.0));
        data.Set("uiScale", CommandData::Double(snapshot.ui_scale));
        data.Set("minCentralWidth", CommandData::Double(snapshot.min_central_width));
        data.Set("minCentralHeight", CommandData::Double(snapshot.min_central_height));

        // W0 후반 성능 기준선. 타깃별 GPU ms 는 잴 수단이 없어 빠졌다 —
        // 이유는 덤프의 [NOTE] 에 적혀 있다.
        data.Set("uiCpuMs", CommandData::Double(snapshot.ui_cpu_ms));
        data.Set("imguiVertices", CommandData::Int(snapshot.imgui_vertices));
        data.Set("imguiIndices", CommandData::Int(snapshot.imgui_indices));
        data.Set("imguiDrawCommands", CommandData::Int(snapshot.imgui_draw_commands));

        if (!audit.clean())
        {
            return Fail("editor.dock.dirty", "도크 배치 감사 실패: " + summary, std::move(data));
        }
        return Ok("도크 노드 " + std::to_string(audit.nodes) + "개, 배치 이상 없음",
                  std::move(data));
    }

    static CommandCore::CommandResult Cmd_editor_sceneview(const ConsoleCommandContext& ctx)
    {
        using namespace CommandCore;
        if (ctx.parts.size() != 1) return InvalidArguments("This command takes no arguments");
        const auto s = editor::ReadSceneOverlaySnapshot();
        if (!s.valid) return Fail("editor.sceneview.no_frame", "Scene view has not published a frame");
        auto data = CommandData::Object();
        const auto vec2 = [](ImVec2 v) { auto a = CommandData::Array(); a.Append(CommandData::Double(v.x)); a.Append(CommandData::Double(v.y)); return a; };
        const auto vec3 = [](math::vector3 v) { auto a = CommandData::Array(); a.Append(CommandData::Double(v.x)); a.Append(CommandData::Double(v.y)); a.Append(CommandData::Double(v.z)); return a; };
        data.Set("mode", CommandData::Int(static_cast<int>(s.mode)));
        data.Set("imageMin", vec2(s.imageMin)); data.Set("imageMax", vec2(s.imageMax));
        data.Set("left", vec2(s.left)); data.Set("right", vec2(s.right));
        data.Set("leftWidth", CommandData::Double(s.leftWidth)); data.Set("rightWidth", CommandData::Double(s.rightWidth));
        data.Set("toolbarHeight", CommandData::Double(s.toolbarHeight));
        data.Set("gizmoCenter", vec2(s.gizmoCenter)); data.Set("gizmoRadius", CommandData::Double(s.gizmoRadius));
        data.Set("gizmoVisible", CommandData::Bool(s.gizmoVisible)); data.Set("gizmoUsing", CommandData::Bool(s.gizmoUsing));
        data.Set("pointerBlocked", CommandData::Bool(s.blocked)); data.Set("operation", CommandData::Int(s.operation));
        data.Set("orthographic", CommandData::Bool(s.orthographic)); data.Set("local", CommandData::Bool(s.local));
        data.Set("cameraPosition", vec3(s.cameraPosition)); data.Set("cameraForward", vec3(s.cameraForward));
        // W4: 캔버스 사각형 셋을 그대로 낸다. 크기는 내지 않는다 — 소비자가
        // `max - min` 으로 구하게 두어야 크기와 사각형이 어긋날 자리가 없다.
        data.Set("imageMode", CommandData::String(
            editor::viewport_fit::crop == s.canvas.fit ? "crop" : "letterbox"));
        data.Set("imageReady", CommandData::Bool(s.canvas.valid));
        data.Set("contentMin", vec2(s.canvas.contentMin)); data.Set("contentMax", vec2(s.canvas.contentMax));
        data.Set("canvasImageMin", vec2(s.canvas.imageMin)); data.Set("canvasImageMax", vec2(s.canvas.imageMax));
        data.Set("clipMin", vec2(s.canvas.clipMin)); data.Set("clipMax", vec2(s.canvas.clipMax));
        data.Set("uvMin", vec2(s.canvas.uvMin)); data.Set("uvMax", vec2(s.canvas.uvMax));
        data.Set("sourceAspect", CommandData::Double(s.canvas.sourceAspect));
        return Ok("Scene viewport overlay", std::move(data));
    }
    // PHASE 21 W4 — 중앙 Host 의 표시 모드와 뷰 수요.
    //
    // W0 에서 이 이름을 한 번 보류했다. 그때 실어야 할 값 셋 중 둘(확정된 play
    // state, 입력 소유권)이 없어서 지으면 상수만 찍는 "관측 흉내" 가 되기
    // 때문이었다. W4 가 그중 하나를 실제로 만들었다 — 모드와 그로부터 나오는 뷰
    // 수요는 지금 존재하는 값이다. 나머지 둘은 W5 가 여기에 더한다.
    static CommandCore::CommandResult Cmd_editor_viewport(const ConsoleCommandContext& ctx)
    {
        using namespace CommandCore;
        const auto& args = ctx.parts;
        if (args.size() > 2) return InvalidArguments("editor.viewport [scene|game]");
        if (2 == args.size())
        {
            // 모드를 밖에서 바꿀 수 있어야 모드가 재시작을 넘는지 **살아 있는**
            // 에디터로 잴 수 있다. 클릭만이 모드를 바꿀 수 있으면 그 왕복은
            // 사람 손으로만 증명되고, 게이트는 기본값을 두 번 읽을 뿐이다.
            if ("scene" == args[1]) ::editor::windows::request_viewport_mode(
                ::editor::windows::viewport_mode::scene);
            else if ("game" == args[1]) ::editor::windows::request_viewport_mode(
                ::editor::windows::viewport_mode::game);
            else return InvalidArguments("editor.viewport [scene|game]");
        }
        const auto demand = ::editor::windows::read_viewport_demand();
        auto data = CommandData::Object();
        data.Set("hostPresent", CommandData::Bool(demand.hostPresent));
        // 게시본의 모드다. 인자로 바꾼 직후라면 아직 옛 모드가 나온다 —
        // 요청은 다음 Host 본문이 소비한다. 게이트는 그래서 `wait` 를 끼운다.
        data.Set("mode", CommandData::String(
            ::editor::windows::viewport_mode::game == demand.mode ? "game" : "scene"));
        data.Set("editorTarget", CommandData::Bool(demand.editorTarget));
        data.Set("gameTarget", CommandData::Bool(demand.gameTarget));
        data.Set("publishedFrames", CommandData::Int(static_cast<int64_t>(demand.publishedFrames)));
        data.Set("sceneModeFrames", CommandData::Int(static_cast<int64_t>(demand.sceneModeFrames)));
        data.Set("gameModeFrames", CommandData::Int(static_cast<int64_t>(demand.gameModeFrames)));
        data.Set("gamePreviewFrames", CommandData::Int(static_cast<int64_t>(demand.gamePreviewFrames)));
        data.Set("suppressedGameViews", CommandData::Int(static_cast<int64_t>(demand.suppressedGameViews)));
        data.Set("suppressedEditorViews", CommandData::Int(static_cast<int64_t>(demand.suppressedEditorViews)));
        // W5(계획서 §1.9): 이 커맨드가 실어야 할 값 셋 중 둘 — committed 와 input
        // owner — 이 이제 있다. 둘 다 컨트롤러가 유도한 값이고 UI 입력 상태는 Host
        // 게시본이다.
        const Editor::PlayModeStatus play = Editor::PlayModeController::Status();
        data.Set("playState", CommandData::String(Editor::PlayStateName(play.state)));
        data.Set("playCommitted", CommandData::Bool(play.committed));
        data.Set("inputOwner", CommandData::String(Editor::InputOwnerName(play.owner)));
        data.Set("uiWantCaptureMouse", CommandData::Bool(demand.uiWantCaptureMouse));
        data.Set("uiWantCaptureKeyboard", CommandData::Bool(demand.uiWantCaptureKeyboard));
        data.Set("uiWantTextInput", CommandData::Bool(demand.uiWantTextInput));
        data.Set("hostFocused", CommandData::Bool(demand.hostFocused));
        data.Set("hostHovered", CommandData::Bool(demand.hostHovered));
        data.Set("gameCanvasClicks", CommandData::Int(static_cast<int64_t>(demand.gameCanvasClicks)));

        // extent 기반 resize. 캔버스 · 배율 · 그 둘이 정한 렌더 해상도를 나란히
        // 낸다 — 밖에서 "보이는 것보다 크게 그리고 있지 않은가" 를 셀 수 있어야
        // 한다. `renderWidth/Height` 는 버스가 실제로 든 값이다(파이프라인이
        // 따라오는 데 프레임이 걸리므로 `dx12.live` 의 width/height 와 잠깐
        // 다를 수 있다).
        const auto scale = ::editor::windows::get_viewport_render_scale();
        const auto busSize = ScreenResizeBus::Get().GetSizeSnapshot();
        data.Set("canvasWidth", CommandData::Int(static_cast<int64_t>(demand.canvasWidth)));
        data.Set("canvasHeight", CommandData::Int(static_cast<int64_t>(demand.canvasHeight)));
        data.Set("renderWidth", CommandData::Int(static_cast<int64_t>(busSize.width)));
        data.Set("renderHeight", CommandData::Int(static_cast<int64_t>(busSize.height)));
        data.Set("dpiScale", CommandData::Double(demand.dpiScale));
        data.Set("renderScale", CommandData::Double(scale.applied));
        data.Set("renderScaleMode", CommandData::String(
            ::editor::windows::render_scale_mode::off == scale.mode ? "off" :
            ::editor::windows::render_scale_mode::fixed == scale.mode ? "fixed" : "auto"));
        if (!demand.hostPresent)
        {
            return Fail("editor.viewport.no_host",
                "중앙 ViewportHost 본문이 아직 한 프레임도 돌지 않았다", std::move(data));
        }
        return Ok("Viewport host", std::move(data));
    }

    /// 표시 크기보다 낮게 그리는 손잡이. Unreal 의 `Disable DPI Based Editor
    /// Viewport Scaling` 에 해당하는 자리이고 기본값은 그쪽과 같은 auto(1/DPI)다.
    // PHASE 21 W7-0 — 패널별 draw 비용.
    //
    // 재는 법이 없으면 캐시가 이득인지 증명할 수 없다. `profile.stats` 는
    // 프로파일러 **자체** 비용과 용량만 낸다. 여기서 내는 것은 패널이 프레임에서
    // 쓴 시간과 **그 프레임에 한 일의 수**다 — 캐시의 목적은 "프레임마다 하던
    // 일을 안 하는 것" 이므로 시간만으로는 기계가 빠른 날 결함이 안 보인다.
    //
    // 게시는 UI 프레임 끝(`EditorRenderer::EndRender`)이고 여기는 게임 스레드다.
    // 사본을 읽는다.
    static CommandCore::CommandResult Cmd_editor_panelcost(const ConsoleCommandContext& ctx)
    {
        using namespace CommandCore;
        const auto& args = ctx.parts;
        if (args.size() > 2 || (2 == args.size() && "reset" != args[1]))
            return InvalidArguments("editor.panelcost [reset]");
        if (2 == args.size())
        {
            ::editor::windows::reset_panel_costs();
            ::editor::windows::reset_window_costs();
        }

        const auto snapshot = ::editor::windows::read_panel_costs();
        auto data = CommandData::Object();
        data.Set("publishedFrames", CommandData::Int(static_cast<long long>(snapshot.publishedFrames)));
        auto panels = CommandData::Array();
        for (std::size_t i = 0; i < ::editor::windows::kPanelCostSlotCount; ++i)
        {
            const auto slot = static_cast<::editor::windows::panel_cost_slot>(i);
            const auto& sample = snapshot.slots[i];
            auto entry = CommandData::Object();
            entry.Set("slot", CommandData::String(::editor::windows::panel_cost_slot_name(slot)));
            entry.Set("frames", CommandData::Int(static_cast<long long>(sample.frames)));
            entry.Set("samples", CommandData::Int(static_cast<long long>(sample.samples)));
            entry.Set("lastMs", CommandData::Double(sample.lastMs));
            entry.Set("avgMs", CommandData::Double(sample.avgMs));
            entry.Set("p95Ms", CommandData::Double(sample.p95Ms));
            entry.Set("maxMs", CommandData::Double(sample.maxMs));
            entry.Set("lastUnits", CommandData::Int(static_cast<long long>(sample.lastUnits)));
            entry.Set("lastScans", CommandData::Int(static_cast<long long>(sample.lastScans)));
            // W7-5: `scans` 옆에 나란히 둔다. 둘의 뜻이 달라서다 — scans 는
            // 디렉터리를 훑은 횟수이고 probes 는 **스캔이 아닌** 디스크 접촉이다.
            // scans 0 · probes 25 가 W7-5 를 발견하게 한 그림이다.
            entry.Set("lastProbes", CommandData::Int(static_cast<long long>(sample.lastProbes)));
            entry.Set("totalUnits", CommandData::Int(static_cast<long long>(sample.totalUnits)));
            entry.Set("totalScans", CommandData::Int(static_cast<long long>(sample.totalScans)));
            entry.Set("totalProbes", CommandData::Int(static_cast<long long>(sample.totalProbes)));
            panels.Append(std::move(entry));
            Debug::PrintLog(spdlog::level::info, "[editor.panelcost] " + std::string(::editor::windows::panel_cost_slot_name(slot)) +
                " frames=" + std::to_string(sample.frames) +
                " lastMs=" + std::to_string(sample.lastMs) +
                " avgMs=" + std::to_string(sample.avgMs) +
                " p95Ms=" + std::to_string(sample.p95Ms) +
                " units=" + std::to_string(sample.lastUnits) +
                " scans=" + std::to_string(sample.lastScans) +
                " probes=" + std::to_string(sample.lastProbes));
        }
        data.Set("panels", std::move(panels));

        // ── W2-4: 창 단위 비용과 잔차 ────────────────────────────────────────
        //
        // 위의 슬롯 표는 창 **안**의 구간이라 프레임 총계와 더할 수 없다. 창 표는
        // 층위가 맞으므로 합을 총계에서 빼면 **어느 창에도 속하지 않은 비용**이
        // 남는다 — ImGui 의 NewFrame·EndFrame·Render, 도킹 갱신, 셸 자신.
        //
        // 그 잔차를 내지 않으면 "원인을 기록한다" 가 성립하지 않는다. 설명된
        // 부분만 보여 주는 표는 설명하지 못한 부분을 0 처럼 보이게 한다.
        const auto windowCosts = ::editor::windows::read_window_costs();
        auto windows = CommandData::Array();
        double explainedAvgMs = 0.0;
        double presentAvgMs = 0.0;
        double presentP95Ms = 0.0;
        for (const auto& sample : windowCosts.windows)
        {
            // `(present)` 는 총계 **밖**이다 — 그 안에 GPU 제출과 Present 가 든다.
            // 잔차에 섞으면 "UI CPU 가 설명됐다" 가 거짓이 된다. 줄은 남기되
            // 합에서 뺀다.
            const bool isPresent = (sample.id == ::editor::windows::kPresentRowId);
            auto entry = CommandData::Object();
            entry.Set("id", CommandData::String(sample.id));
            entry.Set("frames", CommandData::Int(static_cast<long long>(sample.frames)));
            entry.Set("samples", CommandData::Int(static_cast<long long>(sample.samples)));
            entry.Set("lastMs", CommandData::Double(sample.lastMs));
            entry.Set("avgMs", CommandData::Double(sample.avgMs));
            entry.Set("p95Ms", CommandData::Double(sample.p95Ms));
            entry.Set("maxMs", CommandData::Double(sample.maxMs));
            entry.Set("outsideFrameTotal", CommandData::Bool(isPresent));
            windows.Append(std::move(entry));
            if (isPresent)
            {
                presentAvgMs = sample.avgMs;
                presentP95Ms = sample.p95Ms;
            }
            else
            {
                explainedAvgMs += sample.avgMs;
            }
        }
        data.Set("windows", std::move(windows));

        auto frame = CommandData::Object();
        frame.Set("frames", CommandData::Int(static_cast<long long>(windowCosts.frame.frames)));
        frame.Set("samples", CommandData::Int(static_cast<long long>(windowCosts.frame.samples)));
        frame.Set("lastMs", CommandData::Double(windowCosts.frame.lastMs));
        frame.Set("avgMs", CommandData::Double(windowCosts.frame.avgMs));
        frame.Set("p95Ms", CommandData::Double(windowCosts.frame.p95Ms));
        frame.Set("maxMs", CommandData::Double(windowCosts.frame.maxMs));
        data.Set("frame", std::move(frame));
        data.Set("explainedAvgMs", CommandData::Double(explainedAvgMs));
        data.Set("unexplainedAvgMs",
            CommandData::Double(windowCosts.frame.avgMs - explainedAvgMs));
        data.Set("presentAvgMs", CommandData::Double(presentAvgMs));
        data.Set("presentP95Ms", CommandData::Double(presentP95Ms));

        std::printf("[editor.panelcost] frame avg=%.3f p95=%.3f · 창 %zu · 설명 %.3f · 미설명 %.3f\n",
            windowCosts.frame.avgMs, windowCosts.frame.p95Ms, windowCosts.windows.size(),
            explainedAvgMs, windowCosts.frame.avgMs - explainedAvgMs);
        std::fflush(stdout);

        return Ok({}, std::move(data));
    }

    // PHASE 21 W2-1 — 키보드 탐색 계약을 밖에서 읽고, 자극한다.
    //
    // 계획서 §7.1 은 custom widget 이 "ImGui ID, nav, focus, disabled, clipping,
    // tooltip, testability 를 보존해야 한다" 고 적는데 그 어느 절도 **밖에서 읽을
    // 수단이 없었다.** 판정문만 있고 자가 없는 상태다(W8-3 의 검증 레이어와 같은
    // 모양). 이 명령이 그 자다.
    //
    // ★ `keyboardEnabled` 를 함께 낸다. 그것이 거짓이면 아래 수가 전부 0 인 것은
    //   계약이 지켜졌다는 뜻이 아니라 **축이 없다**는 뜻이다.
    // ★ `visitedWidgets` 를 함께 낸다. 비어 있으면 nav 가 custom widget 위에
    //   한 번도 서지 않은 것이고, 그때의 "위반 0" 은 자극하지 못한 것이다.
    static CommandCore::CommandResult Cmd_editor_nav(const ConsoleCommandContext& ctx)
    {
        using namespace CommandCore;
        const auto& args = ctx.parts;
        const char* const usage = "editor.nav [reset | key <tab|up|down|left|right|enter|space|escape>... "
            "| pointer <x> <y> | press | release]";

        // W2-3: 포인터 주입. hover·active 는 이것 없이는 런타임으로 못 잰다.
        if (args.size() >= 2 && "pointer" == args[1])
        {
            if (4 != args.size()) return InvalidArguments(usage);
            try
            {
                ::editor::nav::request_pointer(::editor::nav::pointer_action::move,
                    std::stof(args[2]), std::stof(args[3]));
            }
            catch (const std::exception&)
            {
                return InvalidArguments(usage);
            }
        }
        else if (2 == args.size() && "press" == args[1])
        {
            ::editor::nav::request_pointer(::editor::nav::pointer_action::press, 0.f, 0.f);
        }
        else if (2 == args.size() && "release" == args[1])
        {
            ::editor::nav::request_pointer(::editor::nav::pointer_action::release, 0.f, 0.f);
        }
        else if (args.size() >= 2 && "key" == args[1])
        {
            if (args.size() < 3) return InvalidArguments(usage);
            std::vector<std::string> keys(args.begin() + 2, args.end());
            std::string error;
            if (!::editor::nav::request_keys(keys, error))
                return Fail("editor.nav.key", error);
        }
        else if (2 == args.size() && "reset" == args[1])
        {
            // 수만 비운다. 켜짐 여부는 이 실행의 성질이라 구간마다 달라지지 않는다.
            ::editor::nav::reset_counts();
        }
        else if (args.size() > 1)
        {
            return InvalidArguments(usage);
        }

        const ::editor::nav::contract_view view = ::editor::nav::read();
        auto data = CommandData::Object();
        data.Set("keyboardEnabled", CommandData::Bool(view.keyboardEnabled));
        data.Set("cursorVisible", CommandData::Bool(view.cursorVisible));
        data.Set("navIdIsAlive", CommandData::Bool(view.navIdIsAlive));
        data.Set("navId", CommandData::Int(static_cast<long long>(view.navId)));
        data.Set("activeId", CommandData::Int(static_cast<long long>(view.activeId)));
        data.Set("navWindow", CommandData::String(view.navWindow));
        data.Set("navWidget", CommandData::String(view.navWidget));
        data.Set("frames", CommandData::Int(static_cast<long long>(view.frames)));
        data.Set("announced", CommandData::Int(static_cast<long long>(view.announced)));
        data.Set("cursorsDrawn", CommandData::Int(static_cast<long long>(view.cursorsDrawn)));
        data.Set("silentFrames", CommandData::Int(static_cast<long long>(view.silentFrames)));
        data.Set("delegatedFrames", CommandData::Int(static_cast<long long>(view.delegatedFrames)));
        data.Set("disabledFrames", CommandData::Int(static_cast<long long>(view.disabledFrames)));
        data.Set("navCursorColor", CommandData::Int(static_cast<long long>(view.navCursorColor)));
        data.Set("keysPending", CommandData::Int(static_cast<long long>(view.keysPending)));
        data.Set("keysDelivered", CommandData::Int(static_cast<long long>(view.keysDelivered)));

        const auto names = [](const std::vector<std::string>& source)
        {
            auto array = CommandData::Array();
            for (const std::string& name : source) array.Append(CommandData::String(name));
            return array;
        };
        data.Set("visitedWidgets", names(view.visitedWidgets));
        data.Set("silentWidgets", names(view.silentWidgets));
        data.Set("disabledWidgets", names(view.disabledWidgets));

        std::printf("[editor.nav] keyboard=%s cursor=%s widget=%s window=%s "
            "frames=%llu announced=%llu cursors=%llu silent=%llu delegated=%llu disabled=%llu\n",
            view.keyboardEnabled ? "on" : "off",
            view.cursorVisible ? "visible" : "hidden",
            view.navWidget.empty() ? "-" : view.navWidget.c_str(),
            view.navWindow.empty() ? "-" : view.navWindow.c_str(),
            static_cast<unsigned long long>(view.frames),
            static_cast<unsigned long long>(view.announced),
            static_cast<unsigned long long>(view.cursorsDrawn),
            static_cast<unsigned long long>(view.silentFrames),
            static_cast<unsigned long long>(view.delegatedFrames),
            static_cast<unsigned long long>(view.disabledFrames));
        std::fflush(stdout);

        if (0 != view.silentFrames)
        {
            std::string summary = "키보드 탐색이 선 자리에 커서를 안 그린 프레임 " +
                std::to_string(view.silentFrames) + " 회";
            if (!view.silentWidgets.empty()) summary += ": " + view.silentWidgets.front();
            return Fail("editor.nav.silent_cursor", summary, std::move(data));
        }
        if (0 != view.disabledFrames)
        {
            std::string summary = "키보드 탐색이 손댈 수 없는 자리에 선 프레임 " +
                std::to_string(view.disabledFrames) + " 회";
            if (!view.disabledWidgets.empty()) summary += ": " + view.disabledWidgets.front();
            return Fail("editor.nav.disabled_stop", summary, std::move(data));
        }
        return Ok("키보드 탐색 계약 위반 0", std::move(data));
    }

    // PHASE 21 W7 — 비동기 썸네일 장부.
    //
    // 계약의 완료 판정 셋(*"동일 자산 요청이 중복되지 않는지, 변경·삭제 뒤 늦은
    // 완료가 재게시되지 않는지, 느린 로더에서 아이콘 상태로도 UI 입력이
    // 처리되는지"*)은 전부 **수를 세야** 판정할 수 있다. 화면만 봐서는 중복
    // 요청도, 폐기된 늦은 완료도 보이지 않는다.
    static CommandCore::CommandResult Cmd_editor_thumbnail(const ConsoleCommandContext& ctx)
    {
        using namespace CommandCore;
        const auto& args = ctx.parts;
        const char* const usage = "editor.thumbnail [reset | budget <bytes|default>]";

        if (2 == args.size() && "reset" == args[1]) ::editor::thumbnail_reset_stats();
        else if (3 == args.size() && "budget" == args[1])
        {
            // 예산을 낮춰 축출을 자극하는 창구. 목록이 clipper 로 보이는 타일만
            // 요청하므로 파일 수로는 기본 예산에 닿을 수 없다.
            if ("default" == args[2]) ::editor::thumbnail_set_budget_bytes(0);
            else
            {
                unsigned long long bytes = 0;
                try { bytes = std::stoull(args[2]); }
                catch (const std::exception&) { return InvalidArguments(usage); }
                ::editor::thumbnail_set_budget_bytes(bytes);
            }
        }
        else if (args.size() > 1) return InvalidArguments(usage);

        const auto stats = ::editor::thumbnail_read_stats();
        auto data = CommandData::Object();
        data.Set("requests", CommandData::Int(static_cast<long long>(stats.requests)));
        data.Set("deduped", CommandData::Int(static_cast<long long>(stats.deduped)));
        data.Set("decoded", CommandData::Int(static_cast<long long>(stats.decoded)));
        data.Set("published", CommandData::Int(static_cast<long long>(stats.published)));
        data.Set("failed", CommandData::Int(static_cast<long long>(stats.failed)));
        data.Set("lateDropped", CommandData::Int(static_cast<long long>(stats.lateDropped)));
        data.Set("invalidated", CommandData::Int(static_cast<long long>(stats.invalidated)));
        data.Set("evicted", CommandData::Int(static_cast<long long>(stats.evicted)));
        data.Set("servedThumbnails", CommandData::Int(static_cast<long long>(stats.servedThumbnails)));
        data.Set("servedIcons", CommandData::Int(static_cast<long long>(stats.servedIcons)));
        data.Set("entries", CommandData::Int(static_cast<long long>(stats.entries)));
        data.Set("queued", CommandData::Int(static_cast<long long>(stats.queued)));
        data.Set("working", CommandData::Int(static_cast<long long>(stats.working)));
        data.Set("awaitingUpload", CommandData::Int(static_cast<long long>(stats.awaitingUpload)));
        data.Set("ready", CommandData::Int(static_cast<long long>(stats.ready)));
        data.Set("bytes", CommandData::Int(static_cast<long long>(stats.bytes)));
        data.Set("budgetBytes", CommandData::Int(static_cast<long long>(stats.budgetBytes)));
        data.Set("workerPoolRunning", CommandData::Bool(stats.workerPoolRunning));

        Debug::PrintLog(spdlog::level::info, "[editor.thumbnail]"
            " requests=" + std::to_string(stats.requests) +
            " deduped=" + std::to_string(stats.deduped) +
            " decoded=" + std::to_string(stats.decoded) +
            " published=" + std::to_string(stats.published) +
            " failed=" + std::to_string(stats.failed) +
            " lateDropped=" + std::to_string(stats.lateDropped) +
            " invalidated=" + std::to_string(stats.invalidated) +
            " evicted=" + std::to_string(stats.evicted));
        Debug::PrintLog(spdlog::level::info, "[editor.thumbnail]"
            " entries=" + std::to_string(stats.entries) +
            " queued=" + std::to_string(stats.queued) +
            " working=" + std::to_string(stats.working) +
            " awaitingUpload=" + std::to_string(stats.awaitingUpload) +
            " ready=" + std::to_string(stats.ready) +
            " bytes=" + std::to_string(stats.bytes) +
            " servedThumbnails=" + std::to_string(stats.servedThumbnails) +
            " servedIcons=" + std::to_string(stats.servedIcons) +
            " workerPool=" + std::string(stats.workerPoolRunning ? "running" : "down"));

        return Ok({}, std::move(data));
    }

    static CommandCore::CommandResult Cmd_editor_clipping(const ConsoleCommandContext& ctx)
    {
        using namespace CommandCore;
        const auto& args = ctx.parts;
        const char* const usage = "editor.clipping [reset]";

        if (2 == args.size() && "reset" == args[1])
        {
            ::editor::clipping::reset_counts();
        }
        else if (args.size() > 1)
        {
            return InvalidArguments(usage);
        }

        const ::editor::clipping::contract_view view = ::editor::clipping::read();
        auto data = CommandData::Object();
        data.Set("frames", CommandData::Int(static_cast<long long>(view.frames)));
        data.Set("announced", CommandData::Int(static_cast<long long>(view.announced)));
        data.Set("overflowFrames", CommandData::Int(static_cast<long long>(view.overflowFrames)));
        data.Set("silentFrames", CommandData::Int(static_cast<long long>(view.silentFrames)));
        data.Set("truncated", CommandData::Int(static_cast<long long>(view.truncated)));
        data.Set("unbalanced", CommandData::Int(static_cast<long long>(view.unbalanced)));

        const auto names = [](const std::vector<std::string>& source)
        {
            auto array = CommandData::Array();
            for (const std::string& name : source) array.Append(CommandData::String(name));
            return array;
        };
        data.Set("measuredWidgets", names(view.measuredWidgets));
        data.Set("overflowWidgets", names(view.overflowWidgets));
        data.Set("silentWidgets", names(view.silentWidgets));
        data.Set("truncatedWidgets", names(view.truncatedWidgets));
        data.Set("unbalancedWidgets", names(view.unbalancedWidgets));

        std::printf("[editor.clipping] frames=%llu announced=%llu overflow=%llu "
            "silent=%llu truncated=%llu unbalanced=%llu\n",
            static_cast<unsigned long long>(view.frames),
            static_cast<unsigned long long>(view.announced),
            static_cast<unsigned long long>(view.overflowFrames),
            static_cast<unsigned long long>(view.silentFrames),
            static_cast<unsigned long long>(view.truncated),
            static_cast<unsigned long long>(view.unbalanced));
        std::fflush(stdout);

        if (0 != view.overflowFrames)
        {
            std::string summary = "칸보다 넓은 글자를 자르지 않은 신고 " +
                std::to_string(view.overflowFrames) + " 건";
            if (!view.overflowWidgets.empty()) summary += ": " + view.overflowWidgets.front();
            return Fail("editor.clipping.overflow", summary, std::move(data));
        }
        if (0 != view.silentFrames)
        {
            std::string summary = "잘라 놓고 전체를 돌려주지 않은 신고 " +
                std::to_string(view.silentFrames) + " 건";
            if (!view.silentWidgets.empty()) summary += ": " + view.silentWidgets.front();
            return Fail("editor.clipping.silent_truncation", summary, std::move(data));
        }
        if (0 != view.unbalanced)
        {
            std::string summary = "클립 스택이 균형을 잃은 횟수 " +
                std::to_string(view.unbalanced) + " 회";
            if (!view.unbalancedWidgets.empty()) summary += ": " + view.unbalancedWidgets.front();
            return Fail("editor.clipping.unbalanced", summary, std::move(data));
        }
        return Ok("잘라 그리기 계약 위반 0", std::move(data));
    }

    static CommandCore::CommandResult Cmd_editor_state(const ConsoleCommandContext& ctx)
    {
        using namespace CommandCore;
        const auto& args = ctx.parts;
        const char* const usage = "editor.state [reset]";

        if (2 == args.size() && "reset" == args[1])
        {
            ::editor::state::reset_counts();
        }
        else if (args.size() > 1)
        {
            return InvalidArguments(usage);
        }

        const ::editor::state::contract_view view = ::editor::state::read();

        const auto bits_to_names = [](std::uint32_t mask)
        {
            auto array = CommandData::Array();
            for (std::uint32_t bit = 1u; bit <= ::editor::state::error; bit <<= 1)
            {
                if (0 == (mask & bit)) continue;
                const char* const name = ::editor::state::name_of(bit);
                if (nullptr != name) array.Append(CommandData::String(name));
            }
            return array;
        };

        auto data = CommandData::Object();
        data.Set("frames", CommandData::Int(static_cast<long long>(view.frames)));
        data.Set("announced", CommandData::Int(static_cast<long long>(view.announced)));

        // 선언했는데 한 번도 관측되지 않은 상태. 죽은 분기이거나 자극하지
        // 못한 것이고, 둘을 가르는 것은 게이트의 몫이라 여기서는 **수와 이름**
        // 만 낸다.
        std::uint64_t unobserved = 0;
        auto widgets = CommandData::Array();
        for (const ::editor::state::widget_view& widget : view.widgets)
        {
            const std::uint32_t missing = widget.declared & ~widget.observed;
            unobserved += static_cast<std::uint64_t>(__popcnt(missing));

            auto row = CommandData::Object();
            row.Set("widget", CommandData::String(widget.widget));
            row.Set("declared", bits_to_names(widget.declared));
            row.Set("notApplicable", bits_to_names(widget.notApplicable));
            row.Set("observed", bits_to_names(widget.observed));
            row.Set("missing", bits_to_names(missing));
            row.Set("reason", CommandData::String(widget.reason));
            row.Set("frames", CommandData::Int(static_cast<long long>(widget.frames)));
            auto rect = CommandData::Array();
            rect.Append(CommandData::Double(widget.x0));
            rect.Append(CommandData::Double(widget.y0));
            rect.Append(CommandData::Double(widget.x1));
            rect.Append(CommandData::Double(widget.y1));
            row.Set("rect", std::move(rect));
            widgets.Append(std::move(row));
        }
        data.Set("widgets", std::move(widgets));
        data.Set("unobserved", CommandData::Int(static_cast<long long>(unobserved)));

        std::printf("[editor.state] frames=%llu announced=%llu widgets=%zu unobserved=%llu\n",
            static_cast<unsigned long long>(view.frames),
            static_cast<unsigned long long>(view.announced),
            view.widgets.size(),
            static_cast<unsigned long long>(unobserved));
        std::fflush(stdout);

        if (view.widgets.empty())
        {
            return Fail("editor.state.empty",
                "상태를 신고한 위젯이 하나도 없다 — 빈 표를 위반 0 으로 읽지 않는다",
                std::move(data));
        }
        return Ok("상태 행렬 " + std::to_string(view.widgets.size()) + " 위젯", std::move(data));
    }

    static CommandCore::CommandResult Cmd_editor_renderscale(const ConsoleCommandContext& ctx)
    {
        using namespace CommandCore;
        const auto& args = ctx.parts;
        if (args.size() > 2) return InvalidArguments("editor.renderscale [auto|off|<0.25-1.0>]");
        if (2 == args.size())
        {
            if ("auto" == args[1])
            {
                ::editor::windows::set_viewport_render_scale(
                    ::editor::windows::render_scale_mode::dpi_auto);
            }
            else if ("off" == args[1])
            {
                ::editor::windows::set_viewport_render_scale(
                    ::editor::windows::render_scale_mode::off);
            }
            else
            {
                char* end = nullptr;
                const double parsed = std::strtod(args[1].c_str(), &end);
                if (nullptr == end || *end != '\0' || !(parsed > 0.0))
                    return InvalidArguments("editor.renderscale [auto|off|<0.25-1.0>]");
                if (parsed < ::editor::windows::kMinViewportRenderScale ||
                    parsed > ::editor::windows::kMaxViewportRenderScale)
                {
                    return Fail("editor.renderscale.out_of_range",
                        "렌더 배율은 0.25~1.0 이다. 1 을 넘기면 보이는 것보다 크게 그린다");
                }
                ::editor::windows::set_viewport_render_scale(
                    ::editor::windows::render_scale_mode::fixed, static_cast<float>(parsed));
            }
        }
        const auto scale = ::editor::windows::get_viewport_render_scale();
        auto data = CommandData::Object();
        data.Set("mode", CommandData::String(
            ::editor::windows::render_scale_mode::off == scale.mode ? "off" :
            ::editor::windows::render_scale_mode::fixed == scale.mode ? "fixed" : "auto"));
        data.Set("applied", CommandData::Double(scale.applied));
        data.Set("fixedValue", CommandData::Double(scale.fixedValue));
        data.Set("dpiScale", CommandData::Double(scale.dpiScale));
        return Ok("Viewport render scale", std::move(data));
    }

    static CommandCore::CommandResult Cmd_editor_theme(const ConsoleCommandContext& ctx)
    {
        using namespace CommandCore;
        if (ctx.parts.size() != 1) return InvalidArguments("This command takes no arguments");

        const ::editor::chrome_snapshot snapshot = ::editor::read_chrome_snapshot();
        // 읽은 뒤 다음 한 번을 요청한다 — 살아 있는 에디터에서 같은 명령을
        // 연달아 불러 값이 바뀌는지 볼 수 있어야 한다.
        ::editor::request_chrome_snapshot();
        if (!snapshot.valid)
        {
            return Fail("editor.theme.no_frame",
                "크롬 스냅샷이 없다 — 표시 프레임이 아직 한 번도 돌지 않았다");
        }

        const ::editor::theme_audit audit = ::editor::audit_theme(snapshot);
        const std::string summary = ::editor::dump_theme_audit(snapshot);
        std::printf("%s%s%s", ::editor::dump_theme(snapshot).c_str(),
                    ::editor::dump_fonts(snapshot).c_str(), summary.c_str());

        auto data = CommandData::Object();
        data.Set("colors", CommandData::Int(static_cast<int>(audit.colors)));
        data.Set("scalars", CommandData::Int(static_cast<int>(audit.scalars)));
        data.Set("differing",
                 CommandData::Int(static_cast<int>(audit.colors_differing_from_default)));
        data.Set("fontScaleMain", CommandData::Double(audit.font_scale_main));
        data.Set("preferenceScale", CommandData::Double(audit.preference_scale));
        data.Set("styleApplied", CommandData::Bool(audit.style_applied));
        data.Set("scaleMatches", CommandData::Bool(audit.scale_matches));
        data.Set("fontScaleDpi", CommandData::Double(audit.font_scale_dpi));
        data.Set("windowDpiScale", CommandData::Double(audit.window_dpi_scale));
        data.Set("viewportDpiScale", CommandData::Double(audit.viewport_dpi_scale));
        data.Set("renderedFontSize", CommandData::Double(snapshot.rendered_font_size));
        data.Set("dpiMatches", CommandData::Bool(audit.dpi_matches));
        data.Set("perMonitorDpiAware", CommandData::Bool(snapshot.per_monitor_dpi_aware));
        data.Set("themeMappingMatches", CommandData::Bool(audit.theme_mapping_matches));
        data.Set("geometryMatches", CommandData::Bool(audit.geometry_matches));
        data.Set("missingGlyphLabels",
                 CommandData::Int(static_cast<int>(audit.labels_missing_glyphs.size())));
        // 폰트 (PHASE 21 W1). 파일이 없을 때 죽던 자리라 밖에서 보여야 한다.
        data.Set("fonts", CommandData::Int(static_cast<int>(audit.fonts)));
        data.Set("bodyFontPresent", CommandData::Bool(audit.body_font_present));
        data.Set("bodyFontUsedFallback",
                 CommandData::Bool(audit.body_font_used_fallback));
        data.Set("iconFontMerged", CommandData::Bool(audit.icon_font_merged));
        data.Set("iconRoles", CommandData::Int(static_cast<int>(audit.icon_role_count)));
        data.Set("iconSourcePolicyValid", CommandData::Bool(audit.icon_source_policy_valid));
        data.Set("missingIconRoles", CommandData::Int(static_cast<int>(audit.missing_icon_roles.size())));
        data.Set("fontFallbackProbeOk",
                 CommandData::Bool(audit.font_fallback_probe_ok));
        data.Set("clean", CommandData::Bool(audit.clean()));

        if (!audit.clean())
        {
            return Fail("editor.theme.dirty", "스타일 감사 실패: " + summary, std::move(data));
        }
        return Ok("스타일 색 " + std::to_string(audit.colors) + "개 관측, 이상 없음",
                  std::move(data));
    }

    static CommandCore::CommandResult Cmd_editor_workspace(const ConsoleCommandContext& ctx)
    {
        using namespace CommandCore;
        const auto& args=ctx.parts;
        if(args.size()>1)
        {
            editor::workspace_action action{};
            // `presets` 는 대기열에 넣을 일이 없다 — 표를 읽기만 한다.
            if(args[1]=="presets")
            {
                if(args.size()!=2) return InvalidArguments("editor.workspace presets");
                auto list=CommandData::Array();
                for(const auto& preset:editor::layout_presets())
                {
                    auto one=CommandData::Object();
                    one.Set("id",CommandData::String(std::string(preset.id)));
                    one.Set("label",CommandData::String(std::string(preset.label)));
                    one.Set("overrides",CommandData::Int(static_cast<int64_t>(preset.overrides.size())));
                    list.Append(std::move(one));
                }
                auto presetData=CommandData::Object();
                presetData.Set("presets",std::move(list));
                presetData.Set("active",CommandData::String(editor::get_workspace_status().preset));
                return Ok("Layout presets",std::move(presetData));
            }
            // `list` 도 대기열에 넣을 일이 없다 — 게시된 목록을 읽기만 한다.
            if(args[1]=="list")
            {
                if(args.size()!=2) return InvalidArguments("editor.workspace list");
                const auto listed=editor::get_workspace_status();
                auto names=CommandData::Array();
                for(const auto& name:listed.workspaces) names.Append(CommandData::String(name));
                auto listData=CommandData::Object();
                listData.Set("workspaces",std::move(names));
                listData.Set("active",CommandData::String(listed.name));
                listData.Set("named",CommandData::Bool(listed.named));
                return Ok("Named workspaces",std::move(listData));
            }
            if(args[1]=="save") action=editor::workspace_action::save;
            else if(args[1]=="load") action=editor::workspace_action::load;
            else if(args[1]=="reset") action=editor::workspace_action::reset;
            else if(args[1]=="open") action=editor::workspace_action::open_panel;
            else if(args[1]=="close") action=editor::workspace_action::close_panel;
            else if(args[1]=="preset") action=editor::workspace_action::apply_preset;
            else if(args[1]=="saveas") action=editor::workspace_action::save_as;
            else if(args[1]=="rename") action=editor::workspace_action::rename;
            else if(args[1]=="delete") action=editor::workspace_action::delete_named;
            else return InvalidArguments("editor.workspace [save|load [name]|reset|presets|preset <id>|list|saveas <name>|rename <name>|delete <name>|open <panelId>|close <panelId>]");
            // `load` 는 인자 없이 활성 파일을 다시 읽고, 이름을 주면 그 배치를 연다.
            if(action==editor::workspace_action::load && args.size()>=3)
                action=editor::workspace_action::load_named;
            const bool named=action==editor::workspace_action::save_as ||
                action==editor::workspace_action::rename ||
                action==editor::workspace_action::delete_named ||
                action==editor::workspace_action::load_named;
            const bool panel=action==editor::workspace_action::open_panel ||
                action==editor::workspace_action::close_panel ||
                action==editor::workspace_action::apply_preset;
            std::string argument;
            if(named)
            {
                if(args.size()<3) return InvalidArguments("Wrong workspace argument count");
                // 이름에는 공백이 들어간다. 파서가 공백으로 자른 조각을 도로 붙인다 —
                // 그러지 않으면 "My Layout" 을 GUI 로는 만들 수 있고 CLI 로는 못 만드는
                // 상태가 되어 같은 표면이 둘로 갈린다. 연속된 공백은 한 칸이 된다.
                argument=args[2];
                for(std::size_t part=3;part<args.size();++part) argument+=" "+args[part];
                // 쓸 수 없는 이름은 **여기서** 튕긴다. preset 과 같은 이유다 — 대기열에
                // 넣으면 실패가 다음 프레임의 status.error 로만 남아 명령이 초록이 된다.
                std::string nameError;
                if(!editor::workspace::valid_name(argument,nameError))
                    return InvalidArguments(nameError);
            }
            else
            {
                if(args.size()!=(panel?3u:2u)) return InvalidArguments("Wrong workspace argument count");
                if(panel) argument=args[2];
            }
            // 없는 preset 이름은 **여기서** 튕긴다. 대기열에 넣으면 실패가 다음
            // 프레임의 status.error 로만 남아 명령의 종료 코드가 초록이 된다.
            if(action==editor::workspace_action::apply_preset && !editor::find_layout_preset(argument))
                return InvalidArguments("Unknown layout preset: "+argument);
            // 이름 붙인 배치도 같다. 게시된 목록으로 먼저 거른다 — 여기서 막지 않으면
            // `delete 없는것` 이 "queued" 한 줄과 함께 **초록으로** 끝난다. 파일이
            // 에디터 밖에서 사라지는 경우가 남으므로 스토어 쪽 검사는 그대로 둔다.
            if(named)
            {
                const auto listed=editor::get_workspace_status();
                const bool exists=std::find(listed.workspaces.begin(),listed.workspaces.end(),argument)
                    !=listed.workspaces.end();
                if((action==editor::workspace_action::load_named ||
                    action==editor::workspace_action::delete_named) && !exists)
                    return InvalidArguments("No workspace named "+argument);
                if(action==editor::workspace_action::rename)
                {
                    if(!listed.named)
                        return PreconditionFailed("editor.workspace.unnamed",
                            "The current layout has no saved name; save it first");
                    if(exists && argument!=listed.name)
                        return InvalidArguments("A workspace named "+argument+" already exists");
                }
            }
            if(!editor::request_workspace_action(action,argument))
                return PreconditionFailed("editor.workspace.busy","A workspace operation is pending");
        }
        const auto status=editor::get_workspace_status();
        auto data=CommandData::Object();
        data.Set("ready",CommandData::Bool(status.ready));
        data.Set("pending",CommandData::Bool(status.pending));
        data.Set("recovered",CommandData::Bool(status.recovered));
        data.Set("migrated",CommandData::Bool(status.migrated));
        data.Set("path",CommandData::String(status.path));
        data.Set("preset",CommandData::String(status.preset));
        data.Set("presetLabel",CommandData::String(status.preset_label));
        data.Set("name",CommandData::String(status.name));
        data.Set("named",CommandData::Bool(status.named));
        auto workspaces=CommandData::Array();
        for(const auto& name:status.workspaces) workspaces.Append(CommandData::String(name));
        data.Set("workspaces",std::move(workspaces));
        data.Set("error",CommandData::String(status.error));
        data.Set("revision",CommandData::Int(static_cast<int64_t>(status.revision)));
        auto panels=CommandData::Object();
        for(const auto& [id,open]:status.panels) panels.Set(id,CommandData::Bool(open));
        data.Set("panels",std::move(panels));
        return Ok(status.pending?"Workspace operation queued":status.message,std::move(data));
    }

    static CommandCore::CommandResult Cmd_editor_layout(const ConsoleCommandContext& ctx)
    {
        using namespace CommandCore;
        if (ctx.parts.size() != 1) return InvalidArguments("This command takes no arguments");

        const ::editor::chrome_snapshot snapshot = ::editor::read_chrome_snapshot();
        // 읽은 뒤 다음 한 번을 요청한다 — 살아 있는 에디터에서 같은 명령을
        // 연달아 불러 값이 바뀌는지 볼 수 있어야 한다.
        ::editor::request_chrome_snapshot();
        if (!snapshot.valid)
        {
            return Fail("editor.layout.no_frame",
                "크롬 스냅샷이 없다 — 표시 프레임이 아직 한 번도 돌지 않았다");
        }

        const ::editor::layout_audit audit = ::editor::audit_layout(snapshot);
        const std::string summary = ::editor::dump_layout_audit(snapshot);
        std::printf("%s%s", ::editor::dump_layout(snapshot).c_str(), summary.c_str());

        auto data = CommandData::Object();
        data.Set("iniPath", CommandData::String(audit.ini_path));
        data.Set("iniExists", CommandData::Bool(audit.ini_exists));
        data.Set("iniEntries", CommandData::Int(static_cast<int>(audit.ini_entries)));
        data.Set("matched", CommandData::Int(static_cast<int>(audit.matched)));
        data.Set("duplicateEntries",
                 CommandData::Int(static_cast<int>(audit.duplicate_entries.size())));
        data.Set("orphanEntries",
                 CommandData::Int(static_cast<int>(audit.orphan_entries.size())));
        data.Set("clean", CommandData::Bool(audit.clean()));

        if (!audit.clean())
        {
            return Fail("editor.layout.dirty", "레이아웃 감사 실패: " + summary, std::move(data));
        }
        return Ok("ini 항목 " + std::to_string(audit.ini_entries) + "개 관측, 이상 없음",
                  std::move(data));
    }


    // 선언 배선 자가 검사 둘을 한 번에 돈다. 둘 다 자기 표를 옆으로 치우고
    // 합성 선언 위에서 돌므로 살아 있는 에디터에서 불러도 된다.
    static CommandCore::CommandResult Cmd_editor_selftest(const ConsoleCommandContext& ctx)
    {
        using namespace CommandCore;
        if (ctx.parts.size() != 1) return InvalidArguments("This command takes no arguments");

        std::string windowReport;
        const bool windowsOk = ::editor::run_editor_window_selftest(windowReport);
        std::printf("[CLI] editor.windows selftest: %s %s\n",
            windowsOk ? "PASS" : "FAIL", windowReport.c_str());

        std::string menuReport;
        const bool menusOk = ::editor::run_editor_menu_selftest(menuReport);
        std::printf("[CLI] editor.menu selftest: %s %s\n",
            menusOk ? "PASS" : "FAIL", menuReport.c_str());

        std::string themeReport;
        const bool themeOk = ::editor::RunEditorThemeSelfTest(themeReport);
        std::printf("[CLI] editor.theme selftest: %s %s\n",
            themeOk ? "PASS" : "FAIL", themeReport.c_str());

        auto data = CommandData::Object();
        data.Set("windows", CommandData::Bool(windowsOk));
        data.Set("menus", CommandData::Bool(menusOk));
        data.Set("theme", CommandData::Bool(themeOk));
        data.Set("themeReport", CommandData::String(themeReport));
        data.Set("windowReport", CommandData::String(windowReport));
        data.Set("menuReport", CommandData::String(menuReport));

        if (!windowsOk || !menusOk || !themeOk)
        {
            return Fail("editor.selftest.failed",
                        "editor:: 선언 자가 검사 실패 — 창: " + windowReport +
                        " / 메뉴: " + menuReport + " / 테마: " + themeReport, std::move(data));
        }
        return Ok("editor:: 선언 자가 검사 통과", std::move(data));
    }


    void RegisterCoreCommands(Registrar& reg)
    {
        reg.Result({ "help" }, &Cmd_help);
        reg.Result({ "commands.list" }, &Cmd_commands_list);
        reg.Result({ "commands.describe" }, &Cmd_commands_describe);
        reg.Result({ "commands.selftest" }, &Cmd_commands_selftest);
        reg.Result({ "quit", "exit" }, &Cmd_quit);
        reg.Result({ "cli.echo.args" }, &Cmd_cli_echo_args);
        reg.Result({ "cli.drain.budget" }, &Cmd_cli_drain_budget);
        reg.Result({ "game.pak" }, &Cmd_game_pak);
        reg.Result({ "wait" }, &Cmd_wait);
        reg.Result({ "window.resize" }, &Cmd_window_resize);
        reg.Result({ "window.info" }, &Cmd_window_info);
        reg.Result({ "lifecycle.trace" }, &Cmd_lifecycle_trace);
        reg.Result({ "lifecycle.registry" }, &Cmd_lifecycle_registry);
        reg.Result({ "lifecycle.dump" }, &Cmd_lifecycle_dump);
        reg.Result({ "lifecycle.stress" }, &Cmd_lifecycle_stress);
        reg.Result({ "log.flush" }, &Cmd_log_flush);
        reg.Result({ "editor.menu" }, &Cmd_editor_menu);
        reg.Result({ "editor.window" }, &Cmd_editor_window);
        reg.Result({ "editor.windows" }, &Cmd_editor_windows);
        reg.Result({ "editor.dock" }, &Cmd_editor_dock);
        reg.Result({ "editor.layout" }, &Cmd_editor_layout);
        reg.Result({ "editor.workspace" }, &Cmd_editor_workspace);
        reg.Result({ "editor.theme" }, &Cmd_editor_theme);
        reg.Result({ "editor.sceneview" }, &Cmd_editor_sceneview);
        reg.Result({ "editor.viewport" }, &Cmd_editor_viewport);
        reg.Result({ "editor.panelcost" }, &Cmd_editor_panelcost);
        reg.Result({ "editor.thumbnail" }, &Cmd_editor_thumbnail);
        reg.Result({ "editor.clipping" }, &Cmd_editor_clipping);
        reg.Result({ "editor.nav" }, &Cmd_editor_nav);
        reg.Result({ "editor.state" }, &Cmd_editor_state);
        reg.Result({ "editor.renderscale" }, &Cmd_editor_renderscale);
        reg.Result({ "editor.selftest" }, &Cmd_editor_selftest);
    }
}
