#pragma once
#include "Core.Definition.h"
#include "EngineSetting.h"
#include "PathFinder.h"
#include "LogSystem.h"
#include <DbgHelp.h>

#pragma comment(lib, "dbghelp.lib")

enum DUMP_TYPE
{
    DUNP_TYPE_MINI = MiniDumpWithDataSegs | MiniDumpWithCodeSegs | MiniDumpWithIndirectlyReferencedMemory | MiniDumpScanMemory,
    DUMP_TYPE_FULL = MiniDumpWithFullMemory | MiniDumpIgnoreInaccessibleMemory
};

inline void CreateDump(EXCEPTION_POINTERS* pExceptionPointers, DUMP_TYPE dumpType, HWND handle)
{
    wchar_t moduleFileName[MAX_PATH] = { 0, };
    file::path fileName(PathFinder::DumpPath());

    if (GetModuleFileNameW(NULL, moduleFileName, MAX_PATH) == 0)
    {
        Debug->LogError("Failed to get module file name for dump creation.");
        return;
	}

    fileName /= file::path(moduleFileName).filename().replace_extension(L".dmp");

    HANDLE hFile = CreateFile(fileName.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return;
    MINIDUMP_EXCEPTION_INFORMATION dumpInfo{};
    dumpInfo.ThreadId = GetCurrentThreadId();
    dumpInfo.ExceptionPointers = pExceptionPointers;
    dumpInfo.ClientPointers = TRUE;
    MINIDUMP_TYPE miniDumpType = MiniDumpNormal;

    if (dumpType == DUNP_TYPE_MINI)
    {
        miniDumpType = MINIDUMP_TYPE(
            MiniDumpWithDataSegs |
            MiniDumpWithCodeSegs |
            MiniDumpWithIndirectlyReferencedMemory |
            MiniDumpScanMemory
        );
    }
    else if (dumpType == DUMP_TYPE_FULL)
    {
        miniDumpType = MINIDUMP_TYPE(MiniDumpWithFullMemory | MiniDumpIgnoreInaccessibleMemory);
    }

    if (!MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hFile, miniDumpType, &dumpInfo, NULL, NULL))
    {
        DWORD err = GetLastError();
    }


    CloseHandle(hFile);

    std::wstring adsName = fileName.wstring() + L":GitHash";
    std::ofstream ads(adsName, std::ios::binary);
    if (ads)
    {
        ads << EngineSettingInstance->GetGitVersionHash(); // 또는 g_EngineGitHash
        ads.close();
    }

    Debug->Finalize();

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    PostMessage(handle, WM_CLOSE, 0, 0);
}

inline std::wstring GetDumpGitHashADS(const std::wstring& dumpFilePath)
{
    std::wstring adsPath = dumpFilePath + L":GitHash";
    std::ifstream ads(adsPath, std::ios::binary);

    if (!ads.is_open())
        return L"";

    std::string hashData((std::istreambuf_iterator<char>(ads)), std::istreambuf_iterator<char>());
    ads.close();

    // UTF-8 to wide string 변환 (필요 시)
    int wlen = MultiByteToWideChar(CP_UTF8, 0, hashData.c_str(), -1, nullptr, 0);
    std::wstring result(wlen, 0);
    MultiByteToWideChar(CP_UTF8, 0, hashData.c_str(), -1, &result[0], wlen);
    return result;
}
