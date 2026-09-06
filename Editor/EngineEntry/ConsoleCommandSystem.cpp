#include "Commands/CommandSupport.h"
#include "ConsoleCommandSystem.h"
#include "CommandCore/CommandSession.h" // LC1: 결과 누적과 process exit code
#include "CommandCore/CommandParser.h"
#include "CommandCore/CommandRegistry.h"       // LC3: descriptor snapshot
#include "Commands/CommandRegistrar.h"
#include "Commandlets/EditorCommandlets.h"
#include "CommandCore/CommandDescriptorSeeds.h"
#include "CommandResultJson.h"          // LC9: 배치 JSONL 과 서비스 JSON 의 단일 정본
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
// SceneManager.h는 Scene을 전방 선언만 한다. 여기서는 씬의 멤버를 훑으므로
// 완전한 형이 필요하다 — 유니티 빌드에서는 앞선 파일이 공급했다.
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
// Undo/선택 프로브(E3-2 게이트)가 쓴다. Reflection 사슬이 ReflectionUndo.h를
// 전이로 물어 주지만 그 사슬에 기대지 않는다 — 바로 아래 StringHelper.h가
// 같은 실수로 비유니티 빌드에서 깨진 적이 있다.
#include "ReflectionUndo.h"
#include "GameObjectCommand.h"
// StringToWstring. 유니티 빌드에서는 같은 청크의 EditorAssetDatabase.cpp가
// 대신 물어 줘서 보이지 않던 누락이라, 비유니티 빌드에서만 드러났다.
#include "StringHelper.h"
#include "BlackBoard.h"
#include "TagManager.h"

#include <Windows.h>
#include <psapi.h> // MBC11: assets.modelbench peak working set
#pragma comment(lib, "Psapi.lib")
#include <crtdbg.h>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <limits>
#include <random>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <DbgHelp.h>
#pragma comment(lib, "dbghelp.lib")

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

    // camera.editor follow 의 상태. 게임 스레드(App 프레임 경계와 CLI Pump)
    // 에서만 만지므로 원자성이 필요 없다 — 둘 다 같은 스레드다.
    bool g_editorCameraFollowsGame = false;

    // 앞뒤 공백 제거
    std::string TrimLine(const std::string& s)
    {
        const auto begin = s.find_first_not_of(" \t\r\n");
        if (begin == std::string::npos) return {};
        const auto end = s.find_last_not_of(" \t\r\n");
        return s.substr(begin, end - begin + 1);
    }

    // tokenizer 는 LC2 에서 CommandCore/CommandParser 로 옮겼다.
    //
    // 여기 있던 `Split()` 은 따옴표를 상태 토글로만 다뤄서 escape 가 없었고,
    // 닫히지 않은 따옴표를 조용히 통과시켰다. 무엇보다 **핸들러들이 그 결과를
    // 버리고 원문을 다시 잘랐다**(§3.2). 문법을 한 곳에 모으지 않으면 그
    // 재해석을 막을 자리가 없다.




    // 콘솔을 확보한다(GUI 앱이라 기본적으로 없다).
    //
    // 터미널에서 실행한 경우에는 그 터미널에 그대로 붙는다. Windows Terminal에서
    // 띄우면 별도 conhost 창이 뜨지 않고 그 탭에 출력된다.
    // 부모 콘솔이 없을 때만(탐색기에서 더블클릭 등) 새 콘솔을 만드는데, 이때
    // 어떤 터미널이 열리는지는 Windows 11의 "기본 터미널 앱" 설정을 따른다.
    void EnsureConsole()
    {
        if (::GetConsoleWindow() != nullptr) return;

        if (!::AttachConsole(ATTACH_PARENT_PROCESS))
        {
            if (!::AllocConsole()) return;
        }

        // 이미 파일이나 파이프로 리다이렉트된 스트림은 건드리지 않는다.
        //
        // CONOUT$로 무조건 다시 여는 코드가 `Academy_4Q.exe --exec ... > out.txt`를
        // 조용히 무력화하고 있었다 — 명령은 돌고 출력은 콘솔 창으로만 가서
        // 파일에는 아무것도 남지 않았다. 자동 검증에서는 그 출력이 결과 전부라,
        // '통과했는지 알 수 없음'과 '실패'가 구분되지 않는 상태였다.
        const auto redirected = [](DWORD stdHandle)
        {
            const HANDLE handle = ::GetStdHandle(stdHandle);
            if (nullptr == handle || INVALID_HANDLE_VALUE == handle) return false;
            const DWORD type = ::GetFileType(handle);
            return FILE_TYPE_DISK == type || FILE_TYPE_PIPE == type;
        };

        FILE* dummy = nullptr;
        if (!redirected(STD_INPUT_HANDLE))  freopen_s(&dummy, "CONIN$", "r", stdin);
        if (!redirected(STD_OUTPUT_HANDLE)) freopen_s(&dummy, "CONOUT$", "w", stdout);
        if (!redirected(STD_ERROR_HANDLE))  freopen_s(&dummy, "CONOUT$", "w", stderr);
        std::ios::sync_with_stdio(true);

        // ★ 버퍼링을 끈다.
        //
        // 씬 로드가 멈추는 것을 쫓다가 출력이 0바이트인 실행을 만났다.
        // 프로세스를 죽여도 아무것도 안 남아서, 어디까지 갔는지조차 알 수
        // 없었다 — 멈춘 자리를 찾는 일에 로그가 없는 것이 가장 나쁘다.
        //
        // 버퍼링을 끄면 느려지지만, 이 경로는 진단용 CLI라 그 대가가 싸다.
        setvbuf(stdout, nullptr, _IONBF, 0);
        setvbuf(stderr, nullptr, _IONBF, 0);

        // 로그와 명령 출력이 한글을 쓰므로 UTF-8로 맞춘다.
        // (기본 코드페이지 949에서는 소스의 UTF-8 문자열이 깨진다)
        ::SetConsoleOutputCP(CP_UTF8);
        ::SetConsoleCP(CP_UTF8);
    }
}

ConsoleCommandSystem& ConsoleCommandSystem::Get()
{
    static ConsoleCommandSystem instance;
    return instance;
}

void ConsoleCommandSystem::InitializeFromCommandLine()
{
    int argc = 0;
    LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
    if (!argv) return;

    bool wantConsole = false;

    // 표준 입력을 읽는 스레드는 --console에서만 띄운다.
    //
    // --script / --exec는 명령이 파일과 인자에서 오므로 타이핑할 사람이 없다.
    // 그런데도 예전에는 세 경우 모두 리더를 띄웠고, 그 스레드는 종료 시점에
    // getline에 갇혀 있다가 detach됐다 — 회귀 세트(전부 --script)에서만 나타난
    // 종료 구간 힙 손상(0xC0000374)의 구조적 원인이다.
    // 콘솔 자체(출력)는 세 경우 모두 필요하므로 wantConsole과는 분리한다.
    bool wantStdinReader = false;
    bool hasBatchInput = false;
    bool wantCommandService = false;

    // `--allow-user-code`. **서비스에만 걸린다**(§8 · LC7).
    //
    // ★ 배치(`--exec`·`--script`)와 stdin 은 이 플래그를 보지 않는다. 그 경로로
    //   명령을 넣는 사람은 이미 이 기계에서 이 실행 파일을 인자와 함께 띄운
    //   사람이고, 그에게 사용자 코드 호출을 한 번 더 묻는 것은 통제가 아니라
    //   의식이다. §8 이 통제를 요구하는 것은 **프로세스 경계 밖**에서 오는
    //   호출이고, 그것이 서비스다.
    bool wantUserCode = false;

    auto toUtf8 = [](const wchar_t* w) -> std::string
    {
        if (!w) return {};
        const int need = ::WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
        std::string s(need > 0 ? need - 1 : 0, '\0');
        if (need > 1) ::WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), need, nullptr, nullptr);
        return s;
    };

    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = toUtf8(argv[i]);

        if (arg == "--commandlet")
        {
            if (m_commandletMode) m_commandletError = "Only one --commandlet is allowed";
            m_commandletMode = true;
            wantConsole = true;
            m_resultJsonl = true;
            for (++i; i < argc; ++i)
            {
                std::string token = toUtf8(argv[i]);
                if (token == "--") break;
                m_commandletArguments.push_back(std::move(token));
            }
        }
        else if (arg == "--commandlet-script")
        {
            if (m_commandletMode) m_commandletError = "Only one Commandlet input is allowed";
            m_commandletMode = true;
            if (i + 1 < argc) m_commandletScript = toUtf8(argv[++i]);
            else m_commandletError = "--commandlet-script requires a file path";
            wantConsole = true;
            m_resultJsonl = true;
        }
        else if (arg == "--exec" && i + 1 < argc)
        {
            hasBatchInput = true;
            Enqueue(toUtf8(argv[++i]));
            wantConsole = true;
        }
        else if (arg == "--script" && i + 1 < argc)
        {
            hasBatchInput = true;
            LoadScriptFile(toUtf8(argv[++i]));
            wantConsole = true;
        }
        else if (arg == "--console")
        {
            wantConsole = true;
            wantStdinReader = true;
        }
        else if (arg == "--heapcheck")
        {
            EnableHeapValidation();
        }
        else if (arg == "--exec-args" && i + 1 < argc)
        {
            // `--exec-args <명령> [인자]... [--]`
            //
            // ★ 이것이 오늘 존재하는 **구조화 입력 경로**다. OS 가 이미 갈라 준
            //   argv 를 라인으로 이어 붙이지 않고 그대로 owned argument 로 쓴다.
            //   따옴표 있는 이름을 라인 경로와 구조화 경로 양쪽으로 넣어 같은
            //   invocation 이 나오는지 단정할 수 있게 됐다(§14.2).
            //
            //   `--` 로 끝낸다. 처음에는 남은 argv 를 전부 먹게 했는데, 그러면
            //   **뒤에 `--exec quit` 을 붙일 수가 없어** 무인 실행이 종료하지
            //   못하고 하네스 타임아웃까지 살아 있었다. 실제로 겪었다.
            //   `--` 가 없으면 끝까지 먹는 것은 그대로 둔다(대화형 편의).
            std::vector<std::string> arguments;
            int j = i + 1;
            for (; j < argc; ++j)
            {
                std::string token = toUtf8(argv[j]);
                if ("--" == token) { ++j; break; }
                arguments.push_back(std::move(token));
            }
            i = j - 1;

            hasBatchInput = true;
            EnqueueStructured(std::move(arguments));
            wantConsole = true;
        }
        else if (arg == "--command-service")
        {
            // ★ 기본은 off 다(§8). 켜는 것은 이 명시 플래그뿐이다.
            //
            //   서비스는 실행 표면이라, "설정 파일에 켜져 있었다"로 열리면
            //   안 된다. 실패해도 에디터는 계속 뜬다 — 서비스가 안 열린 것과
            //   에디터가 못 뜨는 것은 다른 사건이다.
            wantCommandService = true;
        }
        else if (arg == "--allow-user-code")
        {
            // ★ 서비스를 켠 것과 사용자 코드를 연 것은 다른 결정이다(§8 · LC7).
            //
            //   `--command-service` 안에는 씬을 열고 자산을 저작하는 것까지
            //   들어 있지만, 그것들은 전부 **엔진이 쓴 코드**가 하는 일이다.
            //   `script.invoke` 는 엔진이 쓰지 않은 코드를 부른다. 둘을 한
            //   플래그에 묶으면 서비스를 켜는 모든 실행이 뒤엣것에 동의한
            //   셈이 되므로, 동의를 따로 받는다.
            //
            //   이 플래그만 주고 서비스를 안 켜면 아무 일도 하지 않는다 —
            //   배치·stdin 경로는 애초에 이 플래그를 보지 않는다(아래 주석).
            wantUserCode = true;
        }
        else if (arg == "--result-format" && i + 1 < argc)
        {
            // LC9 — 배치 결과를 schema v1 JSONL 로 낸다(§18).
            //
            // ★ 오늘 형식은 `jsonl` 하나다. 그래도 값을 받는 이유는 이름이
            //   `--result-jsonl` 이 아니라 `--result-format` 이기 때문이다 —
            //   계획이 그 이름을 골랐고, 그 이름은 값이 늘어날 자리를 약속한다.
            //   모르는 값을 조용히 무시하면 오타가 "형식 없음"으로 지나간다.
            const std::string format = toUtf8(argv[++i]);
            if ("jsonl" == format) { m_resultJsonl = true; }
            else
            {
                std::fprintf(stderr, "[CLI] 알 수 없는 --result-format: %s (jsonl)\n",
                             format.c_str());
                std::fflush(stderr);

                // ★ `SetExitCode` 를 직접 부르지 않는다(§14.1).
                //
                //   여기는 명령이 아니라 **인자** 오류라 CommandResult 를 낼
                //   핸들러가 없다. 그래도 exit code 를 직접 쓰면 "쓰는 곳은
                //   session 하나" 라는 불변식이 깨지고, 뒤의 성공이 이 실패를
                //   지우는 옛 구조로 한 걸음 돌아간다. session 에 기록하면
                //   그 한 곳이 §5.4 의 2 를 정한다.
                CommandCore::CommandSession::Batch().Record("--result-format",
                    CommandCore::InvalidArguments(
                        "알 수 없는 --result-format: " + format, "args.invalid"));
                m_quitRequested.store(true, std::memory_order_release);
            }
        }
        else if (arg == "--result-file" && i + 1 < argc)
        {
            m_resultFilePath = toUtf8(argv[++i]);
        }
        else if (arg == "--fail-fast")
        {
            // 기본은 continue + aggregate 다(§3.1). 시나리오 하나가 실패해도
            // 뒤의 진단 명령이 돌아야 무엇이 왜 실패했는지 같은 실행에서 본다.
            // --fail-fast 는 그 반대를 원하는 호출자(이등분 탐색 등)를 위한 것이다.
            CommandCore::CommandSession::Batch().SetFailFast(true);
        }
    }
    ::LocalFree(argv);
    if (m_commandletMode)
    {
        if (hasBatchInput || wantStdinReader || wantCommandService)
            m_commandletError = "--commandlet cannot be combined with batch, console or HTTP service";
        wantCommandService = false;
        wantStdinReader = false;
        std::lock_guard<std::mutex> guard(m_mutex);
        m_pending.clear();
    }

    // LC4: 서비스를 켠다. 배치 프론트엔드와 독립이라 --console 과 무관하게 뜬다.
    if (wantCommandService)
    {
        // ★ 표를 **열기 전에** 채운다.
        //
        //   `--command-service` 만 준 실행에는 배치 입력이 없어서, 첫 HTTP 요청이
        //   올 때까지 `ExecuteParsed` 가 한 번도 안 돈다 = registry 가 비어 있다.
        //   그 상태로 수신 스레드를 띄우면 첫 요청의 `cost` 조회가 빗나가 Long
        //   명령이 동기로 돌고(LC5 가 존재하는 이유가 사라진다), 동시에 게임
        //   스레드가 211 개를 밀어 넣는 중인 vector 를 수신 스레드가 훑는다.
        EnsureRegistryPopulated();

        std::string error;
        if (EditorCommandService::Start(PathFinder::BaseProjectPath().string(), wantUserCode, error))
        {
            std::printf("[CLI] command service listening 127.0.0.1:%u\n",
                        static_cast<unsigned>(EditorCommandService::Port()));

            // 사용자 코드를 열었다는 것은 **로그에 남아야 한다.** 이 실행의
            // 표면이 다른 실행과 다르고, 감사에서 그 사실이 보여야 한다.
            if (wantUserCode)
            {
                std::printf("[CLI] command service: 사용자 코드 실행 허용 "
                            "(--allow-user-code)\n");
            }
        }
        else
        {
            std::fprintf(stderr, "[CLI] command service 시작 실패: %s\n", error.c_str());
            std::fflush(stderr);
        }
    }

    if (wantConsole)
    {
        // 스크립트로 돌리는 실행에서는 크래시 때 대화상자를 띄우지 않는다 —
        // 답할 사람이 없어 그대로 멈춰 있다가 덤프도 없이 죽는다.
        CoreWindow::SetUnattended(true);
        SuppressCrtDialogs();

        EnsureConsole();

        if (wantStdinReader)
        {
            std::printf("[CLI] 콘솔 명령 사용 가능. 'help' 입력.\n");
            StartStdinReader();
        }
    }

    // 스크립트를 못 열었는데 타이핑할 사람도 없다면, 이 실행은 아무것도 하지
    // 못한다. 계속 돌게 두면 하네스가 타임아웃으로 죽여야 하고 원인도 안 보인다.
    if (m_scriptLoadFailed && !wantStdinReader && !m_commandletMode)
    {
        std::fputs("[CLI] 실행할 명령이 없어 종료한다.\n", stderr);
        std::fflush(stderr);
        m_quitRequested.store(true, std::memory_order_release);
    }
}

void ConsoleCommandSystem::StartStdinReader()
{
    if (m_running.exchange(true)) return;

    // 표준 입력은 블로킹이므로 별도 스레드에서 읽고 큐에만 넣는다.
    // 실제 실행은 Pump()가 게임 스레드에서 수행한다.
    m_stdinDone = std::promise<void>{};
    m_stdinDoneFuture = m_stdinDone.get_future();

    m_stdinThread = std::thread([this]
    {
        // 어떤 경로로 빠져나가든 종료 사실을 알린다. Shutdown이 이걸 기다린다.
        struct DoneSignal
        {
            std::promise<void>& promise;
            ~DoneSignal() { promise.set_value(); }
        } signal{ m_stdinDone };

        std::string line;
        while (m_running.load(std::memory_order_acquire) && std::getline(std::cin, line))
        {
            Enqueue(line);
        }
    });
}

void ConsoleCommandSystem::LoadScriptFile(const std::string& path)
{
    std::ifstream file(path);
    if (!file)
    {
        // 로그에만 남기면 아무도 못 본다. 실행 인자를 준 쪽은 대개 자동화라
        // 콘솔을 보고 있지 않고, 명령이 하나도 없는 에디터는 quit도 받지 못한 채
        // 그냥 계속 돈다 — 하네스가 타임아웃으로 죽을 때까지. 실제로 겪었다.
        std::fprintf(stderr, "[CLI] 스크립트를 열 수 없습니다: %s\n", path.c_str());
        std::fflush(stderr);
        Debug->LogError("[CLI] 스크립트를 열 수 없습니다: " + path);

        m_scriptLoadFailed = true;
        return;
    }

    std::string line;
    while (std::getline(file, line))
    {
        const std::string trimmed = TrimLine(line);
        if (trimmed.empty() || trimmed[0] == '#') continue;   // 주석/빈 줄 무시
        Enqueue(trimmed);
    }
}

void ConsoleCommandSystem::Enqueue(std::string command)
{
    PendingCommand pending;
    pending.text          = std::move(command);
    pending.enqueuedAt    = std::chrono::steady_clock::now();
    pending.enqueuedFrame = m_frameIndex.load(std::memory_order_acquire);

    std::lock_guard<std::mutex> guard(m_mutex);
    m_pending.push_back(std::move(pending));
}

void ConsoleCommandSystem::EnqueueStructured(std::vector<std::string> arguments)
{
    EnqueueStructured(std::move(arguments), CommandCompletion{});
}

ConsoleCommandSystem::ServiceStatus ConsoleCommandSystem::SnapshotStatus() const
{
    ServiceStatus status;
    status.frame     = m_frameIndex.load(std::memory_order_acquire);
    status.executing = m_executing.load(std::memory_order_acquire);

    {
        // ★ 이 락은 실행 중에는 잡혀 있지 않다.
        //
        //   `Pump()` 는 큐에서 꺼낼 때만 잡고 곧 놓은 뒤 명령을 실행한다.
        //   그래서 `scene.load` 가 2.4초 도는 동안에도 여기서 즉시 잠긴다 —
        //   그것이 §7.3 이 성립하는 이유다.
        std::lock_guard<std::mutex> guard(m_mutex);
        status.serviceQueueDepth = m_servicePending.size();
        status.batchQueueDepth   = m_pending.size();
        if (!m_servicePending.empty())
        {
            const std::chrono::duration<double, std::milli> age =
                std::chrono::steady_clock::now() - m_servicePending.front().enqueuedAt;
            status.oldestQueuedMs = age.count();
        }
    }

    status.sceneLoading        = m_sceneLoading.load(std::memory_order_acquire);
    status.waitFramesRemaining = m_waitFramesRemaining.load(std::memory_order_acquire);

    {
        std::lock_guard<std::mutex> guard(m_statusMutex);
        status.currentCommand = m_currentCommand;
    }
    return status;
}

bool ConsoleCommandSystem::EnqueueStructured(std::vector<std::string> arguments,
                                             CommandCompletion completion,
                                             std::size_t serviceQueueCap)
{
    if (arguments.empty()) return false;

    PendingCommand pending;
    pending.completion = std::move(completion);

    // 진단용 재구성. **이 문자열은 다시 파싱되지 않는다** — 실행은 arguments 로
    // 한다. 로그와 계측이 "무엇을 불렀나"를 사람 눈으로 볼 수 있게만 만든다.
    for (const std::string& argument : arguments)
    {
        if (!pending.text.empty()) pending.text.push_back(' ');
        pending.text += argument;
    }

    pending.arguments     = std::move(arguments);
    pending.enqueuedAt    = std::chrono::steady_clock::now();
    pending.enqueuedFrame = m_frameIndex.load(std::memory_order_acquire);

    // completion 이 있으면 결과를 기다리는 사람이 있다는 뜻이고, 오늘 그것은
    // 서비스뿐이다. `--exec-args` 는 completion 없이 들어와 배치 큐로 간다.
    pending.fromService = static_cast<bool>(pending.completion);

    std::lock_guard<std::mutex> guard(m_mutex);
    if (pending.fromService)
    {
        // 상한 확인과 적재가 **같은 락 안**이다. 이것이 상한을 불변식으로
        // 만드는 유일한 배치다 — 서비스 쪽의 사전 검사는 빠른 길일 뿐이다.
        if (0 != serviceQueueCap && m_servicePending.size() >= serviceQueueCap) return false;
        m_servicePending.push_back(std::move(pending));
    }
    else
    {
        m_pending.push_back(std::move(pending));
    }
    return true;
}

void ConsoleCommandSystem::Pump()
{
    // 생명주기 기록기의 프레임 경계(PHASE 9-0).
    //
    // 여기에 두는 이유는 이 함수가 이미 "게임 스레드에서 프레임마다 정확히 한 번"이고,
    // 그 성질을 가진 자리를 새로 만들면 엔진 루프에 진단용 호출이 하나 더 늘기 때문이다.
    // 아래 조기 반환들보다 앞이어야 한다 — wait 중이거나 씬 로딩 중인 프레임도
    // 프레임이고, 그 사이에 일어난 Awake/OnDestroy가 어느 프레임 것인지 알아야 한다.
    Lifecycle::Trace::BeginFrame();

    // Count every editor frame, including frames waiting for scene activation.
    const uint64_t frameIndex = m_frameIndex.fetch_add(1, std::memory_order_acq_rel) + 1;
    const bool sceneLoading = SceneManagers->IsSceneLoading();

    // ★ 조기 반환 사유를 **밖에서 볼 수 있게 남긴다.**
    //
    //   아래 두 반환은 서비스 큐까지 통째로 멈춘다. 그 구간에는 `RunOne` 에
    //   들어가지 않으므로 `m_executing` 도 `m_currentCommand` 도 비어 있고,
    //   `/health` 는 "idle"을 낸다 — HTTP 클라이언트는 자기 요청이 씬 전환에
    //   막혀 있는 동안 한가한 서버를 본다. LC0 실측으로 이 구간은 2.4초까지
    //   간다. 멈춘 것을 한가한 것으로 내면 §7.3 이 성립하지 않는다.
    m_sceneLoading.store(sceneLoading, std::memory_order_release);
    m_waitFramesRemaining.store(static_cast<uint32_t>(m_waitFrames), std::memory_order_release);

    if (m_waitResult)
    {
        std::optional<CommandCore::CommandResult> result;
        try { result = m_waitResult(); }
        catch (const std::exception& error) { result = CommandCore::InternalError("commandlet.poll_exception", error.what()); }
        catch (...) { result = CommandCore::InternalError("commandlet.poll_exception", "Deferred command failed"); }
        if (!result) return;
        m_waitResult = {};
        auto finish = std::move(m_finishWait);
        finish(*result);
        return;
    }

    // wait 명령으로 보류 중이면 프레임만 소모한다.
    if (m_waitFrames > 0)
    {
        --m_waitFrames;
        return;
    }

    // 씬 로딩이 끝나기 전에는 다음 명령을 실행하지 않는다.
    // (전환 중 측정하면 중간값이 섞인다)
    if (sceneLoading) return;

    if (m_commandletMode && !m_commandletDone)
    {
        m_commandletDone = true;
        EnsureRegistryPopulated();
        if (!m_commandletError.empty())
        {
            const auto result = CommandCore::InvalidArguments(m_commandletError, "commandlet.mode_conflict");
            PublishResult("--commandlet", result);
            WriteResultLine("--commandlet", result, 0, 0, 0);
            RequestQuit();
            return;
        }
        if (!m_commandletScript.empty())
        {
            LoadScriptFile(m_commandletScript);
            if (m_scriptLoadFailed)
            {
                const auto result = CommandCore::InvalidArguments("Cannot open commandlet script", "commandlet.script_missing");
                PublishResult("--commandlet-script", result); WriteResultLine("--commandlet-script", result, 0, 0, 0);
                RequestQuit(); return;
            }
        }
        else if (!m_commandletArguments.empty() && CommandCore::CommandRegistry::Commandlets().Find(m_commandletArguments[0]))
            EnqueueStructured(m_commandletArguments);
        else
        {
            const auto result = EditorCommandlets::Run(m_commandletArguments);
            const auto name = m_commandletArguments.empty() ? "--commandlet" : m_commandletArguments[0];
            PublishResult(name, result);
            WriteResultLine(name, result, 0, 0, 0);
            RequestQuit();
            return;
        }
    }

    // ── 배치 큐: 프레임당 정확히 하나 (§7.2 · 기존 의미 보존) ───────────
    //
    // 이 수를 바꾸면 프레임 수로 시간을 재는 기존 시나리오의 측정값이 조용히
    // 이동한다. `wait N` 이 정확히 N 프레임이라는 전제도 여기 걸려 있다.
    {
        PendingCommand pending;
        bool           has = false;
        {
            std::lock_guard<std::mutex> guard(m_mutex);
            if (!m_pending.empty())
            {
                pending = std::move(m_pending.front());
                m_pending.pop_front();
                has = true;
            }
        }
        if (has) RunOne(std::move(pending), frameIndex);
        else if (m_commandletMode) { RequestQuit(); return; }
    }

    if (m_waitResult) return;

    // ── 서비스 큐: 예산만큼 (§7.2) ──────────────────────────────────────
    //
    // 명령 N 개가 N 프레임을 기다리지 않게 하는 자리다. 예산은 시간과 개수 둘
    // 다이고, `cost=Long` 을 만나면 이번 프레임은 그것 하나만 돈다 — 긴 명령이
    // 예산 안에서 다른 명령의 지연을 통째로 먹지 않게.
    const double      budgetMs    = m_drainTimeMs.load(std::memory_order_relaxed);
    const std::size_t budgetCount = m_drainCount.load(std::memory_order_relaxed);
    const auto        drainBegan  = std::chrono::steady_clock::now();

    for (std::size_t drained = 0; drained < budgetCount; ++drained)
    {
        if (drained > 0)
        {
            const std::chrono::duration<double, std::milli> spent =
                std::chrono::steady_clock::now() - drainBegan;
            if (spent.count() >= budgetMs) break;
        }

        PendingCommand pending;
        bool           isLong = false;
        bool           needsFrame = false;
        {
            std::lock_guard<std::mutex> guard(m_mutex);
            if (m_servicePending.empty()) break;

            // 비용을 **꺼내기 전에** 본다. 긴 명령을 만났는데 이미 이 프레임에서
            // 뭔가 돌렸다면 다음 프레임으로 미룬다.
            const std::string& name = m_servicePending.front().arguments.empty()
                ? m_servicePending.front().text
                : m_servicePending.front().arguments.front();
            if (const CommandCore::CommandDescriptor* descriptor =
                    CommandCore::CommandRegistry::Get().Find(name))
            {
                isLong = (CommandCore::CommandCost::Long == descriptor->cost);
                needsFrame = (CommandCore::CommandCost::Immediate != descriptor->cost);
            }
            if (isLong && drained > 0) break;

            pending = std::move(m_servicePending.front());
            m_servicePending.pop_front();
        }

        RunOne(std::move(pending), frameIndex);
        if (needsFrame) break; // Let frame-end lifecycle work finish before the next mutation.
    }
}

