#pragma once
// Generated from Engine/Platform/Windows/AppBackendWinAPI/Creator.AppBackend.WinAPI.ixx; do not edit.
// Regenerate with: py Tools/build/generate-module-adapters.py
#if !defined(CE_CONTRACT_STANDALONE)
#error "Native consumers must import the named module directly."
#endif

#include "../../../Engine/Containers/Creator.Containers.h"
#include "../../../Engine/Utility_Framework/Foundation/StandardLibrary.h"
#include "../../../Engine/Platform/WindowsSDK/Creator.WinAPI.h"
#include "Creator.Platform.Windows.h"

enum class AppCrashDumpMode { Mini, Full };

class AppBackendWinAPI final : public Core::IWindowBackend
{
public:
    using MessageHandler = ce::owned_func<LRESULT(HWND, WPARAM, LPARAM)>;

    AppBackendWinAPI(HINSTANCE instance, const WindowsWindowDesc& desc);
    ~AppBackendWinAPI() override;
    AppBackendWinAPI(const AppBackendWinAPI&) = delete;
    AppBackendWinAPI& operator=(const AppBackendWinAPI&) = delete;
    AppBackendWinAPI(AppBackendWinAPI&&) = delete;
    AppBackendWinAPI& operator=(AppBackendWinAPI&&) = delete;

    void DetachCallbacks() noexcept;
    void Show() override;
    Core::NativeWindowHandle GetNativeHandle() const noexcept override;
    HWND GetHandle() const;
    int GetWidth() const noexcept override;
    int GetHeight() const noexcept override;
    Core::WindowPollResult PollEvents() override;
    void EnsureSwapChainCompatibleStyle();
    void RegisterHandler(UINT message, MessageHandler handler);

    template<typename Instance>
    void RegisterHandler(UINT message, Instance* instance,
        LRESULT(Instance::*handler)(HWND, WPARAM, LPARAM))
    {
        RegisterHandler(message, [instance, handler](HWND window, WPARAM wparam, LPARAM lparam)
        {
            return (instance->*handler)(window, wparam, lparam);
        });
    }

    template<typename Initializer>
    AppBackendWinAPI& InitializeTask(Initializer initialize)
    {
        initialize();
        return *this;
    }

    static AppBackendWinAPI* GetForCurrentInstance();
    static void RegisterCreateEventHandler(MessageHandler handler);
    static void WriteCrashDump(void* exceptionPointers, const char* reason);
    static void SetDumpType(AppCrashDumpMode mode);
    static void SetUnattended(bool unattended);
    static bool IsUnattended();
    static void EnableHeapValidation();
    static void SuppressCrtDialogs();
    static std::string BuildStackTrace(const CONTEXT& capturedContext, int skipFrames = 0);
    static LRESULT CALLBACK WndProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);

private:
    struct state;
    state& ActiveState() const noexcept;
    std::unique_ptr<state> state_;
    static AppBackendWinAPI* instance_;
};
