#include "LogSink.h"
#include "LogSystem.h"
#include "HtmlFileSink.h"
#include "OutputLogText.h"
#include "MenuBarWindow.h"
#include <spdlog/logger.h>
#include <spdlog/pattern_formatter.h>
#include <atomic>
#include <barrier>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <thread>

namespace
{
    void Require(bool condition, const char* description)
    {
        if (!condition) throw std::runtime_error(description);
    }

    LogEntry Entry(std::string message)
    {
        LogEntry entry;
        entry.message = std::move(message);
        return entry;
    }

    void CheckSnapshot(const LogSnapshot& snapshot, LogStoreLimits limits)
    {
        Require(snapshot.entries.size() <= limits.maxEntries, "entry capacity");
        std::size_t bytes = 0;
        std::uint64_t previous = 0;
        for (const auto& entry : snapshot.entries)
        {
            Require(entry.sequence > previous, "strict sequence order");
            previous = entry.sequence;
            bytes += entry.message.size() + entry.loggerName.size() +
                entry.source.file.size() + entry.source.function.size();
        }
        Require(bytes == snapshot.textBytes && bytes <= limits.maxTextBytes, "text accounting");
    }

    void CheckOwnershipAndFormatting()
    {
        auto store = std::make_shared<LogStore>();
        LogSink sink(store);
        std::string payload = "  한글\tmessage  %s ##label\r\nnext\n";
        payload.push_back('\0');
        payload += "after NUL";
        const auto original = payload;
        std::string logger = "producer", file = "C:\\프로젝트\\source.cpp", function = "Producer";
        const auto originalFile = file;
        const auto timestamp = spdlog::log_clock::time_point{ std::chrono::milliseconds{ 1700000000123 } };
        spdlog::details::log_msg message(timestamp, { file.c_str(), 42, function.c_str() },
            { logger.data(), logger.size() }, spdlog::level::warn, { payload.data(), payload.size() });
        message.thread_id = 12345;

        spdlog::pattern_formatter oldFormatter;
        spdlog::memory_buf_t oldText;
        oldFormatter.format(message, oldText);
        const std::string expectedText(oldText.data(), oldText.size());
        sink.set_pattern("this pattern must not become the stored payload");
        sink.log(message);
        payload.assign(200, 'x');
        logger.assign(200, 'y');
        file.assign(200, 'z');
        function.clear();

        auto snapshot = store->ReadSnapshot();
        Require(snapshot.entries.size() == 1, "one captured record");
        const auto& entry = snapshot.entries.front();
        Require(entry.message == original && entry.loggerName == "producer", "owned original strings");
        Require(entry.timestamp == timestamp && entry.threadId == 12345 &&
            entry.level == spdlog::level::warn, "captured occurrence metadata");
        Require(entry.source.file == originalFile && entry.source.function == "Producer" &&
            entry.source.line == 42, "owned source metadata");
        spdlog::pattern_formatter newFormatter;
        Require(editor::FormatOutputLogEntry(entry, newFormatter) == expectedText, "legacy display parity");
        Require(!store->ReadSnapshotIfChanged(snapshot.revision), "unchanged snapshot is not copied");
        snapshot.entries.front().message = "reader mutation";
        Require(store->ReadSnapshot().entries.front().message == original, "reader cannot mutate history");

        spdlog::details::log_msg empty;
        empty.time = timestamp;
        sink.log(empty);
        const auto noSource = store->ReadSnapshot().entries.back();
        Require(noSource.message.empty() && noSource.loggerName.empty() &&
            noSource.source.file.empty() && noSource.source.function.empty() && noSource.source.line == 0,
            "empty borrowed fields and unknown source");
        Require(store->ReadSnapshotIfChanged(snapshot.revision).has_value(), "changed snapshot published");

        std::weak_ptr<LogStore> weak = store;
        store.reset();
        Require(!weak.expired(), "sink retains store lifetime");
        Require(snapshot.entries.front().source.file == originalFile, "snapshot owns its fields");
        std::puts("PASS ownership, metadata, original bytes, formatter parity, snapshot isolation");
    }

