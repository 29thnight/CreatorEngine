import std;
import Creator.Core.Stopwatch;
import Creator.Core.RingBuffer;

namespace
{
    unsigned checks{};
    void require(bool condition, std::string_view name)
    {
        ++checks;
        if (!condition)
        {
            throw std::runtime_error(std::string(name));
        }
    }
}

int main()
{
    try
    {
        ce::ring_buffer<int> values(3);
        require(values.get_all().empty(), "initial snapshot");
        require(!values.is_clear(), "initial clear notification");
        values.push(1);
        values.push(2);
        require(values.get_all() == std::vector{1, 2}, "partial snapshot");
        values.push(3);
        require(values.get_all() == std::vector{1, 2, 3}, "full snapshot");
        values.push(4);
        values.push(5);
        require(values.get_all() == std::vector{3, 4, 5}, "overwrite and ordering");
        auto snapshot = values.get_all();
        snapshot[0] = 99;
        require(values.get_all() == std::vector{3, 4, 5}, "snapshot owns its storage");
        values.clear();
        require(values.get_all().empty() && values.is_clear(), "clear content and notification");
        values.push(6);
        require(values.get_all() == std::vector{6} && values.is_clear(), "push preserves notification");
        values.acknowledge_clear();
        require(!values.is_clear() && values.get_all() == std::vector{6}, "acknowledge preserves content");
        values.push(7);
        values.push(8);
        values.push(9);
        require(values.get_all() == std::vector{7, 8, 9}, "wrap after clear");
        ce::ring_buffer<std::string> one(1);
        one.push("first");
        one.push("last");
        require(one.get_all() == std::vector<std::string>{"last"}, "unit capacity nontrivial values");
        const auto copy = values;
        values.clear();
        require(copy.get_all() == std::vector{7, 8, 9}, "copy owns its storage");
        const ce::stopwatch timer;
        const auto first = timer.elapsed_milliseconds();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        const auto second = timer.elapsed_milliseconds();
        require(first >= 0 && second > first, "elapsed milliseconds advance");
        const auto copied_timer = timer;
        require(copied_timer.elapsed_milliseconds() >= second, "copy retains start time");
        std::cout << "TIMING_CONTAINER_MODULES_OK checks=" << checks << '\n';
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
