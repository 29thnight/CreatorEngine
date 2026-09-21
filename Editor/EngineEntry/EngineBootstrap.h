#pragma once
#include "Camera.h"
#include "ProfileScope.h"
#include "ThreadPool.h"
#include "Render/Scene/EnhancedSceneRenderer.h"
#include "InputManager.h"
#include "PathFinder.h"
#include "DumpHandler.h"
#include "CoreWindow.h"
#include "DataSystem.h"
#include "RuntimeSettings.h"
#include "PrefabUtility.h"
#include "TagManager.h"
#include "ReflectionRegister.h"
#include "ReflectionUndo.h"
#include "ComponentFactory.h"
// PhysicX/PhysicsManager 싱글턴 기동·종료 호출이 완전 타입을 요구한다.
// 예전에는 Scene.h 전이(→PhysicsManager.h)로 우연히 왔지만 Entity.inl의
// Scene.h include 제거로 전이가 끊겨 직접 세운다.
#include "PhysicsManager.h"
#include "InputActionManager.h"
#include "EngineMode.h"
#include "EngineLaunchConfig.h"
#include "JobScheduler.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <exception>

namespace EngineBootstrap
{
    // 종료 단계 추적.
    //
    // 프로세스 종료 중 크래시는 로그 시스템(spdlog/싱크)이 이미 정리된 뒤에 일어나
    // 아무 기록도 남지 않는다. 그래서 여기서는 로그 시스템을 거치지 않고
    // 파일에 직접 append + flush 한다. 경로도 std::string이 아닌 char 배열에 담아
    // 정적 소멸 순서에 영향을 받지 않게 한다.
    inline char g_shutdownTracePath[MAX_PATH]{};

    inline void ShutdownTrace(const char* stage)
    {
        if (g_shutdownTracePath[0] == '\0') return;

        FILE* file = nullptr;
        if (fopen_s(&file, g_shutdownTracePath, "a") == 0 && file)
        {
            std::fprintf(file, "%s\n", stage);
            std::fflush(file);
            std::fclose(file);
        }
    }

    inline void InitializeShutdownTrace()
    {
        const std::string path = (PathFinder::LogPath() / "shutdown_trace.txt").string();
        if (path.size() < MAX_PATH)
        {
            std::memcpy(g_shutdownTracePath, path.c_str(), path.size() + 1);
        }

        // 매 실행의 경계를 표시해 이전 세션 기록과 섞이지 않게 한다.
        ShutdownTrace("=== 세션 시작 ===");
    }

    // 스모크(--smoke) 같은 자동 판정이 성패를 종료 코드로 알릴 수 있게 한다.
    // 기본 0 — 지금까지의 에디터 동작과 같다. 치명 실패를 감지한 쪽이
    // SetExitCode로 비-0을 남기면 Run이 그 값을 반환한다(§2.3의 Verify 규약).
    inline int g_exitCode = 0;
    inline void SetExitCode(int code) { g_exitCode = code; }

