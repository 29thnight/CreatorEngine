#include "ProfileCaptureFile.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <iomanip>
#include <map>
#include <numeric>
#include <ranges>
#include <vector>
#include <tuple>

static void distribution(const std::vector<double>& input)
{
    auto values = input;
    std::ranges::sort(values);
    std::cout << "{\"samples\":" << values.size()
              << ",\"meanUs\":" << std::accumulate(values.begin(), values.end(), 0.0) / values.size()
              << ",\"p99Us\":" << values[static_cast<std::size_t>(std::ceil(values.size() * .99)) - 1]
              << ",\"maxUs\":" << values.back() << ",\"rawUs\":[";
    for (std::size_t i = 0; i < input.size(); ++i)
        std::cout << (i ? "," : "") << input[i];
    std::cout << "]}";
}

int main(int argc, char** argv)
{
    if (argc != 2) return 2;
    const auto recording = ce::open_capture_recording(argv[1]);
    if (!recording) { std::cerr << ce::describe(recording.error()); return 3; }
    if (!(*recording)->complete() || !(*recording)->finalized()) return 4;
    std::map<std::string, std::vector<double>> spans;
    std::vector<double> frames;
    using submission_key = std::tuple<std::uint32_t, std::uint32_t, std::uint16_t, std::uint8_t>;
    std::map<submission_key, std::vector<std::pair<double, double>>> gpuIntervals;
    std::map<std::string, double> closure;
    std::uint64_t gpuSpans = 0, clipped = 0;
    for (std::uint64_t i = 0; i < (*recording)->frame_count(); ++i)
    {
        const auto range = (*recording)->load_range(i, 1);
        if (!range) { std::cerr << ce::describe(range.error()); return 5; }
        const auto& capture = **range;
        for (const auto& frame : capture.frames())
        {
            if (frame.dropped_events) return 6;
            if (frame.tick_end > frame.tick_begin)
                frames.push_back(capture.milliseconds(frame.tick_end - frame.tick_begin) * 1000);
            for (const auto& counter : frame.counters)
            {
                const auto* desc = ce::find_counter(capture.counter_descriptors(), counter.id);
                if (desc && desc->name.starts_with("Capture.")) closure[desc->name] = counter.value;
            }
            for (const auto& event : frame.events)
            {
                if (ce::has_flag(event.flags, ce::event_flags::instant)) continue;
                if (ce::has_flag(event.flags, ce::event_flags::truncated_begin) ||
                    ce::has_flag(event.flags, ce::event_flags::truncated_end)) { ++clipped; continue; }
                if (event.tick_end < event.tick_begin) return 7;
                const bool gpu = ce::has_flag(event.flags, ce::event_flags::gpu_span);
                gpuSpans += gpu;
                if (gpu)
                    gpuIntervals[{event.frame, event.submission, event.view, event.queue}].emplace_back(
                        capture.milliseconds(event.tick_begin) * 1000, capture.milliseconds(event.tick_end) * 1000);
                const auto key = std::string(gpu ? "GPU:" : "CPU:") + std::string(capture.marker(event.marker).name);
                spans[key].push_back(capture.milliseconds(event.tick_end - event.tick_begin) * 1000);
            }
        }
    }
    if (!gpuSpans || frames.empty()) return 8;
    std::vector<double> gpuEnvelope, gpuUnion;
    for (auto& [key, intervals] : gpuIntervals)
    {
        std::ranges::sort(intervals);
        const double begin = intervals.front().first;
        double left = begin, right = intervals.front().second, covered = 0;
        for (const auto& [nextBegin, nextEnd] : intervals | std::views::drop(1))
        {
            if (nextBegin > right) { covered += right - left; left = nextBegin; }
            right = std::max(right, nextEnd);
        }
        covered += right - left;
        gpuEnvelope.push_back(right - begin);
        gpuUnion.push_back(covered);
    }
    std::cout << std::setprecision(10) << "{\"complete\":true,\"frames\":" << (*recording)->frame_count()
              << ",\"gpuSpans\":" << gpuSpans << ",\"clippedSpansExcluded\":" << clipped
              << ",\"frameBoundaryDuration\":";
    distribution(frames);
    std::cout << ",\"gpuSubmissionInstrumentedEnvelope\":";
    distribution(gpuEnvelope);
    std::cout << ",\"gpuSubmissionInstrumentedUnion\":";
    distribution(gpuUnion);
    std::cout << ",\"inclusiveMarkerDurations\":{";
    bool first = true;
    for (const auto& [name, values] : spans)
    {
        if (!first) std::cout << ',';
        first = false;
        // Marker names are engine-owned; std::quoted escapes JSON quote/backslash characters.
        std::cout << std::quoted(name) << ':';
        distribution(values);
    }
    std::cout << "},\"closureCounters\":{";
    first = true;
    for (const auto& [name, value] : closure)
    {
        if (!first) std::cout << ',';
        first = false;
        std::cout << std::quoted(name) << ':' << value;
    }
    std::cout << "}}\n";
}