    void CheckRetentionAndClear()
    {
        // The real facade is usable before sink initialization; UI reads do not
        // depend on a live LogSink pointer or its friend-only storage members.
        const auto initialFacade = Debug->GetLogSnapshot();
        Debug->Clear();
        Require(Debug->GetLogSnapshot().clearGeneration == initialFacade.clearGeneration + 1 &&
            Debug->GetLogSnapshotIfChanged(initialFacade.revision).has_value(),
            "DebugClass snapshot/Clear before sink initialization");
        LogStore store({ 3, 100 });
        Require(store.ReadSnapshotIfChanged(0).has_value(), "initial empty snapshot published");
        store.Append(Entry("a"));
        store.Append(Entry("b"));
        store.Append(Entry("c"));
        const auto old = store.ReadSnapshot();
        store.Append(Entry("d"));
        const auto full = store.ReadSnapshot();
        Require(full.entries.front().message == "b" && full.entries.back().message == "d" &&
            full.evictedEntries == 1, "oldest entry eviction");
        Require(old.entries.front().message == "a", "eviction preserves reader snapshot");
        store.Clear();
        const auto cleared = store.ReadSnapshot();
        Require(cleared.entries.empty() && cleared.textBytes == 0 && cleared.evictedEntries == 0 &&
            cleared.rejectedEntries == 0 && cleared.clearGeneration == old.clearGeneration + 1 &&
            cleared.revision > full.revision, "atomic clear state");
        store.Append(Entry("after clear"));
        Require(store.ReadSnapshot().entries.front().sequence > full.entries.back().sequence,
            "sequence never reused by Clear");

        LogStore bytes({ 10, 10 });
        bytes.Append(Entry("0123456789"));
        bytes.Append(Entry("abcd"));
        Require(bytes.ReadSnapshot().entries.size() == 1 && bytes.ReadSnapshot().textBytes == 4,
            "byte budget evicts whole entry");
        Require(!bytes.Append(Entry("01234567890")), "oversized record rejected");
        auto limited = bytes.ReadSnapshot();
        Require(limited.entries.front().message == "abcd" && limited.rejectedEntries == 1,
            "rejection preserves existing history");
        auto metadataHeavy = Entry("a");
        metadataHeavy.loggerName = "logger";
        metadataHeavy.source.file = "file";
        Require(!bytes.Append(std::move(metadataHeavy)), "metadata included in text budget");
        bytes.Append(Entry("efghij"));
        limited = bytes.ReadSnapshot();
        Require(limited.textBytes == 10 && limited.entries.size() == 2 && limited.rejectedEntries == 2,
            "exact byte capacity accepted");
        CheckSnapshot(limited, { 10, 10 });
        bytes.Clear();
        Require(bytes.ReadSnapshot().rejectedEntries == 0, "Clear resets loss counters");

        for (const auto limits : { LogStoreLimits{ 0, 1 }, LogStoreLimits{ 1, 0 } })
        {
            bool rejected = false;
            try { LogStore invalid(limits); }
            catch (const std::invalid_argument&) { rejected = true; }
            Require(rejected, "zero limits rejected");
        }
        bool rejectedNull = false;
        try { LogSink invalid(nullptr); }
        catch (const std::invalid_argument&) { rejectedNull = true; }
        Require(rejectedNull, "null store rejected");
        std::puts("PASS count/byte limits, whole-record rejection, Clear ordering, stable IDs");
    }

