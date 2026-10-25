module;
#include "AppBackendWinAPI.Crash.h"

module Creator.AppBackend.WinAPI;

import std;
import "Creator.WinAPI.h";

namespace backend_crash = ::ce::platform::winapi::crash;

struct AppBackendWinAPI::state final : public Core::IWindowBackend
{
public:
    using MessageHandler = std::function<LRESULT(HWND, WPARAM, LPARAM)>;

    state(HINSTANCE hInstance, const WindowsWindowDesc& desc, AppBackendWinAPI* owner)
        : m_hInstance(hInstance), m_desc(desc)
        , m_width(desc.clientWidth), m_height(desc.clientHeight)
        , m_iconResourceId(desc.iconResourceId)
    {
        s_instance = this;
        AppBackendWinAPI::instance_ = owner;
        try
        {
            RegisterWindowClass();
            CreateAppWindow();
            if (!m_hWnd)
            {
                throw std::runtime_error("Win32 window creation failed");
            }
            m_appWindow.Attach(m_hWnd);
            m_appWindow.SetTitle(m_desc.title);
            if (m_desc.showOnCreate)
            {
                Show();
            }
        }
        catch (...)
        {
            DetachCallbacks();
            m_appWindow.Detach();
            if (m_hWnd)
            {
                DestroyWindow(m_hWnd);
                m_hWnd = nullptr;
            }
            UnregisterClass(L"CoreWindowApp", m_hInstance);
            if (s_instance == this)
            {
                s_instance = nullptr;
            }
            throw;
        }

        // 크래시 후크는 여기서 걸지 않는다 — LogSystem::InstallCrashGuards가 이미
        // 전부 걸어 두었고, 여기서 또 걸면 설치 순서 싸움이 된다.
        // 덤프 기록자 등록은 SetDumpType에서 한다.
    }

    state(const state&) = delete;
    state& operator=(const state&) = delete;
    state(state&&) = delete;
    state& operator=(state&&) = delete;

    ~state()
    {
        // RestoreDisplayMode() 호출이 여기 있었다 (2026-08-10).
        // 플레이어가 표시 모드를 바꾸지 않게 된 뒤로 되돌릴 것이 없다.

        // Idempotent fallback; bootstrap disconnects while App is still alive.
        DetachCallbacks();
        m_appWindow.Detach();
        if (m_hWnd)
        {
            DestroyWindow(m_hWnd);
            m_hWnd = nullptr;
        }
        UnregisterClass(L"CoreWindowApp", m_hInstance);

        // App/presentation is gone before this native backend. Clear the crash
        // lookup after native teardown so later shutdown cannot read a dead HWND.
        if (s_instance == this)
        {
            s_instance = nullptr;
        }
    }

    // End every borrow before App/InputManager/EditorMain destruction. Native
    // teardown and COM release can synchronously dispatch window messages.
    void DetachCallbacks() noexcept
    {
        m_desc.messageInterceptor = {};
        m_handlers.clear();
    }

    void SetHandler(UINT message, MessageHandler handler)
    {
        m_handlers[message] = std::move(handler);
    }

    template <typename Instance>
    void RegisterHandler(UINT message, Instance* instance, LRESULT(Instance::* handler)(HWND, WPARAM, LPARAM))
    {
        m_handlers[message] = [=](HWND hWnd, WPARAM wParam, LPARAM lParam)
        {
            return (instance->*handler)(hWnd, wParam, lParam);
        };
    }

    template <typename Initializer>
    state& InitializeTask(Initializer fn_initializer)
    {
        fn_initializer();

        return *this;
    }

    Core::WindowPollResult PollEvents() override
    {
        MSG message{};
        if (!PeekMessage(&message, nullptr, 0, 0, PM_REMOVE))
        {
            return Core::WindowPollResult::Idle;
        }
        if (message.message == WM_QUIT)
        {
            return Core::WindowPollResult::Quit;
        }
        DispatchMessage(&message);
        return Core::WindowPollResult::Dispatched;
    }

    /// 숨겨 둔 창을 표시한다. showOnCreate=false인 Host가 초기화를 끝낸 뒤 부른다.
    void Show() override
    {
        if (m_hWnd && m_appWindow.IsAttached() && !IsWindowVisible(m_hWnd))
        {
            m_appWindow.Show();
            UpdateWindow(m_hWnd);
            SetForegroundWindow(m_hWnd);
        }
    }

    Core::NativeWindowHandle GetNativeHandle() const noexcept override
    {
        return { Core::NativeWindowKind::Windows, m_hWnd, nullptr };
    }

    HWND GetHandle() const
    {
        return ce::platform::windows::window_handle(GetNativeHandle());
    }
    int GetWidth() const noexcept override { return m_width; }
    int GetHeight() const noexcept override { return m_height; }

