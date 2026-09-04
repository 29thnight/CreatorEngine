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
        for (std::size_t i = 0; i < CommandCore::DescriptorSeedCount(); ++i)
        {
            const CommandCore::DescriptorSeed* seed = CommandCore::DescriptorSeedAt(i);
            if (nullptr == seed) continue;
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

    // ── LC0 (PHASE 14.5) 기준선 계측 명령 ───────────────────────────────
    //
    // 셋 다 아무것도 바꾸지 않는다. 재고 찍기만 한다. 계획 §13 LC0의
    // "거동 변경 0"이 이 셋에 걸린 제약이다.

    /// commands.dump [경로]
    ///
    /// 런타임 등록 표를 TSV로 덤프한다. 소스를 긁지 않는다 — `Invoke-Dx12Suite`가
    /// C++ 리터럴을 정규식으로 뽑다가 두 번 틀린(26/35만 실행, 그리고 0/35로 읽음)
    /// 바로 그 방식을 여기서 끊는다. LC9가 그 소비자를 이 출력으로 옮긴다.

    static void Cmd_commands_dump(const ConsoleCommandContext& ctx)
    {
        const std::string requested = (ctx.parts.size() > 1) ? ctx.parts[1]
                                                             : std::string("lc0_command_inventory.tsv");
        const std::string path = ResolveTestArtifactPath("lc0", requested);

        if (!CommandBaseline::WriteInventory(path, ConsoleCommandSystem::HelpText()))
        {
            std::printf("[CLI] commands.dump 실패: %s 를 쓸 수 없다\n", path.c_str());
            return;
        }

        const auto& registrations = CommandBaseline::Registrations();
        std::size_t nameCount = 0;
        for (const auto& registration : registrations)
        {
            nameCount += 1 + registration.aliases.size();
        }

        std::printf("[CLI] commands.dump 완료 그룹=%zu 이름=%zu 경로=%s\n",
                    registrations.size(), nameCount, path.c_str());
    }

    /// cli.probe.timing [reset|off|경로]
    ///
    /// 프레임 시간 분포와 명령 왕복 지연의 **바닥값**을 낸다. §7.1의 예산표는
    /// 분모가 없는 상태로 쓰여 있다 — 이 명령이 그 분모다.
    ///
    /// 계측은 기본이 off다. `reset`이 표본을 비우고 수집을 켜고, `off`가 끈다.
    /// 상시 수집을 하지 않는 이유는 `Pump`의 주석에 있다 — 관측 비용이 관측
    /// 대상(프레임 시간)에 섞이면 그 값을 예산의 분모로 못 쓴다.

    static void Cmd_cli_probe_timing(const ConsoleCommandContext& ctx)
    {
        const std::string argument = (ctx.parts.size() > 1) ? ctx.parts[1] : std::string();

        if ("reset" == argument)
        {
            CommandBaseline::SetCollecting(true);
            std::printf("[CLI] cli.probe.timing 표본 초기화·수집 시작\n");
            return;
        }
        if ("off" == argument)
        {
            CommandBaseline::SetCollecting(false);
            std::printf("[CLI] cli.probe.timing 수집 중지\n");
            return;
        }

        if (!CommandBaseline::IsCollecting())
        {
            // 표본 0개짜리 artifact를 조용히 내지 않는다. 전부 0인 표를 받은
            // 사람은 그것을 "빠르다"로 읽는다 — 안 쟀다는 사실이 값처럼 보인다.
            std::printf("[CLI] cli.probe.timing: 수집이 꺼져 있다. "
                        "먼저 'cli.probe.timing reset' 으로 켜라\n");
            return;
        }

        const std::string requested = argument.empty() ? std::string("lc0_timing.tsv") : argument;
        const std::string path = ResolveTestArtifactPath("lc0", requested);

        if (!CommandBaseline::WriteTiming(path))
        {
            std::printf("[CLI] cli.probe.timing 실패: %s 를 온전히 쓰지 못했다\n", path.c_str());
            return;
        }
        std::printf("[CLI] cli.probe.timing 완료 경로=%s\n", path.c_str());
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

    static void Cmd_cli_echo_args(const ConsoleCommandContext& ctx)
    {
        std::printf("[CLI] cli.echo.args count=%zu line_len=%zu\n",
                    ctx.parts.size(), ctx.line.size());
        for (std::size_t i = 0; i < ctx.parts.size(); ++i)
        {
            std::printf("[CLI] cli.echo.args arg[%zu] len=%zu <<<%s>>>\n",
                        i, ctx.parts[i].size(), ctx.parts[i].c_str());
        }
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

    static void Cmd_window_resize(const ConsoleCommandContext& ctx)
    {
        const std::vector<std::string>& parts = ctx.parts;

        // 해상도 독립 검증용(PHASE 7). 창을 실제로 리사이즈해 엔진의 리사이즈 경로를
        // 그대로 태운다 — g_ClientRect 갱신부터 UI 리플로우까지 실제 흐름을 검증해야
        // 의미가 있다.
        if (parts.size() < 3)
        {
            std::printf("[CLI] 사용법: window.resize <너비> <높이>\n");
            return;
        }

        const int width = std::atoi(parts[1].c_str());
        const int height = std::atoi(parts[2].c_str());
        if (width < 320 || height < 240)
        {
            std::printf("[CLI] 너무 작은 크기입니다: %dx%d\n", width, height);
            return;
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
        if (nullptr == hwnd) { std::printf("[CLI] 창 핸들 없음\n"); return; }

        // 클라이언트 영역이 요청 크기가 되도록 창 전체 크기를 역산한다.
        RECT desired{ 0, 0, width, height };
        const LONG style = ::GetWindowLong(hwnd, GWL_STYLE);
        const LONG exStyle = ::GetWindowLong(hwnd, GWL_EXSTYLE);
        ::AdjustWindowRectEx(&desired, style, FALSE, exStyle);

        ::SetWindowPos(hwnd, nullptr, 0, 0,
            desired.right - desired.left, desired.bottom - desired.top,
            SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);

        // 요청한 크기가 그대로 적용되지 않을 수 있다(모니터보다 큰 창은 잘린다).
        // 요청값만 찍으면 검증에서 엉뚱한 기준을 잡게 되므로 실제 결과를 읽어 보고한다.
        RECT actual{};
        ::GetClientRect(hwnd, &actual);
        const int actualWidth = actual.right - actual.left;
        const int actualHeight = actual.bottom - actual.top;

        const std::string message = (actualWidth == width && actualHeight == height)
            ? "[CLI] 창 크기 변경: " + std::to_string(actualWidth) + "x" + std::to_string(actualHeight)
            : "[CLI] 창 크기 변경(클램프됨): 요청 " + std::to_string(width) + "x" + std::to_string(height) +
              " -> 실제 " + std::to_string(actualWidth) + "x" + std::to_string(actualHeight);

        Debug->LogWarning(message);
        std::printf("%s\n", message.c_str());
    }

    static void Cmd_window_info(const ConsoleCommandContext& ctx)
    {
        // 엔진이 실제로 인식하는 클라이언트 크기. window.resize가 리사이즈 경로까지
        // 도달했는지를 UI 계산과 같은 출처(화면 크기 버스)로 확인한다.
        const uint32_t clientW = ScreenResizeBus::Get().GetWidth();
        const uint32_t clientH = ScreenResizeBus::Get().GetHeight();
        std::printf("[CLI] 클라이언트 영역: %ux%u\n", clientW, clientH);
        Debug->LogWarning("[CLI] 클라이언트 영역: " +
            std::to_string(clientW) + "x" + std::to_string(clientH));
    }

    static void Cmd_lifecycle_trace(const ConsoleCommandContext& ctx)
    {
        const std::vector<std::string>& parts = ctx.parts;

        // 생명주기 호출 순서를 받아 적는다(PHASE 9-0).
        //
        // PHASE 9는 이 순서를 만들어 내는 기구를 통째로 바꾼다. 지금 순서는 델리게이트의
        // 우선순위 정렬과 등록 시점이 만드는 창발적 결과라 코드로는 알 수 없다 —
        // 교체 전에 받아 적어 두어야 교체 후 "동작이 같다"를 주장할 수 있다.
        const std::string mode = (parts.size() >= 2) ? parts[1] : "status";

        if (mode == "on")
        {
            // 틱 단계(Update·LateUpdate·FixedUpdate)를 적을 프레임 수.
            // 한 프레임 안의 순서가 알고 싶은 것이지 반복 횟수가 아니라서 예산을 둔다.
            const int frames = (parts.size() >= 3) ? std::atoi(parts[2].c_str()) : 3;
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
    }

    static void Cmd_lifecycle_registry(const ConsoleCommandContext& ctx)
    {
        // 상태 조회만 남았다(PHASE 9-3에서 델리게이트를 철거해 경로가 하나다).
        // 진단 가치는 그대로다 — 각 단계 리스트의 크기가 곧 '무엇이 매 프레임 도는가'이고,
        // 마스크 표 크기는 등록 목록이 실제로 채워졌는지를 알려 준다.
        Scene* scene = SceneManagers->GetActiveScene();
        std::printf("[CLI] lifecycle — 마스크 표 %zu종\n", Lifecycle::Registry::Count());

        if (nullptr != scene)
        {
            const auto counts = scene->GetRegistryCounts();
            std::printf("[CLI]   pendingAwake %zu · pendingStart %zu\n",
                counts.pendingAwake, counts.pendingStart);

            // 트랙 L4 래칫 측정용 — 명시 구독(Schedule().Subscribe) 대 암묵 구독
            // (RegisterComponent 경유)의 잔존 수. 통합 단계에서 배선.
            const auto subCounts = scene->GetSubscriptionCounts();
            std::printf("[CLI]   구독 잔존 — 암묵 %zu · 명시 %zu\n",
                subCounts.implicitCount, subCounts.explicitCount);
        }
    }

    static void Cmd_lifecycle_dump(const ConsoleCommandContext& ctx)
    {
        const std::vector<std::string>& parts = ctx.parts;

        const std::string path = ResolveTestArtifactPath("Traces",
            (parts.size() >= 2) ? parts[1] : std::string("lifecycle_trace.tsv"));
        const size_t count = Lifecycle::Trace::Count();

        if (0 == count)
        {
            // 빈 파일을 성공으로 흘려보내면 "기준선을 떴다"고 착각한 채 다음으로 넘어간다.
            // 3-6에서 겪은 조용한 통과와 같은 부류라 여기서 실패로 못 박는다.
            std::printf("[CLI] lifecycle.dump 실패 — 기록 0건 (lifecycle.trace on 을 먼저 부를 것)\n");
            return;
        }

        if (Lifecycle::Trace::Dump(path))
        {
            std::printf("[CLI] lifecycle.dump %s — %zu건\n", path.c_str(), count);
        }
        else
        {
            std::printf("[CLI] lifecycle.dump 실패 — 파일을 열 수 없다: %s\n", path.c_str());
        }
    }

    static void Cmd_lifecycle_stress(const ConsoleCommandContext& ctx)
    {
        const std::vector<std::string>& parts = ctx.parts;

        // 파괴·생성을 몰아쳐 수명 경로를 흔든다(PHASE 9-0의 ASan 재현용).
        //
        // 지금은 프레임 경계에서 파괴가 일어나는 경로만 흔든다. "순회 도중 파괴"와
        // "Update 안에서 AddComponent" 같은 재진입 재현은 9-1의 레지스트리가 선
        // 뒤에 붙인다 — 지금 구조에는 그 지점을 안전하게 잡을 자리가 없다.
        const std::string mode = (parts.size() >= 2) ? parts[1] : "";
        const int count = (parts.size() >= 3) ? std::atoi(parts[2].c_str()) : 8;

        Scene* scene = SceneManagers->GetActiveScene();
        if (!scene) { std::printf("[CLI] 활성 씬 없음\n"); return; }

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
            std::printf("[CLI] lifecycle.stress %s — 다음 Update 순회 한복판에서 %d건 발화\n",
                mode.c_str(), count);
        }
        else
        {
            std::printf("[CLI] lifecycle.stress destroy|churn|reentrant|reentrant-destroy|reentrant-add [개수]\n");
        }
    }

    static void Cmd_log_flush(const ConsoleCommandContext& ctx)
    {
        Log::FlushNow();
        std::printf("[CLI] 로그 flush\n");
    }

    void RegisterCoreCommands(Registrar& reg)
    {
        reg.Result({ "help" }, &Cmd_help);
        reg.Result({ "commands.list" }, &Cmd_commands_list);
        reg.Result({ "commands.describe" }, &Cmd_commands_describe);
        reg.Result({ "commands.selftest" }, &Cmd_commands_selftest);
        reg.Result({ "quit", "exit" }, &Cmd_quit);
        reg.Legacy({ "commands.dump" }, &Cmd_commands_dump);
        reg.Legacy({ "cli.probe.timing" }, &Cmd_cli_probe_timing);
        reg.Legacy({ "cli.echo.args" }, &Cmd_cli_echo_args);
        reg.Result({ "cli.drain.budget" }, &Cmd_cli_drain_budget);
        reg.Result({ "game.pak" }, &Cmd_game_pak);
        reg.Result({ "wait" }, &Cmd_wait);
        reg.Legacy({ "window.resize" }, &Cmd_window_resize);
        reg.Legacy({ "window.info" }, &Cmd_window_info);
        reg.Legacy({ "lifecycle.trace" }, &Cmd_lifecycle_trace);
        reg.Legacy({ "lifecycle.registry" }, &Cmd_lifecycle_registry);
        reg.Legacy({ "lifecycle.dump" }, &Cmd_lifecycle_dump);
        reg.Legacy({ "lifecycle.stress" }, &Cmd_lifecycle_stress);
        reg.Legacy({ "log.flush" }, &Cmd_log_flush);
    }
}
