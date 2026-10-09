#pragma once

#include <cstdint>
#include <memory>

namespace ce::dx_capture
{
    class transport;

    class timing_collector
    {
    public:
        timing_collector();
        ~timing_collector();
        timing_collector(const timing_collector&) = delete;
        timing_collector& operator=(const timing_collector&) = delete;

        // 인증된 start 뒤에만 호출한다. 이 호출의 제어 스레드가 transport를 독점한다.
        std::uint32_t run(transport& channel);

    private:
        struct state;
        std::unique_ptr<state> state_;
    };
}
