#include "../../Engine/Platform/WindowsSDK/Creator.WinAPI.h"
#include <tdh.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace
{
    constexpr GUID threads{0x3d6fa8d1, 0xfe05, 0x11d0, {0x9d, 0xda, 0, 0xc0, 0x4f, 0xd7, 0xba, 0x7c}};
    constexpr GUID processes{0x3d6fa8d0, 0xfe05, 0x11d0, {0x9d, 0xda, 0, 0xc0, 0x4f, 0xd7, 0xba, 0x7c}};
    std::ofstream metadata;
    bool unsupported{};
    std::vector<unsigned char> Read(EVENT_RECORD* event, const wchar_t* name)
    {
        PROPERTY_DATA_DESCRIPTOR property{};
        property.PropertyName = reinterpret_cast<ULONGLONG>(name);
        property.ArrayIndex = ULONG_MAX;
        ULONG size{};
        if (TdhGetPropertySize(event, 0, nullptr, 1, &property, &size) != ERROR_SUCCESS || size > 65536)
        {
            return {};
        }
        std::vector<unsigned char> value(size);
        if (TdhGetProperty(event, 0, nullptr, 1, &property, size, value.data()) != ERROR_SUCCESS)
        {
            return {};
        }
        return value;
    }
    unsigned long long Number(EVENT_RECORD* event, const wchar_t* name)
    {
        const auto value = Read(event, name);
        unsigned long long number{};
        if (!value.empty())
        {
            std::memcpy(&number, value.data(), (std::min)(value.size(), sizeof(number)));
        }
        return number;
    }
    std::string Text(EVENT_RECORD* event, const wchar_t* name, bool wide)
    {
        const auto value = Read(event, name);
        if (value.empty())
        {
            return {};
        }
        if (!wide)
        {
            return std::string(value.begin(), std::find(value.begin(), value.end(), 0));
        }
        const auto* text = reinterpret_cast<const wchar_t*>(value.data());
        auto count = static_cast<int>(value.size() / sizeof(wchar_t));
        while (count && text[count - 1] == 0)
        {
            --count;
        }
        std::string result(WideCharToMultiByte(CP_UTF8, 0, text, count, nullptr, 0, nullptr, nullptr), 0);
        WideCharToMultiByte(CP_UTF8, 0, text, count, result.data(), static_cast<int>(result.size()), nullptr, nullptr);
        return result;
    }
    void WINAPI Event(EVENT_RECORD* event)
    {
        const auto opcode = event->EventHeader.EventDescriptor.Opcode;
        const auto time = event->EventHeader.TimeStamp.QuadPart;
        if (IsEqualGUID(event->EventHeader.ProviderId, processes) && (opcode == 1 || opcode == 3 || opcode == 4))
        {
            metadata << "P," << time << ',' << Number(event, L"ProcessId") << ',' << Number(event, L"ParentId") << ','
                     << std::quoted(Text(event, L"ImageFileName", false)) << '\n';
        }
        if (!IsEqualGUID(event->EventHeader.ProviderId, threads))
        {
            return;
        }
        if (opcode == 1 || opcode == 3 || opcode == 4 || opcode == 72)
        {
            auto id = Number(event, L"TThreadId");
            if (!id)
            {
                id = Number(event, L"ThreadId");
            }
            metadata << "T," << time << ',' << Number(event, L"ProcessId") << ',' << id << ','
                     << Number(event, L"BasePriority") << ',' << Number(event, L"Affinity") << ','
                     << std::quoted(Text(event, L"ThreadName", true)) << '\n';
        }
        else if (opcode == 36)
        {
            const auto* data = static_cast<const unsigned char*>(event->UserData);
            if (event->EventHeader.EventDescriptor.Version != 5 || event->UserDataLength != 28)
            {
                unsupported = true;
                return;
            }
            ULONG incoming{}, outgoing{};
            std::memcpy(&incoming, data, 4);
            std::memcpy(&outgoing, data + 4, 4);
            std::cout << "S," << time << ',' << event->BufferContext.ProcessorIndex << ',' << incoming << ','
                      << outgoing << ',' << unsigned(data[14]) << ',' << unsigned(data[12]) << ',' << unsigned(data[8])
                      << ',' << unsigned(data[9]) << ',' << unsigned(data[15]) << '\n';
        }
        else if (opcode == 50 && event->UserDataLength >= 8)
        {
            ULONG ready{};
            std::memcpy(&ready, event->UserData, 4);
            const auto* data = static_cast<const unsigned char*>(event->UserData);
            std::cout << "R," << time << ',' << event->BufferContext.ProcessorIndex << ',' << ready << ','
                      << event->EventHeader.ProcessId << ',' << event->EventHeader.ThreadId << ',' << unsigned(data[4])
                      << ',' << unsigned(data[5]) << ',' << unsigned(data[6]) << '\n';
        }
    }
} // namespace
int wmain(int argc, wchar_t** argv)
{
    if (argc != 3)
    {
        return 2;
    }
    metadata.open(std::filesystem::path(argv[2]));
    EVENT_TRACE_LOGFILEW log{};
    log.LogFileName = argv[1];
    log.ProcessTraceMode = PROCESS_TRACE_MODE_EVENT_RECORD | PROCESS_TRACE_MODE_RAW_TIMESTAMP;
    log.EventRecordCallback = Event;
    auto trace = OpenTraceW(&log);
    if (trace == INVALID_PROCESSTRACE_HANDLE)
    {
        return 3;
    }
    const bool qpc = log.LogfileHeader.PerfFreq.QuadPart > 0 && log.LogfileHeader.ReservedFlags == 1;
    const auto result = ProcessTrace(&trace, 1, nullptr, nullptr);
    CloseTrace(trace);
    return result == ERROR_SUCCESS && qpc && !unsupported ? 0 : 4;
}
