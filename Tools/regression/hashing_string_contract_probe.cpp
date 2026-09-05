// PHASE 15 트랙 H — HashingString 계약 프로브.
//
// 이 프로브는 사본이 아니라 **제품 헤더 자체**를 include 한다. 참조 재구현을
// 세우면 정본과 같은 (틀린) 규약을 공유해 눈먼 초록이 나온다는 것이 이미
// 두 번 확인된 함정이다(encoder-bench / experiment.anim).
//
// 재는 축은 넷이다.
//
//   1. 캐시 불변식 — 문자열을 바꾸는 모든 경로(생성자 3종·대입 3종·SetString)가
//      끝난 뒤 m_hash가 항상 지금 문자열의 해시와 같은가. 이 타입의 존재 이유가
//      "해시를 한 번만 계산해 들고 있는 것"이므로, 둘이 어긋나는 순간 == 결과
//      전부가 신뢰할 수 없게 된다.
//
//   2. 부분 string_view 길이 — 널 종료가 없는 부분 뷰를 넘겨도 길이가 보존되는가.
//      호출부가 `sv.data()`를 const char*로 넘기던 UB(§1.8 H-b)가 되살아나면
//      길이 정보가 사라져 뷰 뒤쪽 문자열까지 딸려 들어온다.
//
//   3. 해시 컨테이너 키 계약 — std::hash 특수화와 ==가 unordered 컨테이너의
//      요구를 만족하는가(§1.8 H-e). 순서 컨테이너(<=>)도 같이 본다.
//
//   4. 접근자 형상 — data()/size()/ToString()/GetHash()가 const로 부를 수 있고
//      내부 버퍼의 쓰기 권한을 밖으로 주지 않는가(§1.8 H-g, H-a의 근본 원인).
//      이 축은 static_assert라 되돌리면 컴파일이 먼저 막힌다.
//
// 인스펙터 경로(§1.8 H-a)는 ImGui 컨텍스트가 필요해 여기서 실행하지 못한다.
// 그쪽은 verify-hashing-string.ps1의 정적 래칫이 본다 — 이 프로브가 못 잡는
// 변이라는 사실까지가 이 게이트의 증명 범위다.

#include "HashingString.h"

#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

int g_failures = 0;

void Check(bool condition, const char* contract)
{
    if (condition)
    {
        return;
    }

    std::fprintf(stderr, "[HASHING STRING CONTRACT] FAIL: %s\n", contract);
    ++g_failures;
}

std::size_t ReferenceHash(std::string_view text) noexcept
{
    return std::hash<std::string_view>{}(text);
}

// 축 1·2 공용. 어느 경로로 만들었든 끝난 상태는 하나여야 한다.
void CheckStoredState(const HashingString& subject, std::string_view expected,
    const char* path)
{
    char contract[256];

    std::snprintf(contract, sizeof(contract), "%s: ToString()이 저장한 문자열과 다르다", path);
    Check(subject.ToString() == expected, contract);

    std::snprintf(contract, sizeof(contract), "%s: size()가 원본 길이와 다르다", path);
    Check(subject.size() == expected.size(), contract);

    // 널 종료 + 길이 — 부분 뷰를 넘겼을 때 뒤쪽이 딸려 들어오면 여기서 갈린다.
    std::snprintf(contract, sizeof(contract), "%s: data()가 길이 밖까지 이어진다", path);
    Check(std::strlen(subject.data()) == expected.size(), contract);

    std::snprintf(contract, sizeof(contract), "%s: data() 내용이 원본과 다르다", path);
    Check(std::memcmp(subject.data(), expected.data(), expected.size()) == 0, contract);

    std::snprintf(contract, sizeof(contract), "%s: 캐시된 해시가 지금 문자열의 해시가 아니다", path);
    Check(subject.GetHash() == ReferenceHash(expected), contract);

    std::snprintf(contract, sizeof(contract), "%s: std::hash 특수화가 캐시된 해시를 내지 않는다", path);
    Check(std::hash<HashingString>{}(subject) == ReferenceHash(expected), contract);

    // 같은 내용으로 다시 만든 값과 동등해야 한다(경로 독립).
    const HashingString rebuilt{ std::string(expected) };
    std::snprintf(contract, sizeof(contract), "%s: 같은 내용의 다른 인스턴스와 다르다", path);
    Check(subject == rebuilt, contract);

    std::snprintf(contract, sizeof(contract), "%s: ==와 <=>의 판정이 어긋난다", path);
    Check((subject <=> rebuilt) == 0, contract);
}

