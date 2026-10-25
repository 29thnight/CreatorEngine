#pragma once
// Generated from Engine/Contracts/Core/Creator.Core.Window.ixx; do not edit.
// Regenerate with: py Tools/build/generate-module-adapters.py
#if !defined(CE_CONTRACT_STANDALONE)
#error "Native consumers must import the named module directly."
#endif


#include "../../../Engine/Utility_Framework/Foundation/StandardLibrary.h"

namespace Core
{
    struct WindowDesc
    {
        std::wstring title;
        int clientWidth{ 800 };
        int clientHeight{ 600 };
        bool centerOnDesktop{ true };

        /// `clientWidth`·`clientHeight` 를 **논리 픽셀**로 읽고 대상 모니터의 DPI
        /// 배수만큼 키운다.
        ///
        /// 기본이 거짓인 이유. 게임 창에서 클라이언트 크기는 곧 **렌더 해상도**다.
        /// 200% 모니터에서 말없이 두 배로 키우면 픽셀 수가 네 배가 되어 프레임이
        /// 무너진다. 그쪽은 물리 픽셀이 맞다.
        ///
        /// 에디터는 반대다. UI 가 DPI 배수로 커지는데(ImGui `FontScaleDpi`) 창만
        /// 물리 1920 으로 열리면, 200% 에서 쓸 수 있는 자리가 논리 960 이 된다 —
        /// 도크 넷을 펼칠 수 없는 폭이다. 창도 같은 배수로 커져야 한다.
        bool scaleClientToDpi{ false };
        bool fitNearestMonitor{ false };
        bool showOnCreate{ false };
        bool acceptFileDrops{ false };
    };

    /// DPI 배수로 키운 클라이언트 크기를 정한다 (`scaleClientToDpi`).
    ///
    /// 창 만들기와 떼어 둔 이유. 이 계산의 경계값은 화면을 띄워서는 못 본다 —
    /// 200% 모니터에서 1920 논리는 3840 물리가 되는데 그 모니터가 3840 이면
    /// 작업 표시줄만큼 넘친다. 순수 함수면 검사가 합성 DPI 로 직접 몰 수 있다.
    struct ScaledClientSizeRequest
    {
        /// 호스트가 적은 논리 크기(96 DPI 기준).
        int logicalWidth{ 0 };
        int logicalHeight{ 0 };

        /// 대상 모니터의 DPI. 96 이 배수 1 이다.
        int dpi{ 96 };

        /// 창틀을 뺀 뒤 클라이언트가 쓸 수 있는 최대 크기. 0 이하면 자르지 않는다.
        int maxWidth{ 0 };
        int maxHeight{ 0 };
    };

    struct ScaledClientSize
    {
        int width{ 0 };
        int height{ 0 };
    };

    inline ScaledClientSize ScaleClientSizeToDpi(const ScaledClientSizeRequest& request) noexcept
    {
        // DPI 를 못 읽었거나 0 이 오면 배수 1 로 둔다. 0 으로 나누지 않는 것보다
        // "모르면 키우지 않는다" 가 중요하다 — 잘못 키운 창은 화면 밖으로 나간다.
        const int dpi = request.dpi > 0 ? request.dpi : 96;

        const auto scaled = [dpi](int logical) noexcept
        {
            if (logical <= 0)
            {
                return 0;
            }
            // 96 으로 나누기 전에 곱한다. 정수 나눗셈을 먼저 하면 125%(120 DPI)가
            // 배수 1 로 잘려 아무것도 안 커진다.
            return static_cast<int>((static_cast<long long>(logical) * dpi + 48) / 96);
        };

        ScaledClientSize size{ scaled(request.logicalWidth), scaled(request.logicalHeight) };

        // 작업 영역을 넘지 않게 자른다. 자르지 않으면 200% 모니터에서 창이 화면보다
        // 커져 오른쪽·아래가 잘린 채 열린다.
        if (request.maxWidth > 0 && size.width > request.maxWidth)
        {
            size.width = request.maxWidth;
        }
        if (request.maxHeight > 0 && size.height > request.maxHeight)
        {
            size.height = request.maxHeight;
        }
        return size;
    }

    enum class NativeWindowKind { None, Windows, External };

    struct NativeWindowHandle
    {
        NativeWindowKind kind{ NativeWindowKind::None };
        void* window{};
        void* display{};
    };

    enum class WindowPollResult { Idle, Dispatched, Quit };

    // Construct and drive on the owning UI thread. Backend implementations own
    // their native window and detach host callbacks before destroying it.
    class IWindowBackend
    {
    public:
        virtual ~IWindowBackend() = default;
        virtual void Show() = 0;
        virtual NativeWindowHandle GetNativeHandle() const noexcept = 0;
        virtual int GetWidth() const noexcept = 0;
        virtual int GetHeight() const noexcept = 0;
        virtual WindowPollResult PollEvents() = 0;
    };

    class Window
    {
    public:
        explicit Window(std::unique_ptr<IWindowBackend> backend)
            : backend_(std::move(backend))
        {
            if (!backend_)
            {
                throw std::invalid_argument("Window requires a backend");
            }
        }

        Window(const Window&) = delete;
        Window& operator=(const Window&) = delete;
        Window(Window&&) = delete;
        Window& operator=(Window&&) = delete;

        void Show() { backend_->Show(); }
        NativeWindowHandle GetNativeHandle() const noexcept { return backend_->GetNativeHandle(); }
        int GetWidth() const noexcept { return backend_->GetWidth(); }
        int GetHeight() const noexcept { return backend_->GetHeight(); }
        WindowPollResult PollEvents() { return backend_->PollEvents(); }

        template<typename Tick>
        void Then(Tick&& tick)
        {
            for (;;)
            {
                const auto result = PollEvents();
                if (result == WindowPollResult::Quit)
                {
                    return;
                }
                if (result == WindowPollResult::Idle)
                {
                    tick();
                }
            }
        }

    private:
        std::unique_ptr<IWindowBackend> backend_;
    };
}

using CoreWindow = ::Core::Window;
using WindowDesc = ::Core::WindowDesc;
using ::Core::ScaledClientSizeRequest;
using ::Core::ScaledClientSize;
using ::Core::ScaleClientSizeToDpi;
