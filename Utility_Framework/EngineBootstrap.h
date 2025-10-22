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

namespace EngineBootstrap
{
    inline void InitializeRuntime()
    {
        static DebugStreamBuf debugBuf(std::cout.rdbuf());
        std::cout.rdbuf(&debugBuf);

        Meta::RegisterClassInitalize();
        Meta::VectorFactoryRegistry::GetInstance();
        Meta::VectorInvokerRegistry::GetInstance();
        PathFinder::Initialize();
        Log::Initialize();

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

    }

    inline void FinalizeRuntime()
    {
        CameraContainer::Destroy();
        ComponentFactory::Destroy();
        HotLoadSystem::Destroy();
        SceneManager::Destroy();
        PhysicsManager::Destroy();
        PhysicX::Destroy();
        EngineSetting::Destroy();
        TagManager::Destroy();
        EffectManager::Destroy();
        EffectProxyController::Destroy();
        InputManager::Destroy();
        DataSystem::Destroy();
        PrefabUtility::Destroy();
        ShaderResourceSystem::Destroy();
        Meta::RegisterClassFinalize();
        Meta::VectorFactoryRegistry::Destroy();
        Meta::VectorInvokerRegistry::Destroy();
        DirectX11::DeviceResourceManager::Destroy();

        Log::Finalize();
    }

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
                CoUninitialize();
            }
        } comGuard;

        InitializeRuntime();

        struct RuntimeGuard
        {
            ~RuntimeGuard()
            {
                FinalizeRuntime();
            }
        } runtimeGuard;

        TApp app;
        app.Initialize(hInstance, windowTitle, width, height);
        app.Finalize();

        return 0;
    }
}
#endif // DYNAMICCPP_EXPORTS