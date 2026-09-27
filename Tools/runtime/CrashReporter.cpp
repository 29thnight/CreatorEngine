#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <DbgHelp.h>
#include <commctrl.h>
#include <commdlg.h>
#include <TlHelp32.h>
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>
#include "CrashReporter.h"
#include "../../Engine/Utility_Framework/CrashReportProtocol.h"
#include "../../Engine/Utility_Framework/EngineVersion.h"
#include "../../Engine/Utility_Framework/ScriptApiVersion.h"

namespace
{
    constexpr int kSaveInformation = 1001;
    constexpr DWORD kReportWaitMs = 15000;

    std::wstring WideFromUtf8(const char* value)
    {
        if (!value) return {};
        const int size = MultiByteToWideChar(CP_UTF8, 0, value, -1, nullptr, 0);
        if (size <= 1) return {};
        std::wstring result(static_cast<std::size_t>(size), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, value, -1, result.data(), size);
        result.pop_back();
        return result;
    }

    std::string Utf8FromWide(const std::wstring& value)
    {
        const int size = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1,
            nullptr, 0, nullptr, nullptr);
        if (size <= 1) return {};
        std::string result(static_cast<std::size_t>(size), '\0');
        WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1,
            result.data(), size, nullptr, nullptr);
        result.pop_back();
        return result;
    }

    std::wstring Hex(std::uint64_t value, int digits)
    {
        wchar_t buffer[32]{};
        swprintf_s(buffer, L"0x%0*llX", digits,
            static_cast<unsigned long long>(value));
        return buffer;
    }

    std::wstring ExceptionName(DWORD code)
    {
        switch (code)
        {
        case EXCEPTION_ACCESS_VIOLATION: return L"ACCESS_VIOLATION";
        case EXCEPTION_STACK_OVERFLOW: return L"STACK_OVERFLOW";
        case EXCEPTION_INT_DIVIDE_BY_ZERO: return L"INT_DIVIDE_BY_ZERO";
        case EXCEPTION_ILLEGAL_INSTRUCTION: return L"ILLEGAL_INSTRUCTION";
        case 0xE06D7363: return L"C++_EXCEPTION";
        default: return L"EXCEPTION";
        }
    }

    std::wstring CrashSignature(const crash_report::Request& request)
    {
        if (!request.hasException) return L"Abnormal process exit";
        const auto address = reinterpret_cast<std::uintptr_t>(
            request.exceptionRecord.ExceptionAddress);
        HANDLE snapshot = CreateToolhelp32Snapshot(
            TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, request.targetProcessId);
        if (snapshot != INVALID_HANDLE_VALUE)
        {
            MODULEENTRY32W module{};
            module.dwSize = sizeof(module);
            for (BOOL found = Module32FirstW(snapshot, &module); found;
                found = Module32NextW(snapshot, &module))
            {
                const auto base = reinterpret_cast<std::uintptr_t>(module.modBaseAddr);
                if (address >= base && address - base < module.modBaseSize)
                {
                    CloseHandle(snapshot);
                    return std::wstring(module.szModule) + L"+" +
                        Hex(address - base, 1);
                }
            }
            CloseHandle(snapshot);
        }
        return Hex(address, 16);
    }

    std::filesystem::path ReportPath(const crash_report::Request& request)
    {
        std::filesystem::path path(request.dumpPath);
        path.replace_extension(L".txt");
        return path;
    }

    void WriteFallbackReport(const crash_report::Request& request,
        const std::wstring& signature)
    {
        std::ofstream out(ReportPath(request), std::ios::binary | std::ios::trunc);
        if (!out) return;
        out << "Product: " << CreatorEngineVersion::ProductName
            << "\nEngineBuild: " << CreatorEngineVersion::Build
            << "\nScriptApi: " << CreatorScriptApiVersion
            << "\nDumpWriterProcessId: " << GetCurrentProcessId()
            << "\nTargetProcessId: " << request.targetProcessId
            << "\nFaultThreadId: " << request.faultThreadId
            << "\nCrashSignature: " << Utf8FromWide(signature)
            << "\nReason: " << request.reason << '\n';
        if (request.hasException)
            out << "ExceptionCode: 0x" << std::hex
                << request.exceptionRecord.ExceptionCode << '\n';
        out << "\nThe original process could not provide a symbolized stack."
            << " Open the adjacent .dmp for full diagnostics.\n";
    }

    bool WriteDump(crash_report::Request& request)
    {
        std::error_code directoryError;
        const std::filesystem::path path(request.dumpPath);
        std::filesystem::create_directories(path.parent_path(), directoryError);
        if (directoryError)
        {
            request.dumpError = static_cast<DWORD>(directoryError.value());
            return false;
        }
        HANDLE target = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ,
            FALSE, request.targetProcessId);
        if (!target)
        {
            request.dumpError = GetLastError();
            return false;
        }
        HANDLE output = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (output == INVALID_HANDLE_VALUE)
        {
            request.dumpError = GetLastError();
            CloseHandle(target);
            return false;
        }

        EXCEPTION_POINTERS pointers{};
        MINIDUMP_EXCEPTION_INFORMATION exceptionInfo{};
        if (request.hasException)
        {
            pointers.ExceptionRecord = &request.exceptionRecord;
            pointers.ContextRecord = &request.context;
            exceptionInfo.ThreadId = request.faultThreadId;
            exceptionInfo.ExceptionPointers = &pointers;
            // The exception record and context were copied into this process.
            exceptionInfo.ClientPointers = FALSE;
        }
        const MINIDUMP_TYPE kind = request.dumpKind == 2
            ? static_cast<MINIDUMP_TYPE>(MiniDumpWithFullMemory | MiniDumpIgnoreInaccessibleMemory)
            : static_cast<MINIDUMP_TYPE>(MiniDumpWithDataSegs | MiniDumpWithCodeSegs |
                MiniDumpWithIndirectlyReferencedMemory | MiniDumpScanMemory);
        const BOOL written = MiniDumpWriteDump(target, request.targetProcessId,
            output, kind, request.hasException ? &exceptionInfo : nullptr,
            nullptr, nullptr);
        request.dumpError = written ? ERROR_SUCCESS : GetLastError();
        CloseHandle(output);
        CloseHandle(target);
        if (!written) DeleteFileW(path.c_str());
        return written != FALSE;
    }

    struct State
    {
        crash_report::Request* request{};
        HWND dialog{};
        HANDLE worker{};
        std::atomic<bool> complete{ false };
        bool succeeded{};
        std::wstring signature;
        std::wstring headerText;
        std::wstring details;
        std::wstring content;
    };

    constexpr wchar_t kHeaderClass[] = L"CreatorEngineCrashHeader";

    LRESULT CALLBACK HeaderWindowProc(HWND window, UINT message,
        WPARAM wparam, LPARAM lparam)
    {
        if (message == WM_NCCREATE)
        {
            const auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
            SetWindowLongPtrW(window, GWLP_USERDATA,
                reinterpret_cast<LONG_PTR>(create->lpCreateParams));
            return TRUE;
        }
        if (message == WM_CREATE)
        {
            // TaskDialog can reorder its controls after TDN_CREATED and when
            // details change. Keep the header above them as layout settles.
            SetTimer(window, 1, 200, nullptr);
            return 0;
        }
        if (message == WM_TIMER && wparam == 1)
        {
            if (GetWindow(window, GW_HWNDPREV))
                SetWindowPos(window, HWND_TOP, 0, 0, 0, 0,
                    SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
            return 0;
        }
        if (message == WM_DESTROY)
        {
            KillTimer(window, 1);
            return 0;
        }
        if (message == WM_PAINT)
        {
            PAINTSTRUCT paint{};
            HDC dc = BeginPaint(window, &paint);
            RECT bounds{};
            GetClientRect(window, &bounds);
            const UINT dpi = GetDpiForWindow(window);
            auto scale = [dpi](int value) { return MulDiv(value, dpi, 96); };

            HBRUSH background = CreateSolidBrush(RGB(166, 48, 20));
            FillRect(dc, &bounds, background);
            DeleteObject(background);

            const int iconX = scale(11);
            const int iconY = (bounds.bottom - scale(28)) / 2;
            const int iconSize = scale(28);
            HBRUSH iconBrush = CreateSolidBrush(RGB(128, 34, 17));
            HGDIOBJ previousBrush = SelectObject(dc, iconBrush);
            HGDIOBJ previousPen = SelectObject(dc, GetStockObject(NULL_PEN));
            Ellipse(dc, iconX, iconY, iconX + iconSize, iconY + iconSize);
            SelectObject(dc, previousBrush);
            DeleteObject(iconBrush);

            HPEN cross = CreatePen(PS_SOLID, std::max(2, scale(3)), RGB(255, 255, 255));
            SelectObject(dc, cross);
            const int inner = scale(8);
            MoveToEx(dc, iconX + inner, iconY + inner, nullptr);
            LineTo(dc, iconX + iconSize - inner, iconY + iconSize - inner);
            MoveToEx(dc, iconX + iconSize - inner, iconY + inner, nullptr);
            LineTo(dc, iconX + inner, iconY + iconSize - inner);
            SelectObject(dc, previousPen);
            DeleteObject(cross);

            HFONT font = CreateFontW(-scale(15), 0, 0, 0, FW_SEMIBOLD,
                FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH,
                L"Segoe UI");
            HGDIOBJ previousFont = SelectObject(dc, font);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, RGB(255, 255, 255));
            RECT textBounds{ scale(46), 0, bounds.right - scale(12), bounds.bottom };
            const auto* state = reinterpret_cast<State*>(
                GetWindowLongPtrW(window, GWLP_USERDATA));
            if (state)
                DrawTextW(dc, state->headerText.c_str(), -1, &textBounds,
                    DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
            SelectObject(dc, previousFont);
            DeleteObject(font);
            EndPaint(window, &paint);
            return 0;
        }
        return DefWindowProcW(window, message, wparam, lparam);
    }

    void CreateErrorHeader(HWND dialog, State& state)
    {
        WNDCLASSW windowClass{};
        windowClass.lpfnWndProc = HeaderWindowProc;
        windowClass.hInstance = GetModuleHandleW(nullptr);
        windowClass.lpszClassName = kHeaderClass;
        if (!RegisterClassW(&windowClass) &&
            GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            return;

        RECT bounds{};
        GetClientRect(dialog, &bounds);
        const int height = MulDiv(42, GetDpiForWindow(dialog), 96);
        HWND header = CreateWindowExW(0, kHeaderClass, nullptr,
            WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 0, 0, bounds.right,
            height, dialog, nullptr, windowClass.hInstance, &state);
        if (header)
            SetWindowPos(header, HWND_TOP, 0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    }

    bool DumpAndWaitForReport(State& state)
    {
        auto& request = *state.request;
        const bool written = WriteDump(request);
        InterlockedExchange(&request.dumpSucceeded, written ? 1 : 0);
        SetEvent(request.dumpDoneEvent);
        if (!written) return false;

        // The crashed process preserves its existing symbolized .txt after the
        // external dump is safe. If it dies while doing so, keep a local summary.
        const DWORD reportWait = WaitForSingleObject(request.reportDoneEvent,
            kReportWaitMs);
        if (reportWait != WAIT_OBJECT_0 ||
            !std::filesystem::is_regular_file(ReportPath(request)))
            WriteFallbackReport(request, state.signature);
        return true;
    }

    DWORD WINAPI ReportWorker(void* parameter)
    {
        auto& state = *static_cast<State*>(parameter);
        state.succeeded = DumpAndWaitForReport(state);
        state.content = state.succeeded
            ? L"A crash report was saved on this computer. Restart the application."
            : L"The crash report could not be saved. Error: " +
                std::to_wstring(state.request->dumpError);
        state.complete.store(true, std::memory_order_release);
        if (state.dialog)
        {
            SendMessageW(state.dialog, TDM_SET_PROGRESS_BAR_MARQUEE, FALSE, 0);
            SendMessageW(state.dialog, TDM_SET_MARQUEE_PROGRESS_BAR, FALSE, 0);
            SendMessageW(state.dialog, TDM_SET_PROGRESS_BAR_STATE,
                state.succeeded ? PBST_NORMAL : PBST_ERROR, 0);
            SendMessageW(state.dialog, TDM_SET_PROGRESS_BAR_POS,
                state.succeeded ? 100 : 0, 0);
            SendMessageW(state.dialog, TDM_SET_ELEMENT_TEXT, TDE_CONTENT,
                reinterpret_cast<LPARAM>(state.content.c_str()));
            SendMessageW(state.dialog, TDM_ENABLE_BUTTON, kSaveInformation,
                state.succeeded ? TRUE : FALSE);
        }
        return state.succeeded ? 0 : 1;
    }

    void SaveInformation(State& state)
    {
        if (!state.succeeded) return;
        const std::filesystem::path source = ReportPath(*state.request);
        std::vector<wchar_t> destination(32768, L'\0');
        const std::wstring initial = source.filename().wstring();
        std::copy_n(initial.c_str(),
            std::min(initial.size(), destination.size() - 1), destination.data());
        OPENFILENAMEW dialog{};
        dialog.lStructSize = sizeof(dialog);
        dialog.hwndOwner = state.dialog;
        dialog.lpstrFilter = L"Text files (*.txt)\0*.txt\0All files (*.*)\0*.*\0\0";
        dialog.lpstrFile = destination.data();
        dialog.nMaxFile = static_cast<DWORD>(destination.size());
        dialog.lpstrDefExt = L"txt";
        dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
        if (!GetSaveFileNameW(&dialog)) return;
        if (CopyFileW(source.c_str(), destination.data(), FALSE))
            state.content = L"Crash information saved to " +
                std::wstring(destination.data());
        else
            state.content = L"Could not save the copy. Error: " +
                std::to_wstring(GetLastError());
        SendMessageW(state.dialog, TDM_SET_ELEMENT_TEXT, TDE_CONTENT,
            reinterpret_cast<LPARAM>(state.content.c_str()));
    }

    HRESULT CALLBACK DialogCallback(HWND hwnd, UINT notification,
        WPARAM button, LPARAM, LONG_PTR data)
    {
        auto& state = *reinterpret_cast<State*>(data);
        if (notification == TDN_CREATED)
        {
            state.dialog = hwnd;
            CreateErrorHeader(hwnd, state);
            SendMessageW(hwnd, TDM_ENABLE_BUTTON, kSaveInformation, FALSE);
            SendMessageW(hwnd, TDM_SET_MARQUEE_PROGRESS_BAR, TRUE, 0);
            SendMessageW(hwnd, TDM_SET_PROGRESS_BAR_MARQUEE, TRUE, 0);
            state.worker = CreateThread(nullptr, 0, &ReportWorker, &state, 0, nullptr);
            if (!state.worker) ReportWorker(&state);
        }
        else if (notification == TDN_BUTTON_CLICKED)
        {
            if (static_cast<int>(button) == kSaveInformation)
            {
                SaveInformation(state);
                return S_FALSE;
            }
            if (!state.complete.load(std::memory_order_acquire)) return S_FALSE;
        }
        return S_OK;
    }

    int ShowReport(State& state)
    {
        const auto& request = *state.request;
        state.headerText = L"CreatorEngine error: " +
            (request.hasException
                ? ExceptionName(request.exceptionRecord.ExceptionCode)
                : WideFromUtf8(request.reason));
        const std::wstring reportId =
            std::filesystem::path(request.dumpPath).stem().wstring();
        std::wostringstream details;
        details << L"Crash signature: " << state.signature
            << L"\nLocal report ID: " << reportId
            << L"\nEngine version: " << WideFromUtf8(CreatorEngineVersion::Build)
            << L"\nReason: " << WideFromUtf8(request.reason);
        if (request.hasException)
            details << L"\nException: " <<
                ExceptionName(request.exceptionRecord.ExceptionCode) << L" (" <<
                Hex(request.exceptionRecord.ExceptionCode, 8) << L")";
        details << L"\nDump: " << request.dumpPath
            << L"\nInformation: " << ReportPath(request).wstring();
        state.details = details.str();

        const TASKDIALOG_BUTTON buttons[]{
            { kSaveInformation, L"Save information\nSave a copy of the crash details for support." }
        };
        TASKDIALOGCONFIG config{};
        config.cbSize = sizeof(config);
        config.dwFlags = TDF_USE_COMMAND_LINKS | TDF_SHOW_PROGRESS_BAR |
            TDF_EXPAND_FOOTER_AREA | TDF_EXPANDED_BY_DEFAULT;
        config.dwCommonButtons = TDCBF_CLOSE_BUTTON;
        config.pszWindowTitle = L"CreatorEngine crash report";
        config.pszMainIcon = TD_ERROR_ICON;
        config.pszMainInstruction = L"CreatorEngine stopped unexpectedly";
        config.pszContent = L"Saving a local crash report...";
        config.cButtons = std::size(buttons);
        config.pButtons = buttons;
        config.pszExpandedInformation = state.details.c_str();
        config.pszCollapsedControlText = L"Show details";
        config.pszExpandedControlText = L"Hide details";
        config.pfCallback = &DialogCallback;
        config.lpCallbackData = reinterpret_cast<LONG_PTR>(&state);
        int selected{};
        const HRESULT result = TaskDialogIndirect(&config, &selected, nullptr, nullptr);
        if (state.worker)
        {
            WaitForSingleObject(state.worker, INFINITE);
            CloseHandle(state.worker);
        }
        if (FAILED(result) && !state.complete.load(std::memory_order_acquire))
            return DumpAndWaitForReport(state) ? 0 : 1;
        return state.succeeded ? 0 : 1;
    }
}

int RunCrashReporter(int argc, wchar_t** argv)
{
    if (argc != 3) return ERROR_INVALID_PARAMETER;
    wchar_t* end{};
    const auto handleValue = std::wcstoull(argv[2], &end, 16);
    if (!end || *end != L'\0' || !handleValue) return ERROR_INVALID_PARAMETER;
    HANDLE mapping = reinterpret_cast<HANDLE>(
        static_cast<std::uintptr_t>(handleValue));
    auto* request = static_cast<crash_report::Request*>(
        MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0,
            sizeof(crash_report::Request)));
    if (!request) return GetLastError();
    if (request->magic != crash_report::kMagic ||
        request->version != crash_report::kVersion ||
        request->size != sizeof(crash_report::Request) ||
        !request->dumpDoneEvent || !request->reportDoneEvent ||
        !request->dumpPath[0])
    {
        UnmapViewOfFile(request);
        return ERROR_REVISION_MISMATCH;
    }
    InterlockedExchange(&request->reporterStarted, 1);
    State state{};
    state.request = request;
    state.signature = CrashSignature(*request);
    const int result = request->unattended
        ? (DumpAndWaitForReport(state) ? 0 : 1)
        : ShowReport(state);
    UnmapViewOfFile(request);
    CloseHandle(mapping);
    return result;
}
