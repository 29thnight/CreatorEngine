// UNEXECUTED source fixtures. Intentionally not part of the GUI executable and
// not exposed by a hidden/alternate command-line mode. A future native test
// harness can call RunViewerArgumentsFixtures on Windows without starting UI.
#include "../ViewerArguments.h"

#include <array>
#include <stdexcept>

namespace ce::profiler_viewer::tests
{
    void RunViewerArgumentsFixtures()
    {
        const auto expect = [](bool condition)
        {
            if (!condition)
            {
                throw std::runtime_error("ProfilerViewer argument fixture failed");
            }
        };
        viewer_arguments result;
        expect(parse_arguments({}, result) && !result.live && result.open_path.empty());
        constexpr std::array<std::wstring_view, 8> live{
            L"--parent", L"123", L"--created", L"18446744073709551615",
            L"--session", L"1", L"--nonce", L"0123456789abcdef0123456789abcdef"
        };
        expect(parse_arguments(live, result) && result.live && result.connection.target_pid == 123);
        constexpr std::array<std::wstring_view, 10> scaled{
            L"--parent", L"123", L"--created", L"123456", L"--session", L"1",
            L"--nonce", L"0123456789abcdef0123456789abcdef", L"--ui-scale-milli", L"1250"
        };
        expect(parse_arguments(scaled, result) && result.live && result.ui_scale_milli == 1250);
        auto bad_scale = scaled;
        bad_scale[9] = L"3001";
        expect(!parse_arguments(bad_scale, result));
        bad_scale[9] = L"499";
        expect(!parse_arguments(bad_scale, result));
        bad_scale[9] = L"1.25";
        expect(!parse_arguments(bad_scale, result));
        auto invalid = live;
        invalid[0] = L"--unknown";
        expect(!parse_arguments(invalid, result));
        invalid = live;
        invalid[0] = L"--session";
        expect(!parse_arguments(invalid, result));
        invalid = live;
        invalid[1] = L"4294967296";
        expect(!parse_arguments(invalid, result));
        invalid = live;
        invalid[1] = L"+123";
        expect(!parse_arguments(invalid, result));
        invalid = live;
        invalid[1] = L"0";
        expect(!parse_arguments(invalid, result));
        invalid = live;
        invalid[3] = L"18446744073709551616";
        expect(!parse_arguments(invalid, result));
        invalid = live;
        invalid[7] = L"00000000000000000000000000000000";
        expect(!parse_arguments(invalid, result));
        invalid = live;
        invalid[7] = L"0123456789ABCDEF0123456789ABCDEF";
        expect(!parse_arguments(invalid, result));
        expect(!parse_arguments(std::span(live).first(6), result));
        constexpr std::array<std::wstring_view, 2> open{ L"--open", L"C:\\captures\\sample.ceprof" };
        expect(parse_arguments(open, result) && !result.live && !result.open_path.empty());
        auto invalid_open = open;
        invalid_open[1] = L"C:\\captures\\sample.ceprof:payload";
        expect(!parse_arguments(invalid_open, result));
        invalid_open[1] = L"\\\\.\\GLOBALROOT\\Device\\sample.ceprof";
        expect(!parse_arguments(invalid_open, result));
        invalid_open[1] = L"C:\\captures\\program.exe";
        expect(!parse_arguments(invalid_open, result));
        constexpr std::array<std::wstring_view, 1> implicit_path{ L"C:\\captures\\sample.ceprof" };
        expect(!parse_arguments(implicit_path, result));
    }
}
