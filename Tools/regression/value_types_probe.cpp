#if defined(_DEBUG)
#include <crtdbg.h>
#endif
#if defined(CE_CONTRACT_STANDALONE)
#include <iostream>
#include <memory>
#include <vector>
#include <string>
#include <stdexcept>
#include <limits>
#include "../../Engine/Containers/values/value_types.h"
#else
import std;
import "Creator.Containers.h";
#endif

namespace
{
    unsigned checks = 0;

    void check(bool value, const char* message)
    {
        ++checks;
        if (!value)
            throw std::runtime_error(message);
    }

    struct owned
    {
        static inline int live = 0;
        int value;

        explicit owned(int v) : value(v)
        {
            ++live;
        }

        ~owned()
        {
            --live;
        }
    };

    struct alignas(64) aligned
    {
        int value;
        char padding[60]{};
    };

    struct throwing
    {
        throwing(int)
        {
            throw std::runtime_error("construction");
        }
    };

    struct explicit_value
    {
        explicit explicit_value(int) noexcept
        {
        }
    };

    struct record
    {
        int value = 7;

        int add(int v) const noexcept
        {
            return value + v;
        }

        int fail() const
        {
            throw std::runtime_error("invoke");
        }
    };

    struct tracked
    {
        static inline std::vector<int> events;
        int id;

        explicit tracked(int v) : id(v)
        {
            events.push_back(v);
        }

        ~tracked()
        {
            events.push_back(-id);
        }
    };
#if defined(_DEBUG)
    thread_local bool observing = false;
    thread_local unsigned allocations = 0;

    int allocation_hook(int kind, void*, size_t, int, long, const unsigned char*, int)
    {
        if (observing && (kind == _HOOK_ALLOC || kind == _HOOK_REALLOC))
            ++allocations;
        return 1;
    }
#endif
    constexpr bool constant_contracts()
    {
        ce::pair<int, int> p{3, 4};
        auto [x, y] = p;
        ce::tuple<int, long, int> t{1, 2, 3};
        ce::get<long>(t) = 5;
        int a = 0, b = 0;
        ce::tie(a, b) = p;
        return x == 3 && y == 4 && a == 3 && b == 4 &&
               ce::apply([](auto... v) constexpr noexcept { return (0 + ... + v); }, t) == 9 &&
               ce::tuple<>{} == ce::tuple<>{} && ce::tuple{1, 2} < ce::tuple{1, 3};
    }

    static_assert(constant_contracts());
    static_assert(std::is_trivially_copyable_v<ce::pair<int, int>>);
    static_assert(std::is_trivially_copy_constructible_v<ce::tuple<int, int>>);
    static_assert(!std::is_copy_constructible_v<ce::tuple<std::unique_ptr<owned>>>);
    static_assert(!std::is_default_constructible_v<ce::tuple<int&>>);
    static_assert(!std::is_constructible_v<ce::pair<const std::string&, int>, const char*, int>);
    static_assert(!std::is_constructible_v<ce::tuple<const std::string&>, const char*>);
    static_assert(!std::is_constructible_v<ce::tuple<const int&>, ce::tuple<int>&&>);
    static_assert(!std::is_constructible_v<ce::pair<const int&, int>, ce::pair<int, int>&&>);
    static_assert(std::is_constructible_v<ce::tuple<int&>, ce::tuple<int&>&&>);
    static_assert(std::is_same_v<decltype(ce::get<0>(std::declval<ce::tuple<int>&&>())), int&&>);
    static_assert(std::is_same_v<decltype(ce::get<0>(std::declval<const ce::tuple<int>&>())), const int&>);
    static_assert(std::is_same_v<decltype(ce::get<0>(std::declval<const ce::tuple<int&>&>())), int&>);
    static_assert(std::is_same_v<ce::tuple_element_t<0, const ce::tuple<int>>, const int>);
    static_assert(std::tuple_size_v<ce::pair<int, int>> == 2);
    static_assert(std::tuple_size_v<ce::tuple<int, int, int>> == 3);
    static_assert(std::is_nothrow_constructible_v<ce::tuple<int, int>, int, int>);
    static_assert(!std::is_nothrow_constructible_v<ce::tuple<throwing>, int>);
    static_assert(std::is_constructible_v<ce::tuple<explicit_value>, int>);
    static_assert(!std::is_convertible_v<int, ce::tuple<explicit_value>>);
    static_assert(alignof(ce::tuple<char, aligned>) >= 64);
} // namespace

