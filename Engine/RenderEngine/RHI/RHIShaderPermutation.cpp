#include "RHIShaderPermutation.h"

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace
{
    bool IsIdentifier(std::string_view value)
    {
        if (value.empty()) return false;
        const auto isAlpha = [](char character)
        {
            return (character >= 'A' && character <= 'Z')
                || (character >= 'a' && character <= 'z');
        };
        const auto isDigit = [](char character)
        {
            return character >= '0' && character <= '9';
        };
        if ('_' != value.front() && !isAlpha(value.front())) return false;
        for (const char character : value.substr(1))
        {
            if ('_' != character && !isAlpha(character) && !isDigit(character))
                return false;
        }
        return true;
    }

    bool IsDefinitionValue(std::string_view value)
    {
        if (value.empty()) return false;
        return std::string_view::npos == value.find('\0')
            && std::string_view::npos == value.find('\r')
            && std::string_view::npos == value.find('\n');
    }

    void AddByte(RHIShaderPermutationKey& key, std::uint8_t byte)
    {
        key.lo = (key.lo ^ byte) * 1099511628211ull;
        key.hi = (key.hi ^ static_cast<std::uint8_t>(byte + 0x9du))
            * 14029467366897019727ull;
    }

    void AddSize(RHIShaderPermutationKey& key, std::size_t size)
    {
        const std::uint64_t fixed = static_cast<std::uint64_t>(size);
        for (std::uint32_t shift = 0; shift < 64; shift += 8)
            AddByte(key, static_cast<std::uint8_t>((fixed >> shift) & 0xffu));
    }

    void AddString(RHIShaderPermutationKey& key, std::string_view value)
    {
        AddSize(key, value.size());
        for (const char character : value)
            AddByte(key, static_cast<std::uint8_t>(character));
    }
}

std::string RHIShaderPermutationKey::Hex() const
{
    std::ostringstream stream;
    stream << std::hex << std::setfill('0')
        << std::setw(16) << lo << std::setw(16) << hi;
    return stream.str();
}

bool RHIShaderPermutation::Set(std::string_view name, std::string_view value,
    std::string& outError)
{
    if (!IsIdentifier(name))
    {
        outError = "셰이더 퍼뮤테이션 축 이름이 HLSL 식별자가 아니다: "
            + std::string(name);
        return false;
    }
    if (!IsDefinitionValue(value))
    {
        outError = "셰이더 퍼뮤테이션 값이 비었거나 줄바꿈을 포함한다: "
            + std::string(name);
        return false;
    }

    const auto found = std::lower_bound(m_entries.begin(), m_entries.end(), name,
        [](const Entry& entry, std::string_view candidate)
        {
            return entry.name.compare(candidate) < 0;
        });
    if (found != m_entries.end() && found->name == name)
    {
        outError = "셰이더 퍼뮤테이션 축이 중복됐다: " + std::string(name);
        return false;
    }
    if (m_entries.size() >= kMaxEntries)
    {
        outError = "셰이더 퍼뮤테이션 축 상한을 넘었다: "
            + std::to_string(kMaxEntries);
        return false;
    }

    m_entries.insert(found, Entry{ std::string(name), std::string(value) });
    return true;
}

RHIShaderPermutationKey RHIShaderPermutation::Key() const
{
    RHIShaderPermutationKey key{
        1469598103934665603ull,
        1099511628211ull ^ 0x9e3779b97f4a7c15ull,
    };
    AddSize(key, m_entries.size());
    for (const Entry& entry : m_entries)
    {
        AddString(key, entry.name);
        AddString(key, entry.value);
    }
    return key;
}
