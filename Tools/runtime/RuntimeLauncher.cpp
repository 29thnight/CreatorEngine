#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <shellapi.h>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include "HostAbi.h"
#include "../../Engine/Utility_Framework/EngineVersion.h"

namespace
{
    int Fail(const wchar_t* message, DWORD error = GetLastError())
    {
        std::fwprintf(stderr, L"[CreatorEngine loader] %ls (error=%lu)\n", message, error);
        OutputDebugStringW(message);
        return error ? static_cast<int>(error) : 126;
    }

    int Run(int argc, wchar_t** argv, int show)
    {
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
        if (!AddDllDirectory(common.c_str())) return Fail(L"Cannot add shared runtime directory");
        // Only the editor can resolve editor-only dependencies.
        if (exe.stem() == L"CreatorEditor")
        {
            const auto editor = root / L"Runtime" / L"Editor";
            if (!AddDllDirectory(editor.c_str())) return Fail(L"Cannot add editor runtime directory");
        }
        const auto hostPath = directory / (exe.stem().wstring() + L".runtime.dll");
        // The DLL loader still needs the extended-length prefix for long module
        // names even when ordinary file operations are longPathAware.
        auto loadPath = hostPath.wstring();
        if (loadPath.size() >= MAX_PATH && !loadPath.starts_with(L"\\\\?\\"))
            loadPath = loadPath.starts_with(L"\\\\") ? L"\\\\?\\UNC\\" + loadPath.substr(2) : L"\\\\?\\" + loadPath;
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
    catch (...) { return Fail(L"Invalid runtime layout", ERROR_BAD_PATHNAME); }
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int show)
{
    int argc{};
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return Fail(L"Cannot parse command line");
    int result{};
    try { result = Run(argc, argv, show); }
    catch (...) { result = Fail(L"Invalid runtime layout", ERROR_BAD_PATHNAME); }
    LocalFree(argv);
    return result;
}