struct throwing_product
{
    template<std::size_t> int get() const { throw std::runtime_error("foreign index get"); }
    template<class> int get() const { throw std::runtime_error("foreign type get"); }
};
static_assert(!noexcept(ce::get<0>(std::declval<throwing_product&>())));
static_assert(!noexcept(ce::get<int>(std::declval<throwing_product&>())));
int main()
try
{
    check(constant_contracts(), "constexpr behavior");
    unsigned foreign_throws = 0;
    try { (void)ce::get<0>(throwing_product{}); } catch (const std::runtime_error&) { ++foreign_throws; }
    try { (void)ce::get<int>(throwing_product{}); } catch (const std::runtime_error&) { ++foreign_throws; }
    check(foreign_throws == 2, "foreign get exceptions propagate");
    ce::tuple<> zero;
    check(ce::apply([] { return 9; }, zero) == 9, "empty apply");
    struct coordinates { int x, y; };
    ce::pair<int, coordinates> braced_pair{1, {2, 3}};
    ce::tuple<coordinates, int> braced_tuple{{4, 5}, 6};
    int label = 7;
    ce::pair<int, std::unique_ptr<int>> empty_owner{label, {}};
    check(braced_pair.second.y == 3 && ce::get<0>(braced_tuple).x == 4 &&
          empty_owner.first == 7 && !empty_owner.second, "typed list construction preserves ownership");
    int a = 1, b = 2;
    auto refs = ce::tie(a, b);
    refs = ce::make_tuple(3, 4);
    check(a == 3 && b == 4, "tie assignment writes original objects");
    auto ref_copy = refs;
    ce::get<0>(ref_copy) = 8;
    check(a == 8, "reference copy aliases");
    const auto const_refs = ce::tie(a, b);
    const_refs = ce::pair{11, 12};
    check(a == 11 && b == 12, "const reference product assignment");
    ce::pair<int&, int&> pr{a, b};
    pr = ce::pair{13, 14};
    check(a == 13 && b == 14, "pair reference assignment");
    auto wrapped = ce::make_pair(std::ref(a), 0);
    wrapped.first = 15;
    check(a == 15, "make_pair unwraps reference wrapper");
    auto wrapped_tuple = ce::make_tuple(std::ref(a));
    ce::get<0>(wrapped_tuple) = 16;
    check(a == 16, "make_tuple unwraps reference wrapper");
    ce::tuple<std::string, int> strings{"retained", 7};
    auto strings_copy = strings;
    ce::get<0>(strings).assign("changed");
    check(ce::get<0>(strings_copy) == "retained", "owned copy independence");
    ce::tuple<std::string> converted_tuple{std::tuple<std::string>{"inner"}};
    check(ce::get<0>(converted_tuple) == "inner", "foreign single value product conversion");
    ce::tuple<ce::tuple<int>> nested{ce::tuple<int>{9}};
    check(ce::get<0>(ce::get<0>(nested)) == 9, "nested tuple is not flattened");
    {
        ce::pair<int, std::unique_ptr<owned>> p{1, std::make_unique<owned>(6)};
        auto moved = std::move(p);
        check(!p.second && moved.second->value == 6 && owned::live == 1, "pair move-only ownership");
        ce::tuple<std::unique_ptr<owned>, int> t{std::make_unique<owned>(7), 2};
        auto transferred = ce::apply([](auto value, int) { return value; }, std::move(t));
        check(!ce::get<0>(t) && transferred->value == 7 && owned::live == 2, "apply move-only ownership");
    }
    check(owned::live == 0, "last values destroy owned objects");
    {
        owned borrowed{9};
        {
            auto t = ce::tie(borrowed);
            check(&ce::get<0>(t) == &borrowed, "borrowed identity");
        }
        check(owned::live == 1, "borrowed product does not destroy referent");
    }
    check(owned::live == 0, "referent owner controls lifetime");
    bool failed = false;
    try
    {
        ce::tuple<std::unique_ptr<owned>, throwing> partial{std::make_unique<owned>(1), 0};
    }
    catch (const std::runtime_error&)
    {
        failed = true;
    }
    check(failed && owned::live == 0, "construction exception preserves ownership");
    record target;
    check(ce::apply(&record::add, ce::tie(target, a)) == target.value + a, "member object invocation");
    check(ce::apply(&record::add, ce::make_tuple(&target, 2)) == 9, "member pointer invocation");
    ce::apply(&record::value, ce::tie(target)) = 21;
    check(target.value == 21, "member data reference");
    check(ce::apply(&record::add, ce::make_tuple(std::ref(target), 2)) == 23, "member reference wrapper");
    static_assert(noexcept(ce::apply(&record::add, ce::make_tuple(&target, 1))));
    failed = false;
    try
    {
        ce::apply(&record::fail, ce::tie(target));
    }
    catch (const std::runtime_error&)
    {
        failed = true;
    }
    check(failed, "apply propagates exceptions");
    const std::tuple<int, int> foreign{2, 3};
    check(ce::apply([](int x, int y) { return x + y; }, foreign) == 5, "foreign tuple bridge");
    const ce::fixed_array<int, 2> fixed{4, 5};
    check(ce::apply([](int x, int y) { return x + y; }, fixed) == 9, "fixed array ADL bridge");
    std::variant<int, std::string> variant{17};
    check(ce::get<int>(variant) == 17, "foreign variant get bridge");
    ce::pair<std::string, int> converted{std::pair<std::string, int>{"owned", 8}};
    check(converted.first == "owned" && converted.second == 8, "foreign pair conversion");
    ce::pair<std::string, std::string> pieces(ce::piecewise_construct, ce::make_tuple(4, 'a'), ce::make_tuple(5, 'b'));
    check(pieces.first == "aaaa" && pieces.second == "bbbbb", "piecewise values");
    for (int i = -50; i < 50; ++i)
        for (int j = -20; j < 20; ++j)
        {
            auto x = ce::tuple{i, j, 4};
            auto y = ce::tuple{j, i, 5};
            auto sx = std::tuple{i, j, 4};
            auto sy = std::tuple{j, i, 5};
            check((x < y) == (sx < sy) && (x == y) == (sx == sy), "ordering matches STL");
        }
    const auto nan = std::numeric_limits<double>::quiet_NaN();
    check((ce::tuple{nan} <=> ce::tuple{1.0}) == std::partial_ordering::unordered, "partial ordering");
    check(!(ce::tuple{nan} < ce::tuple{1.0}) && !(ce::tuple{1.0} < ce::tuple{nan}), "unordered less remains false");
    tracked::events.clear();
    {
        std::tuple<tracked, tracked> t{1, 2};
    }
    const auto standard_order = tracked::events;
    tracked::events.clear();
    {
        ce::tuple<tracked, tracked> t{1, 2};
    }
    check(tracked::events == standard_order, "MSVC element lifetime order");
    bool correct = true;
#if defined(_DEBUG)
    const auto previous = _CrtSetAllocHook(allocation_hook);
    allocations = 0;
    observing = true;
#endif
    for (int i = 0; i < 8192; ++i)
    {
        auto t = ce::make_tuple(i, i + 1, i + 2);
        ce::pair<int, int> p{i, i + 3};
        ce::tie(a, b) = p;
        correct &= ce::apply([](auto... v) noexcept { return (0 + ... + v); }, t) == 3 * i + 3 && b == i + 3;
    }
#if defined(_DEBUG)
    observing = false;
    _CrtSetAllocHook(previous);
    check(allocations == 0, "value operations allocate zero");
#endif
    check(correct, "value operations correct");
    std::cout << "VALUE_TYPES_OK checks=" << checks
              << " valueIterations=8192 ownership=1 references=1 memberInvoke=1 order=1";
#if defined(_DEBUG)
    std::cout << " allocations=0\n";
#else
    std::cout << " allocationChecks=0\n";
#endif
    return 0;
}
catch (const std::exception& error)
{
    std::cerr << "VALUE_TYPES_FAIL " << error.what() << '\n';
    return 1;
}