// ── 축 1. 캐시 불변식 ──
void RunCacheInvariantAxis()
{
    CheckStoredState(HashingString("PlayerRoot"), "PlayerRoot", "생성자(const char*)");
    CheckStoredState(HashingString(std::string("PlayerRoot")), "PlayerRoot", "생성자(std::string)");
    CheckStoredState(HashingString(std::string_view("PlayerRoot")), "PlayerRoot",
        "생성자(string_view)");

    // 대입은 이미 다른 값을 들고 있던 인스턴스에 걸어야 의미가 있다. 해시 갱신을
    // 빠뜨린 대입 경로는 "생성은 맞고 대입만 틀린" 모양으로 남는다.
    HashingString subject("초기값");

    subject = "AssignedFromLiteral";
    CheckStoredState(subject, "AssignedFromLiteral", "대입(const char*)");

    subject = std::string("AssignedFromString");
    CheckStoredState(subject, "AssignedFromString", "대입(std::string)");

    subject = std::string_view("AssignedFromView");
    CheckStoredState(subject, "AssignedFromView", "대입(string_view)");

    subject.SetString("SetStringPath");
    CheckStoredState(subject, "SetStringPath", "SetString");

    // 복사·이동으로도 캐시가 함께 옮겨져야 한다.
    const HashingString copied = subject;
    CheckStoredState(copied, "SetStringPath", "복사 생성");

    HashingString movedFrom("MovedPayload");
    const HashingString moved = std::move(movedFrom);
    CheckStoredState(moved, "MovedPayload", "이동 생성");

    // 한글(멀티바이트)도 바이트 길이로 일관되게 다뤄야 한다.
    CheckStoredState(HashingString("플레이어_루트"), "플레이어_루트", "생성자(UTF-8 리터럴)");
}

// ── 축 2. 부분 string_view 길이 ──
void RunPartialViewAxis()
{
    // 널 종료가 없는 뷰다. `.data()`를 const char*로 넘기면 "EnemySpawnerRoot"
    // 전체가 들어와 길이 5가 16이 된다.
    const std::string backing = "EnemySpawnerRoot";
    const std::string_view partial(backing.data(), 5);

    CheckStoredState(HashingString(partial), "Enemy", "부분 뷰 생성자");

    HashingString subject("자리표시자");
    subject = partial;
    CheckStoredState(subject, "Enemy", "부분 뷰 대입");

    subject.SetString(partial);
    CheckStoredState(subject, "Enemy", "부분 뷰 SetString");

    // 문자열 가운데를 자른 뷰 — 시작 오프셋도 보존되는지.
    const std::string_view middle(backing.data() + 5, 7);
    CheckStoredState(HashingString(middle), "Spawner", "중간 부분 뷰");

    // 전체 문자열과 부분 뷰는 서로 달라야 한다(길이 소실의 직접 증상).
    Check(HashingString(partial) != HashingString(backing),
        "부분 뷰가 전체 문자열과 같은 값이 됐다(길이 소실)");
}

