import std;
import Creator.Contracts;
import Creator.Concurrency;
import Creator.Platform.Windows;
import Creator.Core.Window;

constexpr bool ConstantContract()
{
    ce::contracts::enforce_that([] { return true; }, "constant contract through module import");
    return true;
}

static_assert(ConstantContract());

// A backend with no SDK dependency exercises ownership and event-loop policy.
class ProbeWindowBackend final : public Core::IWindowBackend
{
public:
    explicit ProbeWindowBackend(int& destroyed) : destroyed_(destroyed) {}
    ~ProbeWindowBackend() override { ++destroyed_; }
    void Show() override { shown = true; }
    Core::NativeWindowHandle GetNativeHandle() const noexcept override
    {
        return { Core::NativeWindowKind::External, nullptr, nullptr };
    }
    int GetWidth() const noexcept override { return 640; }
    int GetHeight() const noexcept override { return 480; }
    Core::WindowPollResult PollEvents() override
    {
        constexpr std::array events{ Core::WindowPollResult::Idle,
            Core::WindowPollResult::Dispatched, Core::WindowPollResult::Idle,
            Core::WindowPollResult::Quit };
        return events.at(next_++);
    }
    bool shown{};
private:
    int& destroyed_;
    std::size_t next_{};
};

int main()
{
    int destroyed = 0;
    int ticks = 0;
    {
        auto backend = std::make_unique<ProbeWindowBackend>(destroyed);
        auto* observed = backend.get();
        CoreWindow window(std::move(backend));
        window.Show();
        if (!observed->shown || window.GetWidth() != 640 || window.GetHeight() != 480
            || ce::platform::windows::window_handle(window.GetNativeHandle()) != nullptr)
        {
            return 7;
        }
        window.Then([&] { ++ticks; });
    }
    if (ticks != 2 || destroyed != 1)
    {
        return 8;
    }

    const ce::platform::windows::apartment_scope apartment{
        ce::platform::windows::apartment_model::mta };
    if (apartment.result < 0)
    {
        return 6;
    }

    std::vector<int> values{4, 1, 3, 2};
    std::ranges::sort(values);
    const std::array expected{1, 2, 3, 4};
    if (!std::ranges::equal(values, expected))
    {
        return 1;
    }
    const auto value = std::make_unique<int>(42);
    ce::contracts::enforce_that([&] { return *value == 42; }, "imported memory facilities");
    ce::concurrent_queue<int> queue;
    queue.push(42);
    int popped = 0;
    if (!queue.try_pop(popped) || popped != 42 || !queue.empty())
    {
        return 5;
    }

    int assertions = 0;
    ce::contracts::assert_that([&] { return ++assertions == 1; }, "configured lazy assertion");
    if (assertions != (ce::contracts::assertions_enabled ? 1 : 0))
    {
        return 2;
    }
    int enforced = 0;
    ce::contracts::enforce_that([&] { return ++enforced == 1; }, "always enforced contract");
    if (enforced != 1)
    {
        return 3;
    }
    const auto location = std::source_location::current();
    if (std::string_view(location.file_name()).find("import_std_probe.cpp") == std::string_view::npos)
    {
        return 4;
    }
    std::cout << "PASS import std + Creator.Contracts + Creator.Platform.Windows: assertions=" << assertions
              << " enforced=" << enforced << " sorted=" << values.size() << '\n';
    return 0;
}
