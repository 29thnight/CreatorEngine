import std;
import "Creator.Containers.h";
import "Creator.WinAPI.h";

namespace
{
    using word = std::uint64_t;
    volatile word sink = 0;
    constexpr size_t count = 16384;
    template<bool Custom>
    using pair_type = std::conditional_t<Custom, ce::pair<word, word>, std::pair<word, word>>;
    template<bool Custom>
    using tuple_type =
        std::conditional_t<Custom, ce::tuple<word, word, word, word>, std::tuple<word, word, word, word>>;

    template<bool Custom, size_t I, class T>
    constexpr decltype(auto) get(T&& value)
    {
        if constexpr (Custom)
            return ce::get<I>(static_cast<T&&>(value));
        else
            return std::get<I>(static_cast<T&&>(value));
    }

    struct fields
    {
        word a, b, c;

        word combine(word seed) const noexcept
        {
            return (a + seed) * 3 + b * 7 + c;
        }
    };

    template<bool Custom>
    struct data
    {
        std::vector<pair_type<Custom>> pairs{count};
        std::vector<tuple_type<Custom>> tuples{count};
        std::vector<fields> values{count};

        data()
        {
            for (size_t i = 0; i < count; ++i)
            {
                const word x = (i * 1664525u + 1013904223u) & 65535u;
                pairs[i] = pair_type<Custom>{x, i};
                tuples[i] = tuple_type<Custom>{x / 16, x / 4, x, i};
                values[i] = {x, x / 4, i};
            }
        }
    };

    template<bool Custom>
    __declspec(noinline) word kernel(data<Custom>& rows, int test, word seed)
    {
        word sum = 0;
        switch (test)
        {
        case 0:
            for (size_t i = 0; i < count; ++i)
            {
                rows.pairs[i] = pair_type<Custom>{i + seed, (i ^ seed) * 3};
                sum += get<Custom, 0>(rows.pairs[i]) ^ get<Custom, 1>(rows.pairs[i]);
            }
            break;
        case 1:
            for (size_t i = 0; i < count; ++i)
            {
                rows.tuples[i] = tuple_type<Custom>{i + seed, i * 3, i ^ seed, i >> 2};
                sum += get<Custom, 0>(rows.tuples[i]) + get<Custom, 2>(rows.tuples[i]);
            }
            break;
        case 2:
            for (const auto& row : rows.tuples)
            {
                auto operation = [](word a, word b, word c, word d) noexcept { return a * 3 + b * 5 + c * 7 + d; };
                if constexpr (Custom)
                    sum += ce::apply(operation, row);
                else
                    sum += std::apply(operation, row);
            }
            break;
        case 3:
            for (size_t i = 1; i < count; ++i)
                sum += rows.tuples[i - 1] < rows.tuples[i];
            break;
        case 4:
            for (size_t i = 1; i < count; ++i)
            {
                const auto& a = rows.values[i - 1];
                const auto& b = rows.values[i];
                if constexpr (Custom)
                    sum += ce::tie(a.a, a.b, a.c) < ce::tie(b.a, b.b, b.c);
                else
                    sum += std::tie(a.a, a.b, a.c) < std::tie(b.a, b.b, b.c);
            }
            break;
        case 5:
            for (const auto& row : rows.values)
            {
                if constexpr (Custom)
                    sum += ce::apply(&fields::combine, ce::tie(row, seed));
                else
                    sum += std::apply(&fields::combine, std::tie(row, seed));
            }
            break;
        case 6:
            for (size_t i = 0; i < count; ++i)
            {
                const word x = ((i ^ seed) * 1664525u + 1013904223u) & 65535u;
                rows.tuples[i] = tuple_type<Custom>{x / 16, x / 4, x, i};
            }
            std::sort(rows.tuples.begin(), rows.tuples.end());
            for (const auto& row : rows.tuples)
                sum = sum * 3 + get<Custom, 3>(row);
            break;
        }
        return sum;
    }

    struct observation
    {
        double ns;
        word checksum;
        size_t batches;
        word cpu_ns;
        word cycles;
    };

