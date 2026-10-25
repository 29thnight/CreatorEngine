export module Creator.Core.Stopwatch;

import std;

export namespace ce
{
    class stopwatch final
    {
      public:
        stopwatch() : start_(clock_type::now()) {}

        double elapsed_milliseconds() const
        {
            const std::chrono::duration<double, std::milli> elapsed = clock_type::now() - start_;
            return elapsed.count();
        }

      private:
        using clock_type = std::chrono::high_resolution_clock;
        clock_type::time_point start_;
    };
}
