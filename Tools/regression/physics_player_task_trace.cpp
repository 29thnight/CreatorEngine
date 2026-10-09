#include "ProfileCaptureFile.h"
#include <iomanip>
#include <iostream>
#include <string_view>
#include <algorithm>

int main(int argc, char** argv)
{
    if (argc != 2) return 2;
    auto recording = ce::open_capture_recording(argv[1]);
    if (!recording || !(*recording)->complete()) return 3;
    std::cout << std::setprecision(17);
    for (std::uint64_t i = 0; i < (*recording)->frame_count(); ++i)
    {
        auto range = (*recording)->load_range(i, 1);
        if (!range) return 4;
        const auto& capture = **range;
        for (const auto& frame : capture.frames())
            for (const auto& e : frame.events)
            {
                const auto name = capture.marker(e.marker).name;
                if (!name.starts_with("Physics.") || !e.cpu.session || !e.cpu.tick) continue;
                if (ce::has_flag(e.flags, ce::event_flags::truncated_begin) ||
                    ce::has_flag(e.flags, ce::event_flags::truncated_end)) continue;
                const auto thread = std::ranges::find(capture.threads(), e.thread_slot, &ce::thread_info::slot);
                if (thread == capture.threads().end()) return 5;
                std::cout << "{\"name\":" << std::quoted(std::string(name))
                          << ",\"session\":" << e.cpu.session << ",\"tick\":" << e.cpu.tick
                          << ",\"task\":" << e.cpu.task << ",\"frame\":" << e.frame
                          << ",\"osThreadId\":" << thread->os_thread_id << ",\"thread\":" << e.thread_slot << ",\"depth\":" << e.depth
                          << ",\"beginUs\":" << capture.milliseconds(e.tick_begin) * 1000
                          << ",\"endUs\":" << capture.milliseconds(e.tick_end) * 1000
                          << ",\"durationUs\":" << capture.milliseconds(e.tick_end - e.tick_begin) * 1000
                          << "}\n";
            }
    }
}