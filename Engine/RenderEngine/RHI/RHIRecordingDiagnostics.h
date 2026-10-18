#pragma once
#include "JobGraph.h"
#include "RHIRecordingTaskGraph.h"
#include "../../Utility_Framework/StandardLibrary.h"

struct RHIRecordingUnitTiming
{
    uint32_t pass{0}, target{0}, wave{0}, slice{0}, sliceCount{0};
    uint64_t beginNanoseconds{0}, endNanoseconds{0}, estimatedWork{0};
    uint64_t callbackNanoseconds{0}, callbackInvocations{0};
    std::string name;
};

struct RHIRecordingTaskIdentity
{
    uint32_t target{0}, wave{0};
};

// 완료한 CPU 기록의 값만 보관한다. 이름은 소유 문자열이며 callback/encoder/GPU 소유자를 담지 않는다.
struct RHIRecordingDiagnostics
{
    uint64_t frameId{0};
    bool timingEnabled{false}, dependencyGraph{false}, batchSubmit{false};
    bool unitTimingsTruncated{false}, jobTimingsTruncated{false};
    uint64_t dispatchBeginNanoseconds{0}, waitBeginNanoseconds{0}, returnNanoseconds{0};
    std::shared_ptr<job_graph_diagnostics> jobs;
    std::vector<RHIRecordingTaskIdentity> tasks;
    std::vector<RHIRecordingUnitTiming> units;
    RHIRecordingTaskTopology topology;
};

struct RHIRecordingPolicy
{
    bool batchSubmit{true}, timingEnabled{false};

    static uint32_t ReadBounded(const char* name, uint32_t fallback, uint32_t maximum)
    {
        // 값은 프로세스에서 한 번 읽는다. 부호·공백·쓰레기 접미사는 묵인하지 않는다.
#if defined(_WIN32)
        std::unique_ptr<char, decltype(&std::free)> owned{nullptr, &std::free};
        char* environment = nullptr;
        size_t length = 0;
        if (_dupenv_s(&environment, &length, name) != 0)
        {
            return fallback;
        }
        owned.reset(environment);
        const char* text = owned.get();
#else
        const char* text = std::getenv(name);
#endif
        if (!text || !*text)
        {
            return fallback;
        }
        uint32_t value = 0;
        for (const char* next = text; *next; ++next)
        {
            if (*next < '0' || *next > '9')
            {
                return fallback;
            }
            const uint32_t digit = static_cast<uint32_t>(*next - '0');
            if (digit > maximum || value > (maximum - digit) / 10)
            {
                return fallback;
            }
            value = value * 10 + digit;
        }
        return value;
    }

    static const RHIRecordingPolicy& Get()
    {
        static const RHIRecordingPolicy policy = []
        {
            RHIRecordingPolicy value;
            value.batchSubmit = ReadBounded("CREATOR_RECORDING_BATCH_SUBMIT", 1, 1) != 0;
            value.timingEnabled = ReadBounded("CREATOR_RECORDING_JOB_TRACE", 0, 1) != 0;
            return value;
        }();
        return policy;
    }
};
