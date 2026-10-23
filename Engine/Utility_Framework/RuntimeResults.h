#pragma once

#include "DiagnosticDocument.h"
#include <Windows.h>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <stdexcept>
#include <string>

// Machine-readable observations share one ordered journal in the host runtime.
// Each write closes its file: teardown does not depend on the logging service.
// An explicit destination survives Player's normal process-directory cleanup.
namespace Diagnostics::RuntimeResults
{
    inline std::mutex mutex;
    inline std::filesystem::path destination;
    inline std::uint64_t sequence = 0;

    template<typename Fill>
    void Emit(const char* event, Fill fill) noexcept
    {
        try
        {
            std::lock_guard lock(mutex);
            if (destination.empty()) return;

            Document document;
            auto root = document.Root();
            root["schemaVersion"] << 1;
            root["processId"] << GetCurrentProcessId();
            root["sequence"] << ++sequence;
            root["event"] << event;
            auto data = root["data"];
            data.SetMap();
            fill(data);
            const auto json = document.ToJson();

            FILE* file = nullptr;
            if (_wfopen_s(&file, destination.c_str(), L"ab") != 0 || !file)
                throw std::runtime_error("Could not open runtime result journal");

            const bool written = std::fwrite(json.data(), 1, json.size(), file) == json.size()
                && std::fputc('\n', file) != EOF;
            const bool closed = std::fclose(file) == 0;
            if (!written || !closed) throw std::runtime_error("Could not write runtime result journal");
        }
        catch (...)
        {
            std::fputs("Runtime result journal write failed\n", stderr);
        }
    }

    inline void Emit(const char* event)
    {
        Emit(event, [](Node) {});
    }

    inline void Initialize(const std::filesystem::path& logRoot)
    {
        wchar_t path[32768]{};
        const auto length = GetEnvironmentVariableW(L"CREATOR_RUNTIME_RESULTS_FILE", path, 32768);
        if (length >= 32768) throw std::runtime_error("Runtime result destination is too long");
        destination = length ? std::filesystem::path(path) : logRoot / L"runtime-results.jsonl";
        if (!destination.is_absolute()) throw std::runtime_error("Runtime result destination must be absolute");
        // Never truncate an earlier run. Readers reject a reused journal by PID/sequence.
        Emit("session.started");
    }
}
