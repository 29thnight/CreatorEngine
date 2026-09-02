#pragma once
// 소문자 16진 표기 — 신원 계층(epoch seed·authoring key·fingerprint)의 유일한 바이트
// 표기다. 대문자·접두 `0x`·공백은 받지 않는다(표기가 둘이면 언젠가 비교가 어긋난다).

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace assets
{
    inline std::string ToLowerHex(std::span<const std::uint8_t> bytes)
    {
        static constexpr char kHex[] = "0123456789abcdef";
        std::string out;
        out.reserve(bytes.size() * 2u);
        for (std::uint8_t b : bytes)
        {
            out.push_back(kHex[b >> 4]);
            out.push_back(kHex[b & 0x0Fu]);
        }
        return out;
    }

    // 정확히 expectedBytes 바이트(0이면 길이 무관)의 소문자 16진만 받는다.
    [[nodiscard]] inline bool TryParseLowerHex(std::string_view text,
        std::vector<std::uint8_t>& out, std::size_t expectedBytes = 0) noexcept
    {
        out.clear();
        if (text.empty() || (text.size() % 2u) != 0u) return false;
        if (expectedBytes != 0u && text.size() != expectedBytes * 2u) return false;
        out.reserve(text.size() / 2u);
        auto nibble = [](char c) noexcept -> int
        {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            return -1;
        };
        for (std::size_t i = 0; i < text.size(); i += 2u)
        {
            const int hi = nibble(text[i]);
            const int lo = nibble(text[i + 1]);
            if (hi < 0 || lo < 0)
            {
                out.clear();
                return false;
            }
            out.push_back(static_cast<std::uint8_t>((hi << 4) | lo));
        }
        return true;
    }
}