// ── 축 3. 해시 컨테이너 키 계약 ──
void RunHashContainerAxis()
{
    constexpr int keyCount = 512;

    std::unordered_map<HashingString, int> unordered;
    std::map<HashingString, int> ordered;
    std::vector<HashingString> keys;
    keys.reserve(keyCount);

    for (int index = 0; index < keyCount; ++index)
    {
        char name[64];
        std::snprintf(name, sizeof(name), "Entity_%04d", index);
        keys.emplace_back(name);
        unordered.emplace(keys.back(), index);
        ordered.emplace(keys.back(), index);
    }

    Check(unordered.size() == static_cast<std::size_t>(keyCount),
        "unordered_map이 서로 다른 키를 합쳤다");
    Check(ordered.size() == static_cast<std::size_t>(keyCount),
        "map이 서로 다른 키를 합쳤다");

    for (int index = 0; index < keyCount; ++index)
    {
        char name[64];
        std::snprintf(name, sizeof(name), "Entity_%04d", index);

        // 조회는 원본 인스턴스가 아니라 같은 내용으로 새로 만든 키로 한다.
        const HashingString lookup(name);

        const auto unorderedHit = unordered.find(lookup);
        Check(unorderedHit != unordered.end() && unorderedHit->second == index,
            "unordered_map 조회가 같은 내용의 키를 못 찾는다");

        const auto orderedHit = ordered.find(lookup);
        Check(orderedHit != ordered.end() && orderedHit->second == index,
            "map 조회가 같은 내용의 키를 못 찾는다");
    }

    Check(unordered.find(HashingString("Entity_9999")) == unordered.end(),
        "unordered_map이 없는 키를 찾았다");

    // rehash를 강제해도 계약이 유지되는가.
    unordered.rehash(4);
    Check(unordered.size() == static_cast<std::size_t>(keyCount),
        "rehash 후 unordered_map 크기가 달라졌다");
    Check(unordered.find(keys.front()) != unordered.end(),
        "rehash 후 첫 키를 잃었다");

    std::unordered_set<HashingString> unique;
    for (const HashingString& key : keys)
    {
        unique.insert(key);
        unique.insert(HashingString(key.ToString()));  // 같은 내용, 다른 인스턴스
    }
    Check(unique.size() == static_cast<std::size_t>(keyCount),
        "unordered_set이 같은 내용의 키를 중복으로 담았다");

    // 순서 컨테이너의 전순서 — 삼분성이 깨지면 map이 조용히 망가진다.
    for (int index = 1; index < keyCount; ++index)
    {
        const HashingString& lhs = keys[static_cast<std::size_t>(index - 1)];
        const HashingString& rhs = keys[static_cast<std::size_t>(index)];
        const bool trichotomy = (lhs < rhs) != (rhs < lhs) || lhs == rhs;
        Check(trichotomy, "<=>가 전순서를 이루지 않는다");
        Check((lhs == rhs) == ((lhs <=> rhs) == 0), "==와 <=>가 서로 다른 판정을 낸다");
        Check((lhs != rhs) == !(lhs == rhs), "!=가 ==의 부정이 아니다");
    }
}

// ── 축 4. 접근자 형상 ──
//
// const 인스턴스에서 부를 수 있어야 하고, 내부 버퍼의 쓰기 권한을 밖으로
// 주면 안 된다. 되돌리면 실행이 아니라 컴파일이 막힌다.
static_assert(std::is_same_v<
        decltype(std::declval<const HashingString&>().data()), const char*>,
    "data()는 const 인스턴스에서 const char*를 내야 한다");
static_assert(std::is_same_v<
        decltype(std::declval<const HashingString&>().size()), std::size_t>,
    "size()는 const 인스턴스에서 부를 수 있어야 한다");
static_assert(std::is_same_v<
        decltype(std::declval<const HashingString&>().ToString()), const std::string&>,
    "ToString()은 복사가 아니라 참조를 내야 한다");
static_assert(std::is_same_v<
        decltype(std::declval<const HashingString&>().GetHash()), std::size_t>,
    "GetHash()는 const 인스턴스에서 캐시된 해시를 내야 한다");

}  // namespace

int main()
{
    RunCacheInvariantAxis();
    RunPartialViewAxis();
    RunHashContainerAxis();

    if (g_failures != 0)
    {
        std::fprintf(stderr, "[HASHING STRING CONTRACT] %d개 단정 실패.\n", g_failures);
        return 1;
    }

    std::printf("[HASHING STRING CONTRACT] 모든 단정 통과.\n");
    return 0;
}
