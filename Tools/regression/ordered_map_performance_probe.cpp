#include "../../Engine/Utility_Framework/Foundation/EngineOrderedMap.h"
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
        for (size_t i = 0; i != count; ++i) values.try_emplace(static_cast<unsigned>((i * 2654435761u) % (count * 2)), static_cast<unsigned>(i));
        return values;
    }
}

int main(int argc, char** argv)
{
    if (argc > 1 && std::string_view(argv[1]) == "--layout")
    {
        using CE = ce::engine_ordered_map<unsigned, unsigned>;
        using STL = std::map<unsigned, unsigned>;
        std::printf("NODE_LAYOUT ce=%zu stl=%zu ceResult=%zu stlResult=%zu\n", sizeof(CE::node_type), sizeof(STL::node_type), sizeof(CE::insert_return_type), sizeof(STL::insert_return_type));
        return 0;
    }
    if (!SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS) ||
        !SetThreadAffinityMask(GetCurrentThread(), DWORD_PTR{1} << 4)) return 2;
    bool reverse = false, nodeOnly = false, coreOnly = false;
    for (int i = 1; i < argc; ++i)
    {
        const std::string_view argument(argv[i]);
        if (argument == "--reverse") reverse = true;
        else if (argument == "--nodes-only") nodeOnly = true;
        else if (argument == "--core-only") coreOnly = true;
        else return 2;
    }
    std::printf("case,count,sample,customNs,stlNs\n");
    if (!nodeOnly)
    {
    for (const size_t count : {size_t{128}, size_t{4096}})
    {
        Case<ce::engine_ordered_map<unsigned, unsigned>, std::map<unsigned, unsigned>>("insert_clear", count, reverse, [=]<class Set> {
            auto values = Filled<Set>(count);
            return values.size();
        });
        const auto custom = Filled<ce::engine_ordered_map<unsigned, unsigned>>(count);
        const auto reference = Filled<std::map<unsigned, unsigned>>(count);
        Case<ce::engine_ordered_map<unsigned, unsigned>, std::map<unsigned, unsigned>>("hit", count, reverse, [&]<class Set> {
            const auto& values = [&]() -> const Set& { if constexpr (std::is_same_v<Set, decltype(custom)> || std::is_same_v<Set, std::remove_cv_t<decltype(custom)>>) return custom; else return reference; }();
            size_t sum = 0;
            for (size_t i = 0; i != count; ++i) sum += values.contains(static_cast<unsigned>((i * 2654435761u) % (count * 2)));
            return sum;
        });
        Case<ce::engine_ordered_map<unsigned, unsigned>, std::map<unsigned, unsigned>>("miss", count, reverse, [&]<class Set> {
            const auto& values = [&]() -> const Set& { if constexpr (std::is_same_v<Set, std::remove_cv_t<decltype(custom)>>) return custom; else return reference; }();
            size_t sum = 0;
            for (size_t i = 0; i != count; ++i) sum += values.contains(static_cast<unsigned>(count * 2 + i));
            return sum;
        });
        std::vector<unsigned> holes;
        for (unsigned i = 0; i != count * 2; ++i) if (!reference.contains(i)) holes.push_back(i);
        Case<ce::engine_ordered_map<unsigned, unsigned>, std::map<unsigned, unsigned>>("interleaved_miss", count, reverse, [&]<class Set> {
            const auto& values = [&]() -> const Set& { if constexpr (std::is_same_v<Set, std::remove_cv_t<decltype(custom)>>) return custom; else return reference; }();
            size_t sum = 0;
            for (const auto value : holes) sum += values.contains(value);
            return sum;
        });
    }
    Case<ce::engine_ordered_multimap<unsigned, unsigned>, std::multimap<unsigned, unsigned>>("multi_insert_clear", 4096, reverse, []<class Set> {
        Set values;
        for (unsigned i = 0; i != 4096; ++i) values.emplace(i % 32, i);
        return values.size();
    });
    ce::engine_ordered_multimap<unsigned, unsigned> custom;
    std::multimap<unsigned, unsigned> reference;
    for (unsigned i = 0; i != 4096; ++i) { custom.emplace(i % 32, i); reference.emplace(i % 32, i); }
    Case<ce::engine_ordered_multimap<unsigned, unsigned>, std::multimap<unsigned, unsigned>>("multi_equal_range", 4096, reverse, [&]<class Set> {
        const auto& values = [&]() -> const Set& { if constexpr (std::is_same_v<Set, decltype(custom)>) return custom; else return reference; }();
        size_t sum = 0;
        for (unsigned i = 0; i != 32; ++i)
        {
            auto range = values.equal_range(i);
            sum += static_cast<size_t>(std::distance(range.first, range.second));
        }
        return sum;
    });
    }
    if (!coreOnly)
    {
    ce::engine_ordered_map<unsigned, unsigned> transferCustom = Filled<ce::engine_ordered_map<unsigned, unsigned>>(4096);
    ce::engine_ordered_map<unsigned, unsigned> targetCustom;
    std::map<unsigned, unsigned> transferReference = Filled<std::map<unsigned, unsigned>>(4096);
    std::map<unsigned, unsigned> targetReference;
    Case<ce::engine_ordered_map<unsigned, unsigned>, std::map<unsigned, unsigned>>("node_transfer_roundtrip", 4096, reverse, [&]<class Map> {
        auto& source = [&]() -> Map& { if constexpr (std::is_same_v<Map, decltype(transferCustom)>) return transferCustom; else return transferReference; }();
        auto& target = [&]() -> Map& { if constexpr (std::is_same_v<Map, decltype(targetCustom)>) return targetCustom; else return targetReference; }();
        size_t sum = 0;
        for (unsigned i = 0; i != 128; ++i)
        {
            auto node = source.extract(source.begin());
            sum += node.mapped();
            target.insert(std::move(node));
            source.insert(target.extract(target.begin()));
        }
        return sum;
    });
    const unsigned interiorKey = std::next(transferReference.begin(), 2048)->first;
    Case<ce::engine_ordered_map<unsigned, unsigned>, std::map<unsigned, unsigned>>("node_transfer_interior", 4096, reverse, [&]<class Map> {
        auto& source = [&]() -> Map& { if constexpr (std::is_same_v<Map, decltype(transferCustom)>) return transferCustom; else return transferReference; }();
        auto& target = [&]() -> Map& { if constexpr (std::is_same_v<Map, decltype(targetCustom)>) return targetCustom; else return targetReference; }();
        size_t sum = 0;
        for (unsigned i = 0; i != 128; ++i)
        {
            auto node = source.extract(source.find(interiorKey));
            sum += node.mapped();
            target.insert(std::move(node));
            source.insert(target.extract(target.begin()));
        }
        return sum;
    });
    }
    return 0;
}
