#include "RHIValidationLedger.h"

#include <mutex>

namespace rhi::validation::detail
{
    namespace
    {
        std::mutex ledgerMutex;
        ledger_view ledger;
    }
}

void rhi::validation::declare_layer(bool enabled, const char* mode)
{
    using namespace detail;
    std::lock_guard lock(ledgerMutex);
    if (enabled)
    {
        ledger.layerEnabled = true;
        ++ledger.devices;
    }
    // 모드 문자열은 **켠 선언**의 것만 남긴다. 꺼진 디바이스가 뒤에 서면서
    // "off" 로 덮으면, 켠 채 돌던 실행이 끈 실행처럼 보인다.
    if (nullptr != mode && enabled) ledger.mode = mode;
}

void rhi::validation::record_drain(std::uint32_t problems, std::uint32_t messages,
    const std::string& text)
{
    using namespace detail;
    std::lock_guard lock(ledgerMutex);
    ++ledger.drains;
    ledger.messages += messages;
    ledger.problems += problems;

    if (text.empty()) return;

    // 전문은 줄 단위로 온다(`[ERROR] ...\n[WARNING] ...\n`). 줄로 쪼개 담아야
    // 밖에서 한 건씩 셀 수 있다.
    std::size_t at = 0;
    while (at < text.size())
    {
        const std::size_t end = text.find('\n', at);
        const std::size_t stop = (std::string::npos == end) ? text.size() : end;
        if (stop > at)
        {
            if (ledger.retained.size() < kRetainedMessages)
                ledger.retained.emplace_back(text.substr(at, stop - at));
            else
                ++ledger.droppedMessages;
        }
        if (std::string::npos == end) break;
        at = end + 1;
    }
}

rhi::validation::ledger_view rhi::validation::read()
{
    using namespace detail;
    std::lock_guard lock(ledgerMutex);
    return ledger;
}

void rhi::validation::reset_counts()
{
    using namespace detail;
    std::lock_guard lock(ledgerMutex);
    ledger.drains = 0;
    ledger.messages = 0;
    ledger.problems = 0;
    ledger.retained.clear();
    ledger.droppedMessages = 0;
}
