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

#include <mutex>
#include <set>

// 유니티 빌드라 익명 네임스페이스를 쓰지 않는다 — 격리 단위가 파일이 아니라
// 묶음이다. 이름 있는 네임스페이스로 가둔다.
namespace TypeTrait::guid_registry_impl
{
    struct registry
    {
        std::mutex            lock;
        std::set<HashedGuid>  guids;
    };

    // ★ 일부러 해제하지 않는다. EraseGUID 는 Object·Entity 가 사라질 때 불리는데,
    //   그 파괴가 프로세스 종료의 정적 소멸 구간에 걸치면 먼저 소멸한 표를 만지게
    //   된다. ce::profiler() 와 같은 판단이다 — 종료 직전 한 번의 작은 누수가
    //   소멸 순서에 기대는 것보다 낫다. 함수 지역 static 이라 첫 호출에서 서고,
    //   어느 TU 의 정적 초기화가 먼저 부르든 그때 표가 선다.
    registry& get()
    {
        static registry* instance = new registry();
        return *instance;
    }
}

namespace TypeTrait
{
    void GUIDCreator::InsertGUID(HashedGuid guid)
    {
        guid_registry_impl::registry& reg = guid_registry_impl::get();
        std::lock_guard<std::mutex> guard(reg.lock);
        reg.guids.insert(guid);
    }

    void GUIDCreator::EraseGUID(HashedGuid guid)
    {
        guid_registry_impl::registry& reg = guid_registry_impl::get();
        std::lock_guard<std::mutex> guard(reg.lock);
        reg.guids.erase(guid);
    }

    HashedGuid GUIDCreator::MakeGUID()
    {
        guid_registry_impl::registry& reg = guid_registry_impl::get();
        std::lock_guard<std::mutex> guard(reg.lock);

        // 확인과 삽입이 같은 잠금 안에 있어야 한다. 둘 사이가 열려 있으면 두 워커가
        // 같은 해시를 "비어 있다" 고 보고 나란히 가져간다.
        HashedGuid hash = ConvertGUIDToHash(GenerateGUID());
        while (reg.guids.find(hash) != reg.guids.end())
        {
            hash = ConvertGUIDToHash(GenerateGUID());
        }
        reg.guids.insert(hash);
        return hash;
    }

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