    word thread_cpu_ns()
    {
        FILETIME created{}, exited{}, kernel{}, user{};
        if (!GetThreadTimes(GetCurrentThread(), &created, &exited, &kernel, &user))
            throw std::runtime_error("cannot query own thread time");
        const auto ticks = [](FILETIME value) { return (word(value.dwHighDateTime) << 32) | value.dwLowDateTime; };
        return (ticks(kernel) + ticks(user)) * 100;
    }

    word thread_cycles()
    {
        ULONG64 cycles{};
        if (!QueryThreadCycleTime(GetCurrentThread(), &cycles))
            throw std::runtime_error("cannot query own thread cycles");
        return cycles;
    }

    template<bool Custom>
    observation measure(data<Custom>& rows, int test, word seed, std::int64_t target_ns = 100000000)
    {
        const auto cpu_start = thread_cpu_ns();
        const auto cycle_start = thread_cycles();
        const auto start = std::chrono::steady_clock::now();
        size_t batches = 0;
        word checksum = 0;
        std::chrono::nanoseconds elapsed{};
        do
        {
            checksum = kernel(rows, test, seed);
            sink = checksum;
            ++batches;
            elapsed = std::chrono::steady_clock::now() - start;
        } while (elapsed.count() < target_ns);
        return {double(elapsed.count()) / double(batches * count), checksum, batches,
                thread_cpu_ns() - cpu_start, thread_cycles() - cycle_start};
    }
} // namespace

int main(int argc, char** argv)
try
{
    if (argc != 3)
        throw std::runtime_error("output CSV and reverse flag required");
    if (!SetThreadAffinityMask(GetCurrentThread(), DWORD_PTR{1} << 4))
        throw std::runtime_error("cannot pin own benchmark thread to CPU4");
    if (!SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL))
        throw std::runtime_error("cannot set own benchmark thread priority");
    std::cout << "VALUE_BENCH_ENV compiler=" << _MSC_FULL_VER << " stl=" << _MSVC_STL_VERSION
              << " records=" << count << " cpu=4 priority=above_normal batchNs=100000000 warmupNs=1000000000\n";
    std::ofstream output(argv[1]);
    if (!output)
        throw std::runtime_error("cannot open CSV");
    const bool reverse = std::string_view(argv[2]) == "1";
    output << "case,sample,custom_ns,stl_ns,custom_batches,stl_batches,checksum,custom_cpu_ns,stl_cpu_ns,custom_cycles,stl_cycles\n" << std::setprecision(12);
    const char* names[] = {"pair_write_read", "tuple_write_read", "tuple_apply", "tuple_compare",
                           "tie_compare",     "member_apply",     "tuple_sort"};
    data<true> custom;
    data<false> standard;
    for (int index = 0; index < 7; ++index)
    {
        const int test = reverse ? 6 - index : index;
        // Equal warmup and correctness guard. Allocation is outside timing.
        for (unsigned i = 0; i < 3; ++i)
            if (kernel(custom, test, 71) != kernel(standard, test, 71))
                throw std::runtime_error("kernel mismatch");
        if (measure(custom, test, 71, 1000000000).checksum != measure(standard, test, 71, 1000000000).checksum)
            throw std::runtime_error("warmup result mismatch");
        for (unsigned sample = 0; sample < 15; ++sample)
        {
            observation a{}, b{};
            const word seed = 71 + sample;
            if (bool(sample % 2) != reverse)
            {
                a = measure(custom, test, seed);
                b = measure(standard, test, seed);
            }
            else
            {
                b = measure(standard, test, seed);
                a = measure(custom, test, seed);
            }
            if (a.checksum != b.checksum)
                throw std::runtime_error("timed result mismatch");
            output << names[test] << ',' << sample << ',' << a.ns << ',' << b.ns << ',' << a.batches << ',' << b.batches
                   << ',' << a.checksum << ',' << a.cpu_ns << ',' << b.cpu_ns << ',' << a.cycles << ',' << b.cycles << '\n';
        }
        std::cout << "VALUE_BENCH_CASE_OK " << names[test] << '\n';
    }
    std::cout << "VALUE_BENCH_OK cases=7 samples=15 reverse=" << reverse << '\n';
    return 0;
}
catch (const std::exception& error)
{
    std::cerr << "VALUE_BENCH_FAIL " << error.what() << '\n';
    return 1;
}