bool ConsoleCommandSystem::RunOne(PendingCommand pending, uint64_t frameIndex)
{
    const std::string line = TrimLine(pending.text);

    const auto dequeuedAt = std::chrono::steady_clock::now();

    // 실행 중임을 GT 밖에서도 볼 수 있게 찍는다(LC4). 큐 락은 이미 놓았으므로
    // 서비스는 이 명령이 오래 돌아도 상태를 읽을 수 있다.
    {
        const auto nameEnd = line.find_first_of(" \t");
        std::lock_guard<std::mutex> guard(m_statusMutex);
        m_currentCommand = line.substr(0, (nameEnd == std::string::npos) ? line.size() : nameEnd);
    }
    m_executing.store(true, std::memory_order_release);
    m_executingFromService = pending.fromService;

    // 구조화 입력은 라인 문법을 거치지 않는다(LC2). 재구성한 문자열은 진단용
    // 으로만 넘긴다 — 그것을 다시 파싱하면 §3.2 의 왕복 손실이 되살아난다.
    const CommandCore::CommandResult result = pending.IsStructured()
        ? ExecuteParsed(pending.arguments, line)
        : Execute(line);

    auto finish = [this, pending = std::move(pending), frameIndex, dequeuedAt, line](const CommandCore::CommandResult& result)
    {
        const auto finishedAt = std::chrono::steady_clock::now();
        m_executing.store(false, std::memory_order_release);
        m_executingFromService = false;
        {
            std::lock_guard<std::mutex> guard(m_statusMutex);
            m_currentCommand.clear();
        }

        // 이름만 남긴다. 인자는 경로·오브젝트 이름이 섞여 있어 계측 artifact에
        // 그대로 실으면 기계마다 다른 문자열이 들어간다.
        const std::string_view name = pending.IsStructured()
            ? std::string_view(pending.arguments[0])
            : [&line]
              {
                  const auto nameEnd = line.find_first_of(" \t");
                  return std::string_view(line).substr(
                      0, (nameEnd == std::string::npos) ? line.size() : nameEnd);
              }();

        // ★ 서비스 명령은 배치 session 에 누적하지 않는다(LC5).
        //
        //   LC4 직후에는 둘이 같은 session 을 썼다. 그래서 HTTP 로 부른
        //   `scene.load` 하나가 실패하면 **에디터 프로세스가 exit 3 으로 끝났다** —
        //   배치 시나리오는 아무 잘못이 없는데 그 판정이 뒤집힌다. 배치 session 은
        //   배치의 판정이어야 한다. 서비스 요청의 판정은 HTTP 응답으로 간다.
        if (!name.empty() && !pending.fromService)
        {
            PublishResult(std::string(name), result);
        }

        const std::chrono::duration<double, std::milli> queued   = dequeuedAt - pending.enqueuedAt;
        const std::chrono::duration<double, std::milli> executed = finishedAt - dequeuedAt;
        const uint64_t waitedFrames = (frameIndex > pending.enqueuedFrame)
            ? (frameIndex - pending.enqueuedFrame) : 0;


        // ── LC9: 배치 결과를 기계가 읽는 형태로 낸다 (§18) ──────────────────
        //
        // ★ 서비스 요청은 제외한다. 그쪽 판정은 HTTP 응답으로 가고, JSONL 은 **배치
        //   시나리오의 기록**이다. 둘을 한 파일에 섞으면 `--script` 하나를 돌린
        //   소비자가 자기가 넣지 않은 줄을 읽게 된다.
        if (!name.empty() && !pending.fromService)
        {
            WriteResultLine(std::string(name), result,
                            queued.count(), static_cast<uint32_t>(waitedFrames), executed.count());
        }

        // 결과를 기다리는 사람(LC4 의 수신 스레드)을 깨운다. **GT 에서 불린다** —
        // 여기서 오래 걸리면 다음 프레임이 밀린다. 어댑터는 값만 넘기고 곧 반환한다.
        if (pending.completion)
        {
            CommandTiming timing;
            timing.queuedMs     = queued.count();
            timing.waitedFrames = static_cast<uint32_t>(waitedFrames);
            timing.executedMs   = executed.count();
            pending.completion(result, timing);
        }
    };
    if (m_waitResult && result.IsSuccess())
    {
        m_finishWait = std::move(finish);
        return true;
    }
    m_waitResult = {};
    finish(result);
    return true;
}

void ConsoleCommandSystem::WaitForResult(std::function<std::optional<CommandCore::CommandResult>()> poll)
{
    if (!m_commandletMode || m_executingFromService || m_waitResult || !poll)
        throw std::logic_error("Deferred results require one active commandlet");
    m_waitResult = std::move(poll);
}

std::size_t ConsoleCommandSystem::ServiceQueueDepth() const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    return m_servicePending.size();
}

std::size_t ConsoleCommandSystem::BatchQueueDepth() const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    return m_pending.size();
}

void ConsoleCommandSystem::SetDrainBudget(DrainBudget budget) noexcept
{
    m_drainTimeMs.store(budget.timeMs, std::memory_order_relaxed);
    m_drainCount.store(budget.count, std::memory_order_relaxed);
}

ConsoleCommandSystem::DrainBudget ConsoleCommandSystem::GetDrainBudget() const noexcept
{
    DrainBudget budget;
    budget.timeMs = m_drainTimeMs.load(std::memory_order_relaxed);
    budget.count  = m_drainCount.load(std::memory_order_relaxed);
    return budget;
}

namespace
{
    // 리플렉션 골든 덤프(PHASE 18 CT0). 등록된 전 타입을 기본 생성해
    // Meta::Serialize 출력을 한 문서로 쓴다 — 컴파일타임 전환(CT4~CT5) 동안
    // "직렬화 출력이 한 글자도 안 변했다"를 diff 0으로 증명하는 자다.
    // 씬·프리팹 콘텐츠에 기대지 않으므로 게임 데이터가 바뀌어도 흔들리지 않는다.
    CommandCore::CommandResult HandleReflectGolden(const std::vector<std::string>& parts)
    {
        using namespace CommandCore;
        if (parts.size() > 2) return InvalidArguments("reflect.golden [path]");
        const std::string outPath = (parts.size() > 1) ? parts[1] : std::string("reflect_golden.yaml");

        auto names = Meta::Registry::GetInstance()->GetAllTypeNames();
        std::sort(names.begin(), names.end()); // unordered_map 순회 순서를 고정한다

		Authoring::WriteDocument document;
		const Authoring::WriteNode root = document.Root();
		root.SetMap();
        int serialized = 0;
        int noFactory = 0;
        int failed = 0;
        for (const auto& name : names)
        {
            // CT11-b: 이름은 정본 Type::name의 view — yaml 키로 쓸 때만 문자열화.
            const std::string key(name);
            const Meta::Type* type = Meta::Registry::GetInstance()->Find(name);
            if (nullptr == type)
            {
                continue;
            }

            // CT11: 팩토리 접합 — Type이 생성 함수를 직접 든다.
            void* instance = type->create ? type->create() : nullptr;
            if (nullptr == instance)
            {
                // 팩토리 미등록(자동 등록 경로 밖에서 Reflect만 가진 중첩 구조체 등).
                // 누락이 아니라 커버리지 한계다 — 목록으로 남겨 diff 대상에 포함한다.
				Authoring::WriteNode noFactoryNode = root.Child("__no_factory__");
				if (!noFactoryNode.Read().IsSequence()) noFactoryNode.SetSequence();
				noFactoryNode.Append().SetScalar(key);
                ++noFactory;
                continue;
            }

            try
            {
				Meta::SerializeInto(instance, *type, root.Child(key));
                ++serialized;
            }
            catch (const std::exception& e)
            {
				root.Child("__failed__").Child(key).SetScalar(e.what());
                ++failed;
            }
            // instance는 의도적으로 해제하지 않는다 — Type::create에 void*
            // 파괴 경로가 없고, 이 명령은 종료 직전 시나리오에서만 쓰인다.
        }

        std::ofstream out(outPath, std::ios::binary);
        if (!out)
        {
            std::printf("[CLI] reflect.golden: 출력 파일을 열 수 없음: %s\n", outPath.c_str());
            return Fail("reflection.write_failed", "Cannot open output: " + outPath);
        }
		out << "# reflect.golden — 등록 전 타입 default-Serialize 덤프 (PHASE 18 CT0)\n"
			<< document.Dump();
        out.close();
        if (!out) return Fail("reflection.write_failed", "Cannot write output: " + outPath);

        char line[320]{};
        std::snprintf(line, sizeof(line),
            "[reflect.golden] 타입 %zu · 직렬화 %d · 팩토리없음 %d · 실패 %d -> %s",
            names.size(), serialized, noFactory, failed, outPath.c_str());
        std::printf("[CLI] %s\n", line);
        Debug->LogWarning(line);
        auto data = CommandData::Object(); data.Set("path", CommandData::String(outPath)); data.Set("types", CommandData::Int(names.size()));
        data.Set("serialized", CommandData::Int(serialized)); data.Set("noFactory", CommandData::Int(noFactory)); data.Set("failed", CommandData::Int(failed));
        return failed == 0 && serialized > 0 ? Ok({}, std::move(data)) : Fail("reflection.golden_failed", "Default serialization failed or had no coverage", std::move(data));
    }

    // 트랜스폼 값 다이제스트 (SceneGraphRedesignPlan §4 트랙 S, S1-b 선행 게이트).
    //
    // ── 왜 이 명령이 필요한가 ──
    //
    // 역직렬화기는 **모르는 키를 조용히 무시**한다(ReflectionTypedYml.h의
    // DeserializeObjectFrom이 YAML 키를 열거하지 않고 스키마 쪽 이름으로만 당겨
    // 온다 — verify-authored-rects.ps1 머리 주석이 같은 사실을 적어 뒀다). 그래서
    // S1-b가 `m_transform`을 Entity 스키마에서 빼는 순간, 승격 경로가 한 곳이라도
    // 빠지면 그 경로로 로드된 오브젝트의 위치·회전·크기가 **에러 하나 없이 사라진다**.
    //
    // 그런데 기존 회귀 세트는 그걸 못 잡는다 — prefab_roundtrip은 인스턴스 개수만
    // 세고, 값을 실제로 대조하는 검사는 UI 전용인 authored_rects 하나뿐이다.
    // 218개 자산의 형상을 바꾸면서 탐지기가 없으면, 통과는 "안 깨졌다"가 아니라
    // "확인하지 않았다"가 된다. 이 명령이 그 자를 세운다.
    //
    // 출력은 저장·재로드 전후로 그대로 비교할 수 있게 인덱스 순서 고정·고정소수점이다.
    // 소수 4자리인 이유: 이 검사가 잡으려는 것은 값의 **소실**(0/항등으로 무너짐)이지
    // 십진 왕복의 마지막 비트 흔들림이 아니다 — 더 조이면 거짓 실패가 난다.
    CommandCore::CommandResult HandleSceneTransformDigest(const std::vector<std::string>& parts)
    {
        Scene* scene = SceneManagers->GetActiveScene();
        if (nullptr == scene)
        {
            std::printf("[CLI] 활성 씬 없음\n");
            return CommandCore::PreconditionFailed("scene.none", "활성 씬이 없다");
        }

        const std::string label = (parts.size() > 1) ? parts[1] : std::string("digest");

        size_t emitted = 0;
        uint64_t hash = 1469598103934665603ull;   // FNV-1a 64 오프셋 기저
        const auto mix = [&hash](const char* text)
        {
            for (const char* p = text; *p; ++p)
            {
                hash ^= static_cast<uint8_t>(*p);
                hash *= 1099511628211ull;
            }
        };

        for (const auto& object : scene->m_Entities)
        {
            if (!object) continue;

            // 씬 루트(인덱스 0)의 이름은 씬 파일 이름을 따라간다 — 다른 이름으로
            // 저장하면 당연히 달라지므로, 그걸 비교하면 트랜스폼 데이터가 아니라
            // 파일명을 비교하는 셈이 된다(실측: 이것 하나 때문에 68줄 중 1줄이
            // 어긋났다). 루트는 저작 오브젝트가 아니라 컨테이너이므로 이름을 고정
            // 표기로 바꾼다 — 트랜스폼 값 자체는 그대로 비교한다.
            const bool isSceneRoot = (Entity::kSceneRootIndex == object->m_index);
            const std::string displayName = isSceneRoot
                ? std::string("<scene-root>") : object->m_name.ToString();

            // S3 — UI/Canvas는 Transform을 갖지 않는다. 여기서 Transform_()를
            // 그냥 부르면 폴백 로그가 UI 개수만큼 쏟아진다. 없는 것이 정상이므로
            // 표기로 남기고 넘어간다 — 이 줄이 곧 "이 오브젝트에는 공간 데이터가
            // 없다"는 적극적 확인이기도 하다(왕복 대조에도 그대로 실린다).
            if (!object->HasTransform())
            {
                char row[256]{};
                std::snprintf(row, sizeof(row), "%u|%s|%d|<no-transform>",
                    static_cast<unsigned>(object->m_index), displayName.c_str(),
                    static_cast<int>(object->GetParentIndex()));
                mix(row);
                ++emitted;
                std::printf("[tfdigest:%s] %s\n", label.c_str(), row);
                continue;
            }

            const auto& t = object->Transform_();
			const auto& position = t.GetPositionValue();
			const auto& rotation = t.GetRotationValue();
			const auto& scale = t.GetScaleValue();
            char row[320]{};
            std::snprintf(row, sizeof(row),
                "%u|%s|%d|%.4f,%.4f,%.4f|%.4f,%.4f,%.4f,%.4f|%.4f,%.4f,%.4f",
                static_cast<unsigned>(object->m_index),
                displayName.c_str(),
                static_cast<int>(object->GetParentIndex()),
				position.x, position.y, position.z,
				rotation.x, rotation.y, rotation.z, rotation.w,
				scale.x, scale.y, scale.z);

            mix(row);
            ++emitted;
            std::printf("[tfdigest:%s] %s\n", label.c_str(), row);
        }

        char summary[192]{};
        std::snprintf(summary, sizeof(summary),
            "[tfdigest:%s] 합계 오브젝트 %zu · 해시 %016llx",
            label.c_str(), emitted, static_cast<unsigned long long>(hash));
        std::printf("%s\n", summary);
        Debug->LogWarning(summary);

        // ★ 해시를 **문자열로** 낸다. `%016llx` 로 찍는 값과 같은 표기여야 소비자가
        //   stdout 과 JSON 을 같은 값으로 읽는다. Int 로 내면 64비트 부호 없는 값이
        //   JSON 에서 음수로 보이는 경우가 생긴다.
        char hashText[24]{};
        std::snprintf(hashText, sizeof(hashText), "%016llx",
            static_cast<unsigned long long>(hash));

        CommandCore::CommandData data = CommandCore::CommandData::Object();
        data.Set("label", CommandCore::CommandData::String(label));
        data.Set("objects", CommandCore::CommandData::Int(
            static_cast<int64_t>(emitted)));
        data.Set("hash", CommandCore::CommandData::String(hashText));
        return CommandCore::Ok(summary, std::move(data));
    }

    // S4 측정 게이트 (SceneGraphRedesignPlan §4 트랙 S, S4).
    //
    // Scene::CommitRenderProxies()를 frames회 돌며 시간을 잰다. 이 단계는 매 프레임
    // 등록된 렌더 컴포넌트 **전부**에 대해 프록시 구조체를 새로 만들어 큐에 넣는다
    // — 바뀐 게 없어도 그렇다. "변경분만 커밋"이 값을 하는지 재려면 먼저 지금
    // 비용이 얼마인지 알아야 한다.
    //
    // 이 명령 혼자서는 "얼마나 빨라졌는지"를 말하지 않는다 — 최적화 전후로 같은
    // 인자로 두 번 돌려 비교하는 것이 사용법이다(미측정으로 보고할 것).

    // -- D3-b-1(SerializationPlan): ryml 에러 정책이 실제로 abort를 막는가 --
    //
    // ★ 이 검사는 "실패하면 빨개진다"가 아니라 **"정책이 없으면 크래시한다"**로
    //   이빨을 갖는다. ryml은 잘못된 문서를 만나면 예외가 아니라 프로세스를
    //   abort하므로, 콜백이 빠지거나 채널 하나를 놓치면 이 명령은 종료 코드가
    //   아니라 프로세스 사망으로 끝난다. 게이트는 그것도 실패로 읽는다.
    CommandCore::CommandResult HandleSerializeRymlError(const std::vector<std::string>& parts)
    {
        using namespace CommandCore;
        if (parts.size() > 2) return InvalidArguments("serialize.rymlerror [path]");
        // 인자가 있으면 그 파일을 **있는 그대로** ryml에 넣는다. 무엇이 실제로
        // abort를 일으키는지는 지어내지 말고 재야 한다 — 처음 만든 합성 재현
        // 둘을 ryml이 조용히 받아들였다. 프로세스가 죽으면 그것이 답이다.
        if (parts.size() >= 2)
        {
            std::ifstream input(parts[1], std::ios::binary);
            if (!input)
            {
                std::printf("[serialize.rymlerror] probe=fail reason=open-failed path=%s\n",
                    parts[1].c_str());
                return PreconditionFailed("serialization.file_missing", "Cannot open input document");
            }
            const std::string text((std::istreambuf_iterator<char>(input)),
                std::istreambuf_iterator<char>());
            const Authoring::RymlParseAttempt attempt = Authoring::TryParseWithPolicy(text);
            std::printf("[serialize.rymlerror] probe bytes=%zu parsed=%d threw=%d nodes=%llu\n",
                text.size(), attempt.parsed ? 1 : 0, attempt.threw ? 1 : 0,
                static_cast<unsigned long long>(attempt.nodeCount));
            std::printf("[serialize.rymlerror] probeMessage=%s\n",
                attempt.message.empty() ? "(none)" : attempt.message.c_str());
            auto data = CommandData::Object(); data.Set("bytes", CommandData::Int(text.size())); data.Set("parsed", CommandData::Bool(attempt.parsed));
            data.Set("threw", CommandData::Bool(attempt.threw)); data.Set("nodes", CommandData::Int(attempt.nodeCount)); data.Set("message", CommandData::String(attempt.message));
            return attempt.parsed || attempt.threw ? Ok("Parser attempt completed without abort", std::move(data)) : Fail("serialization.parse_unreported", "No parse result", std::move(data));
        }

        const Authoring::RymlErrorPolicyProbe probe = Authoring::ProbeRymlErrorPolicy();

        std::printf("[serialize.rymlerror] loneCr=%d tabIndent=%d distinctChannels=%d\n",
            probe.threwOnLoneCr ? 1 : 0,
            probe.threwOnTabIndent ? 1 : 0,
            probe.coveredDistinctChannels ? 1 : 0);
        std::printf("[serialize.rymlerror] validParsed=%d crlfParsed=%d\n",
            probe.parsedValidDocument ? 1 : 0,
            probe.parsedCrLfDocument ? 1 : 0);
        std::printf("[serialize.rymlerror] firstMessage=%s\n",
            probe.firstMessage.empty() ? "(none)" : probe.firstMessage.c_str());

        const char* fail = nullptr;
        if (!probe.threwOnLoneCr)          fail = "lone-cr-did-not-throw";
        else if (!probe.threwOnTabIndent)  fail = "tab-indent-did-not-throw";
        // 두 실패가 같은 채널을 탔다면 나머지 채널은 여전히 abort할 수 있다.
        else if (!probe.coveredDistinctChannels) fail = "single-channel-only";
        // 대조군: 정책이 모든 파싱을 막아 버린 상태를 통과로 읽지 않는다.
        else if (!probe.parsedValidDocument) fail = "valid-document-rejected";
        // CRLF가 거부되면 정규화 사본이 다시 필요하다 — 성능 전제가 바뀐다.
        else if (!probe.parsedCrLfDocument)  fail = "crlf-document-rejected";
        // 예외는 왔는데 메시지가 비면 실제 실패에서 원인을 못 읽는다.
        else if (probe.firstMessage.empty()) fail = "empty-error-message";

        std::printf("[serialize.rymlerror] selfcheck=%s%s%s\n",
            (nullptr == fail) ? "pass" : "fail",
            (nullptr == fail) ? "" : " reason=",
            (nullptr == fail) ? "" : fail);
        auto data = CommandData::Object();
        data.Set("loneCr", CommandData::Bool(probe.threwOnLoneCr)); data.Set("tabIndent", CommandData::Bool(probe.threwOnTabIndent)); data.Set("distinctChannels", CommandData::Bool(probe.coveredDistinctChannels));
        data.Set("validParsed", CommandData::Bool(probe.parsedValidDocument)); data.Set("crlfParsed", CommandData::Bool(probe.parsedCrLfDocument));
        return fail == nullptr ? Ok({}, std::move(data)) : Fail("serialization.error_policy_failed", fail, std::move(data));
    }


    // ── D3-a-1(SerializationPlan): 저작 노드 구조 비교 계약 ────────────────────
    //
    // 이 검사가 재는 것은 성능이 아니라 **판정 규칙**이다. 구조 비교는 Dump 비교의
    // 동작을 그대로 옮기지 않는다 — 맵 키 순서를 무시하는 것이 의도된 차이이므로,
    // 그 차이를 검사가 직접 단정해 "실수로 바뀐 것"과 구분한다.
    CommandCore::CommandResult HandleSerializeNodeEqual(const std::vector<std::string>& parts)
    {
        using namespace CommandCore;
        if (parts.size() != 1) return InvalidArguments("serialize.nodeequal takes no arguments");
        struct Case
        {
            const char* name;
            const char* lhs;
            const char* rhs;
            bool expectedEqual;
            bool dumpAgrees;   // Dump 비교도 같은 답을 내는가
        };

        // dumpAgrees=false인 항목이 이 슬라이스가 바꾼 동작이다. 그런 항목이 0개면
        // 구조 비교를 넣을 이유가 없었다는 뜻이므로, 아래에서 그 수도 단정한다.
        static const Case kCases[] = {
            { "scalar-same",        "a: 1",                  "a: 1",                  true,  true  },
            { "scalar-diff",        "a: 1",                  "a: 2",                  false, true  },
            { "scalar-notation",    "a: 1",                  "a: 1.0",                false, true  },
            { "seq-same",           "a: [1, 2]",             "a: [1, 2]",             true,  true  },
            { "seq-order-matters",  "a: [1, 2]",             "a: [2, 1]",             false, true  },
            { "seq-length",         "a: [1, 2]",             "a: [1, 2, 3]",          false, true  },
            { "map-key-order",      "a: {x: 1, y: 2}",       "a: {y: 2, x: 1}",       true,  false },
            { "map-style",          "a: {x: 1}",             "a:\n  x: 1",            true,  false },
            { "map-missing-key",    "a: {x: 1, y: 2}",       "a: {x: 1}",             false, true  },
            { "map-value-diff",     "a: {x: 1}",             "a: {x: 2}",             false, true  },
            { "nested-key-order",   "a: {p: {m: 1, n: 2}}",  "a: {p: {n: 2, m: 1}}",  true,  false },
            // 구조 비교는 두 null 표기를 같은 값으로 보지만 ryml emitter는 원래
            // 표기를 보존한다. yaml-cpp 은퇴 뒤에는 이 차이도 의도된 Dump divergence다.
            { "null-vs-null",       "a: ~",                  "a: null",               true,  false },
            { "null-vs-scalar",     "a: ~",                  "a: 0",                  false, true  },
            { "type-mismatch",      "a: [1]",                "a: {x: 1}",             false, true  },
        };

        int passed = 0;
        int failed = 0;
        int divergedFromDump = 0;
        for (const Case& testCase : kCases)
        {
            bool actual = false;
            bool dumpResult = false;
            try
            {
				std::string lhsError;
				std::string rhsError;
				Authoring::ParsedDocument lhsDoc =
					Authoring::ParsedDocument::ParseText(testCase.lhs, lhsError);
				Authoring::ParsedDocument rhsDoc =
					Authoring::ParsedDocument::ParseText(testCase.rhs, rhsError);
				if (!lhsDoc || !rhsDoc)
					throw std::runtime_error(lhsError.empty() ? rhsError : lhsError);
				const Authoring::ReadNode lhs = lhsDoc.Root()["a"];
				const Authoring::ReadNode rhs = rhsDoc.Root()["a"];
				actual = Authoring::NodesEqual(lhs, rhs);
                dumpResult = (lhs.Dump() == rhs.Dump());
            }
            catch (const std::exception& exception)
            {
                std::printf("[serialize.nodeequal] case=%s FAIL(parse) %s\n",
                    testCase.name, exception.what());
                ++failed;
                continue;
            }

            const bool caseOk = (actual == testCase.expectedEqual);
            // 예상한 곳에서 예상한 만큼만 Dump와 갈리는지 함께 본다.
            const bool divergenceOk =
                ((dumpResult == testCase.expectedEqual) == testCase.dumpAgrees);
            if (!testCase.dumpAgrees) ++divergedFromDump;

            if (caseOk && divergenceOk)
            {
                ++passed;
            }
            else
            {
                ++failed;
                std::printf("[serialize.nodeequal] case=%s FAIL structural=%d expected=%d dump=%d dumpAgreesExpected=%d\n",
                    testCase.name, actual ? 1 : 0, testCase.expectedEqual ? 1 : 0,
                    dumpResult ? 1 : 0, testCase.dumpAgrees ? 1 : 0);
            }
        }

        std::printf("[serialize.nodeequal] cases=%d passed=%d failed=%d divergedFromDump=%d\n",
            static_cast<int>(std::size(kCases)), passed, failed, divergedFromDump);

        if (0 == divergedFromDump)
        {
            // 전부 Dump와 같은 답이면 이 슬라이스는 아무것도 바꾸지 않은 것이다.
            std::printf("[serialize.nodeequal] selfcheck=fail reason=no-divergence-covered\n");
            return Fail("serialization.no_divergence", "No divergence covered");
        }
        std::printf("[serialize.nodeequal] selfcheck=%s\n", (0 == failed) ? "pass" : "fail");
        auto data = CommandData::Object(); data.Set("cases", CommandData::Int(std::size(kCases))); data.Set("passed", CommandData::Int(passed));
        data.Set("failed", CommandData::Int(failed)); data.Set("divergedFromDump", CommandData::Int(divergedFromDump));
        return failed == 0 ? Ok({}, std::move(data)) : Fail("serialization.node_equality_failed", "Node comparison verification failed", std::move(data));
    }

    // ── D0(SerializationPlan): 직렬화 기준선 ───────────────────────────────────
    //
    // 이 명령이 재는 것은 벤치가 재현한 모형이 아니라 제품 로드 경로 그 자체다
    // (SerializationProfiler의 Scope가 SceneManager/ComponentFactory/PrefabUtility
    // 본체에 들어 있다). dx12.encoderbench가 모형만 재고 있던 전례를 반복하지 않기
    // 위한 선택이고, 대가로 계측 플래그가 켜져 있는 동안 로드 경로에 시계 읽기가
    // 얹힌다 — 그래서 기본값은 꺼짐이고 이 명령이 켰다가 반드시 되돌린다.
    //
    // 출력은 회귀 스크립트가 파싱한다. key=value 형식을 바꾸면 게이트가 눈을 감는다.
    void PrintSerializationStage(const char* mode,
        SerializationProfile::Stage stage,
        const SerializationProfile::Snapshot& snapshot,
        int iterations)
    {
        const auto& sample = snapshot[stage];
        const double totalUs = static_cast<double>(sample.nanoseconds) / 1000.0;
        // perIterUs는 "반복 1회분"이지 "호출 1회분"이 아니다. calls는 그 회차 안에서
        // 몇 번 불렸는지를 따로 말한다(예: 씬 하나에 EntityDeserialize 68회).
        // 두 값을 같은 것으로 읽으면 단계별 비중을 통째로 오해한다.
        std::printf("[serialize.bench] mode=%s stage=%s totalUs=%.3f perIterUs=%.3f calls=%llu\n",
            mode,
            std::string(SerializationProfile::StageName(stage)).c_str(),
            totalUs,
            totalUs / static_cast<double>((std::max)(1, iterations)),
            static_cast<unsigned long long>(sample.calls));
    }

    // 부팅 구간은 CLI가 켜기 전에 끝난다 — 별도 슬롯에서 읽는다.
    void PrintSerializationBoot()
    {
        const auto boot = SerializationProfile::TakeBoot();
        const auto& catalog = boot[SerializationProfile::Stage::AssetCatalog];
        const double ms = static_cast<double>(catalog.nanoseconds) / 1'000'000.0;
        std::printf("[serialize.bench] mode=boot stage=AssetCatalog totalMs=%.3f parsedMeta=%llu\n",
            ms, static_cast<unsigned long long>(catalog.calls));
        if (0 == catalog.calls)
        {
            // 0개를 재고 "빠르다"고 보고하는 사고를 막는다.
            std::printf("[serialize.bench] mode=boot selfcheck=fail reason=parsed-meta-zero\n");
            return;
        }
        // ★ selfcheck는 언제나 독립 라인이다. 처음에는 perMetaUs와 같은 줄에 붙였는데,
        //   게이트의 `mode=X selfcheck=Y` 정규식이 이 한 줄만 놓쳐 "selfcheck 3개"로
        //   세었다 — 검사가 계약을 못 읽고 스스로 빨개진 형태다. 형식은 계약이다.
        std::printf("[serialize.bench] mode=boot perMetaUs=%.3f\n",
            (ms * 1000.0) / static_cast<double>(catalog.calls));
        std::printf("[serialize.bench] mode=boot selfcheck=pass\n");
    }

