#pragma once

#include "CrashReportProtocol.h"
#include <filesystem>
#include <cwchar>
#include <cstring>
#include <iterator>
#include <vector>

namespace crash_report
{
    class Session
    {
    public:
        Session() = default;
        Session(const Session&) = delete;
        Session& operator=(const Session&) = delete;
        ~Session()
        {
            if (m_request) UnmapViewOfFile(m_request);
            if (m_mapping) CloseHandle(m_mapping);
            if (m_dumpDone) CloseHandle(m_dumpDone);
            if (m_reportDone) CloseHandle(m_reportDone);
            if (m_process) CloseHandle(m_process);
        }

        bool WriteDump(EXCEPTION_POINTERS* exception, const std::filesystem::path& dumpPath,
            const char* reason, DWORD dumpKind, bool unattended)
        {
            const std::wstring path = dumpPath.wstring();
            if (path.empty() || path.size() >= kPathCapacity) return false;

            SECURITY_ATTRIBUTES inherited{ sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE };
            m_dumpDone = CreateEventW(&inherited, TRUE, FALSE, nullptr);
            m_reportDone = CreateEventW(&inherited, TRUE, FALSE, nullptr);
            m_mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, &inherited,
                PAGE_READWRITE, 0, static_cast<DWORD>(sizeof(Request)), nullptr);
            if (!m_dumpDone || !m_reportDone || !m_mapping) return false;
            m_request = static_cast<Request*>(MapViewOfFile(m_mapping, FILE_MAP_ALL_ACCESS,
                0, 0, sizeof(Request)));
            if (!m_request) return false;
            ZeroMemory(m_request, sizeof(Request));
            m_request->magic = kMagic;
            m_request->version = kVersion;
            m_request->size = sizeof(Request);
            m_request->targetProcessId = GetCurrentProcessId();
            m_request->faultThreadId = GetCurrentThreadId();
            m_request->dumpKind = dumpKind;
            m_request->unattended = unattended ? 1 : 0;
            m_request->dumpDoneEvent = m_dumpDone;
            m_request->reportDoneEvent = m_reportDone;
            std::wmemcpy(m_request->dumpPath, path.c_str(), path.size() + 1);
            strncpy_s(m_request->reason, reason ? reason : "Unexpected exit", _TRUNCATE);
            if (exception && exception->ExceptionRecord && exception->ContextRecord)
            {
                m_request->hasException = 1;
                m_request->exceptionRecord = *exception->ExceptionRecord;
                // A chained record points into the target process, not this mapping.
                m_request->exceptionRecord.ExceptionRecord = nullptr;
                m_request->context = *exception->ContextRecord;
            }

            wchar_t executable[4096]{};
            const DWORD length = GetModuleFileNameW(nullptr, executable,
                static_cast<DWORD>(std::size(executable)));
            if (!length || length >= std::size(executable)) return false;
            wchar_t command[4200]{};
            if (swprintf_s(command, std::size(command),
                L"\"%ls\" --crash-reporter %llX", executable,
                static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(m_mapping))) < 0)
                return false;

            // Only the protocol handles should outlive the crashed process.
            // Inheriting every handle could keep unrelated files or services open
            // while the independent crash dialog remains visible.
            SIZE_T attributeBytes{};
            InitializeProcThreadAttributeList(nullptr, 1, 0, &attributeBytes);
            if (!attributeBytes) return false;
            std::vector<BYTE> attributeStorage(attributeBytes);
            STARTUPINFOEXW startup{};
            startup.StartupInfo.cb = sizeof(startup);
            startup.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(
                attributeStorage.data());
            if (!InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0,
                &attributeBytes)) return false;
            HANDLE inheritedHandles[]{ m_mapping, m_dumpDone, m_reportDone };
            const BOOL attributesReady = UpdateProcThreadAttribute(
                startup.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                inheritedHandles, sizeof(inheritedHandles),
                nullptr, nullptr);
            PROCESS_INFORMATION process{};
            const BOOL started = attributesReady && CreateProcessW(executable,
                command, nullptr, nullptr, TRUE,
                CREATE_DEFAULT_ERROR_MODE | EXTENDED_STARTUPINFO_PRESENT,
                nullptr, nullptr, &startup.StartupInfo, &process);
            DeleteProcThreadAttributeList(startup.lpAttributeList);
            if (!started)
                return false;
            CloseHandle(process.hThread);
            m_process = process.hProcess;
            m_reporterProcessId = process.dwProcessId;

            // A stale launcher can start the regular app instead of reporter mode.
            // Require an explicit handshake before waiting for a large dump.
            for (int attempt = 0; attempt < 300 &&
                !InterlockedCompareExchange(&m_request->reporterStarted, 0, 0);
                ++attempt)
            {
                if (WaitForSingleObject(m_process, 50) != WAIT_TIMEOUT) break;
            }
            if (!InterlockedCompareExchange(&m_request->reporterStarted, 0, 0))
            {
                StopReporter();
                return false;
            }

            const HANDLE waits[]{ m_dumpDone, m_process };
            const DWORD timeout = dumpKind == 2 ? 600000 : 180000;
            const DWORD result = WaitForMultipleObjects(2, waits, FALSE, timeout);
            if (result == WAIT_OBJECT_0 && m_request->dumpSucceeded)
                return true;
            // The helper did not finish writing. Stop it before the in-process fallback
            // opens the same path with CREATE_ALWAYS.
            StopReporter();
            return false;
        }

        void ReportReady() const
        {
            if (m_reportDone) SetEvent(m_reportDone);
        }

        DWORD ReporterProcessId() const { return m_reporterProcessId; }

    private:
        void StopReporter() const
        {
            if (m_process && WaitForSingleObject(m_process, 0) == WAIT_TIMEOUT)
            {
                TerminateProcess(m_process, ERROR_CANCELLED);
                WaitForSingleObject(m_process, 5000);
            }
        }

        HANDLE m_mapping{};
        HANDLE m_dumpDone{};
        HANDLE m_reportDone{};
        HANDLE m_process{};
        Request* m_request{};
        DWORD m_reporterProcessId{};
    };
}
