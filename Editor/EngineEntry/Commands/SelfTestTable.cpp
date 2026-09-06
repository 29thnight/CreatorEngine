#include "SelfTestTable.h"

#include <algorithm>
#include <cstdio>
#include <map>

namespace ConsoleCmd
{
    namespace
    {
        // ★ 정렬 컨테이너를 쓴다. `SelfTestNames()` 가 안정된 순서를 내야
        //   게이트가 목록을 골든으로 대조할 수 있다 — 등록 순서(TU 초기화
        //   순서)에 기대면 그 골든이 링커 사정으로 흔들린다.
        std::map<std::string, SelfTestHandler>& Table()
        {
            static std::map<std::string, SelfTestHandler> table;
            return table;
        }
    }

    void RegisterSelfTest(const char* name, SelfTestHandler fn)
    {
        if (nullptr == name || nullptr == fn) return;

        // 같은 이름을 두 번 걸면 **거부하고 알린다.** 조용히 덮으면 어느 검사가
        // 도는지 아무도 모른다 — 명령 registry 가 같은 이유로 중복 이름을
        // 인쇄하고 거부한다.
        if (!Table().emplace(name, fn).second)
        {
            std::printf("[CLI] selftest 이름 중복 등록: %s\n", name);
        }
    }

    std::vector<std::string> SelfTestNames()
    {
        std::vector<std::string> names;
        names.reserve(Table().size());
        for (const auto& entry : Table()) names.push_back(entry.first);
        return names;
    }

    SelfTestHandler FindSelfTest(const std::string& name)
    {
        const auto found = Table().find(name);
        return (Table().end() == found) ? nullptr : found->second;
    }
}