    // ★ 구성 표기는 장식이 아니다. Debug는 같은 조건에서 배 단위로 느리고 단계 간
    //   비중까지 뒤집는다 — 어떤 exe로 잰 값인지 출력이 스스로 말하게 해서, Debug
    //   수치가 기준선처럼 굳는 사고를 막는다.
    constexpr const char* kSerializeBenchConfig =
#if defined(NDEBUG)
        "Release";
#else
        "Debug";
#endif

    CommandCore::CommandResult HandleSerializeBench(const std::vector<std::string>& parts)
    {
        using namespace CommandCore;
        std::printf("[serialize.bench] config=%s\n", kSerializeBenchConfig);
        if (parts.size() < 2)
        {
            std::printf("[CLI] 사용법: serialize.bench boot\n");
            std::printf("[CLI]         serialize.bench scene <절대경로> [반복=3]\n");
            std::printf("[CLI]         serialize.bench prefab <이름|경로> [반복=3]\n");
            return InvalidArguments("serialize.bench boot | scene/prefab <target> [iterations]");
        }

        const std::string mode = parts[1];

        if ("boot" == mode)
        {
            if (parts.size() != 2) return InvalidArguments("serialize.bench boot takes no target");
            PrintSerializationBoot();
            const auto snapshot = SerializationProfile::TakeBoot();
            const auto& sample = snapshot[SerializationProfile::Stage::AssetCatalog];
            auto data = CommandData::Object(); data.Set("mode", CommandData::String("boot")); data.Set("config", CommandData::String(kSerializeBenchConfig));
            data.Set("parsedMeta", CommandData::Int(sample.calls)); data.Set("totalMs", CommandData::Double(sample.nanoseconds / 1000000.0));
            return sample.calls > 0 ? Ok({}, std::move(data)) : Fail("serialization.boot_coverage_missing", "No catalog entries measured", std::move(data));
        }

        if (parts.size() < 3)
        {
            std::printf("[CLI] serialize.bench %s: 대상이 없다\n", mode.c_str());
            return InvalidArguments("serialize.bench requires a target");
        }

        const std::string target = parts[2];
        int iterations = 3;
        if (parts.size() > 4 || (parts.size() == 4 && (!ConsoleCmd::ParseNumber(parts[3], iterations) || iterations < 1 || iterations > 10000)))
            return InvalidArguments("iterations must be 1..10000");
        const auto stageData = [iterations](const SerializationProfile::Snapshot& snapshot) {
            auto rows = CommandData::Array();
            for (uint32_t i = 0; i < SerializationProfile::kStageCount; ++i) {
                const auto stage = static_cast<SerializationProfile::Stage>(i); const auto& sample = snapshot[stage];
                auto row = CommandData::Object(); row.Set("stage", CommandData::String(std::string(SerializationProfile::StageName(stage))));
                row.Set("calls", CommandData::Int(sample.calls)); row.Set("totalUs", CommandData::Double(sample.nanoseconds / 1000.0));
                row.Set("perIterUs", CommandData::Double(sample.nanoseconds / (1000.0 * iterations))); rows.Append(std::move(row));
            }
            return rows;
        };

        if ("scene" == mode)
        {
            // 워밍업 1회는 계측을 끈 채로 돈다 — 첫 로드의 지연 초기화(프리팹 캐시,
            // 애셋 상주화)를 평균에 섞지 않기 위해서다. 워밍업을 빼면 1회차만
            // 유별나게 큰 값이 평균을 지배한다.
            SerializationProfile::SetEnabled(false);
            if (nullptr == SceneManagers->LoadSceneImmediate(target))
            {
                std::printf("[serialize.bench] mode=scene selfcheck=fail reason=warmup-load-failed target=%s\n",
                    target.c_str());
                return PreconditionFailed("scene.load_failed", "Benchmark warmup scene failed to load");
            }

            SerializationProfile::Reset();
            SerializationProfile::SetEnabled(true);
            int loaded = 0;
            for (int i = 0; i < iterations; ++i)
            {
                if (nullptr != SceneManagers->LoadSceneImmediate(target))
                    ++loaded;
            }
            SerializationProfile::SetEnabled(false);

            const auto snapshot = SerializationProfile::Take();
            std::printf("[serialize.bench] mode=scene target=%s iterations=%d loaded=%d warmup=1\n",
                target.c_str(), iterations, loaded);

            using Stage = SerializationProfile::Stage;
            for (uint32_t i = 0; i < SerializationProfile::kStageCount; ++i)
            {
                const Stage stage = static_cast<Stage>(i);
                if (Stage::AssetCatalog == stage) continue; // 부팅 전용 슬롯
                PrintSerializationStage("scene", stage, snapshot, iterations);
            }

            // ★ 자를 먼저 검증한다. 분해 합이 루트를 넘으면 계측 자체가 틀린 것이고,
            //   그 경우 아래 수치는 어떤 판정에도 쓸 수 없다.
            const double rootUs =
                static_cast<double>(snapshot[Stage::SceneLoadTotal].nanoseconds) / 1000.0;
            double childUs = 0.0;
            bool everyChildRan = true;
            for (uint32_t i = 0; i < SerializationProfile::kStageCount; ++i)
            {
                const Stage stage = static_cast<Stage>(i);
                if (!SerializationProfile::IsSceneLoadChild(stage)) continue;
                childUs += static_cast<double>(snapshot[stage].nanoseconds) / 1000.0;
                if (0 == snapshot[stage].calls) everyChildRan = false;
            }

            const double unattributedUs = rootUs - childUs;
            std::printf("[serialize.bench] mode=scene rootUs=%.3f childSumUs=%.3f unattributedUs=%.3f childRatio=%.4f\n",
                rootUs, childUs, unattributedUs,
                (rootUs > 0.0) ? (childUs / rootUs) : 0.0);

            const char* failReason = nullptr;
            if (loaded != iterations)                             failReason = "load-count-mismatch";
            else if (snapshot[Stage::SceneLoadTotal].calls !=
                     static_cast<uint64_t>(iterations))           failReason = "root-call-mismatch";
            else if (!everyChildRan)                              failReason = "child-stage-zero-calls";
            else if (childUs > rootUs)                            failReason = "child-sum-exceeds-root";
            else if (rootUs <= 0.0)                               failReason = "root-zero-time";

            if (nullptr != failReason)
                std::printf("[serialize.bench] mode=scene selfcheck=fail reason=%s\n", failReason);
            else
                std::printf("[serialize.bench] mode=scene selfcheck=pass\n");
            auto data = CommandData::Object(); data.Set("mode", CommandData::String(mode)); data.Set("config", CommandData::String(kSerializeBenchConfig));
            data.Set("target", CommandData::String(target)); data.Set("iterations", CommandData::Int(iterations)); data.Set("loaded", CommandData::Int(loaded));
            data.Set("stages", stageData(snapshot)); data.Set("rootUs", CommandData::Double(rootUs)); data.Set("childSumUs", CommandData::Double(childUs));
            return failReason == nullptr ? Ok({}, std::move(data)) : Fail("serialization.benchmark_failed", failReason, std::move(data));
        }

        if ("prefab" == mode)
        {
            SerializationProfile::Reset();
            SerializationProfile::SetEnabled(true);

            // 캐시 미스는 최초 1회뿐이다 — 그 사실을 숨기지 않고 cacheMiss로 보고한다.
            Prefab* prefab = PrefabUtilitys->LoadPrefab(target);
            const auto afterLoad = SerializationProfile::Take();
            if (nullptr == prefab)
            {
                SerializationProfile::SetEnabled(false);
                std::printf("[serialize.bench] mode=prefab selfcheck=fail reason=load-failed target=%s\n",
                    target.c_str());
                return PreconditionFailed("prefab.not_found", "Benchmark prefab failed to load");
            }

            int instantiated = 0;
            for (int i = 0; i < iterations; ++i)
            {
                if (nullptr != PrefabUtilitys->InstantiatePrefab(
                        prefab, target + "_d0bench" + std::to_string(i)))
                    ++instantiated;
            }
            SerializationProfile::SetEnabled(false);

            const auto snapshot = SerializationProfile::Take();
            using Stage = SerializationProfile::Stage;
            // parseOnLoad는 LoadPrefab이 문서를 실제로 읽은 횟수(캐시 미스), nestedParse는
            // 소환 도중 중첩 프리팹 정의를 추가로 읽은 횟수다. 둘을 합쳐 cacheMiss 하나로
            // 보고했더니 "1이라고 했는데 2가 찍힌" 모순으로 보였다 — 계측이 틀린 게 아니라
            // 소환이 leaf 정의를 더 읽은 것이었고, 그 사실이 보이는 편이 옳다.
            const unsigned long long parseOnLoad =
                static_cast<unsigned long long>(afterLoad[Stage::PrefabParse].calls);
            const unsigned long long totalParse =
                static_cast<unsigned long long>(snapshot[Stage::PrefabParse].calls);
            std::printf("[serialize.bench] mode=prefab target=%s iterations=%d instantiated=%d parseOnLoad=%llu nestedParse=%llu\n",
                target.c_str(), iterations, instantiated,
                parseOnLoad,
                (totalParse >= parseOnLoad) ? (totalParse - parseOnLoad) : 0ull);
            PrintSerializationStage("prefab", Stage::PrefabParse, snapshot, 1);
            PrintSerializationStage("prefab", Stage::PrefabInstantiate, snapshot, iterations);
            PrintSerializationStage("prefab", Stage::ComponentLoad, snapshot, iterations);

            const char* failReason = nullptr;
            if (instantiated != iterations)                              failReason = "instantiate-count-mismatch";
            else if (snapshot[Stage::PrefabInstantiate].calls !=
                     static_cast<uint64_t>(iterations))                  failReason = "instantiate-call-mismatch";
            else if (0 == snapshot[Stage::PrefabInstantiate].nanoseconds) failReason = "instantiate-zero-time";
            // ★ 이 분기는 변이 실험이 열었다. ComponentLoad 계측을 제거한 변이본에서
            //   scene 모드는 fail을 냈는데 prefab 모드는 그대로 통과했다 — 소환은
            //   반드시 컴포넌트를 붙이므로 0 calls는 계측이 끊어졌다는 뜻이다.
            else if (0 == snapshot[Stage::ComponentLoad].calls)          failReason = "component-load-zero-calls";

            if (nullptr != failReason)
                std::printf("[serialize.bench] mode=prefab selfcheck=fail reason=%s\n", failReason);
            else
                std::printf("[serialize.bench] mode=prefab selfcheck=pass\n");
            auto data = CommandData::Object(); data.Set("mode", CommandData::String(mode)); data.Set("config", CommandData::String(kSerializeBenchConfig));
            data.Set("target", CommandData::String(target)); data.Set("iterations", CommandData::Int(iterations)); data.Set("instantiated", CommandData::Int(instantiated));
            data.Set("stages", stageData(snapshot)); data.Set("parseOnLoad", CommandData::Int(parseOnLoad)); data.Set("nestedParse", CommandData::Int(totalParse >= parseOnLoad ? totalParse - parseOnLoad : 0));
            return failReason == nullptr ? Ok({}, std::move(data)) : Fail("serialization.benchmark_failed", failReason, std::move(data));
        }

        std::printf("[CLI] serialize.bench: 알 수 없는 모드 '%s'\n", mode.c_str());
        return InvalidArguments("Unknown serialization benchmark mode");
    }

	CommandCore::CommandResult HandleSceneProxyBench(const std::vector<std::string>& parts)
    {
        if (parts.size() < 2)
        {
            std::printf("[CLI] 사용법: scene.proxybench <프레임수> [등록수]\n");
            return CommandCore::InvalidArguments(
                "scene.proxybench: <프레임수> [등록수] 가 필요하다");
        }

        const int frames = (std::max)(1, std::atoi(parts[1].c_str()));

        Scene* scene = SceneManagers->GetActiveScene();
        if (nullptr == scene)
        {
            std::printf("[CLI] 활성 씬 없음\n");
            return CommandCore::PreconditionFailed("scene.none", "활성 씬이 없다");
        }

		const size_t requestedCount = parts.size() > 2
			? static_cast<size_t>((std::max)(0, std::atoi(parts[2].c_str()))) : 0u;
		std::vector<Entity*> synthetic;
		size_t componentCount = scene->RenderProxyComponentCount();
		if (requestedCount > componentCount)
		{
			synthetic.reserve(requestedCount - componentCount);
			for (size_t index = componentCount; index < requestedCount; ++index)
			{
				Entity* owner = scene->CreateEntity(
					"__proxybench_x8_" + std::to_string(index), GameObjectType::Empty);
				if (!owner || !owner->AddComponent<MeshRenderer>()) break;
				synthetic.push_back(owner);
			}
			scene->DrainPendingLifecycle();
			componentCount = scene->RenderProxyComponentCount();
		}
        if (0 == componentCount)
        {
            // 0개를 재고 "빠르다"고 보고하는 사고를 막는다 — 하한 가드가 없으면
            // 빈 씬에서 측정이 눈을 감는다(회귀 세트의 README 원칙과 같은 이유).
            std::printf("[CLI] scene.proxybench: 등록된 렌더 컴포넌트가 0개다 — 잴 것이 없다\n");
            // 잴 것이 없는 것은 이 기계/씬의 상태이지 벤치의 실패가 아니다.
            return CommandCore::PreconditionFailed("scene.proxybench.empty",
                "등록된 렌더 컴포넌트가 0개다");
        }

        using PerfClock = std::chrono::steady_clock;
        scene->CommitRenderProxies();   // 워밍업 1회(첫 호출의 지연 초기화를 평균에서 뺀다)
		scene->ResetRenderProxyCommitMetrics();

        std::vector<double> samplesUs;
        samplesUs.reserve(static_cast<size_t>(frames));
        for (int f = 0; f < frames; ++f)
        {
            const auto t0 = PerfClock::now();
            scene->CommitRenderProxies();
            const auto t1 = PerfClock::now();
            samplesUs.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
        }

        double sum = 0.0;
        double minUs = samplesUs.front();
        double maxUs = samplesUs.front();
        for (double v : samplesUs)
        {
            sum += v;
            minUs = (std::min)(minUs, v);
            maxUs = (std::max)(maxUs, v);
        }
        const double avg = sum / static_cast<double>(samplesUs.size());
		const RenderProxyCommitMetrics metrics = scene->GetRenderProxyCommitMetrics();

        char line[256]{};
        std::snprintf(line, sizeof(line),
			"[scene.proxybench] mode=stationary registered=%zu frames=%d avg=%.2fus min=%.2fus max=%.2fus committed=%llu selfcheck=%s",
			componentCount, frames, avg, minUs, maxUs,
			static_cast<unsigned long long>(metrics.committed),
			0 == metrics.committed ? "PASS" : "FAIL");
        std::printf("%s\n", line);
        Debug->LogWarning(line);
		for (Entity* owner : synthetic)
		{
			if (owner) owner->Destroy();
		}

        // ★ selfcheck 가 이 벤치의 판정이다. `committed` 가 0 이 아니면 정지 씬에서
        //   프록시가 커밋됐다는 뜻이고, 그러면 잰 값이 무엇의 값인지 알 수 없다.
        const bool selfcheck = (0 == metrics.committed);

        CommandCore::CommandData data = CommandCore::CommandData::Object();
        data.Set("registered", CommandCore::CommandData::Int(
            static_cast<int64_t>(componentCount)));
        data.Set("frames", CommandCore::CommandData::Int(frames));
        data.Set("avgUs", CommandCore::CommandData::Double(avg));
        data.Set("committed", CommandCore::CommandData::Int(
            static_cast<int64_t>(metrics.committed)));
        data.Set("selfcheck", CommandCore::CommandData::Bool(selfcheck));
        if (!selfcheck)
        {
            return CommandCore::Fail("scene.proxybench.selfcheck",
                "정지 씬인데 프록시가 커밋됐다(selfcheck=FAIL)", std::move(data));
        }
        return CommandCore::Ok("scene.proxybench", std::move(data));
    }

    // S2 A/B 토글의 유일한 쓰기 지점(SceneGraphRedesignPlan §4 트랙 S, S2 —
    // Scene::SetDirtyTraversalEnabled). 인자 없이 부르면 현재값만 보여준다.
    // ── LC9 서명 이행 · scene 도메인 ────────────────────────────────────
    //
    // ★ **핸들러가 찍는 줄은 한 글자도 바꾸지 않는다.**
    //
    //   이 도메인의 probe 출력은 오늘 `verify-transform-*.ps1`·
    //   `verify-render-proxy-dirty.ps1` 여섯이 정규식으로 읽고 있다(LC9 의
    //   소비자 래칫에 그대로 세어져 있다). 서명을 바꾸면서 문안까지 손대면
    //   그 여섯이 한꺼번에 붉어지고, 붉어진 이유가 "이행이 잘못됐다"인지
    //   "문안이 달라졌다"인지 가릴 수 없다. 이행과 문안 정리는 다른 슬라이스다
    //   (§12.3 의 "파일 분리와 서명 이행을 같은 커밋에 넣지 않는다"와 같은 이유).
    //
    // ★★ **다만 stdout 이 통째로 불변인 것은 아니다.** 실패·precondition 결과에는
    //   `PublishResult` 가 `[CLI] <명령> <status> (<code>) <메시지>` 한 줄을 덧붙인다.
    //   이행 전에는 이 도메인이 전부 `LegacyUnreported` 라 그 줄이 나오지 않았다.
    //   덧붙는 줄이지 고쳐 쓰는 줄이 아니므로 기존 정규식은 그대로 맞고, 실측으로
    //   확인했다 — 여섯 게이트의 판정이 이행 전후로 같다.
    //
    // ★★★ 이 이행이 **더하는** 것은 하나다: 지금까지 `printf` 에만 있던 판정이
    //   `CommandResult` 로도 나온다. 그 값이 session 을 거쳐 배치 종료 코드가 되고,
    //   `--result-format jsonl` 로 기계가 읽는다. 예전에는 `probe=FAIL` 을 찍고도
    //   프로세스가 0 으로 끝났다 — 세어 놓고 판정하지 않고 있었다.

    // ★ `scene.dirtytraversal` 과 `scene.bonecache` 를 하나로 합쳤다(2026-09-06).
    //
    //   둘은 **순수 boolean get/set** 이었고, 33 줄과 31 줄이 접근자 쌍과 문자열
    //   넷을 빼면 줄 단위로 같았다 — 조작 하나에 64 줄의 near-duplicate 다.
    //   이 도메인에서 진짜 토글은 이 둘뿐이다(`transformstats` ·
    //   `transformwritestats` · `sparseresolver` 는 `0|1` **분기만** 공유하고
    //   명령 자체는 실질 내용이 있다).
    //
    //   `scene.traversalbench` 로 접지 않은 이유: 그 벤치는 이 플래그들을 **읽어
    //   헤더에 찍는다**(`dirtytraversal=%s bonecache=%s`). 벤치 전용 인자로 만들면
    //   다른 명령을 이 플래그 아래에서 재 볼 방법이 사라진다. 플래그는 프로세스
    //   전역이고 벤치만의 것이 아니다.
    //
    // ★★ 인자 없이 부르는 것은 오류가 아니라 **조회**다. 조회를 InvalidArguments
    //   로 내면 상태를 물어본 실행이 exit 2 로 끝난다.
    CommandCore::CommandResult HandleSceneFlag(const std::vector<std::string>& parts)
    {
        struct Flag
        {
            const char* name;
            bool (*get)();
            void (*set)(bool);
            const char* onText;
            const char* offText;
        };

        static constexpr Flag kFlags[] = {
            { "dirtytraversal", &Scene::IsDirtyTraversalEnabled, &Scene::SetDirtyTraversalEnabled,
              "1(dirty·worldChanged만 재계산)", "0(항상 재계산 — 옛 경로, A/B 대조용)" },
            { "bonecache", &Scene::IsBoneCacheEnabled, &Scene::SetBoneCacheEnabled,
              "1(인덱스 캐시)", "0(매 프레임 FindBone — 옛 경로)" },
        };

        const auto names = []() {
            std::string all;
            for (const Flag& f : kFlags)
            {
                if (!all.empty()) all += "|";
                all += f.name;
            }
            return all;
        };

        if (parts.size() < 2)
        {
            // 이름을 안 주면 전부 찍는다. "지금 무엇이 켜져 있나" 가 이 명령에
            // 물을 수 있는 가장 흔한 질문이다.
            CommandCore::CommandData data = CommandCore::CommandData::Object();
            for (const Flag& f : kFlags)
            {
                const bool on = f.get();
                std::printf("[CLI] scene.flag %s = %s\n", f.name, on ? f.onText : f.offText);
                data.Set(f.name, CommandCore::CommandData::Bool(on));
            }
            return CommandCore::Ok("scene.flag 현재값", std::move(data));
        }

        const Flag* found = nullptr;
        for (const Flag& f : kFlags)
        {
            if (parts[1] == f.name) { found = &f; break; }
        }
        if (nullptr == found)
        {
            std::printf("[CLI] 사용법: scene.flag <%s> [0|1]\n", names().c_str());
            return CommandCore::InvalidArguments(
                "scene.flag: 알 수 없는 플래그 '" + parts[1] + "' (가능: " + names() + ")");
        }

        if (parts.size() < 3)
        {
            const bool on = found->get();
            std::printf("[CLI] scene.flag %s = %s\n", found->name, on ? found->onText : found->offText);
            CommandCore::CommandData data = CommandCore::CommandData::Object();
            data.Set("flag", CommandCore::CommandData::String(found->name));
            data.Set("enabled", CommandCore::CommandData::Bool(on));
            return CommandCore::Ok(std::string("scene.flag ") + found->name + " 현재값", std::move(data));
        }

        const bool enable  = ("1" == parts[2] || "on"  == parts[2] || "true"  == parts[2]);
        const bool disable = ("0" == parts[2] || "off" == parts[2] || "false" == parts[2]);
        if (!enable && !disable)
        {
            std::printf("[CLI] 사용법: scene.flag <%s> [0|1]\n", names().c_str());
            return CommandCore::InvalidArguments("scene.flag: 0|1 이 필요하다");
        }

        found->set(enable);
        const std::string msg = std::string("[scene.flag] ") + found->name + " = "
            + (enable ? found->onText : found->offText);
        Debug->LogWarning(msg);
        std::printf("[CLI] %s\n", msg.c_str());

        CommandCore::CommandData data = CommandCore::CommandData::Object();
        data.Set("flag", CommandCore::CommandData::String(found->name));
        data.Set("enabled", CommandCore::CommandData::Bool(enable));
        return CommandCore::Ok(msg, std::move(data));
    }

    // 뼈 이름 조회와 순회 도달성을 한 화면에 놓고 대조하는 진단(트랙 E, E7-b 후속).
    //
    // ── 왜 이 명령이 필요했나 ──
    //
    // scene.traversalbench 0이 "분기 도달 61개 · 인덱스 해석 0개"를 찍었다. 그 한 줄만
    // 보면 Skeleton::FindBone이 이름을 못 찾는 것으로 읽힌다 — FindBone은 못 찾는 것을
    // 정상 경로로 취급해 로그를 한 줄도 안 남기니 확인할 방법도 없었다. 실제로는
    // 이름이 61/61 전부 맞았고, 진짜 원인은 그 위였다: 모델 루트가 씬 루트의
    // m_childrenIndices에서 빠져 AllUpdateWorldMatrix가 서브트리를 통째로 건너뛰고
    // 있었다(SceneManager::RemapLoadBatchIndices — 거기 주석에 경위를 적어 뒀다).
    //
    // 그래서 이 명령은 둘을 **나란히** 찍는다. 이름 조회(FindBone 직접 호출)와
    // 순회 도달성(조상 사슬이 전부 부모의 children에 실려 있는가)은 서로 다른
    // 고장이고, 어느 하나만 보면 반드시 오진한다.
    CommandCore::CommandResult HandleSceneBoneDump(const std::vector<std::string>& parts)
    {
        Scene* scene = SceneManagers->GetActiveScene();
        if (nullptr == scene)
        {
            std::printf("[CLI] 활성 씬 없음\n");
            return CommandCore::PreconditionFailed("scene.none", "활성 씬이 없다");
        }

        const int limit = (parts.size() >= 2) ? (std::max)(0, std::atoi(parts[1].c_str())) : 8;

        // 눈에 안 보이는 차이(앞뒤 공백·제어문자·비ASCII)를 드러내는 표기 — 이름
        // 불일치의 형태를 확정하려면 바이트가 보여야 한다.
        auto quote = [](const std::string& s)
        {
            std::string out = "\"";
            for (unsigned char ch : s)
            {
                if (ch >= 0x20 && ch < 0x7F) { out += static_cast<char>(ch); }
                else { char buf[8]{}; std::snprintf(buf, sizeof(buf), "<%02X>", ch); out += buf; }
            }
            out += "\"";
            return out;
        };

        // 이 노드가 AllUpdateWorldMatrix의 순회에 실제로 닿는가. 순회는
        // m_Entities[0]->m_childrenIndices에서만 내려가므로, 조상 사슬의
        // **모든** 고리가 "부모의 children에 내가 실려 있다"를 만족해야 한다.
        // m_parentIndex만 따라 올라가는 검사는 이번 결함을 통과시킨다(부모 포인터는
        // 멀쩡했고 부모의 목록에서만 빠져 있었다) — 반드시 목록 쪽을 본다.
        auto brokenLinkOf = [&](Entity::Index start) -> Entity::Index
        {
            Entity::Index cur = start;
            for (int hop = 0; hop < 256; ++hop)
            {
                const auto& node = scene->TryGetEntity(cur);
                if (!node) return cur;
                if (Entity::kSceneRootIndex == node->m_index) return Entity::INVALID_INDEX;

                const Entity::Index parentIndex = node->GetParentIndex();
                const auto& parentObj = scene->TryGetEntity(parentIndex);
                if (!parentObj) return cur;
                const auto& sib = parentObj->GetChildrenIndices();
                if (std::find(sib.begin(), sib.end(), node->m_index) == sib.end()) return cur;

                if (parentIndex == cur) return cur;
                cur = parentIndex;
            }
            return start;
        };

        // I6-B3 — 덤프 대상을 legacy 스켈레톤이 아니라 Animator로 든다.
        // 이 진단은 게이트 소비자가 0이지만 손으로 도달성을 볼 때 쓰는
        // 살아 있는 표면이라 폐기가 아니라 이관이다(I6-A의 판정과 다르다 —
        // 그쪽은 legacy 왕복 자체가 목적인 진단이었다).
        Animator* dumpedAnimator = nullptr;
        size_t boneObjectCount = 0;
        size_t hitCount = 0;
        size_t emptyTagCount = 0;
        size_t noTransformCount = 0;
        size_t unreachableCount = 0;
        size_t cachedIndexCount = 0;
        int shownNameFail = 0;
        int shownUnreachable = 0;

        for (const auto& obj : scene->m_Entities)
        {
            if (!obj || obj->IsDestroyMark()) continue;
            BoneComponent* bc = obj->GetComponent<BoneComponent>();
            if (!bc) continue;

            ++boneObjectCount;
            if (!obj->HasTransform()) ++noTransformCount;
            if (bc->GetResolvedBoneIndex() >= 0) ++cachedIndexCount;

            const Entity::Index broken = brokenLinkOf(obj->m_index);
            if (Entity::IsValidIndex(broken))
            {
                ++unreachableCount;
                if (shownUnreachable < limit)
                {
                    ++shownUnreachable;
                    const auto& badNode = scene->TryGetEntity(broken);
                    const auto& badParent = badNode ? scene->TryGetEntity(badNode->GetParentIndex()) : nullptr;
                    std::printf("[scene.bonedump] 순회 미도달 — \"%s\"의 조상 idx=%d(\"%s\")가 부모 idx=%d(\"%s\")의 children에 없다\n",
                        obj->GetHashedName().ToString().c_str(), static_cast<int>(broken),
                        badNode ? badNode->GetHashedName().ToString().c_str() : "?",
                        badNode ? static_cast<int>(badNode->GetParentIndex()) : -1,
                        badParent ? badParent->GetHashedName().ToString().c_str() : "?");
                }
            }

            const auto& rootObj = scene->TryGetEntity(obj->GetRootIndex());
            if (!rootObj) continue;
            const auto& animator = rootObj->GetComponent<Animator>();
            if (!animator || 0 == animator->GetSkeletonSerial()) continue;

            if (!dumpedAnimator) dumpedAnimator = animator;

            const std::string& tag = obj->RemoveSuffixNumberTag();
            if (tag.empty()) ++emptyTagCount;
            if (animator->ResolveBoneIndex(tag) >= 0)
            {
                ++hitCount;
            }
            else if (shownNameFail < limit)
            {
                ++shownNameFail;
                std::printf("[scene.bonedump] 이름 불일치 — idx=%d m_name=%s tag=%s(len=%zu) -> FindBone 실패\n",
                    static_cast<int>(obj->m_index),
                    quote(obj->GetHashedName().ToString()).c_str(),
                    quote(tag).c_str(), tag.size());
            }
        }

        std::printf("[scene.bonedump] 뼈 마커 %zu개 · 이름 조회 적중 %zu개 · tag 빈 문자열 %zu개\n",
            boneObjectCount, hitCount, emptyTagCount);
        std::printf("[scene.bonedump] Transform없음 %zu개 · 순회 미도달 %zu개 · 캐시된 인덱스 %zu개 (bonecache=%s)\n",
            noTransformCount, unreachableCount, cachedIndexCount, Scene::IsBoneCacheEnabled() ? "1" : "0");

        if (dumpedAnimator)
        {
            // m_boneMap은 계수에서 뺐다 — FindBone이 정본이고 그 맵은 아무도
            // 읽지 않는 죽은 필드다(실측). 신원 축은 experiment면 generation,
            // 아니면 legacy serial이다(GetSkeletonSerial 주석 참조).
            bool viaExperiment = false;
            const size_t boneCount = dumpedAnimator->GetBoneCount(&viaExperiment);
            std::printf("[scene.bonedump] 스켈레톤 serial=%llu · 뼈 %zu개 (출처=%s)\n",
                static_cast<unsigned long long>(dumpedAnimator->GetSkeletonSerial()),
                boneCount, viaExperiment ? "experiment" : "legacy");

            int printed = 0;
            for (size_t index = 0; index < boneCount; ++index)
            {
                if (printed >= limit) break;
                ++printed;
                const std::string name =
                    dumpedAnimator->GetBoneName(static_cast<int>(index));
                if (name.empty())
                {
                    std::printf("[scene.bonedump]   뼈[%d] = 이름 없음\n", printed - 1);
                    continue;
                }
                std::printf("[scene.bonedump]   뼈[%d] index=%zu name=%s(len=%zu)\n",
                    printed - 1, index, quote(name).c_str(), name.size());
            }
        }
        else
        {
            std::printf("[scene.bonedump] 스켈레톤에 도달한 뼈 오브젝트가 없다 — 관문 앞에서 끊겼다\n");

            // ★ **판정이 아니라 조회다.** 이 명령은 오진을 막으려고 만든 진단이고
            //   (이름 조회와 순회 도달성을 나란히 찍는다), "도달한 뼈가 없다" 는
            //   그 진단이 답해야 할 상태이지 이 명령의 실패가 아니다. 실패로 내면
            //   스켈레톤 없는 씬에서 이 진단을 부르는 것만으로 배치가 붉어진다.
            return CommandCore::Ok("scene.bonedump — 도달한 뼈 오브젝트 0");
        }

        return CommandCore::Ok("scene.bonedump");
    }

