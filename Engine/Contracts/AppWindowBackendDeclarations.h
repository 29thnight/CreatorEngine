#pragma once

namespace ce::platform::windows::detail
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
