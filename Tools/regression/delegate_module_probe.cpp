#if defined(CE_CONTRACT_STANDALONE)
#include "../build/ModuleAdapters/Creator.Core.Delegate.h"
#else
import std;
import Creator.Core.Delegate;
#endif
namespace { unsigned reports{}; }
extern "C" void creator_report_delegate_exception(const char*, const char*) noexcept { ++reports; }
int main()
{
    unsigned checks{};
    const auto require = [&](bool ok)
    {
        ++checks;
        if (!ok)
        {
            throw std::runtime_error("delegate contract failed");
        }
    };
    ce::delegate_handle empty;
    require(!empty.is_valid() && empty.id() == 0);
    ce::delegate<void, int> event;
    std::vector<int> order;
    auto low = event.add_lambda([&](int v) { order.push_back(v); }, -1);
    auto high = event.add_lambda([&](int) { order.push_back(2); }, 5);
    auto equal = event.add_lambda([&](int) { order.push_back(3); }, 5);
    require(low.is_valid() && high.id() != low.id() && equal.id() != high.id());
    event.broadcast(1);
    require(order == std::vector<int>{3, 2, 1});
    event.remove(high);
    require(!high.is_valid());
    event.add_lambda([](int) { throw std::runtime_error("expected"); }, 10);
    order.clear();
    event(4);
    require(reports == 1 && order == std::vector<int>{3, 4});
    struct receiver { int count{}; void accept(int v) { count += v; } };
    auto target = std::make_shared<receiver>();
    ce::delegate<void, int> weak_event;
    weak_event.add_shared(target, &receiver::accept);
    weak_event.broadcast(7);
    require(target->count == 7 && target.use_count() == 1);
    target.reset();
    weak_event.broadcast(1);
    receiver raw;
    weak_event.add_raw(&raw, &receiver::accept);
    weak_event.broadcast(2);
    require(raw.count == 2);
    ce::delegate<void> stopping;
    int after{};
    stopping.add_lambda([&] { ++after; });
    stopping.add_lambda([&] { stopping.clear(); }, 1);
    stopping.broadcast();
    require(after == 0);
    stopping.add_lambda([&] { ++after; });
    stopping.broadcast();
    require(after == 0);
    ce::delegate<void> snapshot;
    ce::delegate_handle second;
    snapshot.add_lambda([&] { snapshot.remove(second); }, 1);
    second = snapshot.add_lambda([&] { ++after; });
    snapshot.broadcast();
    require(after == 1 && !second.is_valid());
    snapshot.broadcast();
    require(after == 1);
    // Callback destruction may reenter the event after clear releases its lock.
    struct reentrant
    {
        ce::delegate<void>& event;
        ce::delegate_handle& handle;
        ~reentrant() { event.remove(handle); }
    };
    ce::delegate<void> cleanup;
    ce::delegate_handle captured;
    auto capture = std::make_shared<reentrant>(cleanup, captured);
    captured = cleanup.add_lambda([capture] {});
    capture.reset();
    cleanup.clear();
    require(captured.is_valid()); // Empty remove intentionally leaves its handle intact.
    std::cout << "DELEGATE_OK checks=" << checks << '\n';
}