    // 진단용(트랙 P · P4-a 게이트) — prefab.objectguid.
    //
    // ★ else-if 명령 사슬이 아니라 조기 디스패치로 둔 이유: 그 사슬이 이미
    // MSVC의 블록 중첩 상한에 닿아 있어(C1061, 실측) 한 줄만 더해도 컴파일이
    // 깨진다. scene.dirtytraversal·scene.bonecache 등이 쓰는 것과 같은 관례로
    // 함수로 빼고 Execute 앞머리에서 return한다.
    CommandCore::CommandResult HandlePrefabObjectGuid(const std::vector<std::string>& parts)
    {
        // 진단용(트랙 P · P4-a 게이트). prefab.status의 집계 수치(씬 인스턴스/
        // 등록 개수)만으로는 "그 값이 맞는 값인가"를 볼 수 없다 — 중첩 프리팹
        // 스탬핑 버그는 개수를 바꾸지 않고 값만 바꾼다(중첩 루트가 바깥 프리팹의
        // guid로 덮인다. 결함 2). 이 명령은 이름으로 찾은 오브젝트의
        // m_prefabFileGuid를 그대로 문자열로 찍어, 회귀 스크립트가 특정 노드의
        // guid가 "자기 자신의 것"인지 "바깥 것으로 덮였는지"를 값 단위로 대조할
        // 수 있게 한다.
        //
        // Debug->LogWarning은 인메모리·HTML 싱크로만 가고 회귀가 읽는 stdout에는
        // 안 나온다(오늘 실제로 물린 함정) — 그래서 std::printf로 찍는다.
        if (parts.size() < 2)
        {
            std::printf("[CLI] 사용법: prefab.objectguid <오브젝트 이름>\n");
            return CommandCore::InvalidArguments("prefab.objectguid: <오브젝트 이름> 이 필요하다");
        }

        Scene* scene = SceneManagers->GetActiveScene();
        if (!scene)
        {
            // ★ `prefab.objectguid` 는 아직 legacy 서명이다(prefab 도메인 몫).
            //   LC9 의 scene 이행이 이 자리를 함께 바꿀 뻔했는데, 도메인이 다르면
            //   같은 커밋에 넣지 않는다 — 그 원칙이 없으면 "scene 이행" 커밋에
            //   prefab 거동 변경이 섞여 들어간다.
            std::printf("[CLI] 활성 씬 없음\n");
            return CommandCore::PreconditionFailed("prefab.objectguid.no_scene", "활성 씬이 없다");
        }

        auto object = scene->GetEntity(parts[1]);
        if (!object)
        {
            std::printf("[CLI] 오브젝트를 찾을 수 없음: %s\n", parts[1].c_str());
            return CommandCore::Fail("prefab.objectguid.not_found", "오브젝트가 없다: " + parts[1]);
        }

        const std::string guidStr = object->m_prefabFileGuid.ToString();
        std::printf("[CLI] [prefab.objectguid] %s guid=%s\n", parts[1].c_str(), guidStr.c_str());

        CommandCore::CommandData data = CommandCore::CommandData::Object();
        data.Set("object", CommandCore::CommandData::String(parts[1]));
        data.Set("guid", CommandCore::CommandData::String(guidStr));
        return CommandCore::Ok("prefab.objectguid", std::move(data));
        }

    namespace
    {
        struct BenchPercentiles
        {
            double median = 0.0;
            double p95 = 0.0;
            double maximum = 0.0;
        };

        BenchPercentiles SummarizeBenchSamples(std::vector<double> samples)
        {
            if (samples.empty()) return {};
            std::sort(samples.begin(), samples.end());
            const size_t count = samples.size();
            const double median = (0 != count % 2)
                ? samples[count / 2]
                : (samples[count / 2 - 1] + samples[count / 2]) * 0.5;
            const size_t p95Index = (std::min)(count - 1,
                (count * 95 + 99) / 100 - 1);
            return BenchPercentiles{ median, samples[p95Index], samples.back() };
        }

        const char* TransformSyncPointName(TransformSyncPoint syncPoint)
        {
            switch (syncPoint)
            {
            case TransformSyncPoint::FixedUpdate: return "fixed";
            case TransformSyncPoint::PreUpdate: return "pre-update";
            case TransformSyncPoint::LateUpdate: return "late-update";
            case TransformSyncPoint::SceneLoad: return "scene-load";
            case TransformSyncPoint::Benchmark: return "benchmark";
            default: return "unspecified";
            }
        }

		const char* TransformBuildConfiguration()
		{
#if defined(_DEBUG)
			return "Debug";
#else
			return "Release";
#endif
		}

        void PrintTransformMetricSnapshot(const TransformUpdateMetrics& metrics)
        {
            const uint64_t transformCount = metrics.transformOnlyCount
                + metrics.transformAndRectCount;
            const uint64_t rectCount = metrics.rectOnlyCount
                + metrics.transformAndRectCount;
            const double transformDirtyPercent = 0 == transformCount ? 0.0
                : 100.0 * static_cast<double>(metrics.transformDirtyCount)
                    / static_cast<double>(transformCount);
            const double rectDirtyPercent = 0 == rectCount ? 0.0
                : 100.0 * static_cast<double>(metrics.rectDirtyCount)
                    / static_cast<double>(rectCount);

            std::printf(
                "[scene.transformstats] sync=%s domains=UI+Spatial "
                "gate ui=%s spatial=%s "
                "wall-us total=%.2f ui=%.2f spatial=%.2f dispatch=%.2f\n",
                TransformSyncPointName(metrics.syncPoint),
                metrics.uiDomainResolved ? "run" : "empty",
                metrics.spatialDomainResolved ? "run" : "empty",
                metrics.totalUs, metrics.uiUs, metrics.spatialUs, metrics.dispatchUs);
            std::printf(
                "[scene.transformstats] spatial-worker-sum-us "
                "visit=%.2f compose=%.2f multiply=%.2f decompose=%.2f "
                "counts visit=%llu compose=%llu multiply=%llu decompose=%llu roots=%llu\n",
                metrics.visitWorkerUs, metrics.localComposeWorkerUs,
                metrics.worldMultiplyWorkerUs, metrics.decomposeWorkerUs,
                static_cast<unsigned long long>(metrics.spatialVisitCount),
                static_cast<unsigned long long>(metrics.localComposeCount),
                static_cast<unsigned long long>(metrics.worldMultiplyCount),
                static_cast<unsigned long long>(metrics.decomposeCount),
                static_cast<unsigned long long>(metrics.rootDispatchCount));
            std::printf(
                "[scene.transformstats] census entities=%llu transform-only=%llu "
                "rect-only=%llu both=%llu neither=%llu "
                "transform-dirty=%llu(%.2f%%) rect-dirty=%llu(%.2f%%)\n",
                static_cast<unsigned long long>(metrics.entityCount),
                static_cast<unsigned long long>(metrics.transformOnlyCount),
                static_cast<unsigned long long>(metrics.rectOnlyCount),
                static_cast<unsigned long long>(metrics.transformAndRectCount),
                static_cast<unsigned long long>(metrics.neitherCount),
                static_cast<unsigned long long>(metrics.transformDirtyCount),
                transformDirtyPercent,
                static_cast<unsigned long long>(metrics.rectDirtyCount),
                rectDirtyPercent);
        }
    }

    CommandCore::CommandResult HandleSceneTransformStats(const std::vector<std::string>& parts)
    {
        Scene* scene = SceneManagers->GetActiveScene();
        if (nullptr == scene)
        {
            std::printf("[CLI] 활성 씬 없음\n");
            return CommandCore::PreconditionFailed("scene.none", "활성 씬이 없다");
        }

        if (parts.size() > 1 && ("0" == parts[1] || "1" == parts[1]))
        {
            const bool enabled = "1" == parts[1];
            Scene::SetTransformDiagnosticsEnabled(enabled);
            if (enabled) scene->ResetTransformDiagnostics();
            std::printf("[scene.transformstats] enabled=%d build=%s; 노드별 clock/atomic 계측은 ON 동안만 발생\n",
                enabled ? 1 : 0, TransformBuildConfiguration());

            CommandCore::CommandData data = CommandCore::CommandData::Object();
            data.Set("enabled", CommandCore::CommandData::Bool(enabled));
            return CommandCore::Ok("scene.transformstats 계측 토글", std::move(data));
        }

        std::printf("[scene.transformstats] enabled=%d build=%s; worker-sum은 병렬 CPU 합계라 wall에 더하지 말 것\n",
            Scene::IsTransformDiagnosticsEnabled() ? 1 : 0,
			TransformBuildConfiguration());
        for (TransformSyncPoint point : {
            TransformSyncPoint::FixedUpdate,
            TransformSyncPoint::PreUpdate,
            TransformSyncPoint::LateUpdate,
            TransformSyncPoint::SceneLoad,
            TransformSyncPoint::Benchmark })
        {
            const TransformUpdateMetrics& metrics =
                scene->GetLastTransformUpdateMetrics(point);
            if (metrics.totalUs > 0.0) PrintTransformMetricSnapshot(metrics);
        }

        const TransformTopologyMutationCounters frame =
            scene->GetLastFrameTopologyMutations();
		const TransformTopologyMutationCounters window =
			scene->GetTransformDiagnosticTopologyMutations();
		const uint64_t windowFrames = scene->GetTransformDiagnosticFrameCount();
        const TransformTopologyMutationCounters total =
            scene->GetTopologyMutationTotals();
        std::printf(
            "[scene.transformstats] topology last-frame create=%llu destroy=%llu reparent=%llu\n",
            static_cast<unsigned long long>(frame.created),
            static_cast<unsigned long long>(frame.destroyed),
			static_cast<unsigned long long>(frame.reparented));
		if (windowFrames > 0)
		{
			std::printf(
				"[scene.transformstats] topology window frames=%llu total=%llu/%llu/%llu "
				"per-frame=%.6f/%.6f/%.6f (create/destroy/reparent)\n",
				static_cast<unsigned long long>(windowFrames),
				static_cast<unsigned long long>(window.created),
				static_cast<unsigned long long>(window.destroyed),
				static_cast<unsigned long long>(window.reparented),
				static_cast<double>(window.created) / windowFrames,
				static_cast<double>(window.destroyed) / windowFrames,
				static_cast<double>(window.reparented) / windowFrames);
		}
		else
		{
			std::printf("[scene.transformstats] topology window unavailable; 1로 reset 후 한 프레임 이상 관측할 것\n");
		}
		std::printf(
			"[scene.transformstats] topology lifetime create=%llu destroy=%llu reparent=%llu\n",
            static_cast<unsigned long long>(total.created),
            static_cast<unsigned long long>(total.destroyed),
			static_cast<unsigned long long>(total.reparented));

        // 계측 조회다 — 판정할 것이 없다. `Ok` 는 "물어본 것을 냈다" 이지
        // "지표가 좋다" 가 아니다.
        CommandCore::CommandData data = CommandCore::CommandData::Object();
        data.Set("enabled", CommandCore::CommandData::Bool(
            Scene::IsTransformDiagnosticsEnabled()));
        data.Set("windowFrames", CommandCore::CommandData::Int(
            static_cast<int64_t>(windowFrames)));
        return CommandCore::Ok("scene.transformstats", std::move(data));
    }

	void PrintTransformWriteMetrics(const TransformWriteMetrics& metrics)
	{
		std::printf(
			"[scene.transformwritestats] epoch=%llu window-start=%llu total=%llu invalid=%llu\n",
			static_cast<unsigned long long>(metrics.publishEpoch),
			static_cast<unsigned long long>(metrics.windowStartEpoch),
			static_cast<unsigned long long>(metrics.total),
			static_cast<unsigned long long>(metrics.invalidHandle));
		for (size_t i = 0; i < kTransformWriteReasonCount; ++i)
		{
			const auto reason = static_cast<TransformWriteReason>(i);
			std::printf("[scene.transformwritestats] reason=%s count=%llu\n",
				TransformWriteReasonName(reason),
				static_cast<unsigned long long>(metrics.byReason[i]));
		}
	}

	CommandCore::CommandResult HandleSceneProxyDirty(const std::vector<std::string>& parts)
	{
		Scene* scene = SceneManagers->GetActiveScene();
		if (!scene)
		{
		    std::printf("[CLI] 활성 씬 없음\n");
		    return CommandCore::PreconditionFailed("scene.none", "활성 씬이 없다");
		}
		if (parts.size() < 2 || "probe" != parts[1])
		{
			std::printf("[CLI] 사용법: scene.proxydirty probe\n");
			return CommandCore::InvalidArguments("scene.proxydirty: probe");
		}

		Entity* owner = scene->CreateEntity("__proxy_dirty_probe", GameObjectType::Empty);
		MeshRenderer* mesh = owner ? owner->AddComponent<MeshRenderer>() : nullptr;
		if (!owner || !mesh)
		{
			if (owner) owner->Destroy();
			std::printf("[scene.proxydirty] probe=FAIL create=0\n");
			return CommandCore::Fail("scene.proxydirty.probe_create",
				"probe 픽스처를 만들지 못했다(create=0)");
		}

		scene->DrainPendingLifecycle();
		scene->CommitRenderProxies(); // initial create/update tickets are outside the probe

		// Five independent writers in one frame must collapse to one ticket while
		// preserving every reason bit.
		scene->ResetRenderProxyCommitMetrics();
		scene->PublishRenderProxyDirty(mesh, ProxyDirty::Transform);
		scene->PublishRenderProxyDirty(mesh, ProxyDirty::Material);
		scene->PublishRenderProxyDirty(mesh, ProxyDirty::Visibility);
		scene->PublishRenderProxyDirty(mesh, ProxyDirty::LOD);
		scene->PublishRenderProxyDirty(mesh, ProxyDirty::Payload);
		scene->CommitRenderProxies();
		const RenderProxyCommitMetrics dedupe = scene->GetRenderProxyCommitMetrics();
		const bool dedupePass = 5 == dedupe.publishCalls && 4 == dedupe.deduplicated
			&& 1 == dedupe.lastDrained && 1 == dedupe.lastCommitted
			&& 0 == dedupe.lastStale && ProxyDirty::All == dedupe.lastMask;

		// Empty commits may take a lock, but must never scale with registered count.
		scene->ResetRenderProxyCommitMetrics();
		scene->CommitRenderProxies();
		scene->CommitRenderProxies();
		scene->CommitRenderProxies();
		const RenderProxyCommitMetrics stationary = scene->GetRenderProxyCommitMetrics();
		const bool stationaryPass = 3 == stationary.commitPasses
			&& 0 == stationary.committed && 0 == stationary.lastDrained
			&& 0 == stationary.pending;

		// A write resolved before the first sync remains queued across later sync
		// points and is still committed exactly once at the final stage.
		scene->ResetRenderProxyCommitMetrics();
		owner->Transform_().SetPosition({ 17.f, 3.f, -2.f });
		scene->SyncDerivedState(TransformSyncPoint::FixedUpdate);
		const RenderProxyCommitMetrics afterFixed = scene->GetRenderProxyCommitMetrics();
		scene->SyncDerivedState(TransformSyncPoint::PreUpdate);
		scene->SyncDerivedState(TransformSyncPoint::LateUpdate);
		const RenderProxyCommitMetrics beforeFinal = scene->GetRenderProxyCommitMetrics();
		scene->CommitRenderProxies();
		const RenderProxyCommitMetrics phase = scene->GetRenderProxyCommitMetrics();
		const bool phasePass = 1 == afterFixed.pending && 1 == beforeFinal.pending
			&& 1 == phase.lastCommitted && ProxyDirty::Transform == phase.lastMask;

		// Public setters cover material/enabled/LOD and OR into the next commit.
		scene->ResetRenderProxyCommitMetrics();
		mesh->SetMaterial(nullptr);
		mesh->SetLODEnabled(true);
		mesh->SetEnabled(false);
		scene->CommitRenderProxies();
		const RenderProxyCommitMetrics writers = scene->GetRenderProxyCommitMetrics();
		const ProxyDirty writerMask = ProxyDirty::Material
			| ProxyDirty::Visibility | ProxyDirty::LOD;
		const bool writersPass = 3 == writers.publishCalls
			&& 2 == writers.deduplicated && 1 == writers.lastCommitted
			&& writerMask == writers.lastMask;

		// Leave an old ticket alive, unregister, then register the same address.
		// Only the new registration generation may dispatch.
		scene->ResetRenderProxyCommitMetrics();
		scene->PublishRenderProxyDirty(mesh, ProxyDirty::Payload);
		scene->UnCollectMeshRenderer(mesh);
		scene->CollectMeshRenderer(mesh);
		scene->CommitRenderProxies();
		const RenderProxyCommitMetrics lifetime = scene->GetRenderProxyCommitMetrics();
		const bool lifetimePass = 2 == lifetime.lastDrained
			&& 1 == lifetime.lastStale && 1 == lifetime.lastCommitted;

		const bool pass = dedupePass && stationaryPass && phasePass
			&& writersPass && lifetimePass;
		std::printf(
			"[scene.proxydirty] dedupe=%s publish=%llu folded=%llu drained=%llu mask=0x%02x\n",
			dedupePass ? "PASS" : "FAIL",
			static_cast<unsigned long long>(dedupe.publishCalls),
			static_cast<unsigned long long>(dedupe.deduplicated),
			static_cast<unsigned long long>(dedupe.lastDrained),
			static_cast<unsigned>(dedupe.lastMask));
		std::printf(
			"[scene.proxydirty] stationary=%s passes=%llu committed=%llu phase=%s pending=%llu writers=%s\n",
			stationaryPass ? "PASS" : "FAIL",
			static_cast<unsigned long long>(stationary.commitPasses),
			static_cast<unsigned long long>(stationary.committed),
			phasePass ? "PASS" : "FAIL",
			static_cast<unsigned long long>(beforeFinal.pending),
			writersPass ? "PASS" : "FAIL");
		std::printf(
			"[scene.proxydirty] generation=%s drained=%llu stale=%llu committed=%llu probe=%s\n",
			lifetimePass ? "PASS" : "FAIL",
			static_cast<unsigned long long>(lifetime.lastDrained),
			static_cast<unsigned long long>(lifetime.lastStale),
			static_cast<unsigned long long>(lifetime.lastCommitted),
			pass ? "PASS" : "FAIL");

		owner->Destroy();

		CommandCore::CommandData data = CommandCore::CommandData::Object();
		data.Set("pass", CommandCore::CommandData::Bool(pass));
		if (!pass)
		{
			return CommandCore::Fail("scene.proxydirty.probe_failed",
				"probe 판정 실패", std::move(data));
		}
		return CommandCore::Ok("scene.proxydirty probe PASS", std::move(data));
	}

	CommandCore::CommandResult HandleSceneTransformWriteStats(const std::vector<std::string>& parts)
	{
		Scene* scene = SceneManagers->GetActiveScene();
		if (nullptr == scene)
		{
		    std::printf("[CLI] 활성 씬 없음\n");
		    return CommandCore::PreconditionFailed("scene.none", "활성 씬이 없다");
		}

		const std::string action = parts.size() > 1 ? parts[1] : "print";
		if ("0" == action || "1" == action)
		{
			const bool enabled = "1" == action;
			Scene::SetTransformWriteDiagnosticsEnabled(enabled);
			if (enabled) scene->ResetTransformWriteDiagnostics();
			std::printf("[scene.transformwritestats] enabled=%d build=%s\n",
				enabled ? 1 : 0, TransformBuildConfiguration());

			CommandCore::CommandData data = CommandCore::CommandData::Object();
			data.Set("enabled", CommandCore::CommandData::Bool(enabled));
			return CommandCore::Ok("scene.transformwritestats 계측 토글", std::move(data));
		}

		if ("probe" == action)
		{
			const bool wasEnabled = Scene::IsTransformWriteDiagnosticsEnabled();
			Scene::SetTransformWriteDiagnosticsEnabled(true);
			Entity* probe = scene->CreateEntity(
				"__transform_write_probe", GameObjectType::Empty);
			Entity* child = scene->CreateEntity(
				"__transform_write_probe_child", GameObjectType::Empty);
			if (!probe || !child)
			{
				if (child) child->Destroy();
				if (probe) probe->Destroy();
				Scene::SetTransformWriteDiagnosticsEnabled(wasEnabled);
				std::printf("[scene.transformwritestats] probe=FAIL create=0\n");
				return CommandCore::Fail("scene.transformwritestats.probe_create",
					"probe 픽스처를 만들지 못했다(create=0)");
			}

			const EntityHandle handle = scene->HandleOf(probe->m_index);
			const bool resolverBefore = scene->Resolve(handle) == probe;
			scene->ResetTransformWriteDiagnostics();

			Transform& transform = probe->Transform_();
			transform.SetPosition({ 1.f, 2.f, 3.f }, TransformWriteReason::CppSetter);
			transform.SetPosition({ 2.f, 3.f, 4.f }, TransformWriteReason::Script);
			transform.SetPositionValue(
				{ 3.f, 4.f, 5.f, 0.f }, TransformWriteReason::Inspector);
			transform.SetPosition({ 4.f, 5.f, 6.f }, TransformWriteReason::Physics);
			transform.SetLocalMatrix(
				transform.GetLocalMatrix(), TransformWriteReason::Socket);
			transform.SetPosition({ 5.f, 6.f, 7.f }, TransformWriteReason::Gizmo);
			transform.SetPosition({ 6.f, 7.f, 8.f }, TransformWriteReason::Animator);
			transform.SetLocalMatrix(
				transform.GetLocalMatrix(), TransformWriteReason::ModelImport);

			Authoring::WriteDocument reflected = Meta::SerializeDocument(&transform);
			reflected.Root().Child("position").Child("x").SetScalar(7.f);
			Meta::Deserialize(&transform, reflected.Root().Read());

			Authoring::WriteDocument prefab = Meta::SerializeDocument(&transform);
			prefab.Root().Child("position").Child("x").SetScalar(8.f);
			Meta::DeserializePrefab(
				&transform, prefab.Root().Read(),
				std::unordered_set<std::string>{});

			scene->Reparent(scene->HandleOf(child->m_index),
				scene->HandleOf(probe->m_index));
			transform.TransformReset();

			const bool resolverAfter = scene->Resolve(handle) == probe;
			const TransformWriteMetrics metrics = scene->GetTransformWriteMetrics();
			PrintTransformWriteMetrics(metrics);

			size_t missing = 0;
			for (uint64_t count : metrics.byReason)
			{
				if (0 == count) ++missing;
			}
			const bool pass = resolverBefore && resolverAfter
				&& 0 == metrics.invalidHandle && 0 == missing;
			std::printf(
				"[scene.transformwritestats] probe=%s expected-reasons=%zu missing=%zu resolver=%s\n",
				pass ? "PASS" : "FAIL", kTransformWriteReasonCount, missing,
				(resolverBefore && resolverAfter) ? "stable" : "mismatch");
			child->Destroy();
			probe->Destroy();
			Scene::SetTransformWriteDiagnosticsEnabled(wasEnabled);

			CommandCore::CommandData data = CommandCore::CommandData::Object();
			data.Set("pass", CommandCore::CommandData::Bool(pass));
			if (!pass)
			{
				return CommandCore::Fail("scene.transformwritestats.probe_failed",
					"probe 판정 실패", std::move(data));
			}
			return CommandCore::Ok("scene.transformwritestats probe PASS", std::move(data));
		}

		std::printf("[scene.transformwritestats] enabled=%d build=%s\n",
			Scene::IsTransformWriteDiagnosticsEnabled() ? 1 : 0,
			TransformBuildConfiguration());
		PrintTransformWriteMetrics(scene->GetTransformWriteMetrics());

		CommandCore::CommandData data = CommandCore::CommandData::Object();
		data.Set("enabled", CommandCore::CommandData::Bool(
			Scene::IsTransformWriteDiagnosticsEnabled()));
		return CommandCore::Ok("scene.transformwritestats", std::move(data));
	}