    inline bool InitializeRuntime(const EngineLaunchConfig& config)
    {
        // ★ 프로파일러가 가장 먼저 선다(PHASE 14 P2). 두 가지 때문이다:
        //
        //   ① 아래의 job_scheduler().start() 가 enkiTS 워커를 만들고, 워커의
        //      threadStart 콜백은 **그때 한 번** 온다. 훅이 그보다 늦게 걸리면
        //      이미 태어난 워커는 영영 등록되지 않는다 — 실제로 EditorMain
        //      Initialize 에 걸었다가 워커가 0개로 잡혔다.
        //   ② 서비스가 initialize 되기 전의 register_thread 는 무시되므로,
        //      훅만 먼저 걸어도 소용이 없다. 둘 다 여기여야 한다.
        //
        // Player 도 이 경로를 탄다. 녹화를 켜지 않으면 recorder 가 stopped 라
        // 마커는 즉시 return 하므로 비용은 사실상 0 이다.
        ce::profiler().initialize();
        thread_pool::set_worker_hooks({
            [](unsigned int index)
            {
                // enkiTS threadnum_ 은 0..GetNumTaskThreads()-1 로 안정 보장된다.
                char name[32];
                std::snprintf(name, sizeof(name), "[Worker %u]", index);
                ce::profiler().register_thread(name);
            },
            [](unsigned int)
            {
                // 스레드가 죽기 전에 스트림을 끊는다. 이것이 없으면 수집기가
                // 죽은 저장소를 가리킨 채 남는다(옛 코어의 UAF 자리).
                ce::profiler().unregister_thread();
            } });

        // 전용 RenderThread 도 같은 역전으로 붙인다 — RenderEngine 은 관측 도구를
        // 모르고 함수를 받아 두기만 한다. 첫 라이브 발행에서 스레드가 만들어지므로
        // 여기서 걸면 충분히 이르다.
        //
        // ★ 등록만으로는 캡처에 아무것도 안 나온다. 프레임은 **이벤트가 있는
        //   스레드만** 싣기 때문에, 구간 훅까지 있어야 이 스레드가 보인다.
        EnhancedSceneRenderer::SetRenderThreadHooks({
            []() { ce::profiler().register_thread("[RenderThread]"); },
            []() { ce::profiler().unregister_thread(); },
            []() { ce::profile_scope_begin(ce::marker<"RenderThreadFrame">()); },
            []() { ce::profile_scope_end(); } });

        // GPU 구간도 같은 역전으로 받는다(§7.3 의 GPU Graphics queue).
        //
        // ★ 이름은 부르는 동안만 유효하므로 **여기서 곧바로** 표에 옮긴다.
        //   GPU 패스 이름은 그래프가 정하므로 컴파일 시간에 없다 —
        //   그래서 정적 marker<> 가 아니라 런타임 등록 경로를 쓴다.
        //
        // ★ 틱은 이미 CPU(QPC) 축이다. 옮기는 일은 두 시계를 가진 DX12
        //   백엔드의 몫이고, 여기는 옮겨진 것만 받는다.
        SetEnhancedLiveGpuSpanSink({
            [](const char* name, std::uint64_t begin, std::uint64_t end,
               std::uint32_t frame)
            {
                ce::profiler().submit_gpu_span(
                    ce::intern_runtime_marker(name, ce::marker_kind::gpu_span),
                    begin, end, frame);
            },
            []() { ce::profiler().publish_gpu_spans(); } });

		if ((config.prepareRuntimeContent ||
			config.paths.HasRuntimeOwnershipCapability()) &&
			!config.paths.HasValidRuntimeOwnership())
		{
			std::fputs("[EnginePaths] Host runtime ownership capability is invalid\n", stderr);
			return false;
		}

        // SceneManager/TagManager의 남은 호환 분기 때문에 아직 첫 줄에서 모드를
        // 고정한다. PathFinder와 RuntimeSettings는 아래의 명시적 Host config만 쓴다.
        EngineMode::Set(config.compatibilityRunMode);

        Meta::RegisterClassInitalize();
        if (!PathFinder::Initialize(config.paths))
        {
            std::fputs("[EnginePaths] Host가 유효한 경로를 제공하지 않았다\n", stderr);
            Meta::RegisterClassFinalize();
            return false;
        }
		try
		{
			Log::Initialize(config.logSessionName);
		}
		catch (const std::exception& exception)
		{
			std::fprintf(stderr, "[Log] initialization failed: %s\n", exception.what());
			DebugClass::GetInstance()->AbortInitialization();
			DebugClass::Destroy();
			Meta::RegisterClassFinalize();
			return false;
		}
		catch (...)
		{
			std::fputs("[Log] initialization failed with an unknown error\n", stderr);
			DebugClass::GetInstance()->AbortInitialization();
			DebugClass::Destroy();
			Meta::RegisterClassFinalize();
			return false;
		}

        // 크래시 덤프 기록자를 Log::Initialize 바로 다음에 등록한다.
        //
        // Log::Initialize가 크래시 후크(SEH·terminate·abort·purecall·CRT 잘못된 인자)를
        // 이미 다 걸어 둔 상태라, 등록이 늦으면 그 사이에 죽은 크래시는 로그에 CRASH
        // 줄만 남고 .dmp가 없다. 예전에는 App::Initialize 중반이 유일한 등록 지점이라
        // 디바이스 생성·셰이더 컴파일·리소스 로드 구간 전체가 덤프 사각지대였다.
        CoreWindow::SetDumpType(DUMP_TYPE::DUMP_TYPE_FULL);

		// The Host owns package/import preparation. Run it after path, log, and crash
		// diagnostics exist, but before RuntimeSettings reads ProjectSetting from that view.
		if (config.prepareRuntimeContent &&
			!config.prepareRuntimeContent(config.paths))
		{
			std::fputs("[EnginePaths] Host runtime content preparation failed\n", stderr);
			Debug::PrintLog(spdlog::level::err, "Host runtime content preparation failed");
			Meta::RegisterClassFinalize();
			Log::Finalize();
			return false;
		}

		if (!RuntimeSettings::Initialize())
		{
			std::fputs("[RuntimeSettings] EngineSettings 초기화 실패 — 부팅을 중단한다\n",
				stderr);
			Debug::PrintLog(spdlog::level::err, "RuntimeSettings 초기화 실패 — 기본값으로 계속하지 않는다");
			RuntimeSettings::Shutdown();
			Meta::RegisterClassFinalize();
			Log::Finalize();
			return false;
		}

		// Editor-owned preferences/build settings are initialized only by the Editor
		// Host. Player leaves this capability empty and consumes RuntimeSettings alone.
		if (config.initializeHostSettings && !config.initializeHostSettings())
		{
			std::fputs("[HostSettings] Host settings initialization failed\n", stderr);
			Debug::PrintLog(spdlog::level::err, "Host settings initialization failed");
			RuntimeSettings::Shutdown();
			Meta::RegisterClassFinalize();
			Log::Finalize();
			return false;
		}

        CoreWindow::RegisterCreateEventHandler([](HWND, WPARAM, LPARAM) -> LRESULT
        {
            return 0;
        });

		TagManager::GetInstance();
        InputManager::GetInstance();
        PrefabUtility::GetInstance();
        DataSystem::GetInstance();
        PhysicX::GetInstance();
        PhysicsManager::GetInstance();
        SceneManager::GetInstance();
        ComponentFactory::GetInstance();

        // One engine-owned pool for Editor and Player, independent of scene lifetimes.
        ce::get_job_scheduler().start();

        return true;
    }