    void EnsureSwapChainCompatibleStyle();

    static state* GetForCurrentInstance()
    {
        return s_instance;
    }

	static void RegisterCreateEventHandler(MessageHandler handler)
	{
		m_CreateEventHandler = handler;
	}

private:
    static state* s_instance;
    HINSTANCE m_hInstance = nullptr;
    WindowsWindowDesc m_desc{};
    HWND m_hWnd = nullptr;
    ce::platform::windows::app_window_host m_appWindow;
    int m_width = 800;
    int m_iconResourceId = 0;
    int m_height = 600;
    std::unordered_map<UINT, MessageHandler> m_handlers;
	static MessageHandler m_CreateEventHandler;

    // 표시 모드 전환 상태 넷(m_displayModeChanged · m_targetDeviceName ·
    // m_originalMode · m_hasOriginalMode)이 여기 있었다 (2026-08-10).
    // 플레이어가 테두리 없는 '창모드'가 되면서 바꿀 모드가 없어졌다.

    void RegisterWindowClass() const
    {
        WNDCLASS wc = {};
        wc.lpfnWndProc = state::WndProc;
        wc.hInstance = m_hInstance;
        wc.lpszClassName = L"CoreWindowApp";
        if (0 != m_iconResourceId)
        {
            wc.hIcon = LoadIcon(m_hInstance, MAKEINTRESOURCE(m_iconResourceId));
        }
        RegisterClass(&wc);
    }

    // 논리 크기로 만든 창을 대상 모니터의 DPI 배수로 키운다.
    //
    // 이 함수가 없던 동안 에디터는 200% 모니터에서 물리 1920 으로 열렸다.
    // ImGui 는 폰트와 여백을 DPI 배수로 키우므로(`FontScaleDpi`) 그 창의
    // 쓸모는 논리 960 짜리였다 — 씬·하이어라키·인스펙터·콘텐츠를 한 번에
    // 펼칠 수 없는 폭이다. 창만 안 커진 것이 문제였다.
    void ApplyDpiClientSize()
    {
        if (!m_desc.scaleClientToDpi || nullptr == m_hWnd)
        {
            return;
        }

        const UINT dpi = GetDpiForWindow(m_hWnd);
        if (0 == dpi || USER_DEFAULT_SCREEN_DPI == dpi)
        {
            return;
        }

        MONITORINFO monitorInfo{};
        monitorInfo.cbSize = sizeof(MONITORINFO);
        const HMONITOR monitor = MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTONEAREST);
        if (!GetMonitorInfoW(monitor, &monitorInfo))
        {
            return;
        }

        // 창틀도 DPI 를 탄다. DPI 를 모르는 `AdjustWindowRectEx` 로 재면
        // 200% 에서 테두리가 절반 폭으로 계산되어 클라이언트가 그만큼 넘친다.
        RECT frame{ 0, 0, 0, 0 };
        AdjustWindowRectExForDpi(&frame, m_desc.style, FALSE,
            m_desc.extendedStyle, dpi);
        const int frameWidth  = (frame.right - frame.left);
        const int frameHeight = (frame.bottom - frame.top);

        // `rcWork` 다. `rcMonitor` 를 쓰면 작업 표시줄 아래로 창이 깔린다.
        const int workWidth  = monitorInfo.rcWork.right - monitorInfo.rcWork.left;
        const int workHeight = monitorInfo.rcWork.bottom - monitorInfo.rcWork.top;

        ScaledClientSizeRequest request{};
        request.logicalWidth  = m_desc.clientWidth;
        request.logicalHeight = m_desc.clientHeight;
        request.dpi           = static_cast<int>(dpi);
        request.maxWidth      = workWidth - frameWidth;
        request.maxHeight     = workHeight - frameHeight;

        const ScaledClientSize size = ScaleClientSizeToDpi(request);
        if (size.width <= 0 || size.height <= 0)
        {
            return;
        }

        m_width  = size.width;
        m_height = size.height;

        const int windowWidth  = m_width + frameWidth;
        const int windowHeight = m_height + frameHeight;

        // 다시 가운데로 놓는다. 만들 때의 가운데는 논리 크기 기준이라 커진
        // 창이 오른쪽·아래로 밀려 있다. 기준도 `GetDesktopWindow` 가 아니라
        // **이 창이 뜬 모니터**의 작업 영역이다 — 그쪽은 주 모니터만 본다.
        const int x = monitorInfo.rcWork.left + (workWidth - windowWidth) / 2;
        const int y = monitorInfo.rcWork.top + (workHeight - windowHeight) / 2;