	CommandCore::CommandResult HandleSceneTransformDomains(const std::vector<std::string>& parts)
	{
		Scene* scene = SceneManagers->GetActiveScene();
		if (nullptr == scene)
		{
		    std::printf("[CLI] 활성 씬 없음\n");
		    return CommandCore::PreconditionFailed("scene.none", "활성 씬이 없다");
		}
		if (parts.size() < 2 || "probe" != parts[1])
		{
			std::printf("[CLI] 사용법: scene.transformdomains probe\n");
			return CommandCore::InvalidArguments("scene.transformdomains: probe");
		}

		const bool wasDiagnosticsEnabled = Scene::IsTransformDiagnosticsEnabled();
		Scene::SetTransformDiagnosticsEnabled(true);
		Entity* ui = scene->CreateEntity(
			"__transform_x2_ui_probe", GameObjectType::UI);
		Entity* spatial = scene->CreateEntity(
			"__transform_x2_spatial_probe", GameObjectType::Empty);
		RectTransformComponent* rect = ui
			? ui->GetComponent<RectTransformComponent>() : nullptr;
		if (!ui || !spatial || !rect || !spatial->HasTransform())
		{
			if (ui) ui->Destroy();
			if (spatial) spatial->Destroy();
			Scene::SetTransformDiagnosticsEnabled(wasDiagnosticsEnabled);
			std::printf("[scene.transformdomains] probe=FAIL create=0\n");
			return CommandCore::Fail("scene.transformdomains.probe_create",
				"probe 픽스처를 만들지 못했다(create=0)");
		}

		auto snapshot = [&]() -> TransformUpdateMetrics
		{
			scene->SyncDerivedState(TransformSyncPoint::Benchmark);
			return scene->GetLastTransformUpdateMetrics(TransformSyncPoint::Benchmark);
		};
		auto isState = [](const TransformUpdateMetrics& metrics,
			bool uiRun, bool spatialRun)
		{
			return metrics.uiDomainResolved == uiRun
				&& metrics.spatialDomainResolved == spatialRun;
		};

		// 생성 dirty를 먼저 소비한 뒤 queue-empty 기준선을 잰다.
		snapshot();
		const TransformUpdateMetrics clean = snapshot();

		rect->SetAnchoredPosition({ 13.f, 17.f });
		const TransformUpdateMetrics uiOnly = snapshot();

		spatial->Transform_().SetPosition({ 3.f, 5.f, 7.f });
		const TransformUpdateMetrics spatialOnly = snapshot();

		// 즉시 subtree는 그 자리에서 rect를 계산하되, global UI epoch는 다음
		// full pass가 계속 소비하도록 남겨 둔다.
		const math::rect subtreeBefore = rect->GetWorldRect();
		rect->SetAnchoredPosition({ 29.f, 31.f });
		scene->LayoutUISubtree(ui);
		const math::rect subtreeAfter = rect->GetWorldRect();
		const bool subtreeImmediate = !rect->IsDirty()
			&& (subtreeBefore.x != subtreeAfter.x || subtreeBefore.y != subtreeAfter.y);
		const TransformUpdateMetrics subtreeFollowup = snapshot();

		// paused path는 UI만 소비해야 한다. 동시에 pending인 spatial write는
		// 다음 full sync까지 남아야 한다.
		snapshot();
		rect->SetAnchoredPosition({ 37.f, 41.f });
		spatial->Transform_().SetPosition({ 11.f, 13.f, 17.f });
		scene->AllUIUpdateWorldMatrix();
		const bool pausedConsumedUI = !rect->IsDirty();
		const TransformUpdateMetrics afterPaused = snapshot();
		const TransformUpdateMetrics finalClean = snapshot();

		const bool pass = isState(clean, false, false)
			&& isState(uiOnly, true, false)
			&& isState(spatialOnly, false, true)
			&& subtreeImmediate && isState(subtreeFollowup, true, false)
			&& pausedConsumedUI && isState(afterPaused, false, true)
			&& isState(finalClean, false, false);

		std::printf(
			"[scene.transformdomains] clean=%s/%s ui-write=%s/%s "
			"spatial-write=%s/%s subtree=%s+%s/%s paused=%s+%s/%s final=%s/%s\n",
			clean.uiDomainResolved ? "run" : "empty",
			clean.spatialDomainResolved ? "run" : "empty",
			uiOnly.uiDomainResolved ? "run" : "empty",
			uiOnly.spatialDomainResolved ? "run" : "empty",
			spatialOnly.uiDomainResolved ? "run" : "empty",
			spatialOnly.spatialDomainResolved ? "run" : "empty",
			subtreeImmediate ? "immediate" : "stale",
			subtreeFollowup.uiDomainResolved ? "run" : "empty",
			subtreeFollowup.spatialDomainResolved ? "run" : "empty",
			pausedConsumedUI ? "ui-consumed" : "ui-dirty",
			afterPaused.uiDomainResolved ? "run" : "empty",
			afterPaused.spatialDomainResolved ? "run" : "empty",
			finalClean.uiDomainResolved ? "run" : "empty",
			finalClean.spatialDomainResolved ? "run" : "empty");
		std::printf(
			"[scene.transformdomains] probe=%s order=UI->Spatial build=%s\n",
			pass ? "PASS" : "FAIL", TransformBuildConfiguration());

		ui->Destroy();
		spatial->Destroy();
		Scene::SetTransformDiagnosticsEnabled(wasDiagnosticsEnabled);

		CommandCore::CommandData data = CommandCore::CommandData::Object();
		data.Set("pass", CommandCore::CommandData::Bool(pass));
		if (!pass)
		{
			return CommandCore::Fail("scene.transformdomains.probe_failed",
				"probe 판정 실패", std::move(data));
		}
		return CommandCore::Ok("scene.transformdomains probe PASS", std::move(data));
	}

	CommandCore::CommandResult HandleSceneHierarchyMutation(const std::vector<std::string>& parts)
	{
		Scene* scene = SceneManagers->GetActiveScene();
		if (nullptr == scene)
		{
		    std::printf("[CLI] 활성 씬 없음\n");
		    return CommandCore::PreconditionFailed("scene.none", "활성 씬이 없다");
		}
		if (parts.size() < 2 || "probe" != parts[1])
		{
			std::printf("[CLI] 사용법: scene.hierarchymutation probe\n");
			return CommandCore::InvalidArguments("scene.hierarchymutation: probe");
		}

		Entity* ancestor = scene->CreateEntity(
			"__hierarchy_x3_ancestor", GameObjectType::Empty);
		Entity* parent = ancestor ? scene->CreateEntity(
			"__hierarchy_x3_parent", GameObjectType::Empty, ancestor->m_index) : nullptr;
		Entity* child = parent ? scene->CreateEntity(
			"__hierarchy_x3_child", GameObjectType::Empty, parent->m_index) : nullptr;
		if (!ancestor || !parent || !child)
		{
			if (child) child->Destroy();
			if (parent) parent->Destroy();
			if (ancestor) ancestor->Destroy();
			std::printf("[scene.hierarchymutation] probe=FAIL create=0\n");
			return CommandCore::Fail("scene.hierarchymutation.probe_create",
				"probe 픽스처를 만들지 못했다(create=0)");
		}

		const EntityHandle ancestorHandle = scene->HandleOf(ancestor->m_index);
		const EntityHandle parentHandle = scene->HandleOf(parent->m_index);
		const EntityHandle childHandle = scene->HandleOf(child->m_index);
		scene->SyncDerivedState(TransformSyncPoint::Benchmark);
		const uint64_t baseline = scene->GetTopologyVersion();

		const ReparentResult noChange = scene->Reparent(parentHandle, ancestorHandle);
		const ReparentResult self = scene->Reparent(ancestorHandle, ancestorHandle);
		const ReparentResult ancestorCycle = scene->Reparent(ancestorHandle, childHandle);
		EntityHandle staleParent = parentHandle;
		++staleParent.generation;
		if (0 == staleParent.generation) ++staleParent.generation;
		const ReparentResult stale = scene->Reparent(childHandle, staleParent);

		Scene foreignScene;
		foreignScene.AddRootEntity("__hierarchy_x3_foreign_scene");
		Entity* foreignParent = foreignScene.CreateEntity(
			"__hierarchy_x3_foreign_parent", GameObjectType::Empty);
		const ReparentResult cross = foreignParent
			? scene->Reparent(childHandle,
				foreignScene.HandleOf(foreignParent->m_index))
			: ReparentResult::InvalidHandle;
		const uint64_t rejectedDelta = scene->GetTopologyVersion() - baseline;

		const uint64_t successBefore = scene->GetTopologyVersion();
		const ReparentResult success = scene->Reparent(childHandle, ancestorHandle);
		const uint64_t successDelta = scene->GetTopologyVersion() - successBefore;
		const HierarchyIntegrityMetrics integrity =
			scene->GetHierarchyIntegrityMetrics();

		const uint64_t bulkBefore = scene->GetTopologyVersion();
		Entity* bulkParent = nullptr;
		Entity* bulkChild = nullptr;
		{
			[[maybe_unused]] auto bulk = scene->BeginHierarchyBulkBuild();
			bulkParent = scene->CreateEntity(
				"__hierarchy_x3_bulk_parent", GameObjectType::Empty);
			bulkChild = bulkParent ? scene->CreateEntity(
				"__hierarchy_x3_bulk_child", GameObjectType::Empty,
				bulkParent->m_index) : nullptr;
		}
		const uint64_t bulkDelta = scene->GetTopologyVersion() - bulkBefore;

		scene->SyncDerivedState(TransformSyncPoint::Benchmark);
		const uint64_t cleanBefore = scene->GetTopologyVersion();
		scene->SyncDerivedState(TransformSyncPoint::Benchmark);
		const uint64_t cleanDelta = scene->GetTopologyVersion() - cleanBefore;

		const bool pass = ReparentResult::NoChange == noChange
			&& ReparentResult::SelfRejected == self
			&& ReparentResult::CycleRejected == ancestorCycle
			&& ReparentResult::StaleHandle == stale
			&& ReparentResult::CrossScene == cross
			&& 0 == rejectedDelta
			&& ReparentResult::Success == success && 1 == successDelta
			&& 0 == integrity.Total()
			&& bulkParent && bulkChild && 1 == bulkDelta
			&& 0 == cleanDelta;
		std::printf(
			"[scene.hierarchymutation] nochange=%s self=%s ancestor=%s "
			"stale=%s cross=%s rejected-delta=%llu success=%s success-delta=%llu\n",
			ReparentResultName(noChange), ReparentResultName(self),
			ReparentResultName(ancestorCycle), ReparentResultName(stale),
			ReparentResultName(cross),
			static_cast<unsigned long long>(rejectedDelta),
			ReparentResultName(success),
			static_cast<unsigned long long>(successDelta));
		std::printf(
			"[scene.hierarchymutation] symmetry=%llu mismatch=%llu orphan=%llu "
			"duplicate=%llu invalid=%llu bulk-delta=%llu clean-delta=%llu\n",
			static_cast<unsigned long long>(integrity.Total()),
			static_cast<unsigned long long>(integrity.parentChildMismatch),
			static_cast<unsigned long long>(integrity.orphan),
			static_cast<unsigned long long>(integrity.duplicateChild),
			static_cast<unsigned long long>(integrity.invalidReference),
			static_cast<unsigned long long>(bulkDelta),
			static_cast<unsigned long long>(cleanDelta));
		std::printf("[scene.hierarchymutation] probe=%s\n", pass ? "PASS" : "FAIL");

		if (bulkChild) bulkChild->Destroy();
		if (bulkParent) bulkParent->Destroy();
		child->Destroy();
		parent->Destroy();
		ancestor->Destroy();

		CommandCore::CommandData data = CommandCore::CommandData::Object();
		data.Set("pass", CommandCore::CommandData::Bool(pass));
		if (!pass)
		{
			return CommandCore::Fail("scene.hierarchymutation.probe_failed",
				"probe 판정 실패", std::move(data));
		}
		return CommandCore::Ok("scene.hierarchymutation probe PASS", std::move(data));
	}

	CommandCore::CommandResult HandleSceneExecutionGraph(const std::vector<std::string>& parts)
	{
		Scene* scene = SceneManagers->GetActiveScene();
		if (nullptr == scene)
		{
		    std::printf("[CLI] 활성 씬 없음\n");
		    return CommandCore::PreconditionFailed("scene.none", "활성 씬이 없다");
		}
		if (parts.size() < 2 || ("probe" != parts[1] && "bench" != parts[1]))
		{
			std::printf("[CLI] 사용법: scene.executiongraph probe | bench <N> [samples]\n");
			return CommandCore::InvalidArguments(
				"scene.executiongraph: probe | bench <N> [samples]");
		}

		if ("bench" == parts[1])
		{
			const int objectCount = parts.size() > 2
				? (std::max)(1, std::atoi(parts[2].c_str())) : 10000;
			const int sampleCount = parts.size() > 3
				? (std::max)(4, std::atoi(parts[3].c_str())) : 4;
			Entity* parentA = nullptr;
			Entity* parentB = nullptr;
			Entity* mover = nullptr;
			{
				[[maybe_unused]] auto bulk = scene->BeginHierarchyBulkBuild();
				parentA = scene->CreateEntity("__executiongraph_bench_a", GameObjectType::Empty);
				parentB = scene->CreateEntity("__executiongraph_bench_b", GameObjectType::Empty);
				for (int i = 0; parentA && i < objectCount; ++i)
				{
					Entity* node = scene->CreateEntity(
						"__executiongraph_bench_node", GameObjectType::Empty, parentA->m_index);
					if (0 == i) mover = node;
				}
			}

			if (!parentA || !parentB || !mover)
			{
				if (parentA) parentA->Destroy();
				if (parentB) parentB->Destroy();
				std::printf("[scene.executiongraph] bench=FAIL create=0\n");

				// ★ 픽스처를 못 세운 것은 **판정 실패**다(§5.4 의 4).
				//
				//   예전에는 이 자리가 `printf` 뒤 `return;` 이라 프로세스가 0 으로
				//   끝났다 — 벤치가 아무것도 재지 못했는데 자동화는 성공으로 읽었다.
				return CommandCore::Fail("scene.executiongraph.bench_create",
					"벤치 픽스처를 만들지 못했다(create=0)");
			}

			const EntityHandle moverHandle = scene->HandleOf(mover->m_index);
			const EntityHandle parentAHandle = scene->HandleOf(parentA->m_index);
			const EntityHandle parentBHandle = scene->HandleOf(parentB->m_index);
			std::vector<double> samples;
			samples.reserve(sampleCount);
			bool pass = true;
			uint64_t previousCompileCount =
				scene->GetExecutionGraphCompileMetrics().compileCount;
			for (int sample = 0; sample < sampleCount; ++sample)
			{
				if (sample > 0)
				{
					const EntityHandle target = 0 == (sample & 1)
						? parentAHandle : parentBHandle;
					pass = pass && ReparentResult::Success == scene->Reparent(moverHandle, target);
				}
				scene->SyncDerivedState(TransformSyncPoint::Benchmark);
				const ExecutionGraphCompileMetrics metrics =
					scene->GetExecutionGraphCompileMetrics();
				pass = pass && metrics.success
					&& metrics.compileCount == previousCompileCount + 1;
				previousCompileCount = metrics.compileCount;
				samples.push_back(metrics.compileUs);
			}

			std::ranges::sort(samples);
			const double median = 0 == (samples.size() & 1)
				? (samples[samples.size() / 2 - 1] + samples[samples.size() / 2]) * 0.5
				: samples[samples.size() / 2];
			const size_t p95Index = (samples.size() * 95 + 99) / 100 - 1;
			const double p95 = samples[p95Index];
			const double maximum = samples.back();
			constexpr double frameBudgetUs = 1000000.0 / 60.0;
			const bool inBudget = maximum <= frameBudgetUs;
			pass = pass && inBudget;
			const ExecutionGraphCompileMetrics finalMetrics =
				scene->GetExecutionGraphCompileMetrics();
			std::printf(
				"[scene.executiongraph] bench nodes=%d samples=%d spatial=%llu "
				"median-us=%.3f p95-us=%.3f max-us=%.3f budget-us=%.3f budget=%s\n",
				objectCount, sampleCount,
				static_cast<unsigned long long>(finalMetrics.spatialNodes),
				median, p95, maximum, frameBudgetUs, inBudget ? "PASS" : "FAIL");
			std::printf("[scene.executiongraph] bench=%s\n", pass ? "PASS" : "FAIL");
			parentA->Destroy();
			parentB->Destroy();

			CommandCore::CommandData data = CommandCore::CommandData::Object();
			data.Set("mode", CommandCore::CommandData::String("bench"));
			data.Set("nodes", CommandCore::CommandData::Int(objectCount));
			data.Set("samples", CommandCore::CommandData::Int(sampleCount));
			data.Set("medianUs", CommandCore::CommandData::Double(median));
			data.Set("p95Us", CommandCore::CommandData::Double(p95));
			data.Set("maxUs", CommandCore::CommandData::Double(maximum));
			data.Set("budgetUs", CommandCore::CommandData::Double(frameBudgetUs));
			data.Set("pass", CommandCore::CommandData::Bool(pass));
			if (!pass)
			{
				return CommandCore::Fail("scene.executiongraph.bench_failed",
					"bench 판정 실패", std::move(data));
			}
			return CommandCore::Ok("scene.executiongraph bench PASS", std::move(data));
		}

		Entity* spatialAncestor = nullptr;
		Entity* spatialBridge = nullptr;
		Entity* spatialChild = nullptr;
		Entity* layoutAncestor = nullptr;
		Entity* layoutBridge = nullptr;
		Entity* layoutChild = nullptr;
		Entity* canvasEntity = nullptr;
		{
			[[maybe_unused]] auto bulk = scene->BeginHierarchyBulkBuild();
			spatialAncestor = scene->CreateEntity(
				"__executiongraph_spatial_ancestor", GameObjectType::Empty);
			spatialBridge = spatialAncestor ? scene->CreateEntity(
				"__executiongraph_spatial_bridge", GameObjectType::UI,
				spatialAncestor->m_index) : nullptr;
			spatialChild = spatialBridge ? scene->CreateEntity(
				"__executiongraph_spatial_child", GameObjectType::Empty,
				spatialBridge->m_index) : nullptr;

			layoutAncestor = scene->CreateEntity(
				"__executiongraph_layout_ancestor", GameObjectType::UI);
			layoutBridge = layoutAncestor ? scene->CreateEntity(
				"__executiongraph_layout_bridge", GameObjectType::Empty,
				layoutAncestor->m_index) : nullptr;
			layoutChild = layoutBridge ? scene->CreateEntity(
				"__executiongraph_layout_child", GameObjectType::UI,
				layoutBridge->m_index) : nullptr;
			canvasEntity = scene->CreateEntity(
				"__executiongraph_canvas", GameObjectType::Canvas);
			if (canvasEntity) canvasEntity->AddComponent<Canvas>();
		}

		const bool created = spatialAncestor && spatialBridge && spatialChild
			&& layoutAncestor && layoutBridge && layoutChild && canvasEntity;
		if (!created)
		{
			std::printf("[scene.executiongraph] probe=FAIL create=0\n");
			return CommandCore::Fail("scene.executiongraph.probe_create",
				"probe 픽스처를 만들지 못했다(create=0)");
		}

		const EntityHandle spatialAncestorHandle = scene->HandleOf(spatialAncestor->m_index);
		const EntityHandle spatialBridgeHandle = scene->HandleOf(spatialBridge->m_index);
		const EntityHandle spatialChildHandle = scene->HandleOf(spatialChild->m_index);
		const EntityHandle layoutAncestorHandle = scene->HandleOf(layoutAncestor->m_index);
		const EntityHandle layoutBridgeHandle = scene->HandleOf(layoutBridge->m_index);
		const EntityHandle layoutChildHandle = scene->HandleOf(layoutChild->m_index);
		const EntityHandle canvasHandle = scene->HandleOf(canvasEntity->m_index);

		scene->SyncDerivedState(TransformSyncPoint::Benchmark);
		const ExecutionGraphCompileMetrics initial =
			scene->GetExecutionGraphCompileMetrics();
		const ExecutionGraphRelationDiagnostics spatialBridgeRelation =
			scene->GetExecutionGraphRelationDiagnostics(spatialBridgeHandle);
		const ExecutionGraphRelationDiagnostics spatialChildRelation =
			scene->GetExecutionGraphRelationDiagnostics(spatialChildHandle);
		const ExecutionGraphRelationDiagnostics layoutBridgeRelation =
			scene->GetExecutionGraphRelationDiagnostics(layoutBridgeHandle);
		const ExecutionGraphRelationDiagnostics layoutChildRelation =
			scene->GetExecutionGraphRelationDiagnostics(layoutChildHandle);
		const ExecutionGraphRelationDiagnostics canvasRelation =
			scene->GetExecutionGraphRelationDiagnostics(canvasHandle);

		const bool nearestSpatial = !spatialBridgeRelation.spatialMember
			&& spatialChildRelation.spatialMember
			&& spatialChildRelation.spatialParent == spatialAncestorHandle;
		const bool nearestLayout = !layoutBridgeRelation.layoutMember
			&& layoutChildRelation.layoutMember
			&& layoutChildRelation.layoutParent == layoutAncestorHandle;
		const bool canvasBoth = canvasRelation.spatialMember && canvasRelation.layoutMember;
		const uint64_t compileBeforeMembership = initial.compileCount;
		RectTransformComponent* dynamicRect =
			layoutBridge->AddComponent<RectTransformComponent>();
		scene->SyncDerivedState(TransformSyncPoint::Benchmark);
		const ExecutionGraphCompileMetrics afterMembership =
			scene->GetExecutionGraphCompileMetrics();
		const ExecutionGraphRelationDiagnostics dynamicLayoutRelation =
			scene->GetExecutionGraphRelationDiagnostics(layoutBridgeHandle);
		const ExecutionGraphRelationDiagnostics dynamicChildRelation =
			scene->GetExecutionGraphRelationDiagnostics(layoutChildHandle);
		const bool dynamicMembership = dynamicRect && dynamicLayoutRelation.layoutMember
			&& dynamicChildRelation.layoutParent == layoutBridgeHandle
			&& afterMembership.compileCount == compileBeforeMembership + 1;

		spatialAncestor->Transform_().SetPosition({ 3.f, 5.f, 7.f });
		spatialChild->Transform_().SetPosition({ 11.f, 13.f, 17.f });
		scene->SyncDerivedState(TransformSyncPoint::Benchmark);
		const math::matrix4x4 localBefore = spatialChild->Transform_().GetLocalMatrix();
		const math::matrix4x4 worldBefore = spatialChild->Transform_().GetWorldMatrix();
		const uint64_t compileBeforeBulk =
			scene->GetExecutionGraphCompileMetrics().compileCount;

		Entity* bulkParent = nullptr;
		Entity* bulkChild = nullptr;
		{
			[[maybe_unused]] auto bulk = scene->BeginHierarchyBulkBuild();
			bulkParent = scene->CreateEntity(
				"__executiongraph_bulk_parent", GameObjectType::Empty);
			bulkChild = bulkParent ? scene->CreateEntity(
				"__executiongraph_bulk_child", GameObjectType::Empty,
				bulkParent->m_index) : nullptr;
		}
		scene->SyncDerivedState(TransformSyncPoint::Benchmark);
		const ExecutionGraphCompileMetrics afterBulk =
			scene->GetExecutionGraphCompileMetrics();
		const math::matrix4x4 localAfter = spatialChild->Transform_().GetLocalMatrix();
		const math::matrix4x4 worldAfter = spatialChild->Transform_().GetWorldMatrix();
		const bool valuesExact = 0 == std::memcmp(&localBefore, &localAfter, sizeof(localBefore))
			&& 0 == std::memcmp(&worldBefore, &worldAfter, sizeof(worldBefore));
		const bool identityStable = scene->Resolve(spatialAncestorHandle) == spatialAncestor
			&& scene->Resolve(spatialChildHandle) == spatialChild
			&& scene->Resolve(layoutAncestorHandle) == layoutAncestor
			&& scene->Resolve(layoutChildHandle) == layoutChild
			&& scene->Resolve(canvasHandle) == canvasEntity;
		const uint64_t bulkCompileDelta = afterBulk.compileCount - compileBeforeBulk;

		scene->SyncDerivedState(TransformSyncPoint::Benchmark);
		const ExecutionGraphCompileMetrics clean =
			scene->GetExecutionGraphCompileMetrics();
		const uint64_t cleanCompileDelta = clean.compileCount - afterBulk.compileCount;
		constexpr double frameBudgetUs = 1000000.0 / 60.0;
		const bool pass = initial.success && afterBulk.success
			&& 0 == afterBulk.TotalViolations()
			&& nearestSpatial && nearestLayout && canvasBoth && dynamicMembership
			&& valuesExact && identityStable
			&& bulkParent && bulkChild
			&& 1 == bulkCompileDelta && 0 == cleanCompileDelta
			&& afterBulk.compileUs <= frameBudgetUs;

		std::printf(
			"[scene.executiongraph] compile=%s topology=%llu compiled=%llu count=%llu "
			"occupied=%llu spatial=%llu layout=%llu compile-us=%.3f budget-us=%.3f\n",
			afterBulk.success ? "PASS" : "FAIL",
			static_cast<unsigned long long>(afterBulk.topologyVersion),
			static_cast<unsigned long long>(afterBulk.compiledVersion),
			static_cast<unsigned long long>(afterBulk.compileCount),
			static_cast<unsigned long long>(afterBulk.occupiedEntities),
			static_cast<unsigned long long>(afterBulk.spatialNodes),
			static_cast<unsigned long long>(afterBulk.layoutNodes),
			afterBulk.compileUs, frameBudgetUs);
		std::printf(
			"[scene.executiongraph] transformless=%llu nonlayout=%llu mapping=%llu "
			"parent-order=%llu range=%llu hierarchy=%llu unreachable=%llu cycle=%llu\n",
			static_cast<unsigned long long>(afterBulk.transformlessSpatial),
			static_cast<unsigned long long>(afterBulk.nonLayoutMember),
			static_cast<unsigned long long>(afterBulk.mappingViolations),
			static_cast<unsigned long long>(afterBulk.parentOrderViolations),
			static_cast<unsigned long long>(afterBulk.subtreeRangeViolations),
			static_cast<unsigned long long>(afterBulk.hierarchyViolations),
			static_cast<unsigned long long>(afterBulk.unreachableEntities),
			static_cast<unsigned long long>(afterBulk.cycleViolations));
		std::printf(
			"[scene.executiongraph] nearest-spatial=%s nearest-layout=%s canvas-both=%s dynamic-layout=%s "
			"identity=%s values=%s bulk-compile-delta=%llu clean-compile-delta=%llu\n",
			nearestSpatial ? "PASS" : "FAIL", nearestLayout ? "PASS" : "FAIL",
			canvasBoth ? "PASS" : "FAIL", dynamicMembership ? "PASS" : "FAIL",
			identityStable ? "stable" : "changed",
			valuesExact ? "exact" : "changed",
			static_cast<unsigned long long>(bulkCompileDelta),
			static_cast<unsigned long long>(cleanCompileDelta));
		std::printf("[scene.executiongraph] probe=%s\n", pass ? "PASS" : "FAIL");

		// ★ 정리를 **먼저** 하고 결과를 낸다. 여기서 조기 반환하면 아래 Destroy 가
		//   건너뛰어져 프로브가 만든 엔티티가 씬에 남고, 다음 명령이 그 위에서 돈다.
		if (bulkChild) bulkChild->Destroy();
		if (bulkParent) bulkParent->Destroy();
		spatialChild->Destroy();
		spatialBridge->Destroy();
		spatialAncestor->Destroy();
		layoutChild->Destroy();
		layoutBridge->Destroy();
		layoutAncestor->Destroy();
		canvasEntity->Destroy();

		CommandCore::CommandData data = CommandCore::CommandData::Object();
		data.Set("mode", CommandCore::CommandData::String("probe"));
		data.Set("pass", CommandCore::CommandData::Bool(pass));
		if (!pass)
		{
			return CommandCore::Fail("scene.executiongraph.probe_failed",
				"probe 판정 실패", std::move(data));
		}
		return CommandCore::Ok("scene.executiongraph probe PASS", std::move(data));
	}

