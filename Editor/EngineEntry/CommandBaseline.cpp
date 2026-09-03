#include "CommandBaseline.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <map>
#include <unordered_map>

namespace CommandBaseline
{
    namespace
    {
        using Clock = std::chrono::steady_clock;

        // ── 등록 표 ──────────────────────────────────────────────────────
        std::vector<Registration>& MutableRegistrations()
        {
            static std::vector<Registration> registrations;
            return registrations;
        }

        // handler 주소 → 안정 서수. 주소는 실행마다 달라지므로 artifact에 넣지
        // 않는다. 최초 등장 순서로 매기면 등록 코드가 그대로인 한 값도 그대로다.
        std::size_t HandlerOrdinal(const void* handler)
        {
            static std::unordered_map<const void*, std::size_t> ordinals;
            const auto found = ordinals.find(handler);
            if (found != ordinals.end()) return found->second;

            const std::size_t next = ordinals.size();
            ordinals.emplace(handler, next);
            return next;
        }

        // ── 표본 ─────────────────────────────────────────────────────────
        //
        // 링이 아니라 상한 있는 벡터다. LC0의 측정 구간은 명령으로 여닫는
        // 짧은 구간이고, 링으로 만들면 "언제부터의 표본인가"가 흐려진다.
        // 상한에 닿으면 더 받지 않고 버린 수를 센다 — 조용히 덮어쓰는 것보다
        // 몇 개를 못 봤는지 아는 편이 낫다.
        constexpr std::size_t kMaxFrameSamples   = 32768;   // 60fps 기준 약 9분
        constexpr std::size_t kMaxCommandSamples = 4096;

        struct FrameSample
        {
            double     deltaMs{};
            FrameState state{};
        };

        struct CommandSample
        {
            std::string name;
            double      queuedMs{};
            uint64_t    waitedFrames{};
            double      executedMs{};
        };

        // 계측 on/off. 게임 스레드만 쓰지만 Pump의 뜨거운 경로에서 읽으므로
        // 원자 변수로 두고 relaxed로 읽는다 — 순서를 맞출 다른 상태가 없다.
        std::atomic<bool>& CollectingFlag() noexcept
        {
            static std::atomic<bool> collecting{ false };
            return collecting;
        }

        struct Samples
        {
            std::vector<FrameSample>   frames;
            std::vector<CommandSample> commands;
            std::size_t                droppedFrames{};
            std::size_t                droppedCommands{};
            Clock::time_point          previousFrame{};
            bool                       hasPreviousFrame{};
        };

        Samples& State()
        {
            static Samples samples;
            return samples;
        }

        // ── 분포 ─────────────────────────────────────────────────────────
        struct Distribution
        {
            std::size_t count{};
            double      min{};
            double      p50{};
            double      p95{};
            double      p99{};
            double      max{};
            double      mean{};
        };

        // 최근접 순위법(nearest-rank). 보간하지 않는 이유는 표본이 적을 때
        // 보간값이 실제로 관측되지 않은 시간을 만들어내기 때문이다 — 예산의
        // 분모로 쓸 값은 실제로 본 값이어야 한다.
        double Percentile(const std::vector<double>& sorted, double fraction)
        {
            if (sorted.empty()) return 0.0;
            const auto rank = static_cast<std::size_t>(std::ceil(fraction * static_cast<double>(sorted.size())));
            const std::size_t index = (rank == 0) ? 0 : (rank - 1);
            return sorted[std::min(index, sorted.size() - 1)];
        }

        Distribution Describe(std::vector<double> values)
        {
            Distribution distribution;
            if (values.empty()) return distribution;

            std::sort(values.begin(), values.end());
            double sum = 0.0;
            for (const double value : values) sum += value;

            distribution.count = values.size();
            distribution.min   = values.front();
            distribution.max   = values.back();
            distribution.mean  = sum / static_cast<double>(values.size());
            distribution.p50   = Percentile(values, 0.50);
            distribution.p95   = Percentile(values, 0.95);
            distribution.p99   = Percentile(values, 0.99);
            return distribution;
        }

        void WriteDistributionRow(std::FILE* out, const char* kind, const char* bucket,
                                  const char* metric, const char* unit, const Distribution& d)
        {
            std::fprintf(out, "%s\t%s\t%s\t%s\t%zu\t%.3f\t%.3f\t%.3f\t%.3f\t%.3f\t%.3f\n",
                         kind, bucket, metric, unit, d.count,
                         d.min, d.p50, d.p95, d.p99, d.max, d.mean);
        }