    // 종료 단계마다 흔적을 남긴다. 여기서 죽으면 로그가 남지 않는 구간이 많아,
    // 추적 파일의 마지막 줄이 곧 범인이 된다.
#define SHUTDOWN_STEP(expr) do { ShutdownTrace("  - " #expr); expr; } while (0)

    inline void FinalizeRuntime()
    {
        SHUTDOWN_STEP(ComponentFactory::Destroy());
        SHUTDOWN_STEP(SceneManager::Destroy());
        SHUTDOWN_STEP(ce::get_job_scheduler().shutdown());
        SHUTDOWN_STEP(PhysicsManager::Destroy());
        SHUTDOWN_STEP(PhysicX::Destroy());
		SHUTDOWN_STEP(TagManager::Destroy());
		SHUTDOWN_STEP(InputManager::Destroy());
		SHUTDOWN_STEP(DataSystem::Destroy());
		SHUTDOWN_STEP(PrefabUtility::Destroy());
        SHUTDOWN_STEP(Meta::RegisterClassFinalize());
		// Runtime settings outlive every Core consumer and teardown service. Destroying
		// them earlier leaves shutdown hooks with a dangling settings view.
		SHUTDOWN_STEP(RuntimeSettings::Shutdown());
        Log::Finalize();
    }

#undef SHUTDOWN_STEP

    template <typename TApp>
    int Run(HINSTANCE hInstance, const EngineLaunchConfig& config)
    {
        HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(hr))
        {
            return static_cast<int>(hr);
        }

        struct CoUninitializer
        {
            ~CoUninitializer()
            {
                ShutdownTrace("[6] CoUninitialize 직전");
                CoUninitialize();
                ShutdownTrace("[7] CoUninitialize 완료");
            }
        } comGuard;

        if (!InitializeRuntime(config)) return 2;
        InitializeShutdownTrace();

        // CRT 정적 소멸 단계까지 도달하는지 확인한다.
        // 이 줄까지 기록되고 끊기면 정적 객체 소멸자나 DLL 언로드가 범인이다.
        std::atexit([] { ShutdownTrace("[8] atexit 도달 (CRT 정리 시작)"); });

        struct RuntimeGuard
        {
            ~RuntimeGuard()
            {
                ShutdownTrace("[4] FinalizeRuntime 시작");
				FinalizeRuntime();
                ShutdownTrace("[5] FinalizeRuntime 완료");
            }
        } runtimeGuard;

        // app을 명시적 스코프에 두어 소멸 시점을 추적한다.
        // CoreWindow를 app보다 먼저 만들면 역순 소멸로 app/presentation이 HWND보다
        // 먼저 정리된다. 예전 App::Initialize 지역 window는 app.Finalize 전에
        // 파괴됐고, InitializeTask의 값 반환 복사본도 같은 HWND를 조기 파괴했다.
        {
            CoreWindow coreWindow(hInstance, config.window);
            TApp app;
            app.Initialize(coreWindow);
            app.Finalize();
            ShutdownTrace("[1] app.Finalize 완료");
        }
        ShutdownTrace("[2] app 소멸 및 window 파괴 완료");

        ShutdownTrace("[3] main 반환 - 이후 runtimeGuard/comGuard 소멸");
        return g_exitCode;
    }
}