    void CheckConcurrentAccess(bool concurrentClear)
    {
        constexpr int writerCount = 4, writesPerWriter = 20000;
        const LogStoreLimits limits{ 128, 64 * 1024 };
        auto store = std::make_shared<LogStore>(limits);
        LogSink sink(store);
        std::barrier start(writerCount + 2);
        std::atomic<int> finished{};
        std::atomic<bool> failed{};
        std::atomic<std::uint64_t> reads{};
        std::vector<std::jthread> threads;
        for (int writer = 0; writer < writerCount; ++writer)
        {
            threads.emplace_back([&, writer]
            {
                start.arrive_and_wait();
                for (int index = 0; index < writesPerWriter; ++index)
                {
                    const auto text = "writer=" + std::to_string(writer) + ";record=" + std::to_string(index);
                    spdlog::details::log_msg message(
                        spdlog::log_clock::time_point{ std::chrono::milliseconds{ index } },
                        { "producer.cpp", index + 1, "Write" }, "concurrent", spdlog::level::info,
                        { text.data(), text.size() });
                    message.thread_id = static_cast<std::size_t>(writer + 1);
                    sink.log(message);
                }
                ++finished;
            });
        }
        threads.emplace_back([&]
        {
            start.arrive_and_wait();
            try
            {
                std::uint64_t revision = 0, clearGeneration = 0;
                do
                {
                    auto snapshot = store->ReadSnapshot();
                    CheckSnapshot(snapshot, limits);
                    Require(snapshot.revision >= revision && snapshot.clearGeneration >= clearGeneration,
                        "reader version monotonic");
                    revision = snapshot.revision;
                    clearGeneration = snapshot.clearGeneration;
                    for (const auto& entry : snapshot.entries)
                    {
                        const auto index = entry.source.line - 1;
                        const auto expected = "writer=" + std::to_string(entry.threadId - 1) +
                            ";record=" + std::to_string(index);
                        Require(entry.message == expected && entry.loggerName == "concurrent" &&
                            entry.source.file == "producer.cpp" && entry.source.function == "Write" &&
                            entry.timestamp.time_since_epoch() == std::chrono::milliseconds{ index },
                            "no torn metadata/payload");
                    }
                    ++reads;
                    std::this_thread::yield();
                } while (finished.load() != writerCount);
            }
            catch (...) { failed = true; }
        });
        start.arrive_and_wait();
        if (concurrentClear)
        {
            for (int index = 0; index < 400; ++index)
            {
                store->Clear();
                std::this_thread::yield();
            }
        }
        threads.clear();
        Require(!failed && reads > 0, "concurrent reads passed");
        const auto final = store->ReadSnapshot();
        CheckSnapshot(final, limits);
        Require(final.rejectedEntries == 0, "concurrent records fit byte limit");
        if (!concurrentClear)
            Require(final.entries.size() + final.evictedEntries == writerCount * writesPerWriter,
                "all concurrent writes accounted for");
        else
            Require(final.clearGeneration == 400, "all clears accounted for");
        store->Clear();
        store->Append(Entry("after all producers stopped"));
        const auto after = store->ReadSnapshot();
        Require(after.entries.size() == 1 && after.entries.front().sequence == writerCount * writesPerWriter + 1,
            "post-clear record retained with globally increasing ID");
        std::printf("PASS concurrent %s: 80000 writes, %llu snapshots\n",
            concurrentClear ? "append/read/400 clears" : "append/read",
            static_cast<unsigned long long>(reads.load()));
    }

    void CheckFileSinkIndependence(const char* outputPath)
    {
        auto store = std::make_shared<LogStore>(LogStoreLimits{ 1, 32 });
        {
            auto ui = std::make_shared<LogSink>(store);
            auto file = std::make_shared<HtmlFileSink>(outputPath, false);
            spdlog::logger logger("file-test", { ui, file });
            logger.info("before clear");
            store->Clear();
            logger.warn("retained");
            logger.error("oversized-original-message-must-remain-in-file");
            logger.flush();
            Require(file->GetEntryCount() == 3 && file->GetWarnCount() == 1 && file->GetErrorCount() == 1,
                "all occurrences reach file sink");
        }
        const auto snapshot = store->ReadSnapshot();
        Require(snapshot.entries.size() == 1 && snapshot.entries.front().message == "retained" &&
            snapshot.rejectedEntries == 1, "UI retention independent of file");
        std::ifstream file(outputPath, std::ios::binary);
        Require(file.good(), "file sink output exists");
        const std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        Require(content.find("before clear") != std::string::npos &&
            content.find("oversized-original-message-must-remain-in-file") != std::string::npos,
            "Clear and oversize rejection do not delete disk history");
        std::puts("PASS actual spdlog logger / HTML sink independence");
    }
}

int main(int argc, char** argv)
{
    try
    {
        Require(argc == 2, "expected output HTML path");
        CheckOwnershipAndFormatting();
        CheckRetentionAndClear();
        CheckConcurrentAccess(false);
        CheckConcurrentAccess(true);
        CheckFileSinkIndependence(argv[1]);
        std::puts("LOG_STORAGE_OK");
        return 0;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "LOG_STORAGE_FAILED: %s\n", error.what());
        return 1;
    }
}
