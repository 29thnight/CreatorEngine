#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include "Camera.h"
#include "InputManager.h"
#include "PathFinder.h"
#include "DumpHandler.h"
#include "CoreWindow.h"
#include "DataSystem.h"
#include "DebugStreamBuf.h"
#include "EngineSetting.h"
#include "HotLoadSystem.h"
#include "EffectProxyController.h"
#include "PrefabUtility.h"
#include "TagManager.h"
#include "ShaderSystem.h"
#include "ReflectionRegister.h"
#include "ReflectionVectorFactory.h"
#include "DeviceState.h"
#include "ReflectionVectorInvoker.h"
#include "ComponentFactory.h"
#include "InputActionManager.h"
#include "CullingManager.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>

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

    inline void InitializeRuntime()
    {
        static DebugStreamBuf debugBuf(std::cout.rdbuf());
        std::cout.rdbuf(&debugBuf);

        Meta::RegisterClassInitalize();
        Meta::VectorFactoryRegistry::GetInstance();
        Meta::VectorInvokerRegistry::GetInstance();
        PathFinder::Initialize();
#ifdef BUILD_FLAG
        Log::Initialize("Game");
#else
        Log::Initialize("Editor");
#endif

        EngineSettingInstance->Initialize();

        CoreWindow::RegisterCreateEventHandler([](HWND, WPARAM, LPARAM) -> LRESULT
        {
            return 0;
        });

        DirectX11::DeviceResourceManager::GetInstance();
        ShaderResourceSystem::GetInstance();
        EngineSetting::GetInstance();
        TagManager::GetInstance();
        InputManager::GetInstance();
        PrefabUtility::GetInstance();
        EffectManager::GetInstance();
        EffectProxyController::GetInstance();
        DataSystem::GetInstance();
        PhysicX::GetInstance();
        PhysicsManager::GetInstance();
        SceneManager::GetInstance();
        HotLoadSystem::GetInstance();
        ComponentFactory::GetInstance();
        CameraContainer::GetInstance();
        Creator::Culling::CullingManager::GetInstance();

    }

    // 종료 단계마다 흔적을 남긴다. 여기서 죽으면 로그가 남지 않는 구간이 많아,
    // 추적 파일의 마지막 줄이 곧 범인이 된다.
#define SHUTDOWN_STEP(expr) do { ShutdownTrace("  - " #expr); expr; } while (0)

    inline void FinalizeRuntime()
    {
        SHUTDOWN_STEP(Creator::Culling::CullingManager::Destroy());
        SHUTDOWN_STEP(CameraContainer::Destroy());
        SHUTDOWN_STEP(ComponentFactory::Destroy());
        SHUTDOWN_STEP(HotLoadSystem::Destroy());
        SHUTDOWN_STEP(SceneManager::Destroy());
        SHUTDOWN_STEP(PhysicsManager::Destroy());
        SHUTDOWN_STEP(PhysicX::Destroy());
        SHUTDOWN_STEP(EngineSetting::Destroy());
        SHUTDOWN_STEP(TagManager::Destroy());
        SHUTDOWN_STEP(EffectManager::Destroy());
        SHUTDOWN_STEP(EffectProxyController::Destroy());
        SHUTDOWN_STEP(InputManager::Destroy());
        SHUTDOWN_STEP(DataSystem::Destroy());
        SHUTDOWN_STEP(PrefabUtility::Destroy());
        SHUTDOWN_STEP(ShaderResourceSystem::Destroy());
        SHUTDOWN_STEP(Meta::RegisterClassFinalize());
        SHUTDOWN_STEP(Meta::VectorFactoryRegistry::Destroy());
        SHUTDOWN_STEP(Meta::VectorInvokerRegistry::Destroy());
        SHUTDOWN_STEP(DirectX11::DeviceResourceManager::Destroy());

        Log::Finalize();
    }

#undef SHUTDOWN_STEP

    template <typename TApp>
    int Run(HINSTANCE hInstance, const wchar_t* windowTitle, int width, int height)
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

        InitializeRuntime();
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
        // (스코프가 없어도 app은 runtimeGuard보다 먼저 소멸하므로 순서는 동일하다.)
        {
            TApp app;
            app.Initialize(hInstance, windowTitle, width, height);
            app.Finalize();
            ShutdownTrace("[1] app.Finalize 완료");
        }
        ShutdownTrace("[2] app 소멸 완료 (DeviceResources/Dx11Main 해제)");

        ShutdownTrace("[3] main 반환 - 이후 runtimeGuard/comGuard 소멸");
        return 0;
    }
}
#endif // DYNAMICCPP_EXPORTS