        SetWindowPos(m_hWnd, nullptr, x, y, windowWidth, windowHeight,
            SWP_NOZORDER | SWP_NOACTIVATE);
    }

    void CreateAppWindow()
    {
        RECT windowRect{ 0, 0, m_width, m_height };
        AdjustWindowRectEx(&windowRect, m_desc.style, FALSE, m_desc.extendedStyle);

        // A monitor-fitted popup needs a deterministic seed monitor. The previous
        // Player path started at (0, 0), so keep that behavior before expanding it
        // to the selected monitor below.
        int x = m_desc.fitNearestMonitor ? 0 : CW_USEDEFAULT;
        int y = m_desc.fitNearestMonitor ? 0 : CW_USEDEFAULT;
        if (m_desc.centerOnDesktop)
        {
            RECT desktopRect{};
            GetWindowRect(GetDesktopWindow(), &desktopRect);
            const int windowWidth = windowRect.right - windowRect.left;
            const int windowHeight = windowRect.bottom - windowRect.top;
            x = desktopRect.left + (desktopRect.right - desktopRect.left - windowWidth) / 2;
            y = desktopRect.top + (desktopRect.bottom - desktopRect.top - windowHeight) / 2;
        }

        m_hWnd = CreateWindowEx(
            m_desc.extendedStyle,
            L"CoreWindowApp",
            m_desc.title.c_str(),
            m_desc.style,
            x, y,
            windowRect.right - windowRect.left,
            windowRect.bottom - windowRect.top,
            nullptr, nullptr,
            m_hInstance,
            this);

        if (!m_hWnd)
        {
            return;
        }

        if (m_desc.acceptFileDrops)
        {
            DragAcceptFiles(m_hWnd, TRUE);
        }

        // 창을 만든 **뒤에** DPI 를 묻는다. 만들기 전에는 어느 모니터에 뜰지
        // 모르고, 모니터마다 DPI 가 다르다. `GetDpiForWindow` 는 user32 라
        // shcore 를 새로 걸지 않아도 된다.
        ApplyDpiClientSize();

        if (m_desc.fitNearestMonitor)
        {
            RECT target{ 0, 0,
                GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };
            MONITORINFO monitorInfo{};
            monitorInfo.cbSize = sizeof(MONITORINFO);
            const HMONITOR monitor =
                MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTONEAREST);
            if (GetMonitorInfoW(monitor, &monitorInfo))
            {
                target = monitorInfo.rcMonitor;
            }

            m_width = target.right - target.left;
            m_height = target.bottom - target.top;
            SetWindowPos(
                m_hWnd, HWND_TOP,
                target.left, target.top,
                m_width, m_height,
                SWP_FRAMECHANGED | SWP_NOOWNERZORDER);
        }

    }

    // ApplyDisplayModeToMonitor · CaptureOriginalDisplayMode · RestoreDisplayMode가
    // 여기 있었다 (2026-08-10). 셋 다 ChangeDisplaySettingsExW로 모니터 해상도를
    // 강제 전환하고 되돌리기 위한 것이었고, 유일한 호출자가 플레이어 창 생성
    // 경로였다 — 그 경로가 모드를 바꾸지 않게 되면서 셋 다 도달 불가가 됐다.

public:
    static LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

private:
    LRESULT HandleMessage(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
};


LRESULT AppBackendWinAPI::state::WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    state* self = nullptr;

    if (message == WM_NCCREATE)
    {
        // 윈도우 생성 시 초기화
        CREATESTRUCT* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
        self = static_cast<state*>(cs->lpCreateParams);
        SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    else
    {
        self = reinterpret_cast<state*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));
    }

    if (self && self->m_desc.messageInterceptor)
    {
        if (const std::optional<LRESULT> result =
            self->m_desc.messageInterceptor(hWnd, message, wParam, lParam))
        {
            return *result;
        }
    }

    if (message == WM_CREATE && m_CreateEventHandler)
    {
        m_CreateEventHandler(hWnd, wParam, lParam);
    }

    if (self)
    {
        return self->HandleMessage(hWnd, message, wParam, lParam);
    }

    return DefWindowProc(hWnd, message, wParam, lParam);
}

