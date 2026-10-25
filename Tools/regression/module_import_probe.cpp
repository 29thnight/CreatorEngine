// Compile both as an import-only consumer and with the explicit source-tool mode.
#if defined(CE_CONTRACT_STANDALONE)
#include "../build/ModuleAdapters/Creator.Contracts.h"
#include "../build/ModuleAdapters/Creator.Concurrency.h"
#include "../build/ModuleAdapters/Creator.Platform.Windows.h"
#else
import std;
import Creator.Contracts;
import Creator.Concurrency;
import Creator.Core.Window;
import Creator.Platform.Windows;
#endif

namespace
{
    class TestBackend final : public Core::IWindowBackend
    {
    public:
        explicit TestBackend(int& destroyed) : destroyed_(destroyed) {}
        ~TestBackend() override { ++destroyed_; }
        void Show() override { shown = true; }
        Core::NativeWindowHandle GetNativeHandle() const noexcept override
        {
            return { Core::NativeWindowKind::External, nullptr, nullptr };
        }
        int GetWidth() const noexcept override { return 640; }
        int GetHeight() const noexcept override { return 480; }
        Core::WindowPollResult PollEvents() override
        {
            switch (polls_++)
            {
            case 0:
                return Core::WindowPollResult::Dispatched;
            case 1:
                return Core::WindowPollResult::Idle;
            default:
                return Core::WindowPollResult::Quit;
            }
        }
        bool shown{};

    private:
        int& destroyed_;
        int polls_{};
    };
}

int main()
{
    static_assert(std::is_same_v<CoreWindow, Core::Window>);
    static_assert(std::is_base_of_v<WindowDesc, WindowsWindowDesc>);
    static_assert(!std::is_copy_constructible_v<CoreWindow>);
    static_assert(!std::is_move_constructible_v<CoreWindow>);

    const auto scaled = ScaleClientSizeToDpi({ 800, 600, 120, 900, 0 });
    const auto fallback = ScaleClientSizeToDpi({ -1, 600, 0, 0, 0 });
    if (scaled.width != 900 || scaled.height != 750 || fallback.width != 0 || fallback.height != 600)
    {
        return 1;
    }
    int destroyed{};
    int ticks{};
    {
        auto backend = std::make_unique<TestBackend>(destroyed);
        auto* observed = backend.get();
        CoreWindow window(std::move(backend));
        window.Show();
        window.Then([&ticks] { ++ticks; });
        if (!observed->shown || ticks != 1 || window.GetWidth() != 640 || window.GetHeight() != 480)
        {
            return 2;
        }
        if (ce::platform::windows::window_handle(window.GetNativeHandle()) != nullptr)
        {
            return 3;
        }
    }
    if (destroyed != 1)
    {
        return 4;
    }
    try
    {
        CoreWindow invalid(nullptr);
        return 5;
    }
    catch (const std::invalid_argument&)
    {
    }
    int evaluations{};
    ce::contracts::assert_that([&evaluations] { ++evaluations; return true; });
    if (evaluations != (ce::contracts::assertions_enabled ? 1 : 0))
    {
        return 6;
    }
    ce::contracts::enforce_that([&evaluations] { ++evaluations; return true; });
    if (evaluations != (ce::contracts::assertions_enabled ? 2 : 1))
    {
        return 7;
    }
    ce::concurrent_queue<int> queue;
    queue.push(42);
    int value{};
    if (!queue.try_pop(value) || value != 42 || queue.try_pop(value))
    {
        return 8;
    }
    return 0;
}
