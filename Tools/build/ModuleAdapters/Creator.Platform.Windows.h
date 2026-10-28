#pragma once
// Generated from Engine/Contracts/Platform/Windows/Creator.Platform.Windows.ixx; do not edit.
// Regenerate with: py Tools/build/generate-module-adapters.py
#if !defined(CE_CONTRACT_STANDALONE)
#error "Native consumers must import the named module directly."
#endif

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "runtimeobject.lib")

#include "../../../Engine/Containers/Creator.Containers.h"
#include "../../../Engine/Utility_Framework/Foundation/StandardLibrary.h"
#include "../../../Engine/Platform/WindowsSDK/Creator.WinAPI.h"
#include "../../../Engine/Platform/WindowsSDK/Creator.ComSupport.h"
#include "Creator.Core.Window.h"

namespace ce::platform::windows
{
    // Preserve HRESULT-based capability/failure branches while using WinRT's
    // throwing interface conversion. The destination releases its old reference
    // before querying, matching WRL As(&destination).
    template<typename To, typename From>
    HRESULT QueryComInterface(const winrt::com_ptr<From>& source, winrt::com_ptr<To>& destination) noexcept
    {
        destination = nullptr;
        try
        {
            destination = source.template as<To>();
            return S_OK;
        }
        catch (const winrt::hresult_error& error)
        {
            return error.code().value;
        }
        catch (const std::bad_alloc&)
        {
            // C++/WinRT throws bad_alloc specifically for E_OUTOFMEMORY.
            return E_OUTOFMEMORY;
        }
    }

    enum class apartment_model
    {
        mta, // Engine-owned main, presentation and render threads.
        sta, // Message-pumping UI hosts, currently ProfilerViewer.
        current_or_mta // Borrowed decoding calls: respect existing STA/MTA.
    };

    // Pool, submission, socket and file-watcher threads have no implicit WinRT
    // capability. A COM/WinRT job must acquire a scope before using such APIs.
    // No apartment-affine result may escape to another thread without marshaling.
    // Construct before apartment-bound objects; destroy after their release and
    // worker shutdown, on the same thread. Never copy or transfer this scope.
    // A successful RoInitialize (including S_FALSE) owns exactly one uninitialize.
    // HRESULT failure remains a failure; callers choose their existing error path.
    class apartment_scope
    {
    public:
        explicit apartment_scope(apartment_model model) noexcept
            : result(initialize(model)), initialized_(SUCCEEDED(result))
        {
        }

        ~apartment_scope() noexcept
        {
            reset();
        }

        apartment_scope(const apartment_scope&) = delete;
        apartment_scope& operator=(const apartment_scope&) = delete;
        apartment_scope(apartment_scope&&) = delete;
        apartment_scope& operator=(apartment_scope&&) = delete;

        // Only for an explicit host teardown boundary after all users release.
        void reset() noexcept
        {
            if (initialized_)
            {
                initialized_ = false;
                RoUninitialize();
            }
        }

        const HRESULT result;

    private:
        static HRESULT initialize(apartment_model model) noexcept
        {
            if (model == apartment_model::current_or_mta)
            {
                APTTYPE type{};
                APTTYPEQUALIFIER qualifier{};
                const HRESULT queried = CoGetApartmentType(&type, &qualifier);
                if (queried == CO_E_NOTINITIALIZED)
                {
                    model = apartment_model::mta;
                }
                else if (FAILED(queried))
                {
                    return queried;
                }
                else if (type == APTTYPE_STA || type == APTTYPE_MAINSTA)
                {
                    model = apartment_model::sta;
                }
                else if (type == APTTYPE_MTA)
                {
                    model = apartment_model::mta;
                }
                else
                {
                    // Do not silently reinterpret a neutral apartment as MTA.
                    return RPC_E_CHANGED_MODE;
                }
            }
            return RoInitialize(model == apartment_model::sta
                ? RO_INIT_SINGLETHREADED : RO_INIT_MULTITHREADED);
        }

        bool initialized_;
    };
}

namespace ce::platform::windows
{
    using window_message_interceptor =
        ce::owned_func<std::optional<LRESULT>(HWND, UINT, WPARAM, LPARAM)>;

    // Only the Win32 adapter sees SDK style bits, resource IDs and raw messages.
    struct window_desc : ::Core::WindowDesc
    {
        int iconResourceId{ 0 };
        DWORD style{ WS_OVERLAPPEDWINDOW };
        DWORD extendedStyle{ 0 };
        window_message_interceptor messageInterceptor{};
    };

    inline HWND window_handle(::Core::NativeWindowHandle handle) noexcept
    {
        if (handle.kind != ::Core::NativeWindowKind::Windows)
        {
            return nullptr;
        }
        return static_cast<HWND>(handle.window);
    }
}

namespace ce::platform::windows
{
    // Construct/use/destroy on the window owner thread. Owns a runtime lease,
    // borrows HWND, and never calls AppWindow.Destroy or DestroyWindow.
    // Detach before native teardown; keep this host alive through DestroyWindow.
    class app_window_host
    {
    public:
        app_window_host();
        ~app_window_host() noexcept;
        app_window_host(const app_window_host&) = delete;
        app_window_host& operator=(const app_window_host&) = delete;
        app_window_host(app_window_host&&) = delete;
        app_window_host& operator=(app_window_host&&) = delete;

        void Attach(HWND window);
        void Detach() noexcept;
        bool IsAttached() const noexcept;
        void Show();
        void SetTitle(std::wstring_view title);
        ScaledClientSize ClientSize() const;
        void ResizeClient(int width, int height);

    private:
        struct state;
        std::unique_ptr<state> state_;
    };
}

using WindowsWindowDesc = ::ce::platform::windows::window_desc;
