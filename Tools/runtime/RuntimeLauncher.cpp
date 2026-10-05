#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <shellapi.h>
#include <cstdio>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <string>
#include "HostAbi.h"
#include "CrashReporter.h"
#include "../../Engine/Utility_Framework/EngineVersion.h"

namespace
{
    int Fail(const wchar_t* message, DWORD error = GetLastError())
    {
        std::fwprintf(stderr, L"[CreatorEngine loader] %ls (error=%lu)\n", message, error);
        OutputDebugStringW(message);
        return error ? static_cast<int>(error) : 126;
    }

    // 모듈 경로가 MAX_PATH 아래여도 로더가 그 폴더에서 의존 DLL 경로를 만들며 한계를
    // 넘긴다(모듈 경로 255자에서 ERROR_FILENAME_EXCED_RANGE 실측). 길이와 무관하게
    // 확장 경로 접두를 붙인다. 경로는 GetModuleFileNameW 에서 온 절대 경로다.
    std::wstring ExtendedPath(const std::filesystem::path& path)
    {
        const std::wstring value = path.wstring();
        if (value.starts_with(L"\\\\?\\")) return value;
        return value.starts_with(L"\\\\") ? L"\\\\?\\UNC\\" + value.substr(2) : L"\\\\?\\" + value;
    }

    int Run(int argc, wchar_t** argv, int show)
    {
        // Crash reporting must run without loading the possibly broken engine DLL.
        if (argc >= 2 && wcscmp(argv[1], L"--crash-reporter") == 0)
            return RunCrashReporter(argc, argv);
        wchar_t executable[32768]{};
        const DWORD length = GetModuleFileNameW(nullptr, executable, 32768);
        if (!length || length >= 32768) return Fail(L"Cannot locate executable");
        const std::filesystem::path exe(executable);
        const auto directory = exe.parent_path();
        auto root = directory;
        bool found = false;
        for (int depth = 0; depth <= 2; ++depth)
        {
            if (std::filesystem::is_regular_file(root / L"Runtime" / L"layout.version"))
            { found = true; break; }
            root = root.parent_path();
        }
        if (!found) return Fail(L"Runtime/layout.version is missing", ERROR_PATH_NOT_FOUND);
        std::ifstream layout(root / L"Runtime" / L"layout.version");
        std::string layoutVersion;
        std::getline(layout, layoutVersion);
        if (layoutVersion != "1") return Fail(L"Unsupported runtime layout", ERROR_REVISION_MISMATCH);
        if (!SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32 | LOAD_LIBRARY_SEARCH_USER_DIRS))
            return Fail(L"Cannot set DLL search policy");
        const auto common = root / L"Runtime" / L"Common";
        if (!AddDllDirectory(ExtendedPath(common).c_str())) return Fail(L"Cannot add shared runtime directory");
        // Only the editor can resolve editor-only dependencies.
        if (exe.stem() == L"CreatorEditor")
        {
            const auto editor = root / L"Runtime" / L"Editor";
            if (!AddDllDirectory(ExtendedPath(editor).c_str())) return Fail(L"Cannot add editor runtime directory");
        }
        const auto hostPath = directory / (exe.stem().wstring() + L".runtime.dll");
        const std::wstring loadPath = ExtendedPath(hostPath);
        HMODULE host = LoadLibraryExW(loadPath.c_str(), nullptr,
            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_USER_DIRS | LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!host) return Fail(hostPath.c_str());
        // Process lifetime ownership: CoreCLR callbacks and native statics outlive Run.
        // The OS tears down this module at process exit; never hot-unload the engine.
        auto getInfo = reinterpret_cast<CreatorHostGetInfo>(GetProcAddress(host, "CreatorHostGetInfoV1"));
        auto run = reinterpret_cast<CreatorHostRun>(GetProcAddress(host, "CreatorHostRunV1"));
        if (!getInfo || !run) return Fail(L"Host ABI entry point is missing", ERROR_PROC_NOT_FOUND);
        const auto* info = getInfo();
        if (!info || info->size != sizeof(CreatorHostInfoV1) || info->hostAbi != 1 || info->pointerBits != 64)
            return Fail(L"Host ABI mismatch", ERROR_REVISION_MISMATCH);
        if (!info->productName || !info->featureRelease || !info->engineVersion ||
            strcmp(info->engineVersion, CreatorEngineVersion::Build) != 0 ||
            strcmp(info->productName, CreatorEngineVersion::ProductName) != 0 ||
            strcmp(info->featureRelease, CreatorEngineVersion::FeatureRelease) != 0 ||
            info->localDevelopment != static_cast<unsigned>(CreatorEngineVersion::LocalDevelopment))
            return Fail(L"Launcher and host engine versions differ", ERROR_REVISION_MISMATCH);
        if (argc == 2 && wcscmp(argv[1], L"--engine-info") == 0)
        {
            std::printf("{\"hostAbi\":%u,\"compiler\":%u,\"iteratorDebugLevel\":%u,"
                "\"debug\":%u,\"shipping\":%u,\"scriptApi\":%u,\"pointerBits\":%u,"
                "\"localDevelopment\":%s,\"productName\":\"%s\",\"featureRelease\":\"%s\",\"version\":\"%s\"}\n",
                info->hostAbi, info->compiler, info->iteratorDebugLevel, info->debug,
                info->shipping, info->scriptApi, info->pointerBits,
                info->localDevelopment ? "true" : "false", info->productName, info->featureRelease, info->engineVersion);
            return 0;
        }
        return run(argc, argv, show);
    }
}

int wmain(int argc, wchar_t** argv)
{
    try { return Run(argc, argv, SW_SHOWNORMAL); }
    catch (const std::exception& error) { std::fprintf(stderr, "[CreatorEngine loader] %s\n", error.what()); return 161; }
    catch (...) { return Fail(L"Invalid runtime layout", ERROR_BAD_PATHNAME); }
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int show)
{
    int argc{};
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return Fail(L"Cannot parse command line");
    int result{};
    try { result = Run(argc, argv, show); }
    catch (const std::exception& error) { std::fprintf(stderr, "[CreatorEngine loader] %s\n", error.what()); result = 161; }
    catch (...) { result = Fail(L"Invalid runtime layout", ERROR_BAD_PATHNAME); }
    LocalFree(argv);
    return result;
}
