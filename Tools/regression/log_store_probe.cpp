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
#include <deque>
#include <iterator>
#include <map>
#include <unordered_map>
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
        Require(snapshot.entries.size() <= limits.maxOccurrences, "occurrence capacity");
        Require(snapshot.groups.size() <= limits.maxGroups, "group capacity");

        std::unordered_map<std::uint64_t, std::uint64_t> counted;
        std::uint64_t previous = 0;
        for (const auto& entry : snapshot.entries)
        {
            Require(entry.sequence > previous, "strict sequence order");
            previous = entry.sequence;
            Require(entry.groupId != 0, "occurrence names its group");
            ++counted[entry.groupId];
        }

        // Text is billed per group, so repeats of one message are counted once.
        std::size_t bytes = 0;
        std::uint64_t previousGroup = 0;
        for (const auto& group : snapshot.groups)
        {
            Require(group.groupId > previousGroup, "ascending distinct group ids");
            previousGroup = group.groupId;
            Require(group.retainedOccurrences <= group.totalCount, "retained never exceeds total");
            const auto seen = counted.find(group.groupId);
            const std::uint64_t retained = seen == counted.end() ? 0 : seen->second;
            Require(retained == group.retainedOccurrences, "retained count matches the history");
            if (seen != counted.end()) counted.erase(seen);
            bytes += group.message.size() + group.loggerName.size() +
                group.source.file.size() + group.source.function.size();
        }
        Require(counted.empty(), "every retained occurrence still has its group");
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
        LogStore store({ 3, 64, 100 });
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

        LogStore bytes({ 10, 64, 10 });
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
        CheckSnapshot(limited, { 10, 64, 10 });
        bytes.Clear();
        Require(bytes.ReadSnapshot().rejectedEntries == 0, "Clear resets loss counters");

        for (const auto limits : { LogStoreLimits{ 0, 1, 1 }, LogStoreLimits{ 1, 0, 1 },
            LogStoreLimits{ 1, 1, 0 } })
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
        const LogStoreLimits limits{ 128, 512, 64 * 1024 };
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
        auto store = std::make_shared<LogStore>(LogStoreLimits{ 1, 8, 32 });
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

    // ── 2단계: 반복 집계 ──────────────────────────────────────────────────
    //
    // Unity 의 Collapse 는 "비슷한" 줄이 아니라 **같은 정체성**을 접는다.
    // 아래 단정은 무엇이 정체성이고(여섯 축) 무엇이 발생 정보인지(시각·스레드)
    // 를 양쪽에서 못 박는다. 한쪽만 재면 "전부 한 줄로 접히는" 퇴행과
    // "아무것도 안 접히는" 퇴행 중 하나를 놓친다.
    LogEntry Keyed(std::string message)
    {
        LogEntry entry;
        entry.level = spdlog::level::warn;
        entry.loggerName = "gameplay";
        entry.message = std::move(message);
        entry.source.file = "C:\\src\\Player.cpp";
        entry.source.function = "Tick";
        entry.source.line = 95;
        entry.threadId = 7;
        entry.timestamp = spdlog::log_clock::time_point{ std::chrono::milliseconds{ 1000 } };
        return entry;
    }

    const LogGroup& FindGroup(const LogSnapshot& snapshot, std::uint64_t groupId)
    {
        for (const auto& group : snapshot.groups)
        {
            if (group.groupId == groupId) return group;
        }
        throw std::runtime_error("group missing from snapshot");
    }

    void CheckGrouping()
    {
        const LogStoreLimits wide{ 64, 64, 4096 };
        LogStore store(wide);
        for (int repeat = 0; repeat < 3; ++repeat)
        {
            auto entry = Keyed("마우스 GetCurrentReading 실패!");
            entry.threadId = static_cast<std::size_t>(repeat + 1);
            entry.timestamp = spdlog::log_clock::time_point{ std::chrono::milliseconds{ 100 + repeat } };
            store.Append(std::move(entry));
        }
        auto snapshot = store.ReadSnapshot();
        CheckSnapshot(snapshot, wide);
        Require(snapshot.groups.size() == 1 && snapshot.entries.size() == 3,
            "repeats of one identity collapse into a single group");
        const auto repeated = snapshot.groups.front();
        Require(repeated.totalCount == 3 && repeated.retainedOccurrences == 3, "repeat count");
        Require(repeated.firstTimestamp.time_since_epoch() == std::chrono::milliseconds{ 100 } &&
            repeated.lastTimestamp.time_since_epoch() == std::chrono::milliseconds{ 102 },
            "first and last occurrence times");
        Require(snapshot.entries.front().threadId == 1 && snapshot.entries.back().threadId == 3,
            "each occurrence keeps its own thread");
        Require(snapshot.textBytes == repeated.message.size() + repeated.loggerName.size() +
            repeated.source.file.size() + repeated.source.function.size(),
            "a repeat does not store its payload again");

        // 정체성을 가르는 축을 **선언하고**, 선언한 수만큼 그룹이 늘었는지 본다.
        struct KeyAxis { const char* name; LogEntry entry; };
        std::vector<KeyAxis> axes;
        {
            const std::string base = repeated.message;
            auto level = Keyed(base); level.level = spdlog::level::err;
            axes.push_back({ "level", std::move(level) });
            auto logger = Keyed(base); logger.loggerName = "input";
            axes.push_back({ "logger", std::move(logger) });
            axes.push_back({ "message", Keyed(base + "?") });
            auto file = Keyed(base); file.source.file = "C:\\src\\Other.cpp";
            axes.push_back({ "source file", std::move(file) });
            auto function = Keyed(base); function.source.function = "Update";
            axes.push_back({ "source function", std::move(function) });
            auto line = Keyed(base); line.source.line = 96;
            axes.push_back({ "source line", std::move(line) });
        }
        for (const auto& axis : axes) store.Append(axis.entry);
        snapshot = store.ReadSnapshot();
        CheckSnapshot(snapshot, wide);
        Require(snapshot.groups.size() == 1 + axes.size(), "every declared key axis makes its own group");
        std::size_t freshGroups = 0;
        for (const auto& group : snapshot.groups)
        {
            if (group.totalCount == 1) ++freshGroups;
        }
        Require(freshGroups == axes.size(), "each axis produced exactly one new identity");

        // 시각·스레드는 정체성이 아니다 — 프레임을 넘겨도 같은 줄로 모인다.
        const auto beforeGroups = snapshot.groups.size();
        auto later = Keyed(repeated.message);
        later.threadId = 99;
        later.timestamp = spdlog::log_clock::time_point{ std::chrono::milliseconds{ 50000 } };
        store.Append(std::move(later));
        snapshot = store.ReadSnapshot();
        Require(snapshot.groups.size() == beforeGroups, "time and thread are occurrence data, not identity");
        Require(FindGroup(snapshot, repeated.groupId).totalCount == 4, "a later frame joins the same group");

        // 비슷한 문장을 합치지 않는다. NUL 뒤의 바이트도 키의 일부다.
        LogStore literal(wide);
        literal.Append(Keyed("HP=10"));
        literal.Append(Keyed("HP=20"));
        std::string left = "a", right = "a";
        left.push_back('\0'); left += "b";
        right.push_back('\0'); right += "c";
        literal.Append(Keyed(left));
        literal.Append(Keyed(right));
        Require(literal.ReadSnapshot().groups.size() == 4,
            "numbers are not normalised and an embedded NUL does not cut the key short");

        // 누적 횟수가 이력 버퍼보다 오래 산다 — 이번 단계의 핵심이다.
        const LogStoreLimits narrow{ 4, 64, 4096 };
        LogStore spam(narrow);
        for (int index = 0; index < 100; ++index) spam.Append(Keyed("per-frame failure"));
        const auto spammed = spam.ReadSnapshot();
        CheckSnapshot(spammed, narrow);
        Require(spammed.entries.size() == 4 && spammed.groups.size() == 1, "occurrence history stays bounded");
        Require(spammed.groups.front().totalCount == 100 &&
            spammed.groups.front().retainedOccurrences == 4,
            "the repeat count outlives the occurrence buffer");
        Require(spammed.evictedEntries == 96, "evicted occurrences are counted");
        Require(spammed.entries.front().message == "per-frame failure" &&
            spammed.entries.front().groupId == spammed.groups.front().groupId,
            "the legacy occurrence view restores its payload from the group");

        // 그룹 예산이 차면 가장 오래된 정체성이 나가고, 다시 찍히면 새 id 다.
        const LogStoreLimits crowdedLimits{ 8, 2, 4096 };
        LogStore crowded(crowdedLimits);
        crowded.Append(Keyed("first"));
        const auto firstId = crowded.ReadSnapshot().groups.front().groupId;
        crowded.Append(Keyed("second"));
        crowded.Append(Keyed("third"));
        auto crowdedSnapshot = crowded.ReadSnapshot();
        CheckSnapshot(crowdedSnapshot, crowdedLimits);
        Require(crowdedSnapshot.groups.size() == 2 && crowdedSnapshot.evictedGroups == 1,
            "the group budget evicts the least recent identity");
        for (const auto& group : crowdedSnapshot.groups)
            Require(group.groupId != firstId, "the evicted identity is gone");
        crowded.Append(Keyed("first"));
        crowdedSnapshot = crowded.ReadSnapshot();
        CheckSnapshot(crowdedSnapshot, crowdedLimits);
        std::uint64_t rebornId = 0;
        for (const auto& group : crowdedSnapshot.groups)
        {
            if (group.message == "first") rebornId = group.groupId;
        }
        Require(rebornId > firstId && FindGroup(crowdedSnapshot, rebornId).totalCount == 1,
            "a re-created identity gets a new id and restarts its count");
        std::puts("PASS grouping identity axes, repeat counts beyond the buffer, group eviction");
    }

    // 화면이 프레임마다 전체를 다시 받지 않도록, 독자는 변경분만 적용한다.
    // 아래 reader 는 그 적용을 그대로 흉내 내고 매번 전체 snapshot 과 맞댄다 —
    // 변경분이 하나라도 빠지면 두 상태가 갈라진다.
    struct DeltaReader
    {
        LogCursor cursor;
        std::map<std::uint64_t, LogGroup> groups;
        std::deque<LogOccurrence> occurrences;
        std::uint64_t resynchronisations{};

        void Apply(const LogDelta& delta)
        {
            if (delta.resynchronized)
            {
                groups.clear();
                occurrences.clear();
                ++resynchronisations;
            }
            for (const auto groupId : delta.removedGroups) groups.erase(groupId);
            for (const auto& group : delta.changedGroups) groups[group.groupId] = group;
            for (const auto& occurrence : delta.appendedOccurrences) occurrences.push_back(occurrence);
            while (!occurrences.empty() && occurrences.front().sequence < delta.oldestRetainedSequence)
                occurrences.pop_front();
            cursor = delta.cursor;
        }
    };

    void RequireReaderMatches(const DeltaReader& reader, const LogSnapshot& snapshot, const char* what)
    {
        Require(reader.groups.size() == snapshot.groups.size(), what);
        for (const auto& group : snapshot.groups)
        {
            const auto held = reader.groups.find(group.groupId);
            Require(held != reader.groups.end(), what);
            Require(held->second.message == group.message && held->second.level == group.level &&
                held->second.loggerName == group.loggerName &&
                held->second.source.line == group.source.line &&
                held->second.totalCount == group.totalCount &&
                held->second.retainedOccurrences == group.retainedOccurrences, what);
        }
        Require(reader.occurrences.size() == snapshot.entries.size(), what);
        for (std::size_t index = 0; index < snapshot.entries.size(); ++index)
        {
            Require(reader.occurrences[index].sequence == snapshot.entries[index].sequence &&
                reader.occurrences[index].groupId == snapshot.entries[index].groupId, what);
        }
    }

    void CheckDelta()
    {
        const LogStoreLimits limits{ 6, 3, 4096 };
        LogStore store(limits);
        DeltaReader reader;

        auto initial = store.ReadDeltaSince(reader.cursor);
        Require(initial && initial->resynchronized, "a reader holding nothing receives the full state");
        reader.Apply(*initial);
        Require(!store.ReadDeltaSince(reader.cursor), "a current reader is not copied to");

        store.Append(Keyed("alpha"));
        auto delta = store.ReadDeltaSince(reader.cursor);
        Require(delta && !delta->resynchronized && delta->appendedOccurrences.size() == 1 &&
            delta->changedGroups.size() == 1 && delta->removedGroups.empty(),
            "one append ships one occurrence and one group");
        reader.Apply(*delta);
        RequireReaderMatches(reader, store.ReadSnapshot(), "reader matches after the first append");

        store.Append(Keyed("beta"));
        store.Append(Keyed("alpha"));
        delta = store.ReadDeltaSince(reader.cursor);
        Require(delta && delta->appendedOccurrences.size() == 2 && delta->changedGroups.size() == 2,
            "only the touched identities travel");
        reader.Apply(*delta);
        RequireReaderMatches(reader, store.ReadSnapshot(), "reader matches after mixed appends");

        // 이력 퇴거와 그룹 퇴거를 둘 다 지나가게 한다.
        for (int index = 0; index < 24; ++index)
        {
            store.Append(Keyed(index % 3 == 0 ? std::string("alpha") : "gamma" + std::to_string(index)));
            if (auto step = store.ReadDeltaSince(reader.cursor)) reader.Apply(*step);
            RequireReaderMatches(reader, store.ReadSnapshot(), "reader tracks eviction step by step");
        }
        const auto evicting = store.ReadSnapshot();
        Require(evicting.evictedGroups > 0 && evicting.evictedEntries > 0, "the run exercised both evictions");
        Require(reader.resynchronisations == 1,
            "a reader that keeps up never needs a resynchronisation");

        store.Clear();
        delta = store.ReadDeltaSince(reader.cursor);
        Require(delta && delta->resynchronized && delta->changedGroups.empty() &&
            delta->appendedOccurrences.empty(), "Clear resynchronises the reader");
        reader.Apply(*delta);
        RequireReaderMatches(reader, store.ReadSnapshot(), "reader is empty after Clear");
        Require(!store.ReadDeltaSince(reader.cursor), "the cleared reader is current");

        // 뒤처진 커서는 사라진 그룹을 들을 수 없다 — 조용히 틀리는 대신 다시 받는다.
        DeltaReader stale;
        if (auto seed = store.ReadDeltaSince(stale.cursor)) stale.Apply(*seed);
        for (int index = 0; index < 24; ++index) store.Append(Keyed("stale" + std::to_string(index)));
        auto staleDelta = store.ReadDeltaSince(stale.cursor);
        Require(staleDelta && staleDelta->resynchronized,
            "a cursor older than the removal log is resynchronised");
        stale.Apply(*staleDelta);
        RequireReaderMatches(stale, store.ReadSnapshot(), "the stale reader recovers in full");
        std::puts("PASS delta cursor, per-change payload, removal reporting, stale-cursor recovery");
    }
}

int main(int argc, char** argv)
{
    try
    {
        Require(argc == 2, "expected output HTML path");
        CheckOwnershipAndFormatting();
        CheckRetentionAndClear();
        CheckGrouping();
        CheckDelta();
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
