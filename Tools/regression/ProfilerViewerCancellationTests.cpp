// Source fixture only: NOT built or executed during this change.
#include "ProfileCaptureFile.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stop_token>
#include <string>
#include <vector>

namespace profiler_viewer_cancellation_tests
{
    void check(bool condition, const char* message)
    {
        if (!condition)
        {
            std::fprintf(stderr, "ProfilerViewerCancellationTests: %s\n", message);
            std::abort();
        }
    }

    void canceled_file_work_preserves_destination()
    {
        std::stop_source canceled;
        canceled.request_stop();
        const auto missing = ce::open_capture_recording("nonexistent.ceprof", canceled.get_token());
        check(!missing && missing.error() == ce::capture_file_error::canceled,
              "canceled open returns before touching the source");

        const auto root = std::filesystem::temp_directory_path() /
            ("ce-viewer-cancel-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        check(std::filesystem::create_directory(root), "fixture creates a private test directory");
        const auto source = root / "source.ceprof";
        const auto destination = root / "destination.ceprof";
        ce::frame_record frame;
        frame.engine_frame = 1;
        frame.tick_begin = 100;
        frame.tick_end = 200;
        std::vector<ce::frame_record> frames;
        frames.push_back(std::move(frame));
        const ce::capture_session capture(std::move(frames), {}, {}, {1000}, true, 0);
        check(ce::save_capture(capture, source).has_value(), "fixture writes a one-frame v2 capture");
        const auto opened = ce::open_capture_recording(source);
        check(opened.has_value(), "fixture is a readable capture");
        const auto range = (*opened)->load_range(0, 1, ce::kDefaultMemoryBudget, canceled.get_token());
        check(!range && range.error() == ce::capture_file_error::canceled,
              "canceled range does not publish a partial window");
        {
            std::ofstream output(destination, std::ios::binary);
            output << "existing destination must survive cancellation";
        }
        const auto saved = ce::save_recording(**opened, destination, canceled.get_token());
        check(!saved && saved.error() == ce::capture_file_error::canceled,
              "canceled export reports cancellation");
        std::ifstream input(destination, std::ios::binary);
        const std::string bytes{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        check(bytes == "existing destination must survive cancellation", "export never replaces destination after cancellation");
        input.close();
        check(std::distance(std::filesystem::directory_iterator(root), std::filesystem::directory_iterator{}) == 2,
              "canceled export removes its private staging directory");
        std::filesystem::remove_all(root);
    }
}

int main()
{
    profiler_viewer_cancellation_tests::canceled_file_work_preserves_destination();
}