        // 프레임 시간 히스토그램의 경계. 로그에 가까운 폭이다 — 16ms 근처를
        // 잘게 보고 싶은데(vsync 없는 present라 그 근처에 몰릴지 아직 모른다),
        // 긴 꼬리도 한 칸에 뭉개지 않아야 한다.
        constexpr double kHistogramEdgesMs[] = { 1.0, 2.0, 4.0, 8.0, 16.0, 33.0, 66.0, 133.0, 500.0 };

        // 쓰기 실패를 삼키지 않는다.
        //
        // fprintf의 반환값을 하나하나 보지 않는 대신, 스트림의 오류 플래그를
        // 닫기 직전에 한 번 본다 — 디스크가 차거나 권한이 바뀌면 그때까지의
        // fprintf가 조용히 실패하고 파일은 잘린 채로 남는데, 그 상태를 "완료"로
        // 보고하면 다음 사람이 잘린 artifact를 값으로 읽는다. 계측 도구가
        // 낼 수 있는 가장 나쁜 결과가 그것이다.
        bool CloseChecked(std::FILE* out)
        {
            const bool failed = (0 != std::ferror(out));
            const bool closeFailed = (0 != std::fclose(out));
            return !failed && !closeFailed;
        }

        std::string IsoTimestampUtc()
        {
            const std::time_t now = std::time(nullptr);
            std::tm utc{};
#if defined(_WIN32)
            gmtime_s(&utc, &now);
#else
            gmtime_r(&now, &utc);
#endif
            char buffer[32] = {};
            std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &utc);
            return buffer;
        }

