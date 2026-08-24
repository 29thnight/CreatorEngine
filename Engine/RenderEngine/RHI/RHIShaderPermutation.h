#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

struct RHIShaderPermutationKey
{
    std::uint64_t lo{};
    std::uint64_t hi{};

    bool operator==(const RHIShaderPermutationKey&) const = default;
    std::string Hex() const;
};

// 셰이더 컴파일 변형의 정본. 이름순으로 보관하므로 호출자가 축을 추가한
// 순서와 무관하게 같은 Entry 목록과 PermutationKey를 만든다. 문자열 수명과
// 널 종료 sentinel을 호출자에게 맡기지 않는다.
class RHIShaderPermutation final
{
public:
    struct Entry
    {
        std::string name;
        std::string value;

        bool operator==(const Entry&) const = default;
    };

    static constexpr std::size_t kMaxEntries = 64;

    // HLSL 전처리 식별자 하나를 값에 연결한다. 같은 축을 두 번 넣는 것은
    // 조립 버그이므로 값이 같아도 거부한다.
    bool Set(std::string_view name, std::string_view value, std::string& outError);
    bool Enable(std::string_view name, std::string& outError)
    {
        return Set(name, "1", outError);
    }

    bool Empty() const { return m_entries.empty(); }
    std::size_t Size() const { return m_entries.size(); }
    const std::vector<Entry>& Entries() const { return m_entries; }
    RHIShaderPermutationKey Key() const;

private:
    std::vector<Entry> m_entries;
};
