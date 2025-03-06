#pragma once
#include "Core.Definition.h"
#include <DbgHelp.h>

#pragma comment(lib, "dbghelp.lib")

enum DUMP_TYPE
{
    DUNP_TYPE_MINI = MiniDumpWithDataSegs | MiniDumpWithCodeSegs | MiniDumpWithIndirectlyReferencedMemory | MiniDumpScanMemory,
    DUMP_TYPE_FULL = MiniDumpWithFullMemory | MiniDumpIgnoreInaccessibleMemory
};

inline void CreateDump(EXCEPTION_POINTERS* pExceptionPointers, DUMP_TYPE dumpType)
{
    wchar_t moduleFileName[MAX_PATH] = { 0, };
    std::wstring fileName(moduleFileName);
    if (GetModuleFileName(NULL, moduleFileName, MAX_PATH) == 0)
    {
        fileName = L"unknown_project.dmp";
    }
    else
    {
        fileName = std::wstring(moduleFileName);
        size_t pos = fileName.find_last_of(L"\\/");
        if (pos != std::wstring::npos)
        {
            fileName = fileName.substr(pos + 1);
        }
        pos = fileName.find_last_of(L'.');
        if (pos != std::wstring::npos)
        {
            fileName = fileName.substr(0, pos);
        }
        fileName += L".dmp";
    }
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
}