	CommandCore::CommandResult HandleSceneSparseResolver(const std::vector<std::string>& parts)
	{
		Scene* scene = SceneManagers->GetActiveScene();
		if (!scene)
		{
		    std::printf("[CLI] 활성 씬 없음\n");
		    return CommandCore::PreconditionFailed("scene.none", "활성 씬이 없다");
		}
		if (parts.size() < 2)
		{
			std::printf("[CLI] 사용법: scene.sparseresolver 0|1|print|probe|bench <N> <frames>\n");
			return CommandCore::InvalidArguments(
				"scene.sparseresolver: 0|1|print|probe|bench <N> <frames>");
		}

		if ("0" == parts[1] || "1" == parts[1])
		{
			const bool enabled = "1" == parts[1];
			Scene::SetSparseSpatialResolverEnabled(enabled);
			scene->MarkSpatialTransformsDirty();
			std::printf("[scene.sparseresolver] enabled=%d\n", enabled ? 1 : 0);

			CommandCore::CommandData data = CommandCore::CommandData::Object();
			data.Set("enabled", CommandCore::CommandData::Bool(enabled));
			return CommandCore::Ok("scene.sparseresolver 토글", std::move(data));
		}

		if ("print" == parts[1])
		{
			const SpatialResolveMetrics& m = scene->GetLastSpatialResolveMetrics();
			std::printf(
				"[scene.sparseresolver] enabled=%d resolved=%d sparse=%d fallback=%d full=%d "
				"requests=%llu stale=%llu ranges=%llu merged=%llu nodes=%llu "
				"compose=%llu writes=%llu resolve-us=%.3f\n",
				Scene::IsSparseSpatialResolverEnabled() ? 1 : 0, m.resolved ? 1 : 0,
				m.sparseExecuted ? 1 : 0, m.legacyFallback ? 1 : 0,
				m.fullResolve ? 1 : 0,
				static_cast<unsigned long long>(m.dirtyRequests),
				static_cast<unsigned long long>(m.staleRequests),
				static_cast<unsigned long long>(m.canonicalRanges),
				static_cast<unsigned long long>(m.mergedRequests),
				static_cast<unsigned long long>(m.resolvedNodes),
				static_cast<unsigned long long>(m.localComposes),
				static_cast<unsigned long long>(m.worldWrites), m.resolveUs);

			// 계측 조회다 - 판정할 것이 없다.
			CommandCore::CommandData data = CommandCore::CommandData::Object();
			data.Set("resolvedNodes", CommandCore::CommandData::Int(
				static_cast<int64_t>(m.resolvedNodes)));
			data.Set("resolveUs", CommandCore::CommandData::Double(m.resolveUs));
			return CommandCore::Ok("scene.sparseresolver print", std::move(data));
		}

		if ("bench" == parts[1])
		{
			if (parts.size() < 4)
			{
				std::printf("[CLI] 사용법: scene.sparseresolver bench <N> <frames>\n");
				return CommandCore::InvalidArguments(
					"scene.sparseresolver bench: <N> <frames> 가 필요하다");
			}
			const int objectCount = (std::max)(1, std::atoi(parts[2].c_str()));
			const int frames = (std::max)(4, std::atoi(parts[3].c_str()));
			const bool wasSparse = Scene::IsSparseSpatialResolverEnabled();
			Entity* root = nullptr;
			std::vector<Entity*> nodes;
			nodes.reserve(objectCount);
			{
				[[maybe_unused]] auto bulk = scene->BeginHierarchyBulkBuild();
				root = scene->CreateEntity("__sparse_bench_root", GameObjectType::Empty);
				for (int i = 0; root && i < objectCount; ++i)
				{
					nodes.push_back(scene->CreateEntity(
						"__sparse_bench_" + std::to_string(i),
						GameObjectType::Empty, root->m_index));
				}
			}
			if (!root || nodes.size() != static_cast<size_t>(objectCount)
				|| std::ranges::find(nodes, nullptr) != nodes.end())
			{
				if (root) root->Destroy();
				Scene::SetSparseSpatialResolverEnabled(wasSparse);
				std::printf("[scene.sparseresolver] bench=FAIL create=0\n");
				return CommandCore::Fail("scene.sparseresolver.bench_create",
					"벤치 픽스처를 만들지 못했다(create=0)");
			}

			scene->SyncDerivedState(TransformSyncPoint::Benchmark);
			enum class Scenario { Idle, Leaf, RandomOnePercent, Root, Full };
			const auto scenarioName = [](Scenario scenario)
			{
				switch (scenario)
				{
				case Scenario::Idle: return "idle";
				case Scenario::Leaf: return "leaf";
				case Scenario::RandomOnePercent: return "random-1pct";
				case Scenario::Root: return "root-subtree";
				case Scenario::Full: return "full";
				default: return "unknown";
				}
			};
			struct BenchResult
			{
				double syncMedianUs = 0.0;
				double resolveMedianUs = 0.0;
				uint64_t nodes = 0;
				uint64_t ranges = 0;
			};

			auto runScenario = [&](bool sparse, Scenario scenario)
			{
				Scene::SetSparseSpatialResolverEnabled(sparse);
				scene->MarkSpatialTransformsDirty();
				scene->SyncDerivedState(TransformSyncPoint::Benchmark);
				std::vector<double> syncSamples;
				std::vector<double> resolveSamples;
				syncSamples.reserve(frames);
				resolveSamples.reserve(frames);
				SpatialResolveMetrics last{};
				const int movingOnePercent = (std::max)(1, objectCount / 100);
				for (int frame = -2; frame < frames; ++frame)
				{
					const float x = 0 == (frame & 1) ? 1.f : 2.f;
					switch (scenario)
					{
					case Scenario::Idle: break;
					case Scenario::Leaf:
						nodes.back()->Transform_().SetPosition({ x, 0.f, 0.f });
						break;
					case Scenario::RandomOnePercent:
						for (int i = 0; i < movingOnePercent; ++i)
							nodes[static_cast<size_t>(i)]->Transform_().SetPosition({ x, 0.f, 0.f });
						break;
					case Scenario::Root:
						root->Transform_().SetPosition({ x, 0.f, 0.f });
						break;
					case Scenario::Full:
						for (Entity* node : nodes)
							node->Transform_().SetPosition({ x, 0.f, 0.f });
						break;
					}

					const auto begin = std::chrono::steady_clock::now();
					scene->SyncDerivedState(TransformSyncPoint::Benchmark);
					const auto end = std::chrono::steady_clock::now();
					last = scene->GetLastSpatialResolveMetrics();
					if (frame >= 0)
					{
						syncSamples.push_back(std::chrono::duration<double, std::micro>(end - begin).count());
						resolveSamples.push_back(last.resolveUs);
					}
				}
				std::ranges::sort(syncSamples);
				std::ranges::sort(resolveSamples);
				const auto medianOf = [](const std::vector<double>& values)
				{
					return 0 == (values.size() & 1)
						? (values[values.size() / 2 - 1] + values[values.size() / 2]) * 0.5
						: values[values.size() / 2];
				};
				return BenchResult{ medianOf(syncSamples), medianOf(resolveSamples),
					last.resolvedNodes, last.canonicalRanges };
			};

			constexpr Scenario scenarios[] = {
				Scenario::Idle, Scenario::Leaf, Scenario::RandomOnePercent,
				Scenario::Root, Scenario::Full };
			std::array<BenchResult, std::size(scenarios)> legacy{};
			std::array<BenchResult, std::size(scenarios)> sparse{};
			for (size_t i = 0; i < std::size(scenarios); ++i)
				legacy[i] = runScenario(false, scenarios[i]);
			for (size_t i = 0; i < std::size(scenarios); ++i)
				sparse[i] = runScenario(true, scenarios[i]);

			for (size_t i = 0; i < std::size(scenarios); ++i)
			{
				std::printf(
					"[scene.sparseresolver] bench n=%d mode=legacy scenario=%s "
					"sync-median-us=%.3f resolve-median-us=%.3f\n",
					objectCount, scenarioName(scenarios[i]), legacy[i].syncMedianUs,
					legacy[i].resolveMedianUs);
				std::printf(
					"[scene.sparseresolver] bench n=%d mode=sparse scenario=%s "
					"sync-median-us=%.3f resolve-median-us=%.3f nodes=%llu ranges=%llu\n",
					objectCount, scenarioName(scenarios[i]), sparse[i].syncMedianUs,
					sparse[i].resolveMedianUs,
					static_cast<unsigned long long>(sparse[i].nodes),
					static_cast<unsigned long long>(sparse[i].ranges));
			}
			const bool pass = 1 == sparse[1].nodes && 1 == sparse[1].ranges
				&& static_cast<uint64_t>((std::max)(1, objectCount / 100)) == sparse[2].nodes
				&& sparse[3].nodes >= static_cast<uint64_t>(objectCount)
				&& sparse[4].nodes >= static_cast<uint64_t>(objectCount)
				&& sparse[4].resolveMedianUs <= legacy[4].resolveMedianUs * 1.10;
			std::printf("[scene.sparseresolver] bench n=%d result=%s full-ratio=%.3f\n",
				objectCount, pass ? "PASS" : "FAIL",
				legacy[4].resolveMedianUs > 0.0
					? sparse[4].resolveMedianUs / legacy[4].resolveMedianUs : 0.0);
			root->Destroy();
			Scene::SetSparseSpatialResolverEnabled(wasSparse);

			CommandCore::CommandData data = CommandCore::CommandData::Object();
			data.Set("mode", CommandCore::CommandData::String("bench"));
			data.Set("objects", CommandCore::CommandData::Int(objectCount));
			data.Set("pass", CommandCore::CommandData::Bool(pass));
			if (!pass)
			{
				return CommandCore::Fail("scene.sparseresolver.bench_failed",
					"bench 판정 실패", std::move(data));
			}
			return CommandCore::Ok("scene.sparseresolver bench PASS", std::move(data));
		}

		if ("probe" != parts[1])
		{
			std::printf("[CLI] 사용법: scene.sparseresolver 0|1|print|probe|bench <N> <frames>\n");
			return CommandCore::InvalidArguments(
				"scene.sparseresolver: 0|1|print|probe|bench <N> <frames>");
		}

		const bool wasSparse = Scene::IsSparseSpatialResolverEnabled();
		Scene::SetSparseSpatialResolverEnabled(true);
		Entity* ancestor = nullptr;
		Entity* bridge = nullptr;
		Entity* leaf = nullptr;
		Entity* sibling = nullptr;
		{
			[[maybe_unused]] auto bulk = scene->BeginHierarchyBulkBuild();
			ancestor = scene->CreateEntity("__sparse_probe_ancestor", GameObjectType::Empty);
			bridge = ancestor ? scene->CreateEntity(
				"__sparse_probe_bridge", GameObjectType::UI, ancestor->m_index) : nullptr;
			leaf = bridge ? scene->CreateEntity(
				"__sparse_probe_leaf", GameObjectType::Empty, bridge->m_index) : nullptr;
			sibling = ancestor ? scene->CreateEntity(
				"__sparse_probe_sibling", GameObjectType::Empty, ancestor->m_index) : nullptr;
		}
		if (!ancestor || !bridge || !leaf || !sibling)
		{
			std::printf("[scene.sparseresolver] probe=FAIL create=0\n");
			Scene::SetSparseSpatialResolverEnabled(wasSparse);
			return CommandCore::Fail("scene.sparseresolver.probe_create",
				"probe 픽스처를 만들지 못했다(create=0)");
		}

		scene->SyncDerivedState(TransformSyncPoint::Benchmark);
		scene->SyncDerivedState(TransformSyncPoint::Benchmark);
		const SpatialResolveMetrics idle = scene->GetLastSpatialResolveMetrics();
		leaf->Transform_().SetPosition({ 1.f, 2.f, 3.f });
		leaf->Transform_().SetPosition({ 4.f, 5.f, 6.f });
		scene->SyncDerivedState(TransformSyncPoint::Benchmark);
		const SpatialResolveMetrics leafOnly = scene->GetLastSpatialResolveMetrics();

		ancestor->Transform_().SetPosition({ 7.f, 0.f, 0.f });
		leaf->Transform_().SetPosition({ 8.f, 0.f, 0.f });
		scene->SyncDerivedState(TransformSyncPoint::Benchmark);
		const SpatialResolveMetrics merged = scene->GetLastSpatialResolveMetrics();
		const math::matrix4x4 sparseWorld = leaf->Transform_().GetWorldMatrix();

		Scene::SetSparseSpatialResolverEnabled(false);
		ancestor->Transform_().SetPosition({ 9.f, 0.f, 0.f });
		leaf->Transform_().SetPosition({ 10.f, 0.f, 0.f });
		scene->SyncDerivedState(TransformSyncPoint::Benchmark);
		const SpatialResolveMetrics legacy = scene->GetLastSpatialResolveMetrics();
		const math::matrix4x4 legacyWorld = leaf->Transform_().GetWorldMatrix();
		Scene::SetSparseSpatialResolverEnabled(true);
		scene->MarkSpatialTransformsDirty();
		scene->SyncDerivedState(TransformSyncPoint::Benchmark);
		const SpatialResolveMetrics returnedSparse = scene->GetLastSpatialResolveMetrics();
		const math::matrix4x4 returnedWorld = leaf->Transform_().GetWorldMatrix();
		const bool abExact = 0 == std::memcmp(
			&legacyWorld, &returnedWorld, sizeof(legacyWorld));

		const bool pass = !idle.resolved
			&& leafOnly.sparseExecuted && 1 == leafOnly.dirtyRequests
			&& 1 == leafOnly.canonicalRanges && 1 == leafOnly.resolvedNodes
			&& merged.sparseExecuted && 2 == merged.dirtyRequests
			&& 1 == merged.canonicalRanges && merged.mergedRequests >= 1
			&& 3 == merged.resolvedNodes
			&& legacy.resolved && !legacy.sparseExecuted
			&& returnedSparse.sparseExecuted && returnedSparse.fullResolve
			&& abExact && sparseWorld != legacyWorld;
		std::printf(
			"[scene.sparseresolver] idle=%s leaf=requests:%llu/ranges:%llu/nodes:%llu "
			"merged=requests:%llu/ranges:%llu/merged:%llu/nodes:%llu\n",
			idle.resolved ? "run" : "empty",
			static_cast<unsigned long long>(leafOnly.dirtyRequests),
			static_cast<unsigned long long>(leafOnly.canonicalRanges),
			static_cast<unsigned long long>(leafOnly.resolvedNodes),
			static_cast<unsigned long long>(merged.dirtyRequests),
			static_cast<unsigned long long>(merged.canonicalRanges),
			static_cast<unsigned long long>(merged.mergedRequests),
			static_cast<unsigned long long>(merged.resolvedNodes));
		std::printf(
			"[scene.sparseresolver] legacy=%s return-sparse=%s/full:%s ab=%s\n",
			legacy.sparseExecuted ? "sparse" : "recursive",
			returnedSparse.sparseExecuted ? "packed" : "recursive",
			returnedSparse.fullResolve ? "yes" : "no", abExact ? "exact" : "changed");
		std::printf("[scene.sparseresolver] probe=%s\n", pass ? "PASS" : "FAIL");

		ancestor->Destroy();
		bridge->Destroy();
		leaf->Destroy();
		sibling->Destroy();
		Scene::SetSparseSpatialResolverEnabled(wasSparse);

		CommandCore::CommandData data = CommandCore::CommandData::Object();
		data.Set("mode", CommandCore::CommandData::String("probe"));
		data.Set("pass", CommandCore::CommandData::Bool(pass));
		if (!pass)
		{
			return CommandCore::Fail("scene.sparseresolver.probe_failed",
				"probe 판정 실패", std::move(data));
		}
		return CommandCore::Ok("scene.sparseresolver probe PASS", std::move(data));
	}

	CommandCore::CommandResult HandleSceneTransformPull(const std::vector<std::string>& parts)
	{
		Scene* scene = SceneManagers->GetActiveScene();
		if (!scene)
		{
		    std::printf("[CLI] 활성 씬 없음\n");
		    return CommandCore::PreconditionFailed("scene.none", "활성 씬이 없다");
		}
		if (parts.size() < 2 || ("print" != parts[1] && "probe" != parts[1]))
		{
			std::printf("[CLI] 사용법: scene.transformpull print|probe\n");
			return CommandCore::InvalidArguments("scene.transformpull: print|probe");
		}

		if ("print" == parts[1])
		{
			const SpatialPullMetrics& m = scene->GetLastSpatialPullMetrics();
			std::printf(
				"[scene.transformpull] attempted=%d resolved=%d packed=%d fallback=%d "
				"stale=%d queue=%s signal=%s path=%llu recomputed=%llu compose=%llu "
				"writes=%llu pending=%llu->%llu epoch=%llu->%llu\n",
				m.attempted ? 1 : 0, m.resolved ? 1 : 0, m.packed ? 1 : 0,
				m.legacyFallback ? 1 : 0, m.staleHandle ? 1 : 0,
				m.queuePreserved ? "kept" : "changed",
				m.propagationSignalPreserved ? "kept" : "changed",
				static_cast<unsigned long long>(m.pathNodes),
				static_cast<unsigned long long>(m.recomputedNodes),
				static_cast<unsigned long long>(m.localComposes),
				static_cast<unsigned long long>(m.worldWrites),
				static_cast<unsigned long long>(m.pendingRequestsBefore),
				static_cast<unsigned long long>(m.pendingRequestsAfter),
				static_cast<unsigned long long>(m.dirtyEpochBefore),
				static_cast<unsigned long long>(m.dirtyEpochAfter));

			CommandCore::CommandData data = CommandCore::CommandData::Object();
			data.Set("recomputedNodes", CommandCore::CommandData::Int(
				static_cast<int64_t>(m.recomputedNodes)));
			return CommandCore::Ok("scene.transformpull print", std::move(data));
		}

		const bool wasSparse = Scene::IsSparseSpatialResolverEnabled();
		Scene::SetSparseSpatialResolverEnabled(true);
		Entity* parent = nullptr;
		Entity* childA = nullptr;
		Entity* childB = nullptr;
		{
			[[maybe_unused]] auto bulk = scene->BeginHierarchyBulkBuild();
			parent = scene->CreateEntity("__pull_probe_parent", GameObjectType::Empty);
			childA = parent ? scene->CreateEntity(
				"__pull_probe_a", GameObjectType::Empty, parent->m_index) : nullptr;
			childB = parent ? scene->CreateEntity(
				"__pull_probe_b", GameObjectType::Empty, parent->m_index) : nullptr;
		}
		if (!parent || !childA || !childB)
		{
			if (parent) parent->Destroy();
			Scene::SetSparseSpatialResolverEnabled(wasSparse);
			std::printf("[scene.transformpull] probe=FAIL create=0\n");
			return CommandCore::Fail("scene.transformpull.probe_create",
				"probe 픽스처를 만들지 못했다(create=0)");
		}

		childA->Transform_().SetPosition({ 1.f, 2.f, 3.f });
		childB->Transform_().SetPosition({ 2.f, 0.f, 0.f });
		scene->SyncDerivedState(TransformSyncPoint::Benchmark);
		const EntityHandle childAHandle = scene->HandleOf(childA->m_index);

		const auto nearScalar = [](float a, float b)
		{
			return std::abs(a - b) <= 1e-4f;
		};
		const auto nearVector = [&](const math::vector3& a, const math::vector3& b)
		{
			return nearScalar(a.x, b.x) && nearScalar(a.y, b.y) && nearScalar(a.z, b.z);
		};
		const auto nearQuaternion = [&](const math::quaternion& a, const math::quaternion& b)
		{
			const bool same = nearScalar(a.x, b.x) && nearScalar(a.y, b.y)
				&& nearScalar(a.z, b.z) && nearScalar(a.w, b.w);
			const bool negated = nearScalar(a.x, -b.x) && nearScalar(a.y, -b.y)
				&& nearScalar(a.z, -b.z) && nearScalar(a.w, -b.w);
			return same || negated;
		};

		Transform& transformA = childA->Transform_();
		transformA.SetPosition({ 3.f, 4.f, 5.f }, TransformWriteReason::Script);
		transformA.SetRotation(
			{ 0.f, 0.38268343f, 0.f, 0.92387953f }, TransformWriteReason::Script);
		transformA.SetScale({ 2.f, 3.f, 4.f }, TransformWriteReason::Script);
		bool immediatePulls = scene->EnsureResolved(childAHandle);
		SpatialPullMetrics immediate = scene->GetLastSpatialPullMetrics();
		immediatePulls = immediatePulls && immediate.packed && immediate.queuePreserved
			&& immediate.propagationSignalPreserved
			&& 1 == immediate.pendingRequestsBefore
			&& immediate.pendingRequestsBefore == immediate.pendingRequestsAfter;

		// World setter들도 ClrHost와 같은 순서(먼저 Ensure, setter, 즉시 getter)를 탄다.
		immediatePulls = immediatePulls && scene->EnsureResolved(childAHandle);
		transformA.SetWorldPosition({ 6.f, 7.f, 8.f }, TransformWriteReason::Script);
		immediatePulls = immediatePulls && scene->EnsureResolved(childAHandle)
			&& nearVector(transformA.GetWorldPosition(), { 6.f, 7.f, 8.f });
		immediatePulls = immediatePulls && scene->EnsureResolved(childAHandle);
		const math::quaternion requestedWorldRotation{
			0.25881905f, 0.f, 0.f, 0.96592583f };
		transformA.SetWorldRotation(requestedWorldRotation, TransformWriteReason::Script);
		immediatePulls = immediatePulls && scene->EnsureResolved(childAHandle)
			&& nearQuaternion(transformA.GetWorldQuaternion(), requestedWorldRotation);
		immediatePulls = immediatePulls && scene->EnsureResolved(childAHandle);
		transformA.SetWorldScale({ 1.5f, 2.f, 2.5f }, TransformWriteReason::Script);
		immediatePulls = immediatePulls && scene->EnsureResolved(childAHandle)
			&& nearVector(transformA.GetWorldScale(), { 1.5f, 2.f, 2.5f });

		const math::matrix4x4 immediateWorld = transformA.GetWorldMatrix();
		math::vector3 expectedScale{};
		math::quaternion expectedRotation{};
		math::vector3 expectedPosition{};
		const bool decomposed = math::decompose(
			immediateWorld, expectedScale, expectedRotation, expectedPosition);
		expectedRotation = math::normalize(expectedRotation);
		const bool allGetters = decomposed
			&& nearVector(transformA.GetWorldPosition(), expectedPosition)
			&& nearVector(transformA.GetWorldScale(), expectedScale)
			&& nearQuaternion(transformA.GetWorldQuaternion(), expectedRotation)
			&& nearVector(transformA.GetForward(), math::normalize(math::transform_direction(
				math::vector3::unit_z(), immediateWorld)))
			&& nearVector(transformA.GetRight(), math::normalize(math::transform_direction(
				math::vector3::unit_x(), immediateWorld)))
			&& nearVector(transformA.GetUp(), math::normalize(math::transform_direction(
				math::vector3::unit_y(), immediateWorld)));

		// 첫 묶음의 queue를 비우고 parent-only write가 sibling에 전파되는 경계를 잰다.
		scene->SyncDerivedState(TransformSyncPoint::Benchmark);
		const math::vector3 childBeforeParent = transformA.GetWorldPosition();
		const math::vector3 siblingBefore = childB->Transform_().GetWorldPosition();
		parent->Transform_().SetPosition(
			{ 10.f, 0.f, 0.f }, TransformWriteReason::Script);
		const bool parentPulled = scene->EnsureResolved(childAHandle);
		const SpatialPullMetrics parentPull = scene->GetLastSpatialPullMetrics();
		const math::vector3 childAfterPull = transformA.GetWorldPosition();
		const math::vector3 siblingBeforeGlobal = childB->Transform_().GetWorldPosition();
		const bool targetedBoundary = parentPulled && parentPull.packed
			&& parentPull.queuePreserved && parentPull.propagationSignalPreserved
			&& 1 == parentPull.pendingRequestsBefore
			&& nearScalar(childAfterPull.x, childBeforeParent.x + 10.f)
			&& nearVector(siblingBeforeGlobal, siblingBefore);

		scene->SyncDerivedState(TransformSyncPoint::Benchmark);
		const SpatialResolveMetrics global = scene->GetLastSpatialResolveMetrics();
		const math::vector3 siblingAfterGlobal = childB->Transform_().GetWorldPosition();
		const bool siblingPropagated = nearScalar(
			siblingAfterGlobal.x, siblingBefore.x + 10.f)
			&& global.sparseExecuted && 1 == global.dirtyRequests
			&& 1 == global.canonicalRanges && global.resolvedNodes >= 3;

		const bool cleanResolved = scene->EnsureResolved(childAHandle);
		const SpatialPullMetrics clean = scene->GetLastSpatialPullMetrics();
		EntityHandle stale = childAHandle;
		if (0 == ++stale.generation) ++stale.generation;
		const bool staleRejected = !scene->EnsureResolved(stale);
		const SpatialPullMetrics staleMetrics = scene->GetLastSpatialPullMetrics();
		const bool failClose = staleRejected && staleMetrics.staleHandle
			&& staleMetrics.queuePreserved;
		const bool cleanEmpty = cleanResolved && clean.packed
			&& 0 == clean.recomputedNodes && 0 == clean.worldWrites
			&& clean.queuePreserved;

		const math::vector3 fallbackBefore = transformA.GetWorldPosition();
		Scene::SetSparseSpatialResolverEnabled(false);
		transformA.SetPosition({ 7.f, 7.f, 8.f }, TransformWriteReason::Script);
		const bool fallbackResolved = scene->EnsureResolved(childAHandle);
		const SpatialPullMetrics fallback = scene->GetLastSpatialPullMetrics();
		const bool fallbackPass = fallbackResolved && fallback.legacyFallback
			&& fallback.queuePreserved && fallback.propagationSignalPreserved
			&& nearScalar(transformA.GetWorldPosition().x, fallbackBefore.x + 1.f);
		Scene::SetSparseSpatialResolverEnabled(true);
		scene->MarkSpatialTransformsDirty();
		scene->SyncDerivedState(TransformSyncPoint::Benchmark);

		const bool pass = immediatePulls && allGetters && targetedBoundary
			&& siblingPropagated && cleanEmpty && failClose && fallbackPass;
		std::printf(
			"[scene.transformpull] immediate=%s getters=%s queue=%llu->%llu signal=%s "
			"path=%llu recomputed=%llu writes=%llu\n",
			immediatePulls ? "PASS" : "FAIL", allGetters ? "PASS" : "FAIL",
			static_cast<unsigned long long>(immediate.pendingRequestsBefore),
			static_cast<unsigned long long>(immediate.pendingRequestsAfter),
			immediate.propagationSignalPreserved ? "kept" : "changed",
			static_cast<unsigned long long>(immediate.pathNodes),
			static_cast<unsigned long long>(immediate.recomputedNodes),
			static_cast<unsigned long long>(immediate.worldWrites));
		std::printf(
			"[scene.transformpull] parent-pull=%s sibling-before=%s sibling-global=%s "
			"requests=%llu ranges=%llu nodes=%llu\n",
			targetedBoundary ? "PASS" : "FAIL",
			nearVector(siblingBeforeGlobal, siblingBefore) ? "stale" : "changed",
			siblingPropagated ? "updated" : "stale",
			static_cast<unsigned long long>(global.dirtyRequests),
			static_cast<unsigned long long>(global.canonicalRanges),
			static_cast<unsigned long long>(global.resolvedNodes));
		std::printf("[scene.transformpull] clean=%s stale=%s fallback=%s probe=%s\n",
			cleanEmpty ? "empty" : "work", failClose ? "fail-close" : "accepted",
			fallbackPass ? "PASS" : "FAIL", pass ? "PASS" : "FAIL");

		parent->Destroy();
		Scene::SetSparseSpatialResolverEnabled(wasSparse);

		CommandCore::CommandData data = CommandCore::CommandData::Object();
		data.Set("pass", CommandCore::CommandData::Bool(pass));
		data.Set("fallbackPass", CommandCore::CommandData::Bool(fallbackPass));
		if (!pass)
		{
			return CommandCore::Fail("scene.transformpull.probe_failed",
				"probe 판정 실패", std::move(data));
		}
		return CommandCore::Ok("scene.transformpull probe PASS", std::move(data));
	}

