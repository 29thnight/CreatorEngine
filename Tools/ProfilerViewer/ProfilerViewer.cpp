#include "PreparationWorker.h"
#include "ViewerArguments.h"
#include "Presentation/ProfilerPresenter.h"
#include "ProfilerViewerClient.h"
#include "EditorFontResources.h"
#include "EditorTheme.h"

#include <Windows.h>
#include <d3d11.h>
#include <shellapi.h>
#include <shlobj.h>
#include <wrl/client.h>

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace ce::profiler_viewer
{
    namespace
    {
        using Microsoft::WRL::ComPtr;
        constexpr wchar_t window_class[] = L"CreatorEngine.ProfilerViewer";

        std::string utf8(const std::filesystem::path& path)
        {
            const auto value = path.u8string();
            return std::string(value.begin(), value.end());
        }

        void check(HRESULT result, const char* operation)
        {
            if (FAILED(result))
            {
                char message[256]{};
                std::snprintf(message, sizeof(message), "%s failed (HRESULT 0x%08lx).",
                              operation, static_cast<unsigned long>(result));
                throw std::runtime_error(message);
            }
        }

        bool ordinary_token()
        {
            HANDLE token = nullptr;
            if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
            {
                return false;
            }
            TOKEN_ELEVATION elevation{};
            DWORD bytes = sizeof(elevation);
            const bool queried = GetTokenInformation(token, TokenElevation, &elevation, bytes, &bytes) != FALSE;
            alignas(TOKEN_MANDATORY_LABEL) std::array<std::byte, 256> integrity{};
            const bool integrity_queried = GetTokenInformation(token, TokenIntegrityLevel, integrity.data(),
                static_cast<DWORD>(integrity.size()), &bytes) != FALSE;
            bool ordinary = false;
            if (queried && !elevation.TokenIsElevated && integrity_queried)
            {
                const auto* label = reinterpret_cast<const TOKEN_MANDATORY_LABEL*>(integrity.data());
                if (IsValidSid(label->Label.Sid) && *GetSidSubAuthorityCount(label->Label.Sid) > 0)
                {
                    const DWORD rid = *GetSidSubAuthority(label->Label.Sid,
                        static_cast<DWORD>(*GetSidSubAuthorityCount(label->Label.Sid) - 1));
                    ordinary = rid < SECURITY_MANDATORY_HIGH_RID;
                }
            }
            CloseHandle(token);
            return ordinary;
        }

        std::filesystem::path executable_directory()
        {
            std::wstring buffer(32768, L'\0');
            const DWORD written = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
            if (written == 0 || written >= buffer.size())
            {
                throw std::runtime_error("Cannot determine the ProfilerViewer executable directory.");
            }
            buffer.resize(written);
            return std::filesystem::path(buffer).parent_path();
        }

        std::filesystem::path settings_directory()
        {
            PWSTR value = nullptr;
            if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &value)))
            {
                return {};
            }
            const auto root = std::filesystem::path(value) / L"CreatorEngine" / L"ProfilerViewer";
            CoTaskMemFree(value);
            std::error_code error;
            std::filesystem::create_directories(root, error);
            return error ? std::filesystem::path{} : root;
        }

        struct window_settings
        {
            RECT normal{ 100, 100, 1500, 1000 };
            bool maximized = false;
            int scale_milli = 1000;

            void load(const std::filesystem::path& directory)
            {
                if (!directory.empty())
                {
                    std::ifstream input(directory / L"window.ini");
                    int version = 0;
                    int was_maximized = 0;
                    RECT candidate{};
                    int scale = 1000;
                    if (input >> version >> candidate.left >> candidate.top >> candidate.right >> candidate.bottom >> was_maximized >> scale &&
                        version == 1 && candidate.left >= -100000 && candidate.top >= -100000 &&
                        candidate.right <= 100000 && candidate.bottom <= 100000 &&
                        candidate.right > candidate.left && candidate.bottom > candidate.top)
                    {
                        normal = candidate;
                        maximized = was_maximized == 1;
                        scale_milli = std::clamp(scale, 500, 3000);
                    }
                }
                // A removed monitor or corrupt placement must never hide the
                // title bar. Clamp the complete normal rectangle to a work area.
                MONITORINFO info{ sizeof(info) };
                if (GetMonitorInfoW(MonitorFromRect(&normal, MONITOR_DEFAULTTONEAREST), &info))
                {
                    const LONG width = std::min(std::max(normal.right - normal.left, 900L), info.rcWork.right - info.rcWork.left);
                    const LONG height = std::min(std::max(normal.bottom - normal.top, 600L), info.rcWork.bottom - info.rcWork.top);
                    normal.left = std::clamp(normal.left, info.rcWork.left, info.rcWork.right - width);
                    normal.top = std::clamp(normal.top, info.rcWork.top, info.rcWork.bottom - height);
                    normal.right = normal.left + width;
                    normal.bottom = normal.top + height;
                }
            }

            void save(const std::filesystem::path& directory) const
            {
                if (directory.empty())
                {
                    return;
                }
                // These are exclusively viewer settings. No Editor paths, scene
                // workspace, dock layout or Editor preference store is touched.
                const auto temporary = directory / (L"window-" + std::to_wstring(GetCurrentProcessId()) + L".tmp");
                std::ofstream output(temporary, std::ios::trunc);
                output << "1 " << normal.left << ' ' << normal.top << ' ' << normal.right << ' '
                       << normal.bottom << ' ' << (maximized ? 1 : 0) << ' ' << scale_milli << '\n';
                output.close();
                if (output)
                {
                    MoveFileExW(temporary.c_str(), (directory / L"window.ini").c_str(), MOVEFILE_REPLACE_EXISTING);
                }
            }
        };

        class application
        {
        public:
            explicit application(HINSTANCE instance) : instance_(instance) {}
            ~application()
            {
                if (presenter_initialized_)
                {
                    editor::profiler_view::shutdown();
                }
                source_.disconnect();
                worker_.drain();
                diagnostics_worker_.drain();
                if (renderer_initialized_)
                {
                    ImGui_ImplDX11_Shutdown();
                }
                if (platform_initialized_)
                {
                    ImGui_ImplWin32_Shutdown();
                }
                if (imgui_initialized_)
                {
                    editor::fonts::clear_loaded_fonts();
                    ImGui::DestroyContext();
                }
                target_.Reset();
                if (context_)
                {
                    context_->ClearState();
                    context_->Flush();
                }
                swapchain_.Reset();
                context_.Reset();
                device_.Reset();
                if (window_)
                {
                    if (fully_initialized_)
                    {
                        settings_.save(settings_root_);
                    }
                    DestroyWindow(window_);
                }
                if (registered_)
                {
                    UnregisterClassW(window_class, instance_);
                }
            }

            void initialize(const viewer_arguments& arguments)
            {
                settings_root_ = settings_directory();
                settings_.load(settings_root_);
                if (arguments.ui_scale_milli != 0)
                {
                    // A new live viewer inherits the originating Editor scale.
                    // Focusing an existing viewer never resets its own changes.
                    settings_.scale_milli = static_cast<int>(arguments.ui_scale_milli);
                }
                const bool restore_maximized = settings_.maximized;
                WNDCLASSEXW type{ sizeof(type) };
                type.style = CS_CLASSDC;
                type.lpfnWndProc = window_proc;
                type.hInstance = instance_;
                type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
                type.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
                type.lpszClassName = window_class;
                if (!RegisterClassExW(&type))
                {
                    throw std::runtime_error("Cannot register the ProfilerViewer window.");
                }
                registered_ = true;
                window_ = CreateWindowExW(0, window_class, L"Frame Profiler", WS_OVERLAPPEDWINDOW,
                    settings_.normal.left, settings_.normal.top,
                    settings_.normal.right - settings_.normal.left, settings_.normal.bottom - settings_.normal.top,
                    nullptr, nullptr, instance_, this);
                if (!window_)
                {
                    throw std::runtime_error("Cannot create the ProfilerViewer window.");
                }
                create_device();
                IMGUI_CHECKVERSION();
                ImGui::CreateContext();
                imgui_initialized_ = true;
                auto& io = ImGui::GetIO();
                io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
                // The native window owns geometry. Do not create engine docking
                // surfaces or secondary platform windows in this standalone host.
                ini_path_ = settings_root_.empty() ? std::string{} : utf8(settings_root_ / L"imgui.ini");
                io.IniFilename = ini_path_.empty() ? nullptr : ini_path_.c_str();
                io.LogFilename = nullptr;
                editor::fonts::set_resource_root(executable_directory() / L"Resources");
                const auto body = editor::fonts::add_required_font("body", editor::fonts::body_candidates(),
                    editor::EditorThemeTokens::BodyFontSize);
                if (!body.font)
                {
                    throw std::runtime_error("Cannot initialize the ProfilerViewer font atlas.");
                }
                io.FontDefault = body.font;
                editor::fonts::merge_icon_font(editor::EditorThemeTokens::IconFontSize,
                    editor::EditorThemeTokens::IconBaselineOffset);
                dpi_scale_ = static_cast<float>(GetDpiForWindow(window_)) / 96.0f;
                apply_scale();
                if (!ImGui_ImplWin32_Init(window_))
                {
                    throw std::runtime_error("ProfilerViewer Win32 ImGui backend initialization failed.");
                }
                platform_initialized_ = true;
                if (!ImGui_ImplDX11_Init(device_.Get(), context_.Get()))
                {
                    throw std::runtime_error("ProfilerViewer D3D11 ImGui backend initialization failed.");
                }
                renderer_initialized_ = true;
                if (!ImGui_ImplDX11_CreateDeviceObjects())
                {
                    throw std::runtime_error("ProfilerViewer D3D11 ImGui device object creation failed.");
                }
                editor::profiler_view::initialize([this](std::function<void()> work)
                {
                    worker_.submit(std::move(work));
                }, source_, [this](std::function<void()> work)
                {
                    diagnostics_worker_.submit(std::move(work));
                });
                presenter_initialized_ = true;
                if (arguments.live)
                {
                    // Connection work and validation belong to the client worker.
                    // Failure is retained in its status for the existing toolbar.
                    source_.connect(arguments.connection);
                }
                if (!arguments.open_path.empty())
                {
                    editor::profiler_view::open_path(arguments.open_path);
                }
                fully_initialized_ = true;
                ShowWindow(window_, restore_maximized ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL);
                UpdateWindow(window_);
            }

            int run()
            {
                while (!closing_)
                {
                    MSG message{};
                    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
                    {
                        if (message.message == WM_QUIT)
                        {
                            closing_ = true;
                            break;
                        }
                        TranslateMessage(&message);
                        DispatchMessageW(&message);
                    }
                    if (closing_)
                    {
                        break;
                    }
                    if (source_.take_focus_request())
                    {
                        ShowWindow(window_, IsIconic(window_) ? SW_RESTORE : SW_SHOW);
                        SetForegroundWindow(window_);
                    }
                    if (minimized_)
                    {
                        wait_for_messages();
                        continue;
                    }
                    if (occluded_)
                    {
                        const HRESULT visibility = swapchain_->Present(0, DXGI_PRESENT_TEST);
                        if (visibility == DXGI_STATUS_OCCLUDED)
                        {
                            wait_for_messages();
                            continue;
                        }
                        check(visibility, "D3D11 visibility check");
                        occluded_ = false;
                    }
                    if (resize_width_ != 0 && resize_height_ != 0)
                    {
                        context_->OMSetRenderTargets(0, nullptr, nullptr);
                        target_.Reset();
                        check(swapchain_->ResizeBuffers(0, resize_width_, resize_height_, DXGI_FORMAT_UNKNOWN, 0), "D3D11 resize");
                        resize_width_ = resize_height_ = 0;
                        create_target();
                    }
                    if (scale_dirty_)
                    {
                        apply_scale();
                    }
                    ImGui_ImplDX11_NewFrame();
                    ImGui_ImplWin32_NewFrame();
                    ImGui::NewFrame();
                    handle_scale_shortcuts();
                    const auto* viewport = ImGui::GetMainViewport();
                    ImGui::SetNextWindowPos(viewport->WorkPos);
                    ImGui::SetNextWindowSize(viewport->WorkSize);
                    ImGui::SetNextWindowViewport(viewport->ID);
                    constexpr auto flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove |
                        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;
                    if (ImGui::Begin("Frame Profiler", nullptr, flags))
                    {
                        editor::profiler_view::draw();
                    }
                    ImGui::End();
                    ImGui::Render();
                    const auto color = editor::ThemeColorValue(editor::ThemeColor::Canvas);
                    const float clear[]{ color.x, color.y, color.z, color.w };
                    ID3D11RenderTargetView* target = target_.Get();
                    context_->OMSetRenderTargets(1, &target, nullptr);
                    context_->ClearRenderTargetView(target, clear);
                    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
                    const HRESULT presented = swapchain_->Present(1, 0);
                    occluded_ = presented == DXGI_STATUS_OCCLUDED;
                    check(presented, "D3D11 presentation (device removed or reset)");
                    if (!occluded_)
                    {
                        source_.note_frame_presented();
                    }
                }
                return 0;
            }

        private:
            void apply_scale()
            {
                editor::ApplyEditorTheme(ImGui::GetStyle(), settings_.scale_milli / 1000.0f, dpi_scale_);
                scale_dirty_ = false;
            }

            void handle_scale_shortcuts()
            {
                const auto& io = ImGui::GetIO();
                if (!io.KeyCtrl || io.WantTextInput)
                {
                    return;
                }
                int next = settings_.scale_milli;
                if (ImGui::IsKeyPressed(ImGuiKey_Equal) || ImGui::IsKeyPressed(ImGuiKey_KeypadAdd))
                {
                    next += 250;
                }
                if (ImGui::IsKeyPressed(ImGuiKey_Minus) || ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract))
                {
                    next -= 250;
                }
                if (ImGui::IsKeyPressed(ImGuiKey_0) || ImGui::IsKeyPressed(ImGuiKey_Keypad0))
                {
                    next = 1000;
                }
                next = std::clamp(next, 500, 3000);
                scale_dirty_ = next != settings_.scale_milli;
                settings_.scale_milli = next;
            }

            static void wait_for_messages()
            {
                // A bounded wake also observes transport focus requests while
                // minimized without a busy loop or creating engine frames.
                MsgWaitForMultipleObjectsEx(0, nullptr, 100, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
            }

            void create_device()
            {
                DXGI_SWAP_CHAIN_DESC description{};
                description.BufferCount = 2;
                description.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
                description.OutputWindow = window_;
                description.SampleDesc.Count = 1;
                description.Windowed = TRUE;
                description.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
                constexpr D3D_FEATURE_LEVEL levels[]{ D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
                auto create = [&](D3D_DRIVER_TYPE type)
                {
                    return D3D11CreateDeviceAndSwapChain(nullptr, type, nullptr, 0, levels,
                        static_cast<UINT>(std::size(levels)), D3D11_SDK_VERSION, &description,
                        swapchain_.ReleaseAndGetAddressOf(), device_.ReleaseAndGetAddressOf(), nullptr,
                        context_.ReleaseAndGetAddressOf());
                };
                HRESULT result = create(D3D_DRIVER_TYPE_HARDWARE);
                if (FAILED(result))
                {
                    result = create(D3D_DRIVER_TYPE_WARP);
                }
                check(result, "ProfilerViewer D3D11 device/swapchain initialization");
                ComPtr<IDXGIFactory> factory;
                check(swapchain_->GetParent(IID_PPV_ARGS(&factory)), "DXGI factory lookup");
                check(factory->MakeWindowAssociation(window_, DXGI_MWA_NO_ALT_ENTER), "DXGI window association");
                create_target();
            }

            void create_target()
            {
                ComPtr<ID3D11Texture2D> buffer;
                check(swapchain_->GetBuffer(0, IID_PPV_ARGS(&buffer)), "D3D11 backbuffer lookup");
                check(device_->CreateRenderTargetView(buffer.Get(), nullptr, target_.ReleaseAndGetAddressOf()), "D3D11 render target creation");
            }

            static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
            {
                auto* self = reinterpret_cast<application*>(GetWindowLongPtrW(window, GWLP_USERDATA));
                if (message == WM_NCCREATE)
                {
                    self = static_cast<application*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
                    SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
                }
                if (ImGui::GetCurrentContext() && ImGui_ImplWin32_WndProcHandler(window, message, wparam, lparam))
                {
                    return 1;
                }
                if (self)
                {
                    switch (message)
                    {
                    case WM_SIZE:
                        self->minimized_ = wparam == SIZE_MINIMIZED;
                        if (!self->minimized_)
                        {
                            self->resize_width_ = LOWORD(lparam);
                            self->resize_height_ = HIWORD(lparam);
                            self->settings_.maximized = wparam == SIZE_MAXIMIZED;
                        }
                        [[fallthrough]];
                    case WM_MOVE:
                        if (!IsIconic(window) && !IsZoomed(window))
                        {
                            GetWindowRect(window, &self->settings_.normal);
                        }
                        return 0;
                    case WM_DPICHANGED:
                    {
                        self->dpi_scale_ = static_cast<float>(HIWORD(wparam)) / 96.0f;
                        self->scale_dirty_ = true;
                        const auto* rectangle = reinterpret_cast<const RECT*>(lparam);
                        SetWindowPos(window, nullptr, rectangle->left, rectangle->top,
                            rectangle->right - rectangle->left, rectangle->bottom - rectangle->top,
                            SWP_NOZORDER | SWP_NOACTIVATE);
                        return 0;
                    }
                    case WM_SYSCOMMAND:
                        if ((wparam & 0xfff0) == SC_KEYMENU)
                        {
                            return 0;
                        }
                        break;
                    case WM_CLOSE:
                        self->closing_ = true;
                        return 0;
                    case WM_DESTROY:
                        PostQuitMessage(0);
                        return 0;
                    }
                }
                return DefWindowProcW(window, message, wparam, lparam);
            }

            HINSTANCE instance_ = nullptr;
            HWND window_ = nullptr;
            bool registered_ = false;
            bool imgui_initialized_ = false;
            bool platform_initialized_ = false;
            bool renderer_initialized_ = false;
            bool presenter_initialized_ = false;
            bool fully_initialized_ = false;
            bool closing_ = false;
            bool minimized_ = false;
            bool occluded_ = false;
            bool scale_dirty_ = false;
            float dpi_scale_ = 1.0f;
            UINT resize_width_ = 0;
            UINT resize_height_ = 0;
            window_settings settings_;
            std::filesystem::path settings_root_;
            std::string ini_path_;
            ComPtr<ID3D11Device> device_;
            ComPtr<ID3D11DeviceContext> context_;
            ComPtr<IDXGISwapChain> swapchain_;
            ComPtr<ID3D11RenderTargetView> target_;
            preparation_worker worker_;
            preparation_worker diagnostics_worker_;
            client source_;
        };
    }
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int)
{
    using namespace ce::profiler_viewer;
    if (!SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_APPLICATION_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32))
    {
        MessageBoxW(nullptr, L"ProfilerViewer could not establish its application/system-only DLL search path.",
                    L"ProfilerViewer", MB_OK | MB_ICONERROR);
        return 1;
    }
    if (!ordinary_token())
    {
        MessageBoxW(nullptr, L"ProfilerViewer requires an ordinary user token. Close this instance and launch it without Run as administrator.",
                    L"ProfilerViewer", MB_OK | MB_ICONERROR);
        return 1;
    }
    int count = 0;
    LPWSTR* raw = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!raw)
    {
        return 1;
    }
    viewer_arguments arguments;
    bool valid = false;
    try
    {
        std::vector<std::wstring_view> values;
        for (int index = 1; index < count; ++index)
        {
            values.emplace_back(raw[index]);
        }
        valid = parse_arguments(values, arguments);
    }
    catch (...)
    {
        valid = false;
    }
    LocalFree(raw);
    if (!valid)
    {
        MessageBoxW(nullptr, L"Use ProfilerViewer.exe, --open <capture.ceprof|capture.cedx>, or the complete Editor-provided --parent/--created/--session/--nonce arguments. Unknown, duplicate, mixed, or malformed arguments are rejected.",
                    L"ProfilerViewer: invalid arguments", MB_OK | MB_ICONERROR);
        return 2;
    }
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    int result = 1;
    try
    {
        check(com, "ProfilerViewer COM initialization");
        application app(instance);
        app.initialize(arguments);
        result = app.run();
    }
    catch (const std::exception& error)
    {
        MessageBoxA(nullptr, error.what(), "ProfilerViewer", MB_OK | MB_ICONERROR);
    }
    catch (...)
    {
        MessageBoxW(nullptr, L"ProfilerViewer stopped after an unexpected host initialization or rendering failure.", L"ProfilerViewer", MB_OK | MB_ICONERROR);
    }
    if (SUCCEEDED(com))
    {
        CoUninitialize();
    }
    return result;
}