LRESULT AppBackendWinAPI::state::HandleMessage(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{

    auto it = m_handlers.find(message);
    if (it != m_handlers.end())
    {
        return it->second(hWnd, wParam, lParam);
    }

    return DefWindowProc(hWnd, message, wParam, lParam);
}

void AppBackendWinAPI::state::EnsureSwapChainCompatibleStyle()
{
    if (!m_hWnd)
    {
        return;
    }

    constexpr LONG requiredStyleMask = WS_POPUP | WS_BORDER | WS_CAPTION | WS_SYSMENU |
        WS_MAXIMIZEBOX | WS_MINIMIZEBOX | WS_THICKFRAME;

    LONG currentStyle = GetWindowLong(m_hWnd, GWL_STYLE);
    LONG currentExStyle = GetWindowLong(m_hWnd, GWL_EXSTYLE);

    bool styleUpdated = false;

    if ((currentStyle & requiredStyleMask) == 0)
    {
        currentStyle |= WS_OVERLAPPEDWINDOW;
        styleUpdated = true;
    }

    if ((currentExStyle & WS_EX_TOPMOST) != 0)
    {
        currentExStyle &= ~WS_EX_TOPMOST;
        styleUpdated = true;
    }

    if (styleUpdated)
    {
        SetWindowLong(m_hWnd, GWL_STYLE, currentStyle);
        SetWindowLong(m_hWnd, GWL_EXSTYLE, currentExStyle);
        SetWindowPos(m_hWnd, HWND_NOTOPMOST, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED | SWP_NOACTIVATE);
    }
}

AppBackendWinAPI* AppBackendWinAPI::instance_ = nullptr;
AppBackendWinAPI::state* AppBackendWinAPI::state::s_instance = nullptr;
AppBackendWinAPI::MessageHandler AppBackendWinAPI::state::m_CreateEventHandler = nullptr;

AppBackendWinAPI::AppBackendWinAPI(HINSTANCE instance, const WindowsWindowDesc& desc)
{
    try
    {
        state_ = std::make_unique<state>(instance, desc, this);
    }
    catch (...)
    {
        if (instance_ == this)
        {
            instance_ = nullptr;
        }
        throw;
    }
}

AppBackendWinAPI::~AppBackendWinAPI()
{
    state_.reset();
    if (instance_ == this)
    {
        instance_ = nullptr;
    }
}

AppBackendWinAPI::state& AppBackendWinAPI::ActiveState() const noexcept
{
    // Synchronous creation callbacks can query the singleton before make_unique returns.
    return state_ ? *state_ : *state::GetForCurrentInstance();
}

void AppBackendWinAPI::DetachCallbacks() noexcept { ActiveState().DetachCallbacks(); }
void AppBackendWinAPI::Show() { ActiveState().Show(); }
Core::NativeWindowHandle AppBackendWinAPI::GetNativeHandle() const noexcept { return ActiveState().GetNativeHandle(); }
HWND AppBackendWinAPI::GetHandle() const
{
    const auto* active = state_ ? state_.get() : state::GetForCurrentInstance();
    return active ? active->GetHandle() : nullptr;
}
int AppBackendWinAPI::GetWidth() const noexcept { return ActiveState().GetWidth(); }
int AppBackendWinAPI::GetHeight() const noexcept { return ActiveState().GetHeight(); }
Core::WindowPollResult AppBackendWinAPI::PollEvents() { return ActiveState().PollEvents(); }
void AppBackendWinAPI::EnsureSwapChainCompatibleStyle() { ActiveState().EnsureSwapChainCompatibleStyle(); }
void AppBackendWinAPI::RegisterHandler(UINT message, MessageHandler handler)
{
    ActiveState().SetHandler(message, std::move(handler));
}
AppBackendWinAPI* AppBackendWinAPI::GetForCurrentInstance()
{
    return state::GetForCurrentInstance() ? instance_ : nullptr;
}
void AppBackendWinAPI::RegisterCreateEventHandler(MessageHandler handler)
{
    state::RegisterCreateEventHandler(std::move(handler));
}
void AppBackendWinAPI::SetUnattended(bool unattended) { backend_crash::SetUnattended(unattended); }
bool AppBackendWinAPI::IsUnattended() { return backend_crash::IsUnattended(); }
void AppBackendWinAPI::EnableHeapValidation() { backend_crash::EnableHeapValidation(); }
void AppBackendWinAPI::SuppressCrtDialogs() { backend_crash::SuppressCrtDialogs(); }
std::string AppBackendWinAPI::BuildStackTrace(const CONTEXT& capturedContext, int skipFrames)
{
    std::string result;
    backend_crash::CopyStackTrace(&capturedContext, skipFrames, &result,
        [](void* output, const char* text, unsigned long long size)
        {
            static_cast<std::string*>(output)->assign(text, static_cast<std::size_t>(size));
        });
    return result;
}
void AppBackendWinAPI::WriteCrashDump(void* pointers, const char* reason) { backend_crash::WriteDump(pointers, reason); }
void AppBackendWinAPI::SetDumpType(AppCrashDumpMode mode)
{
    backend_crash::InstallWriter(mode == AppCrashDumpMode::Full, []() -> void*
    {
        const auto* window = GetForCurrentInstance();
        return window ? window->GetHandle() : nullptr;
    });
}
LRESULT CALLBACK AppBackendWinAPI::WndProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    return state::WndProc(window, message, wparam, lparam);
}