	CommandCore::CommandResult HandleSceneTransformBulk(const std::vector<std::string>& parts)
	{
		Scene* scene = SceneManagers->GetActiveScene();
		if (!scene)
		{
		    std::printf("[CLI] 활성 씬 없음\n");
		    return CommandCore::PreconditionFailed("scene.none", "활성 씬이 없다");
		}
		if (parts.size() != 4 || "probe" != parts[1])
		{
			std::printf("[CLI] 사용법: scene.transformbulk probe <pose-model-path> <rebind-model-path>\n");
			return CommandCore::InvalidArguments("scene.transformbulk: probe");
		}

		const auto pose = [](const math::vector3& position)
		{
			return math::compose(math::vector3{ 1.f, 1.f, 1.f },
				math::quaternion{ 0.f, 0.f, 0.f, 1.f }, position);
		};
		const auto nearVector = [](const math::vector3& lhs, const math::vector3& rhs)
		{
			return std::abs(lhs.x - rhs.x) <= 1e-4f
				&& std::abs(lhs.y - rhs.y) <= 1e-4f
				&& std::abs(lhs.z - rhs.z) <= 1e-4f;
		};
		// The caller owns both fixture inputs; the probe requires two distinct skeleton identities.
		const auto poseModel = DataSystems->LoadModelAssetGenerationByPath(parts[2]);
		const auto rebindModel = DataSystems->LoadModelAssetGenerationByPath(parts[3]);
		const assets::ModelSkeletonAsset* poseSkeleton =
			poseModel ? poseModel->Skeleton() : nullptr;
		if (nullptr == poseSkeleton || poseSkeleton->bones.size() < 2
			|| !rebindModel || nullptr == rebindModel->Skeleton()
            || poseModel == rebindModel)
		{
			std::printf("[scene.transformbulk] probe=FAIL skeleton=0\n");
			// 스켈레톤 자산이 없으면 잴 것이 없다. 자산 부재는 이 기계의 사정이라
			// precondition 이고, 픽스처 생성 실패(아래 create=0)와는 다른 사건이다.
			return CommandCore::PreconditionFailed("scene.transformbulk.no_skeleton",
				"스켈레톤이 없다(skeleton=0)");
		}
		const std::string boneAName = poseSkeleton->bones[0].name;
		const std::string boneBName = poseSkeleton->bones[1].name;
		const auto bindAnimator = [](Animator& target, const FileGuid& modelId)
		{
			target.m_Motion = modelId;
			target.EnsureAnimationBinding();
		};

		Entity* animatorRoot = nullptr;
		Entity* boneA = nullptr;
		Entity* boneB = nullptr;
		Entity* invalidBone = nullptr;
		Entity* physicsParent = nullptr;
		Entity* physicsChild = nullptr;
		Entity* socketTarget = nullptr;
		Animator* animator = nullptr;
		{
			[[maybe_unused]] auto bulk = scene->BeginHierarchyBulkBuild();
			animatorRoot = scene->CreateEntity(
				"__bulk_animator", GameObjectType::Empty);
			animator = animatorRoot ? animatorRoot->AddComponent<Animator>() : nullptr;
			boneA = animatorRoot ? scene->CreateEntity(
				boneAName, GameObjectType::Empty, animatorRoot->m_index) : nullptr;
			boneB = boneA ? scene->CreateEntity(
				boneBName, GameObjectType::Empty, boneA->m_index) : nullptr;
			invalidBone = animatorRoot ? scene->CreateEntity(
				"Missing", GameObjectType::Empty, animatorRoot->m_index) : nullptr;
			if (boneA) boneA->AddComponent<BoneComponent>();
			if (boneB) boneB->AddComponent<BoneComponent>();
			if (invalidBone) invalidBone->AddComponent<BoneComponent>();

			physicsParent = scene->CreateEntity(
				"__bulk_rigidbody", GameObjectType::Empty);
			physicsChild = physicsParent ? scene->CreateEntity(
				"__bulk_cct", GameObjectType::Empty, physicsParent->m_index) : nullptr;
			socketTarget = scene->CreateEntity(
				"__bulk_socket", GameObjectType::Empty);
		}
		if (!animatorRoot || !animator || !boneA || !boneB || !invalidBone
			|| !physicsParent || !physicsChild || !socketTarget)
		{
			if (animatorRoot) animatorRoot->Destroy();
			if (physicsParent) physicsParent->Destroy();
			if (socketTarget) socketTarget->Destroy();
			std::printf("[scene.transformbulk] probe=FAIL create=0\n");
			return CommandCore::Fail("scene.transformbulk.probe_create",
				"probe 픽스처를 만들지 못했다(create=0)");
		}

		bindAnimator(*animator, FileGuid(poseModel->Identity().modelId));
		invalidBone->Transform_().SetPosition({ 0.f, 7.f, 0.f });
		scene->SyncDerivedState(TransformSyncPoint::Benchmark);

		animator->m_localTransforms[0] = pose({ 1.f, 0.f, 0.f });
		animator->m_localTransforms[1] = pose({ 0.f, 2.f, 0.f });
		const AnimatorPoseUploadMetrics first = scene->PublishAnimatorPose(*animator);
		scene->SyncDerivedState(TransformSyncPoint::Benchmark);
		const bool firstWorld = nearVector(
			boneB->Transform_().GetWorldPosition(), { 1.f, 2.f, 0.f });
		const bool invalidHeld = nearVector(
			invalidBone->Transform_().GetWorldPosition(), { 0.f, 7.f, 0.f });

		animator->m_localTransforms[0] = pose({ 3.f, 0.f, 0.f });
		animator->m_localTransforms[1] = pose({ 0.f, 4.f, 0.f });
		const AnimatorPoseUploadMetrics steady = scene->PublishAnimatorPose(*animator);
		scene->SyncDerivedState(TransformSyncPoint::Benchmark);
		const bool steadyWorld = nearVector(
			boneB->Transform_().GetWorldPosition(), { 3.f, 4.f, 0.f });

		animator->SetEnabled(false);
		animator->m_localTransforms[0] = pose({ 9.f, 0.f, 0.f });
		const AnimatorPoseUploadMetrics disabled = scene->PublishAnimatorPose(*animator);
		scene->SyncDerivedState(TransformSyncPoint::Benchmark);
		const bool disabledHeld = nearVector(
			boneB->Transform_().GetWorldPosition(), { 3.f, 4.f, 0.f });
		animator->SetEnabled(true);
		const AnimatorPoseUploadMetrics reenabled = scene->PublishAnimatorPose(*animator);
		scene->SyncDerivedState(TransformSyncPoint::Benchmark);
		const bool reenabledWorld = nearVector(
			boneB->Transform_().GetWorldPosition(), { 9.f, 4.f, 0.f });

		bindAnimator(*animator, FileGuid(rebindModel->Identity().modelId));
		const AnimatorPoseUploadMetrics reloaded = scene->PublishAnimatorPose(*animator);
		scene->SyncDerivedState(TransformSyncPoint::Benchmark);
		const bool animatorPass = first.packed && first.rebound
			&& 3 == first.bindLookups && 2 == first.validBones
			&& 1 == first.invalidBones && 1 == first.queuedRoots
			&& firstWorld && invalidHeld
			&& steady.packed && !steady.rebound && 0 == steady.bindLookups
			&& steadyWorld && disabled.disabled && disabledHeld
			&& reenabled.packed && !reenabled.rebound
			&& 0 == reenabled.bindLookups && reenabledWorld
			&& reloaded.rebound && 3 == reloaded.bindLookups;

		const std::array<TransformWorldWrite, 2> physicsWrites{
			TransformWorldWrite{ scene->HandleOf(physicsParent->m_index),
				pose({ 10.f, 0.f, 0.f }) },
			TransformWorldWrite{ scene->HandleOf(physicsChild->m_index),
				pose({ 12.f, 3.f, 0.f }) }
		};
		const TransformBulkWriteMetrics physics = scene->ApplyWorldWriteBatch(
			physicsWrites, TransformWriteReason::Physics);
		const bool physicsImmediate = nearVector(
			physicsParent->Transform_().GetWorldPosition(), { 10.f, 0.f, 0.f })
			&& nearVector(physicsChild->Transform_().GetWorldPosition(), { 12.f, 3.f, 0.f });
		scene->SyncDerivedState(TransformSyncPoint::Benchmark);
		const bool physicsGlobal = nearVector(
			physicsChild->Transform_().GetWorldPosition(), { 12.f, 3.f, 0.f });
		const bool physicsPass = physics.packed && 2 == physics.requested
			&& 2 == physics.accepted && 0 == physics.stale
			&& 1 == physics.epochAdvances && physicsImmediate && physicsGlobal;

		Socket socket;
		socket.AttachObject(socketTarget);
		socket.transform.SetLocalMatrix(
			pose({ 4.f, 5.f, 6.f }), TransformWriteReason::Animator);
		socket.Update();
		scene->SyncDerivedState(TransformSyncPoint::Benchmark);
		const bool socketPass = nearVector(
			socketTarget->Transform_().GetWorldPosition(), { 4.f, 5.f, 6.f });
		socket.DetachAllObject();

		const bool pass = animatorPass && physicsPass && socketPass;
		std::printf(
			"[scene.transformbulk] animator=%s first-lookups=%llu valid=%llu invalid=%llu "
			"steady-lookups=%llu off=%s on-lookups=%llu reload-lookups=%llu\n",
			animatorPass ? "PASS" : "FAIL",
			static_cast<unsigned long long>(first.bindLookups),
			static_cast<unsigned long long>(first.validBones),
			static_cast<unsigned long long>(first.invalidBones),
			static_cast<unsigned long long>(steady.bindLookups),
			disabledHeld ? "held" : "changed",
			static_cast<unsigned long long>(reenabled.bindLookups),
			static_cast<unsigned long long>(reloaded.bindLookups));
		std::printf(
			"[scene.transformbulk] physics=%s requested=%llu accepted=%llu epoch=%llu "
			"immediate=%s global=%s socket=%s\n",
			physicsPass ? "PASS" : "FAIL",
			static_cast<unsigned long long>(physics.requested),
			static_cast<unsigned long long>(physics.accepted),
			static_cast<unsigned long long>(physics.epochAdvances),
			physicsImmediate ? "PASS" : "FAIL",
			physicsGlobal ? "PASS" : "FAIL", socketPass ? "PASS" : "FAIL");
		std::printf("[scene.transformbulk] barrier=main-thread invalid=%s probe=%s\n",
			invalidHeld ? "held" : "changed", pass ? "PASS" : "FAIL");

		bindAnimator(*animator, FileGuid{});
		animatorRoot->Destroy();
		physicsParent->Destroy();
		socketTarget->Destroy();

		CommandCore::CommandData data = CommandCore::CommandData::Object();
		data.Set("pass", CommandCore::CommandData::Bool(pass));
		if (!pass)
		{
			return CommandCore::Fail("scene.transformbulk.probe_failed",
				"probe 판정 실패", std::move(data));
		}
		return CommandCore::Ok("scene.transformbulk probe <pose-model-path> <rebind-model-path> PASS", std::move(data));
	}

    // X0 측정 게이트(TransformUpdatePlan). flat fan-out, wide tree, deep chain,
	// skeleton-like forest를 같은 objectCount로 합성해 "전부 정지"·"10% 이동"을
	// 각각 잰다. UI/Spatial wall과 Spatial 내부 worker CPU 합계를 분리해 출력하고,
	// 끝나면 만든 오브젝트를 전부 파괴 마크해 씬을 원상 복구한다. 이 명령 혼자서는
	// 이후 X1~X6 변경의 성능을 말하지 않는다(아직 구현 전 기준선일 뿐이다).
    //
    // ★ 레인 2(트랙 E E7-b 측정 준비) — <오브젝트수> 0은 합성을 건너뛰고 "현재
    // 씬을 그대로" 잰다. Bone 판정(순회의 Scene::UpdateModelRecursive Bone
    // 분기)의 이득은 합성 Empty 트리가 아니라 실제 뼈가 있는 저작 씬에서만
    // 드러나기 때문이다. 시나리오는 "전부 정지" 하나뿐이다(판단) — "10% 이동"은
    // 대상을 골라 매 프레임 실제 Transform 위치를 덮어쓰고 되돌리지 않는데,
    // 합성 오브젝트라면 무해해도 저작 오브젝트에 그대로 쓰면 씬을 오염시킨다.
    CommandCore::CommandResult HandleSceneTraversalBench(const std::vector<std::string>& parts)
    {
        if (parts.size() < 3)
        {
			std::printf("[CLI] 사용법: scene.traversalbench <오브젝트수> <프레임수> [flat|wide|deep|skeleton] (0 = 현재 씬)\n");
            return CommandCore::InvalidArguments(
                "scene.traversalbench: <오브젝트수> <프레임수> 가 필요하다");
        }

        const int objectCount = (std::max)(0, std::atoi(parts[1].c_str()));
		const int frames = (std::max)(4, std::atoi(parts[2].c_str()));
		const std::string topology = 0 == objectCount ? "current"
			: (parts.size() > 3 ? parts[3] : "wide");
		if (0 != objectCount && "flat" != topology && "wide" != topology
			&& "deep" != topology && "skeleton" != topology)
		{
			std::printf("[CLI] topology는 flat|wide|deep|skeleton 중 하나여야 한다\n");
			return CommandCore::InvalidArguments(
				"scene.traversalbench: topology 는 flat|wide|deep|skeleton 이다");
		}

        Scene* scene = SceneManagers->GetActiveScene();
        if (nullptr == scene)
        {
            std::printf("[CLI] 활성 씬 없음\n");
            return CommandCore::PreconditionFailed("scene.none", "활성 씬이 없다");
        }

		const bool diagnosticsWasEnabled = Scene::IsTransformDiagnosticsEnabled();
		struct DiagnosticsRestore
		{
			bool previous = false;
			~DiagnosticsRestore()
			{
				Scene::SetTransformDiagnosticsEnabled(previous);
			}
		} diagnosticsRestore{ diagnosticsWasEnabled };
		Scene::SetTransformDiagnosticsEnabled(true);
		scene->ResetTransformDiagnostics();
		const TransformTopologyMutationCounters topologyBeforeSetup =
			scene->GetTopologyMutationTotals();

		constexpr size_t kWideWidth = 64;
		constexpr size_t kDeepChainLength = 64; // 현재 순환 방어 깊이와 같은 상한
		constexpr size_t kSkeletonNodeCount = 64;

        // 0개 모드는 아무것도 만들지 않으므로 둘 다 빈 채로 남는다 — 정리 단계와
        // "10% 이동" 시나리오가 아래에서 이 빈 상태를 보고 스스로 건너뛴다.
        std::vector<GameObjectIndex> created;
        std::vector<GameObjectIndex> movers;

        if (0 == objectCount)
        {
			// 만들지 않고 이미 있는 것을 센다 — 무엇을 쟀는지 모르면 수치가
			// 의미가 없다. E7-c 이후 뼈 정체성은 BoneComponent 하나가 정본이다.
            size_t liveObjectCount = 0;
            size_t markedCount = 0;
            for (const auto& obj : scene->m_Entities)
            {
                if (!obj || obj->IsDestroyMark()) continue;
                ++liveObjectCount;
                if (obj->GetComponent<BoneComponent>()) ++markedCount;
            }

            if (0 == liveObjectCount)
            {
                // 0개를 재고 "빠르다"고 보고하는 사고를 막는다 — scene.proxybench
                // (HandleSceneProxyBench)와 같은 하한 가드 관례.
                std::printf("[CLI] scene.traversalbench: 활성 씬에 오브젝트가 0개다 — 잴 것이 없다\n");
                return CommandCore::PreconditionFailed("scene.traversalbench.empty",
                    "활성 씬에 오브젝트가 0개다");
            }

            char header[192]{};
            std::snprintf(header, sizeof(header),
				"[scene.traversalbench] build=%s topology=current domains=UI+Spatial objects=%zu bones=%zu dirtytraversal=%s bonecache=%s frames=%d warmup=2",
				TransformBuildConfiguration(), liveObjectCount, markedCount, Scene::IsDirtyTraversalEnabled() ? "1" : "0",
				Scene::IsBoneCacheEnabled() ? "1" : "0", frames);
            std::printf("%s\n", header);
            Debug->LogWarning(header);
        }
        else
        {
			const GameObjectIndex sceneRoot = Entity::kSceneRootIndex;
			for (int madeCount = 0; madeCount < objectCount; ++madeCount)
			{
				GameObjectIndex parent = sceneRoot;
				const size_t index = static_cast<size_t>(madeCount);
				if ("wide" == topology && index > 0)
				{
					parent = created[(index - 1) / kWideWidth];
				}
				else if ("deep" == topology && 0 != index % kDeepChainLength)
				{
					parent = created.back();
				}
				else if ("skeleton" == topology)
				{
					const size_t local = index % kSkeletonNodeCount;
					const size_t base = index - local;
					if (local > 0 && local < 16)
					{
						parent = created.back(); // spine chain
					}
					else if (local >= 16)
					{
						const size_t branch = (local - 16) / 4;
						const size_t branchOffset = (local - 16) % 4;
						parent = 0 == branchOffset
							? created[base + 2 + branch % 12]
							: created.back();
					}
				}

				Entity* child = scene->CreateEntity(
					"__bench_" + std::to_string(madeCount),
					GameObjectType::Empty, parent);
				if (!child)
				{
					std::printf("[CLI] scene.traversalbench: 합성 생성 실패 index=%d\n",
						madeCount);
					break;
				}
				created.push_back(child->m_index);
			}

			if (created.empty())
			{
				std::printf("[CLI] scene.traversalbench: 합성 오브젝트 생성 실패\n");
				return CommandCore::Fail("scene.traversalbench.create_failed",
					"합성 오브젝트를 만들지 못했다");
			}

			// 10%만 매 프레임 이동시킬 대상.
			for (size_t i = 9; i < created.size(); i += 10)
            {
                movers.push_back(created[i]);
            }

			char header[256]{};
            std::snprintf(header, sizeof(header),
				"[scene.traversalbench] build=%s topology=%s domains=UI+Spatial objects=%zu dirtytraversal=%s frames=%d warmup=2",
				TransformBuildConfiguration(), topology.c_str(), created.size(),
				Scene::IsDirtyTraversalEnabled() ? "1" : "0", frames);
            std::printf("%s\n", header);
            Debug->LogWarning(header);
        }

        const auto runScenario = [&](const char* label, bool moveEach)
        {
			// 첫 패스는 새 슬롯의 dirty를 소비하고 월드를 쓴다. 그 쓰기가 세운
			// worldChanged를 둘째 패스가 소비해야 비로소 정지 steady-state다.
			// 둘 다 버리고 난 뒤부터 측정한다.
			scene->AllUpdateWorldMatrix(TransformSyncPoint::Benchmark);
			scene->AllUpdateWorldMatrix(TransformSyncPoint::Benchmark);

			std::vector<double> totalSamples;
			std::vector<double> uiSamples;
			std::vector<double> spatialSamples;
			std::vector<double> dispatchSamples;
			std::vector<double> visitSamples;
			std::vector<double> composeSamples;
			std::vector<double> multiplySamples;
			std::vector<double> decomposeSamples;
			for (auto* samples : { &totalSamples, &uiSamples, &spatialSamples,
				&dispatchSamples, &visitSamples, &composeSamples,
				&multiplySamples, &decomposeSamples })
			{
				samples->reserve(static_cast<size_t>(frames));
			}
			const TransformTopologyMutationCounters topologyBefore =
				scene->GetTopologyMutationTotals();
			TransformUpdateMetrics lastMetrics{};
            for (int f = 0; f < frames; ++f)
            {
                if (moveEach)
                {
                    for (size_t i = 0; i < movers.size(); ++i)
                    {
                        if (Entity* mover = scene->GetEntityRaw(movers[i]))
                        {
                            const float x = static_cast<float>((f + static_cast<int>(i)) % 100) * 0.01f;
                            mover->Transform_().SetPosition(math::vector3{ x, 0.f, 0.f });
                        }
                    }
                }

				scene->AllUpdateWorldMatrix(TransformSyncPoint::Benchmark);
				lastMetrics = scene->GetLastTransformUpdateMetrics(
					TransformSyncPoint::Benchmark);
				totalSamples.push_back(lastMetrics.totalUs);
				uiSamples.push_back(lastMetrics.uiUs);
				spatialSamples.push_back(lastMetrics.spatialUs);
				dispatchSamples.push_back(lastMetrics.dispatchUs);
				visitSamples.push_back(lastMetrics.visitWorkerUs);
				composeSamples.push_back(lastMetrics.localComposeWorkerUs);
				multiplySamples.push_back(lastMetrics.worldMultiplyWorkerUs);
				decomposeSamples.push_back(lastMetrics.decomposeWorkerUs);
            }

			const BenchPercentiles total = SummarizeBenchSamples(totalSamples);
			const BenchPercentiles ui = SummarizeBenchSamples(uiSamples);
			const BenchPercentiles spatial = SummarizeBenchSamples(spatialSamples);
			const BenchPercentiles dispatch = SummarizeBenchSamples(dispatchSamples);
			const BenchPercentiles visit = SummarizeBenchSamples(visitSamples);
			const BenchPercentiles compose = SummarizeBenchSamples(composeSamples);
			const BenchPercentiles multiply = SummarizeBenchSamples(multiplySamples);
			const BenchPercentiles decompose = SummarizeBenchSamples(decomposeSamples);

			char line[640]{};
            std::snprintf(line, sizeof(line),
				"[scene.traversalbench] %s wall-us median/p95/max "
				"total=%.2f/%.2f/%.2f ui=%.2f/%.2f/%.2f "
				"spatial=%.2f/%.2f/%.2f dispatch=%.2f/%.2f/%.2f frames=%d",
				label,
				total.median, total.p95, total.maximum,
				ui.median, ui.p95, ui.maximum,
				spatial.median, spatial.p95, spatial.maximum,
				dispatch.median, dispatch.p95, dispatch.maximum, frames);
            std::printf("%s\n", line);
            Debug->LogWarning(line);

			std::snprintf(line, sizeof(line),
				"[scene.traversalbench] %s spatial-worker-sum-us median/p95/max "
				"visit=%.2f/%.2f/%.2f compose=%.2f/%.2f/%.2f "
				"multiply=%.2f/%.2f/%.2f decompose=%.2f/%.2f/%.2f",
				label,
				visit.median, visit.p95, visit.maximum,
				compose.median, compose.p95, compose.maximum,
				multiply.median, multiply.p95, multiply.maximum,
				decompose.median, decompose.p95, decompose.maximum);
			std::printf("%s\n", line);
			Debug->LogWarning(line);

			PrintTransformMetricSnapshot(lastMetrics);
			const TransformTopologyMutationCounters topologyAfter =
				scene->GetTopologyMutationTotals();
			std::printf(
				"[scene.traversalbench] %s topology measured-total create=%llu destroy=%llu reparent=%llu per-frame=%.3f/%.3f/%.3f\n",
				label,
				static_cast<unsigned long long>(topologyAfter.created - topologyBefore.created),
				static_cast<unsigned long long>(topologyAfter.destroyed - topologyBefore.destroyed),
				static_cast<unsigned long long>(topologyAfter.reparented - topologyBefore.reparented),
				static_cast<double>(topologyAfter.created - topologyBefore.created) / frames,
				static_cast<double>(topologyAfter.destroyed - topologyBefore.destroyed) / frames,
				static_cast<double>(topologyAfter.reparented - topologyBefore.reparented) / frames);
        };

		const TransformTopologyMutationCounters topologyAfterSetup =
			scene->GetTopologyMutationTotals();
		std::printf(
			"[scene.traversalbench] topology setup create=%llu destroy=%llu reparent=%llu; runtime frequency는 scene.transformstats 1 후 print로 last-frame 확인\n",
			static_cast<unsigned long long>(
				topologyAfterSetup.created - topologyBeforeSetup.created),
			static_cast<unsigned long long>(
				topologyAfterSetup.destroyed - topologyBeforeSetup.destroyed),
			static_cast<unsigned long long>(
				topologyAfterSetup.reparented - topologyBeforeSetup.reparented));

        // 0개 모드(현재 씬)는 "전부 정지" 하나만 — 헤더 위에서 이미 적었다.
        // 합성 모드는 기존 그대로 둘 다 잰다.
        runScenario(0 == objectCount ? "전부 정지(현재 씬)" : "전부 정지", false);
        if (0 != objectCount)
        {
            runScenario("10% 매프레임 이동", true);
        }
        else
        {
            // ★ 잰 것이 뼈 경로를 실제로 태웠는지 사후 확인한다.
            //
            // 마커가 붙어 있어도 Scene::UpdateModelRecursive의 뼈 분기는 루트의
            // Animator·Skeleton이 준비되고 켜져 있을 때만 FindBone까지 간다 —
            // 아니면 그 앞에서 return한다. 그 경우 scene.bonecache를 켜든 끄든
            // 같은 수치가 나오고, 그것을 "캐시가 효과 없다"로 오독하게 된다.
            // 해석된 뼈 수(m_boneIndex >= 0)를 함께 찍어 그 오독을 막는다.
            // "해석 0개"는 두 가지 서로 다른 사실을 같은 모습으로 보여 준다:
            // ① 분기 앞의 관문(루트·Animator·Skeleton·활성)에서 되돌아왔다,
            // ② 분기는 탔지만 FindBone이 못 찾았다(스켈레톤의 m_bones가 비었거나
            //    이름이 안 맞는다). 둘은 원인도 조치도 다르므로 관문을 하나씩
            //    되짚어 어디서 끊겼는지 그대로 찍는다 — 숫자 하나만 보고
            //    "캐시가 효과 없다"고 결론 내리는 것을 막는다.
            size_t resolvedCount = 0;
            size_t noRootCount = 0;
            size_t noAnimatorCount = 0;
            size_t noSkeletonCount = 0;
            size_t reachedCount = 0;
            size_t skeletonBoneMax = 0;
            for (const auto& obj : scene->m_Entities)
            {
                if (!obj || obj->IsDestroyMark()) continue;
                BoneComponent* bc = obj->GetComponent<BoneComponent>();
                if (!bc) continue;

                const auto& rootObj = scene->TryGetEntity(obj->GetRootIndex());
                if (!rootObj) { ++noRootCount; continue; }
                const auto& animator = rootObj->GetComponent<Animator>();
                if (!animator || !animator->IsEnabled()) { ++noAnimatorCount; continue; }
                // I6-B3 — 관문과 본 계수가 창구를 탄다. 벤치가 legacy 객체
                // 존재를 관문으로 쓰면 그 객체를 은퇴시킬 수 없다.
                if (0 == animator->GetSkeletonSerial()) { ++noSkeletonCount; continue; }

                ++reachedCount;
                skeletonBoneMax = (std::max)(skeletonBoneMax, animator->GetBoneCount());
                if (bc->GetResolvedBoneIndex() >= 0) ++resolvedCount;
            }
            // 512 — 한글이 UTF-8에서 글자당 3바이트라 이 문장은 값이 다 차면 300바이트를
            // 넘긴다. snprintf는 넘치면 조용히 잘라내므로, 진단이 스스로 잘린 채 나가는
            // 것을 막으려고 여유를 둔다.
            char diag[512]{};
            std::snprintf(diag, sizeof(diag),
                // ★ "도달"이 아니라 "관문 통과"라고 쓴다 — 이 값은 순회가 그 노드에
                // 닿았는지가 아니라, 닿았다면 통과했을 조건(루트·Animator·Skeleton·
                // 활성)을 만족하는지만 센다. 아래 주석의 사건이 정확히 이 두 낱말의
                // 차이에서 났다: "도달 61개"를 읽고 순회는 당연히 닿은 줄 알았는데
                // 실은 서브트리가 통째로 순회 밖이었다. 라벨이 스스로를 설명하면
                // 주석을 안 읽어도 오독하지 않는다.
                "[scene.traversalbench] 뼈 경로 — 마커 보유 중 관문 통과 %zu개(루트없음 %zu · 애니메이터없음/꺼짐 %zu · 스켈레톤없음 %zu)"
                " · 인덱스 해석 %zu개 · 스켈레톤 뼈 수 최대 %zu"
                " ※ 관문 통과 ≠ 순회 도달(도달성은 scene.bonedump)",
                reachedCount, noRootCount, noAnimatorCount, noSkeletonCount, resolvedCount, skeletonBoneMax);
            std::printf("%s\n", diag);
            Debug->LogWarning(diag);

            // ★ 이 관문 계수는 **순회와 무관한 코드**가 자기 힘으로 조상을 타고
            // 올라가 센 값이다 — 순회가 실제로 이 노드에 닿았는지는 재지 않는다.
            // 실제로 한 번 이 차이에 걸렸다: 관문 61개 전부 통과 · 해석 0개가
            // 나왔는데, 진짜 원인은 모델 루트가 씬 루트의 m_childrenIndices에서
            // 빠져 순회가 서브트리를 통째로 건너뛴 것이었다(FindBone은 멀쩡했다).
            // 그래서 해석 0개면 조회가 아니라 도달성부터 의심하도록 안내한다.
            if (0 == resolvedCount && reachedCount > 0)
            {
                std::printf("[scene.traversalbench] ↑ 해석 0개 — 이 관문 계수는 순회 도달성을 재지 않는다."
                    " scene.bonedump으로 이름 조회와 순회 도달성을 갈라 볼 것\n");
            }
        }

        // 정리 — 만든 오브젝트가 있을 때만 파괴 마크해 씬을 원상 복구한다(0개
        // 모드는 애초에 아무것도 만들지 않았으므로 created가 비어 있어 아래
        // 루프가 그냥 아무 일도 안 한다 — 그래도 완료 로그는 만든 게 있을 때만
        // 찍는다, 안 그러면 "0개 파괴 마크 완료"가 매번 찍혀 헷갈린다).
        // 실제 슬롯 회수는 엔진의 정상 프레임 종료 파괴 지점(FlushPendingDestroy →
        // DestroyEntities)이 다음 틱에 한다 — 다른 destroy 계열 콘솔 명령과
        // 같은 규약이다(이 명령은 여기서 프레임을 진행시키지 않는다).
        if (!created.empty())
        {
            for (GameObjectIndex idx : created)
            {
                scene->DestroyEntity(idx);
            }
            std::printf("[CLI] scene.traversalbench: 오브젝트 %zu개 파괴 마크 완료\n", created.size());
        }

        // 벤치는 재는 것이 일이라 PASS/FAIL 판정이 없다. 값만 낸다 —
        // 예산 판정은 `verify-transform-*.ps1` 이 자기 상한으로 한다.
        CommandCore::CommandData data = CommandCore::CommandData::Object();
        data.Set("topology", CommandCore::CommandData::String(topology));
        data.Set("objects", CommandCore::CommandData::Int(objectCount));
        data.Set("frames", CommandCore::CommandData::Int(frames));
        return CommandCore::Ok("scene.traversalbench", std::move(data));
    }
}

// ── 콘솔 명령 디스패치 표 ────────────────────────────────────────────────
//
// 예전에는 Execute 안의 else-if 사슬 하나가 명령 117개를 받았다. 사슬은 단마다
// 블록을 한 겹씩 더 쌓아서 MSVC 중첩 한계(C1061)에 닿았고, 명령을 더 붙일 수가
// 없었다. 이름→핸들러 표로 바꾸면 명령이 몇 개든 중첩 깊이는 1이라 그 한계가
// 구조적으로 사라진다.
//
// 핸들러는 전부 내부 링크(static)다 - 이 파일은 유니티 빌드에 들어가므로
// 외부 링크 심볼을 늘리면 다른 TU와 부딪힐 수 있다.
namespace ConsoleCmd
{
    static std::string ResolveTestArtifactPath(
        std::string_view category, std::string_view requestedPath)
    {
        file::path output(requestedPath);
        if (output.is_relative())
        {
            output = PathFinder::TestArtifactPath(category) / output;
        }
        output = output.lexically_normal();
        std::error_code error{};
        file::create_directories(output.parent_path(), error);
        return output.string();
    }


    struct CommandEntry
    {
        ConsoleCommandResultHandler modern{ nullptr };

        // 예외를 결과로 바꾸지 않고 그대로 빠져나가게 둔다.
        //
        // ★ 이 플래그가 없어서 `crash.test throw` 를 깨뜨렸다.
        //
        //   `Execute` 에 예외 경계를 두자 **일부러 죽는 것이 일인 명령**이 죽지
        //   않게 됐다. 크래시 덤프 경로 회귀가 프로세스 종료를 기다리다 타임아웃
        //   났고, 덤프 검증은 크래시가 나야만 돌아가는 검사라 그대로 사각지대가
        //   될 뻔했다. 예외 경계는 좋은 기본값이지만 기본값에는 예외가 있어야 한다.
        bool letExceptionsEscape{ false };

        // LC7 — 이 명령이 사용자 코드를 부를 수 있는가(§6.2 의 `ExecutesUserCode`).
        //
        // seed 표가 정하고 `ExecuteParsed` 가 읽는다. 참인 명령만 실행 동안
        // `ClrHost::UserCodeScope` 를 열고, 그 창 밖에서의 호출은 `NotPermitted`
        // 로 거부된다. 오늘 참인 것은 `script.invoke` 하나다.
        bool executesUserCode{ false };