        // help 텍스트에서 명령 이름 하나가 실제로 안내되고 있는지.
        //
        // 단순 부분 문자열 검색은 못 쓴다 — "scene.load"를 찾을 때 설명문에 섞인
        // 다른 줄이 걸리고, 반대로 "help"는 아무 문장에나 들어 있다. 안내 줄은
        // 전부 "  <이름> ..." 형태이므로 줄 단위로 첫 토큰만 본다.
        std::vector<std::string> HelpListedNames(const char* helpText)
        {
            std::vector<std::string> names;
            if (nullptr == helpText) return names;

            const std::string text(helpText);
            std::size_t lineBegin = 0;
            while (lineBegin <= text.size())
            {
                std::size_t lineEnd = text.find('\n', lineBegin);
                if (lineEnd == std::string::npos) lineEnd = text.size();

                std::string line = text.substr(lineBegin, lineEnd - lineBegin);
                lineBegin = lineEnd + 1;

                // 안내 줄은 들여쓴다. 들여쓰지 않은 줄은 제목·빈 줄·실행 인자 안내다.
                const std::size_t first = line.find_first_not_of(" \t");
                if (first == std::string::npos || first == 0) continue;

                const std::size_t tokenEnd = line.find_first_of(" \t", first);
                std::string token = line.substr(first, (tokenEnd == std::string::npos)
                                                       ? std::string::npos : (tokenEnd - first));
                if (token.empty()) continue;

                // dotted ID이거나 소문자 단어인 것만 명령 후보로 본다. 설명이
                // 이어지는 줄("(기본 1)" 같은)이 이름으로 새지 않게.
                const bool looksLikeCommand =
                    std::all_of(token.begin(), token.end(), [](unsigned char c)
                    {
                        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_';
                    });
                if (!looksLikeCommand) continue;

                names.push_back(std::move(token));
            }
            return names;
        }
    }

    void RecordRegistration(const std::vector<const char*>& accepted,
                            const std::vector<const char*>& rejected,
                            const void*                     handler)
    {
        // 표에 한 이름도 들어가지 못한 등록은 inventory에 남기지 않는다 —
        // 그런 항목은 "호출 가능한 명령"이 아니다. 대신 거부 목록으로만 남는다.
        if (accepted.empty())
        {
            for (const char* name : rejected)
            {
                if (nullptr == name) continue;
                Registration orphan;
                orphan.handlerIndex = HandlerOrdinal(handler);
                orphan.rejected.emplace_back(name);
                MutableRegistrations().push_back(std::move(orphan));
            }
            return;
        }

        Registration registration;
        registration.handlerIndex = HandlerOrdinal(handler);

        bool first = true;
        for (const char* name : accepted)
        {
            if (nullptr == name) continue;
            if (first) { registration.canonical = name; first = false; }
            else       { registration.aliases.emplace_back(name); }
        }
        for (const char* name : rejected)
        {
            if (nullptr != name) registration.rejected.emplace_back(name);
        }

        if (registration.canonical.empty()) return;
        MutableRegistrations().push_back(std::move(registration));
    }

    bool IsCollecting() noexcept
    {
        return CollectingFlag().load(std::memory_order_relaxed);
    }

    void SetCollecting(bool enabled) noexcept
    {
        if (enabled) ResetSamples();
        CollectingFlag().store(enabled, std::memory_order_relaxed);
    }

    const std::vector<Registration>& Registrations() noexcept
    {
        return MutableRegistrations();
    }

    void NoteFrame(FrameState state)
    {
        if (!IsCollecting()) return;

        Samples& samples = State();
        const Clock::time_point now = Clock::now();

        if (samples.hasPreviousFrame)
        {
            if (samples.frames.size() < kMaxFrameSamples)
            {
                const std::chrono::duration<double, std::milli> delta = now - samples.previousFrame;
                samples.frames.push_back(FrameSample{ delta.count(), state });
            }
            else
            {
                ++samples.droppedFrames;
            }
        }

        samples.previousFrame    = now;
        samples.hasPreviousFrame = true;
    }

    void NoteCommand(std::string_view name, double queuedMs, uint64_t waitedFrames, double executedMs)
    {
        if (!IsCollecting()) return;

        Samples& samples = State();
        if (samples.commands.size() >= kMaxCommandSamples) { ++samples.droppedCommands; return; }

        CommandSample sample;
        sample.name         = std::string(name);
        sample.queuedMs     = queuedMs;
        sample.waitedFrames = waitedFrames;
        sample.executedMs   = executedMs;
        samples.commands.push_back(std::move(sample));
    }

    void ResetSamples() noexcept
    {
        Samples& samples = State();
        samples.frames.clear();
        samples.commands.clear();
        samples.droppedFrames    = 0;
        samples.droppedCommands  = 0;
        samples.hasPreviousFrame = false;
    }

    bool WriteInventory(const std::string& path, const char* helpText)
    {
        std::FILE* out = nullptr;
#if defined(_WIN32)
        if (0 != fopen_s(&out, path.c_str(), "wb") || nullptr == out) return false;
#else
        out = std::fopen(path.c_str(), "wb");
        if (nullptr == out) return false;
#endif

        // 정렬 사본. 등록 순서는 소스 편집으로 흔들리지만 이름 순은 안 흔들린다.
        std::vector<Registration> sorted = MutableRegistrations();
        std::sort(sorted.begin(), sorted.end(),
                  [](const Registration& a, const Registration& b) { return a.canonical < b.canonical; });
        for (Registration& registration : sorted)
        {
            std::sort(registration.aliases.begin(), registration.aliases.end());
        }

        const std::vector<std::string> helpNames = HelpListedNames(helpText);
        const auto isHelpListed = [&helpNames](const std::string& name)
        {
            return std::find(helpNames.begin(), helpNames.end(), name) != helpNames.end();
        };

        // 등록된 전체 이름(별칭 포함) 집합 — help의 고아 항목을 가려내는 데 쓴다.
        std::map<std::string, std::size_t> registeredNames;
        for (const Registration& registration : sorted)
        {
            registeredNames.emplace(registration.canonical, registration.handlerIndex);
            for (const std::string& alias : registration.aliases)
            {
                registeredNames.emplace(alias, registration.handlerIndex);
            }
        }

        std::size_t helpCoveredCanonical = 0;
        std::size_t helpCoveredAnyName   = 0;
        for (const Registration& registration : sorted)
        {
            if (isHelpListed(registration.canonical)) { ++helpCoveredCanonical; ++helpCoveredAnyName; continue; }
            const bool aliasListed = std::any_of(registration.aliases.begin(), registration.aliases.end(), isHelpListed);
            if (aliasListed) ++helpCoveredAnyName;
        }

        std::vector<std::string> helpOrphans;
        for (const std::string& name : helpNames)
        {
            if (registeredNames.find(name) == registeredNames.end()) helpOrphans.push_back(name);
        }
        std::sort(helpOrphans.begin(), helpOrphans.end());
        helpOrphans.erase(std::unique(helpOrphans.begin(), helpOrphans.end()), helpOrphans.end());

        std::fprintf(out, "# lc0-inventory v1\n");
        std::fprintf(out, "# generated\t%s\n", IsoTimestampUtc().c_str());
        std::fprintf(out, "# groups\t%zu\n", sorted.size());
        std::fprintf(out, "# names\t%zu\n", registeredNames.size());
        std::fprintf(out, "# help_listed_lines\t%zu\n", helpNames.size());
        std::fprintf(out, "# help_covered_canonical\t%zu\n", helpCoveredCanonical);
        std::fprintf(out, "# help_covered_any_name\t%zu\n", helpCoveredAnyName);
        std::fprintf(out, "# help_orphans\t%zu\n", helpOrphans.size());
        for (const std::string& orphan : helpOrphans)
        {
            std::fprintf(out, "# help_orphan\t%s\n", orphan.c_str());
        }

        // 이름 충돌로 조회 표에 못 들어간 등록. 0이 아니면 소스가 같은 이름을
        // 두 번 등록했다는 뜻이고, 그 이름을 치면 먼저 등록된 handler가 돈다.
        std::size_t rejectedNames = 0;
        for (const Registration& registration : sorted) rejectedNames += registration.rejected.size();
        std::fprintf(out, "# rejected_names\t%zu\n", rejectedNames);

        std::fprintf(out, "canonical\taliases\thandler_ordinal\thelp_listed\trejected\n");

        for (const Registration& registration : sorted)
        {
            std::string aliases;
            for (const std::string& alias : registration.aliases)
            {
                if (!aliases.empty()) aliases.push_back(',');
                aliases += alias;
            }
            if (aliases.empty()) aliases = "-";

            std::string rejected;
            for (const std::string& name : registration.rejected)
            {
                if (!rejected.empty()) rejected.push_back(',');
                rejected += name;
            }
            if (rejected.empty()) rejected = "-";

            std::fprintf(out, "%s\t%s\t%zu\t%s\t%s\n",
                         registration.canonical.empty() ? "-" : registration.canonical.c_str(),
                         aliases.c_str(),
                         registration.handlerIndex,
                         isHelpListed(registration.canonical) ? "yes" : "no",
                         rejected.c_str());
        }

        return CloseChecked(out);
    }

    bool WriteTiming(const std::string& path)
    {
        std::FILE* out = nullptr;
#if defined(_WIN32)
        if (0 != fopen_s(&out, path.c_str(), "wb") || nullptr == out) return false;
#else
        out = std::fopen(path.c_str(), "wb");
        if (nullptr == out) return false;
#endif

        const Samples& samples = State();

        std::fprintf(out, "# lc0-timing v1\n");
        std::fprintf(out, "# generated\t%s\n", IsoTimestampUtc().c_str());
        std::fprintf(out, "# frame_samples\t%zu\n", samples.frames.size());
        std::fprintf(out, "# frame_dropped\t%zu\n", samples.droppedFrames);
        std::fprintf(out, "# command_samples\t%zu\n", samples.commands.size());
        std::fprintf(out, "# command_dropped\t%zu\n", samples.droppedCommands);

        // focused 표본이 0일 때 그것이 관측인지 계측 고장인지 가른다.
        // frames_with_window가 0이면 창을 못 찾은 것이므로 focused 열을 읽지 않는다.
        std::size_t framesWithWindow = 0;
        for (const FrameSample& sample : samples.frames)
        {
            if (sample.state.windowKnown) ++framesWithWindow;
        }
        std::fprintf(out, "# frames_with_window\t%zu\n", framesWithWindow);

        std::size_t focusedFrames = 0;
        for (const FrameSample& sample : samples.frames)
        {
            if (sample.state.focused) ++focusedFrames;
        }
        if (framesWithWindow > 0 && 0 == focusedFrames)
        {
            // 아래 focused 행이 전부 0인 것은 "포커스 프레임이 0ms"라는 뜻이
            // 아니라 **그 축을 재지 못했다**는 뜻이다. artifact가 스스로
            // 그렇게 말하지 않으면 다음 사람이 0을 값으로 읽는다.
            std::fprintf(out, "# focused_unmeasured\t1\t"
                              "창 핸들은 있었으나 이 실행에서 에디터 창이 한 번도 전경이 아니었다. "
                              "focused 행은 표본 0이며 값이 아니다.\n");
        }

        std::fprintf(out, "kind\tbucket\tmetric\tunit\tsamples\tmin\tp50\tp95\tp99\tmax\tmean\n");

        // 프레임 시간 — 조건별 주변 분포. 한 프레임이 여러 조건에 동시에
        // 들 수 있으므로(재생 중이면서 포커스) 배타 분할이 아니라 겹치는 축이다.
        struct FrameBucket
        {
            const char* name;
            bool (*select)(const FrameState&);
        };
        static const FrameBucket kFrameBuckets[] = {
            { "all",           [](const FrameState&)   { return true; } },
            { "focused",       [](const FrameState& s) { return s.focused; } },
            { "unfocused",     [](const FrameState& s) { return !s.focused; } },
            { "scene-loading", [](const FrameState& s) { return s.sceneLoading; } },
            { "playing",       [](const FrameState& s) { return s.playing; } },
            { "waiting",       [](const FrameState& s) { return s.waiting; } },
            { "quiescent",     [](const FrameState& s) { return !s.sceneLoading && !s.playing && !s.waiting; } },
        };

        for (const FrameBucket& bucket : kFrameBuckets)
        {
            std::vector<double> values;
            values.reserve(samples.frames.size());
            for (const FrameSample& sample : samples.frames)
            {
                if (bucket.select(sample.state)) values.push_back(sample.deltaMs);
            }
            WriteDistributionRow(out, "frame", bucket.name, "delta", "ms", Describe(std::move(values)));
        }

        // 명령 왕복 — 오늘의 바닥값. queued가 프레임 경계 대기이고,
        // executed가 handler 자체다. 서비스가 더할 것(accept·파싱·인증·직렬화)은
        // 여기에 없다 — 그래서 이 값이 **바닥**이다.
        {
            std::vector<double> queued;
            std::vector<double> executed;
            std::vector<double> waited;
            queued.reserve(samples.commands.size());
            executed.reserve(samples.commands.size());
            waited.reserve(samples.commands.size());
            for (const CommandSample& sample : samples.commands)
            {
                queued.push_back(sample.queuedMs);
                executed.push_back(sample.executedMs);
                waited.push_back(static_cast<double>(sample.waitedFrames));
            }
            WriteDistributionRow(out, "command", "all", "queued",       "ms",     Describe(std::move(queued)));
            WriteDistributionRow(out, "command", "all", "executed",     "ms",     Describe(std::move(executed)));
            WriteDistributionRow(out, "command", "all", "waitedFrames", "frames", Describe(std::move(waited)));
        }

        // 프레임 시간 히스토그램. 백분위는 꼬리를 요약하지만 봉우리가 몇 개인지는
        // 숨긴다 — 포커스 유무로 프레임 시간이 두 봉우리가 되는지가 §7.1 예산에서
        // 실제로 중요한 질문이라 계급도 함께 낸다.
        std::fprintf(out, "\nkind\tbucket\tbin_lo_ms\tbin_hi_ms\tcount\n");
        for (const FrameBucket& bucket : kFrameBuckets)
        {
            const std::size_t edgeCount = sizeof(kHistogramEdgesMs) / sizeof(kHistogramEdgesMs[0]);
            std::vector<std::size_t> bins(edgeCount + 1, 0);
            for (const FrameSample& sample : samples.frames)
            {
                if (!bucket.select(sample.state)) continue;
                std::size_t index = edgeCount;
                for (std::size_t i = 0; i < edgeCount; ++i)
                {
                    if (sample.deltaMs < kHistogramEdgesMs[i]) { index = i; break; }
                }
                ++bins[index];
            }
            for (std::size_t i = 0; i < bins.size(); ++i)
            {
                const double lo = (0 == i) ? 0.0 : kHistogramEdgesMs[i - 1];
                const double hi = (i < edgeCount) ? kHistogramEdgesMs[i] : -1.0;   // -1 = 상한 없음
                std::fprintf(out, "frame-hist\t%s\t%.1f\t%.1f\t%zu\n", bucket.name, lo, hi, bins[i]);
            }
        }

        // 명령별 원표본. 개수가 적고(짧은 시나리오), 어느 명령이 꼬리를 만드는지
        // 요약만으로는 못 본다.
        std::fprintf(out, "\nkind\tsequence\tcommand\tqueued_ms\twaited_frames\texecuted_ms\n");
        for (std::size_t i = 0; i < samples.commands.size(); ++i)
        {
            const CommandSample& sample = samples.commands[i];
            std::fprintf(out, "command-sample\t%zu\t%s\t%.3f\t%llu\t%.3f\n",
                         i, sample.name.c_str(), sample.queuedMs,
                         static_cast<unsigned long long>(sample.waitedFrames), sample.executedMs);
        }

        return CloseChecked(out);
    }
}
