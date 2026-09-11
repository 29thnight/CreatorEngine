// 헤더 인라인 Meyers 싱글턴을 여기로 내렸다 (PlayerModuleBoundaryAnalysis Step 0).
//
// 왜: `static T& Get() {{ static T instance; return instance; }}` 를 헤더에 두면
// include한 TU마다 인스턴스가 생긴다. 지금은 전 모듈이 StaticLibrary라 COMDAT
// folding이 하나로 접어 주지만, 그 folding은 **하나의 PE 안에서만** 일어난다.
// 엔진을 DLL로 떼는 순간(축 B) exe 쪽 사본과 DLL 쪽 사본이 갈려, 크래시도
// 로그도 없이 서로 다른 상태를 보는 결함이 된다.
//
//   근거: docs/analysis/PlayerModuleBoundaryAnalysis.md §4.2·§10.4
//   게이트: Tools/regression/verify-header-inline-singleton.ps1

#include "TypeTrait.h"

namespace TypeTrait
{
    std::unordered_map<HashedGuid, uint32_t>& ComponentTypeIndex::Table()
    {
        static std::unordered_map<HashedGuid, uint32_t> table;
        return table;
    }

    std::vector<std::pair<std::string, Uuid::Uuid16>>& ComponentUUIDRegistry::Entries()
    {
        static std::vector<std::pair<std::string, Uuid::Uuid16>> entries;
        return entries;
    }
}