        CommandCore::CommandResult Invoke(const ConsoleCommandContext& ctx) const
        {
            return modern ? modern(ctx) : CommandCore::InternalError("command.empty_entry", "Command handler is empty");
        }
    };

    using Table = std::unordered_map<std::string, CommandEntry>;

    Table& CommandletTable() { static Table table; return table; }

    const Table& GetTable()
    {
        static const Table table = []
        {
            Table t;

            // 등록 본체. 두 서명이 같은 경로로 들어가야 중복 검사·inventory 가
            // 한 곳에 남는다.
            auto regEntry = [&t](std::initializer_list<const char*> names,
                                 CommandEntry entry)
            {
                // LC0: canonical↔alias 관계와 handler 그룹을 한 벌 더 남긴다.
                //
                // 아래 표는 이름마다 entry를 만들기 때문에 "quit과 exit이 같은
                // 명령"이라는 사실을 잃는다. 조회에는 필요 없지만 inventory에는
                // 필요하다 — 219개 이름이 몇 개의 명령인지가 계약의 크기다.
                // 조회 표는 건드리지 않는다(거동 변경 0).
                //
                // ★ 등록을 **먼저 하고** 그 결과를 갈라 넘긴다. emplace는 같은
                //   이름이 두 번 오면 나중 것을 조용히 버리는데, 시도한 이름을
                //   그대로 inventory에 적으면 실제로는 죽어 있는 이름이 "이
                //   handler로 간다"고 주장하게 된다. LC0이 없애려는 drift를
                //   LC0이 만드는 셈이라, 살아남은 이름과 버려진 이름을 나눈다.
                const auto* registrationSeed = CommandCore::FindDescriptorSeed(*names.begin());
                const bool commandlet = registrationSeed && registrationSeed->commandlet;
                Table& destination = commandlet ? CommandletTable() : t;
                std::vector<const char*> accepted;
                std::vector<const char*> rejected;
                for (const char* n : names)
                {
                    // 같은 이름을 두 번 등록하면 조용히 한쪽이 먹힌다.
                    const bool inserted = destination.emplace(n, entry).second;
                    if (!inserted) { std::printf("[CLI] 명령 이름 중복 등록: %s\n", n); }
                    (inserted ? accepted : rejected).push_back(n);
                }


                // ── LC3: schema 를 함께 세운다 ──────────────────────────
                //
                // descriptor 가 없으면 registry 가 문제로 기록하고, selftest 가
                // 그것을 판정한다. 서명이 요구하지 않으면 아무도 안 쓴다 —
                // 78 개가 help 에 없던 이유가 그것이므로, 여기서 막는다.
                CommandCore::CommandRegistry& registry = commandlet ? CommandCore::CommandRegistry::Commandlets() : CommandCore::CommandRegistry::Get();

                // 조회 표에 못 들어간 이름을 registry 에도 알린다.
                //
                // 이것이 없으면 `Add` 안의 중복 검사가 죽은 코드다 — 진 이름은
                // descriptor 에 애초에 담기지 않으므로 검사가 볼 것이 없다.
                // 그 상태에서 `commands.selftest` 는 "조용히 한쪽이 먹혔다"를
                // 못 잡으면서 잡는다고 주장하게 된다.
                const std::string canonicalForReport =
                    accepted.empty() ? std::string(*names.begin()) : accepted.front();
                for (const char* name : rejected)
                {
                    registry.RecordRejectedName(name, canonicalForReport);
                }

                if (accepted.empty()) return;

                const std::string canonical = accepted.front();
                const CommandCore::DescriptorSeed* seed =
                    CommandCore::FindDescriptorSeed(canonical);

                CommandCore::CommandDescriptor descriptor;
                descriptor.canonical        = canonical;
                descriptor.resultBearing    = (nullptr != entry.modern);
                descriptor.exceptionsEscape = entry.letExceptionsEscape;
                for (std::size_t i = 1; i < accepted.size(); ++i)
                {
                    descriptor.aliases.emplace_back(accepted[i]);
                }

                if (nullptr != seed)
                {
                    descriptor.summary  = seed->summary;
                    descriptor.usage    = seed->usage;
                    descriptor.cost     = seed->cost;
                    descriptor.cls      = seed->cls;
                    descriptor.liveness = seed->liveness;
                    descriptor.roles    = seed->roles;   // LC8
                    descriptor.undoable = seed->undoable;
                    const std::string fields = seed->namedParameters;
                    descriptor.hasNamedInput = !fields.empty();
                    if (fields != "()") for (std::size_t start = 0; start < fields.size();)
                    {
                        const auto comma = fields.find(',', start);
                        std::string spec = fields.substr(start, comma == std::string::npos ? comma : comma - start);
                        CommandCore::CommandParameter parameter;
                        const auto equal = spec.find('=');
                        parameter.optional = equal != std::string::npos;
                        if (parameter.optional) { parameter.defaultValue = spec.substr(equal + 1); spec.resize(equal); }
                        const auto colon = spec.find(':');
                        parameter.name = spec.substr(0, colon);
                        if (colon != std::string::npos) parameter.kind = spec.substr(colon + 1);
                        descriptor.namedParameters.push_back(std::move(parameter));
                        if (comma == std::string::npos) break;
                        start = comma + 1;
                    }

                    // ── LC7: 사용자 코드 창을 여는 권한을 조회 표에도 박는다 ──
                    //
                    // descriptor 는 discovery 가 읽고, 조회 표는 **실행이** 읽는다.
                    // `ExecuteParsed` 가 매 명령마다 registry 를 다시 뒤지지 않게
                    // 하려고 여기서 한 번 옮겨 둔다.
                    //
                    // ★ 등록된 **이름 전부**에 박는다. 별칭으로 부른 호출이 창을
                    //   못 열면 같은 명령이 이름에 따라 다르게 동작한다 — 오늘
                    //   `script.invoke` 는 별칭이 없지만, 없다는 사실에 기대어
                    //   canonical 만 칠하면 별칭이 붙는 날 조용히 갈라진다.
                    descriptor.executesUserCode = seed->executesUserCode;
                    if (seed->executesUserCode)
                    {
                        for (const char* name : accepted) destination[name].executesUserCode = true;
                    }
                }
                // summary 가 비면 Add 가 거부하고 사유를 남긴다.
                registry.Add(std::move(descriptor));
            };

            auto regResult = [&regEntry](std::initializer_list<const char*> names,
                                         ConsoleCommandResultHandler fn)
            {
                CommandEntry entry;
                entry.modern = fn;
                regEntry(names, entry);
            };

            // 일부러 프로세스를 죽이는 명령. 예외 경계를 통과시킨다.
            auto regEscaping = [&regEntry](std::initializer_list<const char*> names,
                                           ConsoleCommandResultHandler fn)
            {
                CommandEntry entry;
                entry.modern              = fn;
                entry.letExceptionsEscape = true;
                regEntry(names, entry);
            };

            // ── LC6: 도메인 TU 가 자기 명령을 등록한다 ──────────────────
            //
            // 창구 하나를 넘겨 주고, 도메인은 "무엇을 등록할지"만 말한다.
            // 등록에 붙는 일 넷(조회 표 · 중복 보고 · descriptor · inventory)은
            // 위의 `regEntry` 한 곳에 그대로 남는다 — 그 넷을 도메인마다
            // 되풀이하게 두면 한 곳만 빠뜨려도 그 도메인이 조용히
            // half-registered 가 된다.
            // 가상 함수가 있으므로 집합체가 아니다 — 생성자로 묶는다.
            struct TableRegistrar final : Registrar
            {
                decltype(regResult)*   result;
                decltype(regEscaping)* escaping;

                TableRegistrar(decltype(regResult)& r,
                               decltype(regEscaping)& e) noexcept
                    : result(&r), escaping(&e) {}

                void Result(std::initializer_list<const char*> names,
                            ConsoleCommandResultHandler fn) override { (*result)(names, fn); }
                void Escaping(std::initializer_list<const char*> names,
                              ConsoleCommandResultHandler fn) override { (*escaping)(names, fn); }

            };
            TableRegistrar registrar(regResult, regEscaping);

            RegisterRenderTestCommands(registrar);
            RegisterRenderDebugCommands(registrar);
            RegisterDiagnosticsCommands(registrar);
            RegisterScriptUiAnimatorCommands(registrar);
            RegisterSceneObjectCommands(registrar);
            RegisterAssetAuthoringCommands(registrar);
            RegisterCoreCommands(registrar);

            // LC3: discovery. 소비자가 C++ 소스를 긁지 않게 한다.
            // LC0(PHASE 14.5) 기준선 계측. 거동 변경 0 — 재고 찍기만 한다.
            // 죽는 것이 일인 명령 — 예외 경계를 통과시킨다(위 regEscaping 주석).
            regResult({ "reflect.golden" }, [](const ConsoleCommandContext& c) { return HandleReflectGolden(c.parts); });
            regResult({ "scene.flag" }, [](const ConsoleCommandContext& c) { return HandleSceneFlag(c.parts); });
            regResult({ "prefab.objectguid" }, [](const ConsoleCommandContext& c) { return HandlePrefabObjectGuid(c.parts); });
			regResult({ "scene.transformstats" }, [](const ConsoleCommandContext& c) { return HandleSceneTransformStats(c.parts); });
			regResult({ "scene.transformwritestats" }, [](const ConsoleCommandContext& c) { if (c.parts.size() > 2 || (c.parts.size() == 2 && c.parts[1] != "0" && c.parts[1] != "1" && c.parts[1] != "print")) return CommandCore::InvalidArguments("Verification requires --commandlet scene.transformwritestats.check", "commandlet.required"); return HandleSceneTransformWriteStats(c.parts); });
			regResult({ "scene.transformdomains" }, [](const ConsoleCommandContext& c) { return HandleSceneTransformDomains(c.parts); });
			regResult({ "scene.hierarchymutation" }, [](const ConsoleCommandContext& c) { return HandleSceneHierarchyMutation(c.parts); });
			regResult({ "scene.executiongraph" }, [](const ConsoleCommandContext& c) { return HandleSceneExecutionGraph(c.parts); });
			regResult({ "scene.sparseresolver" }, [](const ConsoleCommandContext& c) { if (c.parts.size() > 2 || (c.parts.size() == 2 && c.parts[1] != "0" && c.parts[1] != "1" && c.parts[1] != "print")) return CommandCore::InvalidArguments("Verification requires --commandlet scene.sparseresolver.check", "commandlet.required"); return HandleSceneSparseResolver(c.parts); });
			regResult({ "scene.transformpull" }, [](const ConsoleCommandContext& c) { if (c.parts.size() > 2 || (c.parts.size() == 2 && c.parts[1] != "print")) return CommandCore::InvalidArguments("Verification requires --commandlet scene.transformpull.check", "commandlet.required"); return HandleSceneTransformPull(c.parts); });
			regResult({ "scene.transformbulk" }, [](const ConsoleCommandContext& c) { return HandleSceneTransformBulk(c.parts); });
            regResult({ "scene.traversalbench" }, [](const ConsoleCommandContext& c) { return HandleSceneTraversalBench(c.parts); });
            regResult({ "scene.transformwritestats.check" }, [](const ConsoleCommandContext& c) {
                if (c.parts.size() != 2 || c.parts[1] != "probe")
                    return CommandCore::InvalidArguments("Commandlet requires probe");
                return HandleSceneTransformWriteStats(c.parts);
            });
            regResult({ "scene.sparseresolver.check" }, [](const ConsoleCommandContext& c) {
                if (c.parts.size() < 2 || (c.parts[1] != "probe" && c.parts[1] != "bench"))
                    return CommandCore::InvalidArguments("Commandlet requires probe or bench");
                return HandleSceneSparseResolver(c.parts);
            });
            regResult({ "scene.transformpull.check" }, [](const ConsoleCommandContext& c) {
                if (c.parts.size() != 2 || c.parts[1] != "probe")
                    return CommandCore::InvalidArguments("Commandlet requires probe");
                return HandleSceneTransformPull(c.parts);
            });
            regResult({ "scene.bonedump" }, [](const ConsoleCommandContext& c) { return HandleSceneBoneDump(c.parts); });
            regResult({ "scene.transformdigest" }, [](const ConsoleCommandContext& c) { return HandleSceneTransformDigest(c.parts); });
            regResult({ "serialize.bench" }, [](const ConsoleCommandContext& c) { return HandleSerializeBench(c.parts); });
            regResult({ "serialize.nodeequal" }, [](const ConsoleCommandContext& c) { return HandleSerializeNodeEqual(c.parts); });
            regResult({ "serialize.rymlerror" }, [](const ConsoleCommandContext& c) { return HandleSerializeRymlError(c.parts); });
            regResult({ "scene.proxybench" }, [](const ConsoleCommandContext& c) { return HandleSceneProxyBench(c.parts); });
			regResult({ "scene.proxydirty" }, [](const ConsoleCommandContext& c) { return HandleSceneProxyDirty(c.parts); });

            return t;
        }();
        return table;
    }
}

CommandCore::CommandResult ConsoleCommandSystem::Execute(const std::string& line)
{
    // 빈 줄은 명령이 아니다 — 결과도 없다. 시나리오 파일의 빈 줄이 여기까지
    // 오지는 않지만(LoadScriptFile 이 거른다) stdin 은 빈 줄을 보낸다.
    if (line.empty()) return CommandCore::Ok();

    // LC2: 라인 문법은 여기서만 산다. 아래로는 owned 토큰만 내려간다.
    const CommandCore::TokenizeResult tokenized = CommandCore::Tokenize(line);
    if (!tokenized.ok)
    {
        return CommandCore::InvalidArguments(tokenized.errorMessage + ": " + line,
                                             tokenized.errorCode);
    }

    return ExecuteParsed(tokenized.tokens, line);
}

CommandCore::CommandResult ConsoleCommandSystem::ExecuteParsed(
    const std::vector<std::string>& parts, const std::string& diagnosticLine)
{
    if (parts.empty()) return CommandCore::Ok();

    const std::string& cmd = parts[0];

    const auto& live = ConsoleCmd::GetTable();
    const auto& tests = ConsoleCmd::CommandletTable();
    const auto& table = m_commandletMode && tests.contains(cmd) ? tests : live;
    const auto it = table.find(cmd);
    if (it == table.end())
    {
        if (m_commandletMode) return EditorCommandlets::Run(parts);
        // ★ 예전에는 여기서 printf 하고 그냥 return 했다.
        //
        //   그래서 오타 하나가 exit 0 이었고, help 가 안내하지만 등록돼 있지
        //   않은 이름 6 개(experiment.anim 등, LC0 §2.1.1)도 성공으로 끝났다.
        //   자동화가 "명령이 돌았다"와 "이름이 없다"를 구분할 방법이 없었다.
        return CommandCore::InvalidArguments(
            "알 수 없는 명령: " + cmd + "  ('help' 참고)", "command.unknown");
    }

    // ★ 서비스 세션에서 `wait` 를 금지한다(§7.2).
    //
    //   `wait N` 은 **전역 프레임 보류**다. 배치 시나리오에서는 그것이 문법이고
    //   측정의 단위지만, 동시 세션이 있는 서비스에서 한 요청이 프레임을 붙잡으면
    //   다른 요청 전부의 지연이 된다. 자기 요청만 늦추는 것이 아니다.
    if (m_executingFromService && "wait" == cmd)
    {
        return CommandCore::InvalidArguments(
            "wait 는 배치 시나리오의 문법이다. 서비스 세션에서는 전역 프레임 보류가 "
            "다른 요청의 지연이 되므로 받지 않는다", "service.wait_forbidden");
    }

    const ConsoleCommandContext ctx{ cmd, parts, diagnosticLine, *this };

    // ── LC7: 사용자 코드에 닿는 창은 표시된 명령에만 열린다 (§6.2 · §10.2) ──
    //
    // ★ 이것이 "`ExecutesUserCode` 없는 경로로는 B 에 도달 불가" 를 규약이
    //   아니라 **구조**로 만드는 자리다. 규약으로만 두면 — "그 capability 를
    //   가진 핸들러에서만 `ClrHost::InvokeCallableStatic` 을 부르기로 한다" 로 두면 —
    //   지켜지는지 확인할 방법이 소스를 눈으로 훑는 것뿐이고, 두 번째 호출자가
    //   생기는 날 아무도 모른 채 경계가 사라진다.
    //
    // 창은 명령 하나의 실행 동안만 열린다. 범위를 더 좁힐 수는 없다 — 핸들러
    // 안에서 어느 줄이 부를지는 여기서 보이지 않기 때문이다.
    //
    // ※ RAII 로 둔 이유는 아래 세 갈래의 catch 다. 핸들러가 던지면 창을 닫는
    //   줄을 건너뛰게 되고, 그러면 그 뒤 **모든 명령**이 사용자 코드를 부를 수 있다.
    struct UserCodeGate
    {
        std::optional<ClrHost::UserCodeScope> scope;
        explicit UserCodeGate(bool allow) { if (allow) scope.emplace(); }
    } gate(it->second.executesUserCode);

    // 일부러 죽는 명령은 그대로 죽게 둔다(`crash.test`). 크래시 덤프 경로는
    // 크래시가 나야만 도는 검사라, 여기서 삼키면 그 검사가 통째로 사각지대가 된다.
    if (it->second.letExceptionsEscape) return it->second.Invoke(ctx);

    // 핸들러가 던지면 그것은 명령의 실패가 아니라 내부 결함이다. 프로세스를
    // 죽이지 않고 결과로 바꾼다 — 무인 실행에서 예외 하나가 남은 시나리오를
    // 통째로 날리면 어디까지 갔는지 알 수 없다.
    try
    {
        return it->second.Invoke(ctx);
    }
    catch (const std::bad_alloc&)
    {
        // 할당 실패 뒤에 다음 명령을 계속 밀어 넣지 않는다. 남은 시나리오가
        // 무엇을 하든 같은 벽에 부딪히고, 그 과정에서 남기는 진단이 오히려
        // 원인을 밀어낸다. 여기서 멈추고 종료 코드로 알린다.
        RequestQuit();
        return CommandCore::InternalError("command.out_of_memory",
            "메모리 할당 실패 — 남은 명령을 실행하지 않는다");
    }
    catch (const std::exception& error)
    {
        return CommandCore::InternalError("command.exception",
            std::string("핸들러 예외: ") + error.what());
    }
    catch (...)
    {
        return CommandCore::InternalError("command.exception", "핸들러에서 알 수 없는 예외");
    }
}

void ConsoleCommandSystem::WriteResultLine(const std::string& commandId,
                                           const CommandCore::CommandResult& result,
                                           double queuedMs, uint32_t waitedFrames,
                                           double executedMs)
{
    if (!m_resultJsonl) return;

    // 파일은 첫 줄에서 연다. 시작할 때 열면 `--result-format` 만 주고 명령을
    // 하나도 안 돌린 실행이 빈 파일을 남기고, 소비자는 그것을 "0 건 성공"으로
    // 읽는다 — 실제로는 아무것도 돌지 않은 것이다.
    if (!m_resultFilePath.empty() && nullptr == m_resultFile)
    {
        if (0 != fopen_s(&m_resultFile, m_resultFilePath.c_str(), "wb") || nullptr == m_resultFile)
        {
            // ★ 조용히 stdout 으로 흘리지 않는다. 소비자는 파일을 읽으려고
            //   기다리고 있고, 그 파일이 영영 안 생기면 원인이 여기라는 것을
            //   알 방법이 없다. 이 실행은 판정을 낼 수 없으므로 실패로 끝낸다.
            std::fprintf(stderr, "[CLI] --result-file 을 열 수 없다: %s\n",
                         m_resultFilePath.c_str());
            std::fflush(stderr);
            m_resultJsonl = false;

            // exit code 를 쓰는 곳은 session 하나다(§14.1). 여기서 직접 쓰면
            // 그 불변식이 깨진다 — 위 `--result-format` 과 같은 이유.
            CommandCore::CommandSession::Batch().Record("--result-file",
                CommandCore::InternalError("results.write_failed",
                    "--result-file 을 열 수 없다: " + m_resultFilePath));
            RequestQuit();
            return;
        }
    }

    const std::string line = EditorCommandJson::ResultLine(
        commandId, result, queuedMs, waitedFrames, executedMs);

    std::FILE* out = (nullptr != m_resultFile) ? m_resultFile : stdout;
    std::fwrite(line.data(), 1, line.size(), out);
    std::fputc('\n', out);

    // ★ 줄마다 flush 한다. 프로세스가 중간에 죽어도 그때까지의 판정이 파일에
    //   남아야 한다 — session 이 명령마다 exit code 를 쓰는 것과 같은 이유다.
    //   비용은 시나리오당 명령 수만큼이고, 그 수는 수십이다.
    std::fflush(out);
}

void ConsoleCommandSystem::PublishResult(const std::string& commandId,
                                         const CommandCore::CommandResult& result)
{
    CommandCore::CommandSession& session = CommandCore::CommandSession::Batch();
    session.Record(commandId, result);

    // 사람이 읽는 줄. **판정의 정본이 아니다** — 정본은 session 이 든 결과다.
    //
    // Successful results are recorded without an additional diagnostic line.
    if (CommandCore::CommandStatus::Succeeded == result.status)
    {
        return;
    }

    std::printf("[CLI] %s %.*s (%s) %s\n",
                commandId.c_str(),
                static_cast<int>(CommandCore::ToString(result.status).size()),
                CommandCore::ToString(result.status).data(),
                result.code.c_str(),
                result.message.c_str());

    if (session.ShouldStopEarly())
    {
        std::printf("[CLI] --fail-fast: 남은 명령을 버리고 종료한다\n");
        DiscardPending();
        RequestQuit();
    }
}

void ConsoleCommandSystem::DiscardPending()
{
    // `--fail-fast` 는 **배치** 시나리오의 규약이다. 서비스 큐를 함께 비우면
    // 결과를 기다리는 HTTP 요청이 응답 없이 버려지고, 그쪽은 타임아웃으로만
    // 알게 된다 — 배치의 판단이 서비스 요청을 조용히 죽이는 것은 옳지 않다.
    std::lock_guard<std::mutex> guard(m_mutex);
    m_pending.clear();
}

void ConsoleCommandSystem::RequestQuit() noexcept
{
    m_quitRequested.store(true, std::memory_order_release);
}

void ConsoleCommandSystem::SetWaitFrames(int frames) noexcept
{
    m_waitFrames = frames;
}

bool ConsoleCommandSystem::IsEditorCameraFollowing() noexcept
{
    return g_editorCameraFollowsGame;
}

void ConsoleCommandSystem::SetEditorCameraFollowing(bool follow) noexcept
{
    g_editorCameraFollowsGame = follow;
}

bool ConsoleCommandSystem::MatchEditorCameraToGameCamera()
{
    EditorCameraRig* editorRig = EditorSessionState::Get().CameraRig();
    Scene* activeScene = SceneManagers->GetActiveScene();
    CameraComponent* gameCamera = (nullptr != activeScene)
        ? activeScene->Cameras().GetPrimaryCamera() : nullptr;
    if (nullptr == editorRig || nullptr == gameCamera)
    {
        return false;
    }

    editorRig->ApplySnapshot(gameCamera->CaptureFrameSnapshot());
    return true;
}

const char* ConsoleCommandSystem::HelpText() noexcept
{
    // ★ LC3: 손으로 쓴 목록이 사라졌다.
    //
    //   예전에는 여기에 143 줄짜리 문자열이 있었고 registry 는 별도 표였다.
    //   둘을 잇는 것이 없어서 조용히 벌어졌다 — 등록 205 개 중 help 에 실린
    //   것이 130 개(63%)였고, 반대로 help 는 **등록돼 있지도 않은 이름 6 개**를
    //   안내하고 있었다(`experiment.anim` 등). 문서가 없는 것보다 틀린 문서가
    //   나쁘고, 그 틀림을 아무도 못 알아챈 이유는 대조할 것이 없었기 때문이다.
    //
    //   이제 descriptor 에서 **생성**하므로 갈라질 자리가 없다.
    //
    //   문자열을 static 으로 한 번만 만든다 — 이 함수는 `const char*` 를
    //   돌려주므로 호출자보다 오래 살아야 하고, help 는 등록이 끝난 뒤로는
    //   바뀌지 않는다.
    //
    // ★ 등록을 **여기서 강제로 끝낸다.**
    //
    //   `CommandRegistry::Get()` 은 싱글턴 참조만 돌려주고 채우지 않는다.
    //   registry 를 채우는 것은 `GetTable()` 의 function-local static 이다.
    //   오늘의 호출 경로는 전부 `ExecuteParsed` 를 거치고 그것이 `GetTable()` 을
    //   먼저 부르므로 순서가 맞지만, 그 순서는 **호출자 규율**일 뿐이다.
    //   magic static 은 한 번만 초기화되므로, 앞으로 누가 `--help` 조기 처리나
    //   LC4 의 수신 스레드에서 이 함수를 먼저 부르면 help 가 "0개"로 **영구
    //   동결**된다. 되돌릴 방법이 없다. 여기서 직접 부르면 그 위험이 사라진다.
    EnsureRegistryPopulated();

    static const std::string help =
        CommandCore::RenderHelp(CommandCore::CommandRegistry::Get());
    return help.c_str();
}

void ConsoleCommandSystem::EnsureRegistryPopulated()
{
    // magic static 이라 몇 번을 불러도 채우는 것은 한 번이다.
    (void)ConsoleCmd::GetTable();

    // 정렬 캐시까지 여기서 만든다. `Sorted()` 는 `mutable` 캐시를 **지연**
    // 생성하므로, 비워 둔 채 수신 스레드 둘이 동시에 부르면 const 메서드가
    // 서로의 쓰기를 밟는다. 채우는 쪽을 게임 스레드 한 곳으로 고정하면 이후의
    // 조회는 순수한 읽기가 된다.
    (void)CommandCore::CommandRegistry::Get().Sorted();
}

void ConsoleCommandSystem::PrintHelp() const
{
    // 서식 인자가 없으므로 printf가 아니라 fputs다. 출력 바이트는 같고,
    // 사용자가 준 문자열이 서식 문자열 자리에 앉는 사고가 원천적으로 없다.
    std::fputs(HelpText(), stdout);
}

void ConsoleCommandSystem::Shutdown()
{
    if (m_finishWait)
    {
        m_waitResult = {};
        auto finish = std::move(m_finishWait);
        finish({ CommandCore::CommandStatus::Cancelled, "commandlet.shutdown", "Editor is shutting down" });
    }

    // 서비스를 먼저 내린다 — 수신 스레드가 살아 있는 채로 엔진이 정리되면
    // 그 스레드가 사라진 것을 만진다. endpoint 파일도 여기서 지워진다.
    EditorCommandService::Stop();

    // LC9 — JSONL 파일을 닫는다. 줄마다 flush 하므로 내용은 이미 디스크에 있고,
    // 여기는 핸들 회수다. `return` 이 아래에 여럿 있어 이 자리에 둔다.
    if (nullptr != m_resultFile)
    {
        std::fclose(m_resultFile);
        m_resultFile = nullptr;
    }

    m_running.store(false, std::memory_order_release);

    if (!m_stdinThread.joinable()) return;

    // 블로킹 중인 콘솔 읽기를 깨운다.
    //
    // 예전에는 깨우지 않고 그냥 detach했다. 그러면 이 스레드는 getline 안 —
    // 즉 CRT의 stdio·iostream 내부 — 에 갇힌 채로 남고, 프로세스가 죽을 때
    // ExitProcess가 그 임의 지점에서 스레드를 강제 종료한다. 하필 힙 락을 쥔
    // 순간이었다면 뒤이은 종료 절차가 그 위에서 힙을 만지게 되고, 결과는
    // 종료 구간의 간헐적 힙 손상(0xC0000374)이다. 회귀 세트가 전부 CLI
    // 실행이라 이 경로에서만 나타났고, 에디터를 손으로 쓸 때는 보이지 않았다.
    ::CancelSynchronousIo(static_cast<HANDLE>(m_stdinThread.native_handle()));

    // 취소가 항상 듣는다는 보장은 없어서(콘솔 구현에 따라 다르다) 기한을 둔다.
    // 종료가 통째로 막히는 것은 더 나쁘다.
    constexpr auto kJoinTimeout = std::chrono::milliseconds(500);
    const bool exited = m_stdinDoneFuture.valid()
        && m_stdinDoneFuture.wait_for(kJoinTimeout) == std::future_status::ready;

    if (exited)
    {
        m_stdinThread.join();
        return;
    }

    // 최후의 수단. 여기까지 오면 예전과 같은 위험이 남지만, 적어도 스크립트
    // 실행 경로(--script/--exec)에는 이 스레드가 아예 없다.
    std::fputs("[CLI] 표준 입력 스레드를 회수하지 못했다 - 종료 중 위험 구간.\n", stdout);
    std::fflush(stdout);
    m_stdinThread.detach();
}
