module;
#include <windows.h>
#include <MddBootstrap.h>
#include <WindowsAppSDK-VersionInfo.h>

module Creator.Platform.Windows;

import std;
import winrt.Microsoft.UI;
import winrt.Microsoft.UI.Windowing;

// Only the native HWND bridge is textual. The projection stays imported:
// C++/WinRT's module guard suppresses its nested projection header.
#define WINRT_IMPORT_MODULE
#pragma warning(push)
#pragma warning(disable: 5244) // Intentional native bridge after projection imports.
#include <winrt/Microsoft.UI.Interop.h>
#pragma warning(pop)
#undef WINRT_IMPORT_MODULE

namespace ce::platform::windows::detail
{
    namespace
    {
        struct runtime_registry
        {
            std::mutex mutex;
            std::size_t users{};
        };

        runtime_registry& registry()
        {
            static runtime_registry current;
            return current;
        }

        class runtime_lease
        {
        public:
            runtime_lease()
            {
                auto& current = registry();
                std::lock_guard lock(current.mutex);
                if (current.users == 0)
                {
                    PACKAGE_VERSION minimum{};
                    minimum.Version = WINDOWSAPPSDK_RUNTIME_VERSION_UINT64;
                    winrt::check_hresult(MddBootstrapInitialize2(
                        WINDOWSAPPSDK_RELEASE_MAJORMINOR, WINDOWSAPPSDK_RELEASE_VERSION_TAG_W,
                        minimum, MddBootstrapInitializeOptions_None));
                }
                ++current.users;
            }

            ~runtime_lease() noexcept
            {
                auto& current = registry();
                std::lock_guard lock(current.mutex);
                if (--current.users == 0)
                {
                    // Serialize final shutdown against another window's startup.
                    winrt::clear_factory_cache();
                    MddBootstrapShutdown();
                }
            }

            runtime_lease(const runtime_lease&) = delete;
            runtime_lease& operator=(const runtime_lease&) = delete;
        };

        [[noreturn]] void report_failure(const winrt::hresult_error& error)
        {
            throw std::system_error(static_cast<int>(error.code()), std::system_category(),
                "AppWindow/Windows App Runtime initialization or operation failed");
        }
    }

    struct app_window_host::state
    {
        // Reverse destruction: projection, runtime, then apartment.
        apartment_scope apartment{ apartment_model::current_or_mta };
        std::unique_ptr<runtime_lease> runtime;
        winrt::Microsoft::UI::Windowing::AppWindow app{ nullptr };
        DWORD owner{ GetCurrentThreadId() };

        state()
        {
            winrt::check_hresult(apartment.result);
            runtime = std::make_unique<runtime_lease>();
        }

        void RequireOwner() const
        {
            if (owner != GetCurrentThreadId())
            {
                throw std::logic_error("AppWindow operation must run on the window owner thread");
            }
        }

        void RequireWindow() const
        {
            RequireOwner();
            if (!app)
            {
                throw std::logic_error("AppWindow is not attached");
            }
        }
    };

    app_window_host::app_window_host()
    {
        try
        {
            state_ = std::make_unique<state>();
        }
        catch (const winrt::hresult_error& error)
        {
            report_failure(error);
        }
    }

    app_window_host::~app_window_host() noexcept
    {
        // RoUninitialize and HWND operations must stay on the constructing thread.
        if (state_->owner != GetCurrentThreadId())
        {
            std::terminate();
        }
    }

    void app_window_host::Attach(HWND window)
    {
        state_->RequireOwner();
        if (state_->app || !IsWindow(window) ||
            GetWindowThreadProcessId(window, nullptr) != GetCurrentThreadId())
        {
            throw std::invalid_argument("AppWindow requires an unattached HWND owned by this thread");
        }
        try
        {
            const auto id = winrt::Microsoft::UI::GetWindowIdFromWindow(window);
            auto app = winrt::Microsoft::UI::Windowing::AppWindow::GetFromWindowId(id);
            if (!app)
            {
                throw std::runtime_error("AppWindow could not resolve the existing HWND");
            }
            state_->app = std::move(app);
        }
        catch (const winrt::hresult_error& error)
        {
            report_failure(error);
        }
    }

    void app_window_host::Detach() noexcept
    {
        // Releases only the borrowed projection. The native owner destroys HWND
        // while this host's runtime lease is still alive.
        if (state_->owner != GetCurrentThreadId())
        {
            std::terminate();
        }
        state_->app = nullptr;
    }

    bool app_window_host::IsAttached() const noexcept
    {
        return static_cast<bool>(state_->app);
    }

    void app_window_host::Show()
    {
        state_->RequireWindow();
        try
        {
            state_->app.Show();
        }
        catch (const winrt::hresult_error& error)
        {
            report_failure(error);
        }
    }

    void app_window_host::SetTitle(std::wstring_view title)
    {
        state_->RequireWindow();
        try
        {
            state_->app.Title(winrt::hstring{ title });
        }
        catch (const winrt::hresult_error& error)
        {
            report_failure(error);
        }
    }

    ScaledClientSize app_window_host::ClientSize() const
    {
        state_->RequireWindow();
        try
        {
            const auto size = state_->app.ClientSize();
            return { size.Width, size.Height };
        }
        catch (const winrt::hresult_error& error)
        {
            report_failure(error);
        }
    }

    void app_window_host::ResizeClient(int width, int height)
    {
        state_->RequireWindow();
        if (width <= 0 || height <= 0)
        {
            throw std::invalid_argument("AppWindow client dimensions must be positive");
        }
        try
        {
            state_->app.ResizeClient({ width, height });
        }
        catch (const winrt::hresult_error& error)
        {
            report_failure(error);
        }
    }
}
