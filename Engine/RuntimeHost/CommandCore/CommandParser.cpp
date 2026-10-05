#include "CommandParser.h"

#include <atomic>

namespace CommandCore
{
    namespace
    {
        bool IsSpace(char c) noexcept
        {
            return ' ' == c || '\t' == c || '\r' == c || '\n' == c;
        }

        std::atomic<uint64_t>& LegacyJoinCounter() noexcept
        {
            static std::atomic<uint64_t> counter{ 0 };
            return counter;
        }
    }

    TokenizeResult Tokenize(std::string_view line)
    {
        TokenizeResult result;

        std::string token;
        bool inQuotes = false;

        // 빈 따옴표("")도 '값을 비웠다'는 뜻이라 토큰으로 남긴다 —
        // 길이만 보면 그것을 버리게 된다.
        bool hasToken = false;

        const auto flush = [&result, &token, &hasToken]
        {
            if (!hasToken) return;
            result.tokens.push_back(token);
            token.clear();
            hasToken = false;
        };

        for (std::size_t i = 0; i < line.size(); ++i)
        {
            const char c = line[i];

            if (inQuotes)
            {
                // 따옴표 안에서만 escape 를 본다. 그것도 두 가지뿐이다.
                //
                // 나머지 backslash 를 escape 로 소비하면 Windows 경로가 죽는다 —
                // `"C:\Program Files\x"` 의 `\P`·`\x` 는 그대로 살아야 한다.
                if ('\\' == c && (i + 1) < line.size())
                {
                    const char next = line[i + 1];
                    if ('"' == next || '\\' == next)
                    {
                        token.push_back(next);
                        hasToken = true;
                        ++i;
                        continue;
                    }
                }
                if ('"' == c) { inQuotes = false; continue; }
                token.push_back(c);
                hasToken = true;
                continue;
            }

            if ('"' == c)
            {
                inQuotes = true;
                hasToken = true;   // 빈 따옴표도 토큰이다
                continue;
            }
            if (IsSpace(c)) { flush(); continue; }

            token.push_back(c);
            hasToken = true;
        }

        if (inQuotes)
        {
            // ★ 예전에는 여기서 그냥 flush 하고 통과시켰다.
            //
            //   `object.parent "Big Boss` 가 토큰 하나로 조용히 넘어가 "이름을
            //   못 찾음"으로 끝났다. 문법 오류와 "그런 이름이 없다"는 다른
            //   사건이고, 섞이면 사용자가 따옴표를 의심하지 않는다.
            result.ok           = false;
            result.errorCode    = "parse.unclosed_quote";
            result.errorMessage = "닫히지 않은 따옴표";
            return result;
        }

        flush();
        return result;
    }

    namespace
    {
        /// [begin, end) 토큰을 공백 하나로 잇는다. 둘 이상이면 legacy 로 센다.
        std::string JoinRange(const std::vector<std::string>& parts,
                              std::size_t begin, std::size_t end, bool* usedLegacyJoin)
        {
            std::string joined;
            if (begin >= end) return joined;

            if ((end - begin) > 1)
            {
                if (nullptr != usedLegacyJoin) *usedLegacyJoin = true;
                LegacyJoinCounter().fetch_add(1, std::memory_order_relaxed);
            }

            for (std::size_t i = begin; i < end; ++i)
            {
                if (!joined.empty()) joined.push_back(' ');
                joined += parts[i];
            }
            return joined;
        }
    }

    std::string JoinFrom(const std::vector<std::string>& parts, std::size_t index)
    {
        return JoinRange(parts, index, parts.size(), nullptr);
    }

    TrailingNameSplit SplitTrailingName(const std::vector<std::string>& parts,
                                        std::size_t                     firstIndex,
                                        std::size_t                     trailingCount)
    {
        TrailingNameSplit split;
        if (0 == trailingCount) return split;

        // 앞 이름과 뒤 이름들이 둘 다 있어야 한다.
        if (parts.size() < firstIndex + trailingCount + 1) return split;

        split.ok       = true;
        split.trailing = parts.back();

        // 예전에는 원문을 rfind 로 잘랐고 그래서 따옴표가 섞여 들어왔다.
        // 토큰을 잇는 방식은 그 손실이 없는 대신 공백 연속이 하나로 접힌다 —
        // 이름에서 공백 연속이 뜻을 갖는 경우는 없으므로 받아들인다.
        split.leading = JoinRange(parts, firstIndex, parts.size() - trailingCount,
                                  &split.usedLegacyJoin);
        return split;
    }

    uint64_t LegacyJoinUseCount() noexcept
    {
        return LegacyJoinCounter().load(std::memory_order_relaxed);
    }

    void ResetLegacyJoinUseCount() noexcept
    {
        LegacyJoinCounter().store(0, std::memory_order_relaxed);
    }

    std::vector<std::string> CommandInvocation::ToLegacyParts() const
    {
        std::vector<std::string> parts;
        parts.reserve(arguments.size() + 1);
        parts.push_back(commandId);
        for (const std::string& argument : arguments) parts.push_back(argument);
        return parts;
    }
}
