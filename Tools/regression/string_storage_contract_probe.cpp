#include "../../Engine/Utility_Framework/Foundation/StandardLibrary.h"

int RunStringStorageContract()
{
    int failures = 0;
    const auto check = [&](bool value, const char* contract)
    {
        if (!value)
        {
            std::fprintf(stderr, "[STRING STORAGE] FAIL: %s\n", contract);
            ++failures;
        }
    };

    std::string name;
    check(name.empty(), "empty names remain representable");
    const std::string backing = "EnemyTail";
    const std::string_view partial(backing.data(), 5);
    name = partial;
    check(name == "Enemy" && name != backing, "partial views preserve length");
    check(name == partial, "lookup compares a view without an owning temporary");
    const std::string_view embedded("a\0b", 3);
    name = embedded;
    check(name.size() == 3 && name == embedded, "embedded NUL preserves bytes");
    name = "\xeb\xa3\xa8\xed\x8a\xb8";
    const auto copy = name;
    name.assign(1024, 'x');
    check(copy.size() == 6 && name.size() == 1024, "owned names survive mutation and growth");
    std::unordered_map<std::string, int> tags;
    tags.emplace(copy, 7);
    check(tags.at(copy) == 7, "standard owned string keys");
    std::printf("[STRING STORAGE] failures=%d\n", failures);
    return failures == 0 ? 0 : 1;
}
