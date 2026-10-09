#include "../../Engine/EngineDiagnostics/ProfileCaptureFile.h"
#include <iostream>
#include <string_view>

int main(int argc, char** argv)
{
    if (argc != 2) return 2;
    const auto loaded = ce::load_capture(argv[1]);
    if (!loaded) return 3;
    const auto& capture = **loaded;

    unsigned collected = 0, published = 0;
    for (const auto& frame : capture.frames())
    {
        if (frame.dropped_events) return 5;
        for (const auto& event : frame.events)
        {
            const auto name = capture.marker(event.marker).name;
            if (name != "Physics.ContactCollect" && name != "Physics.ContactPublish") continue;
            if (!event.cpu.session || !event.cpu.tick || event.cpu.task) return 6;

            if (name == "Physics.ContactCollect") ++collected;
            else ++published;
        }
    }

    if (!collected || !published) return 7;
    std::cout << "{\"result\":\"CONTACT_MARKERS_OBSERVED\",\"collect\":" << collected
              << ",\"publish\":" << published << ",\"frames\":" << capture.frame_count()
              << ",\"complete\":" << (capture.complete() ? "true" : "false")
              << ",\"unacked\":" << capture.unacked_streams()
              << ",\"droppedCounters\":" << capture.dropped_counters() << "}\n";

    if (!capture.complete() || capture.unacked_streams() || capture.dropped_counters()) return 4;
}
