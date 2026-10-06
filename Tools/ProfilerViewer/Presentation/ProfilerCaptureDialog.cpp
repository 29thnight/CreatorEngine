#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <commdlg.h>
#include <objbase.h>

#include "ImGui.h"
#include "ProfilerView.h"

#include <array>
#include <string>

namespace editor::profiler_view
{
    namespace
    {
        std::filesystem::path pick_file(bool save, const wchar_t* filter,
                                       const wchar_t* title, const wchar_t* extension)
        {
            std::array<wchar_t, 32768> filename{};
            OPENFILENAMEW options{};
            options.lStructSize = sizeof(options);
            options.hwndOwner = static_cast<HWND>(ImGui::GetMainViewport()->PlatformHandleRaw);
            options.lpstrFilter = filter;
            options.lpstrFile = filename.data();
            options.nMaxFile = static_cast<DWORD>(filename.size());
            options.lpstrTitle = title;
            options.lpstrDefExt = extension;
            options.Flags = OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR |
                (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
            if (save ? GetSaveFileNameW(&options) : GetOpenFileNameW(&options))
            {
                return std::filesystem::path(filename.data());
            }
            return {};
        }
    }

    std::filesystem::path pick_dx_capture_to_open()
    {
        return pick_file(false, L"Creator DX12 Deep Capture (*.cedx)\0*.cedx\0\0",
                         L"Open DX12 Deep Capture", L"cedx");
    }

    std::filesystem::path pick_dx_capture_to_save()
    {
        return pick_file(true, L"Creator DX12 Deep Capture (*.cedx)\0*.cedx\0\0",
                         L"Record DX12 Deep Capture", L"cedx");
    }

    std::filesystem::path pick_capture_to_open()
    {
        return pick_file(false, L"Creator Profiler Capture (*.ceprof)\0*.ceprof\0\0",
                         L"Open Profiler Capture", L"ceprof");
    }

    std::filesystem::path pick_capture_to_save()
    {
        return pick_file(true, L"Creator Profiler Capture (*.ceprof)\0*.ceprof\0\0",
                         L"Save Profiler Capture", L"ceprof");
    }

    bool copy_dx_capture_file(const std::filesystem::path& source,
                              const std::filesystem::path& destination,
                              std::string& error)
    {
        std::error_code ec;
        if (std::filesystem::equivalent(source, destination, ec))
        {
            error = "The source capture and destination must be different files";
            return false;
        }
        GUID id{};
        wchar_t suffix[40]{};
        if (FAILED(CoCreateGuid(&id)) || StringFromGUID2(id, suffix, 40) == 0)
        {
            error = "Could not create a unique export file";
            return false;
        }
        std::filesystem::path temporary = destination;
        temporary += std::wstring(L".") + suffix + L".tmp";
        if (!CopyFileW(source.c_str(), temporary.c_str(), TRUE))
        {
            error = "Capture copy failed (Windows error " + std::to_string(GetLastError()) + ")";
            DeleteFileW(temporary.c_str());
            return false;
        }
        if (!MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            error = "Capture export could not replace destination (Windows error " +
                std::to_string(GetLastError()) + ")";
            DeleteFileW(temporary.c_str());
            return false;
        }
        return true;
    }
}
