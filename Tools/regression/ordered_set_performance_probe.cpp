#include "../../Engine/Utility_Framework/Foundation/EngineOrderedSet.h"
#include <Windows.h>

namespace
{
    using Clock = std::chrono::steady_clock;
    volatile size_t sink = 0;

    template<class Function>
    std::array<double, 15> Measure(Function function)
    {
        size_t checksum = 0;
        const auto warmEnd = Clock::now() + std::chrono::seconds(1);
        do { checksum += function(); } while (Clock::now() < warmEnd);
        std::array<double, 15> samples{};
        for (auto& value : samples)
        {
            size_t iterations = 0;
            const auto start = Clock::now();
            auto now = start;
            do { checksum += function(); ++iterations; now = Clock::now(); }
            while (now - start < std::chrono::milliseconds(100));
            value = std::chrono::duration<double, std::nano>(now - start).count() / static_cast<double>(iterations);
        }
        sink = checksum;
        return samples;
    }

    template<class CE, class STL, class Function>
    void Case(const char* name, size_t count, bool reverse, Function operation)
    {
        std::array<double, 15> custom{}, reference{};
        if (reverse) { reference = Measure([&] { return operation.template operator()<STL>(); }); custom = Measure([&] { return operation.template operator()<CE>(); }); }
        else { custom = Measure([&] { return operation.template operator()<CE>(); }); reference = Measure([&] { return operation.template operator()<STL>(); }); }
        for (size_t i = 0; i != custom.size(); ++i)
            std::printf("%s,%zu,%zu,%.9f,%.9f\n", name, count, i, custom[i], reference[i]);
        std::fflush(stdout);
    }

    template<class Container>
    Container Filled(size_t count)
    {
        Container values;
        for (size_t i = 0; i != count; ++i) values.insert(static_cast<unsigned>((i * 2654435761u) % (count * 2)));
        return values;
    }
}

int main(int argc, char** argv)
{
    if (!SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS) ||
        !SetThreadAffinityMask(GetCurrentThread(), DWORD_PTR{1} << 4)) return 2;
    const bool reverse = argc > 1 && std::string_view(argv[1]) == "--reverse";
    std::printf("case,count,sample,customNs,stlNs\n");
    for (const size_t count : {size_t{128}, size_t{4096}})
    {
        Case<ce::engine_ordered_set<unsigned>, std::set<unsigned>>("insert_clear", count, reverse, [=]<class Set> {
            auto values = Filled<Set>(count);
            return values.size();
        });
        const auto custom = Filled<ce::engine_ordered_set<unsigned>>(count);
        const auto reference = Filled<std::set<unsigned>>(count);
        Case<ce::engine_ordered_set<unsigned>, std::set<unsigned>>("hit", count, reverse, [&]<class Set> {
            const auto& values = [&]() -> const Set& { if constexpr (std::is_same_v<Set, decltype(custom)> || std::is_same_v<Set, std::remove_cv_t<decltype(custom)>>) return custom; else return reference; }();
            size_t sum = 0;
            for (size_t i = 0; i != count; ++i) sum += values.contains(static_cast<unsigned>((i * 2654435761u) % (count * 2)));
            return sum;
        });
        Case<ce::engine_ordered_set<unsigned>, std::set<unsigned>>("miss", count, reverse, [&]<class Set> {
            const auto& values = [&]() -> const Set& { if constexpr (std::is_same_v<Set, std::remove_cv_t<decltype(custom)>>) return custom; else return reference; }();
            size_t sum = 0;
            for (size_t i = 0; i != count; ++i) sum += values.contains(static_cast<unsigned>(count * 2 + i));
            return sum;
        });
        std::vector<unsigned> holes;
        for (unsigned i = 0; i != count * 2; ++i) if (!reference.contains(i)) holes.push_back(i);
        Case<ce::engine_ordered_set<unsigned>, std::set<unsigned>>("interleaved_miss", count, reverse, [&]<class Set> {
            const auto& values = [&]() -> const Set& { if constexpr (std::is_same_v<Set, std::remove_cv_t<decltype(custom)>>) return custom; else return reference; }();
            size_t sum = 0;
            for (const auto value : holes) sum += values.contains(value);
            return sum;
        });
    }
    Case<ce::engine_ordered_multiset<unsigned>, std::multiset<unsigned>>("multi_insert_clear", 4096, reverse, []<class Set> {
        Set values;
        for (unsigned i = 0; i != 4096; ++i) values.insert(i % 32);
        return values.size();
    });
    ce::engine_ordered_multiset<unsigned> custom;
    std::multiset<unsigned> reference;
    for (unsigned i = 0; i != 4096; ++i) { custom.insert(i % 32); reference.insert(i % 32); }
    Case<ce::engine_ordered_multiset<unsigned>, std::multiset<unsigned>>("multi_equal_range", 4096, reverse, [&]<class Set> {
        const auto& values = [&]() -> const Set& { if constexpr (std::is_same_v<Set, decltype(custom)>) return custom; else return reference; }();
        size_t sum = 0;
        for (unsigned i = 0; i != 32; ++i)
        {
            auto range = values.equal_range(i);
            sum += static_cast<size_t>(std::distance(range.first, range.second));
        }
        return sum;
    });
    return 0;
}
