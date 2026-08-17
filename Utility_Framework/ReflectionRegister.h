#pragma once
// CT3: ClassProperty.h·yaml-cpp(본문 사용 0회 — 죽은 include)와
// MetaStateCommand.h·<stack>(UndoManager 전속 → ReflectionUndo.h로 이관)을
// 잘랐다. 이 헤더는 등록 코어(Registry·FactoryRegistry)만 남는다.
#include "ReflectionType.h"
#include "ManagedHeapObject.h"
#include "DLLAcrossSingleton.h"
#include <functional>
#include <any>
#include <typeindex>
#include <string_view>

class ComponentFactory;
namespace Meta
{
    // TypeCaster(any/void* 캐스터)는 구조 리뷰(8-17)에서 삭제 — 레거시
    // 직렬화 워크·인스펙터 체인이 은퇴하며 ToVoidPtr/MakeAnyFromRaw 소비가
    // 0건이 됐는데 등록만 맵을 채우고 있었다(좀비).

    class Registry : public DLLCore::Singleton<Registry>
    {
    private:
        Registry()
        {
            // 등록 규모(본대 76 + 중첩)를 아는 일회성 초기화 — 재해시 없이 채운다.
            nameIndex.reserve(128);
            idIndex.reserve(128);
        }
        ~Registry() = default;
        friend DLLCore::Singleton<Registry>;
        friend class ::ComponentFactory;
    public:
        // CT11-b 효율화 (구조 리뷰 8-17의 포인터 인덱스 위에서):
        //   ① 이름 키의 std::string 사본(타입당 1벌) 소멸 — 정본 Type::name
        //      (adapt의 함수-로컬 static, 프로그램 수명)을 가리키는 string_view.
        //      수명 계약: 등록된 Type은 이동/파괴되지 않는다.
        //   ② 조회가 std::string을 만들지 않는다 — Find(string_view)로 씬 로드의
        //      이름 판별 경로(ExtractTypeFromYAML)가 조회당 할당 0회가 된다.
        //   ③ name 파라미터 제거 — type.name과 항상 동일했던 중복 인자.
        //   ④ find+insert 2회 해시 → try_emplace 1회(선등록 우선 의미 동일).
        void Register(const Type& type)
        {
            nameIndex.try_emplace(std::string_view{ type.name }, &type);
            idIndex.try_emplace(type.typeID.m_ID_Data, &type);
        }

        // ScriptRegister/UnRegister(스크립트 핫리로드용 갱신·해제)는 CT2에서
        // 삭제 — 호출처 0건. 덕분에 이름 맵·해시 맵의 탈동기화 창구(분석 F-4)도
        // 등록 단일 경로로 좁혀졌다.

        const Type* Find(std::string_view name) const
        {
            auto it = nameIndex.find(name);
            return it != nameIndex.end() ? it->second : nullptr;
        }

        const Type* Find(size_t typeID) const
        {
            auto it = idIndex.find(typeID);
            return it != idIndex.end() ? it->second : nullptr;
        }

        // 등록 타입 전수 열람(PHASE 18 CT0 — reflect.golden). 복사본을 돌려주는
        // 이유는 종전과 동일(호출자가 정렬) — 원소가 정본 Type::name을 가리키는
        // view라 문자열 사본은 더 이상 없다.
        std::vector<std::string_view> GetAllTypeNames() const
        {
            std::vector<std::string_view> names;
            names.reserve(nameIndex.size());
            for (const auto& [name, type] : nameIndex)
            {
                names.push_back(name);
            }
            return names;
        }

    private:
        std::unordered_map<std::string_view, const Type*> nameIndex;
        std::unordered_map<size_t, const Type*> idIndex;
    };

    inline auto MetaDataRegistry = Registry::GetInstance();

    // EnumRegistry(이름 키 enum 표)는 열거형 점검(8-17)에서 삭제 — 소비 3곳
    // (콘솔 enum 설정·DrawEnumProperty·MeshRenderer 헬퍼) 전부가 Property를
    // 경유하고, MakeEnumPropertyImpl이 EnumT를 정적으로 알므로 Property가
    // create_enum_type<EnumT>() 정본을 직접 든다(Property::enumType). 이름 키
    // 매칭(ToString vs magic_enum 표기)의 불일치 실패 여지도 함께 소멸.
    // 등록자였던 AUTO_REGISTER_ENUM 매크로·EnumAutoRegistrar도 같이 은퇴.

    // FactoryRegistry(typeID 해시 조회 싱글턴)는 CT11에서 Type 필드로 접혔다 —
    // 소비자(AddComponent(Type&)·Object::Instantiate·골든) 전원이 이미 Type*를
    // 쥐고 있어 조회 자체가 무의미했다. 팩토리는 adapt<T>()가 Type::create/
    // createShared에 직접 채운다. 남은 등록 싱글턴은 Registry 하나다.

    // UndoManager·UndoCommandManager는 CT3에서 ReflectionUndo.h로 이관 —
    // 등록 코어와 무관한 에디터 계층이 MetaStateCommand·<stack>을 여기 소비자
    // 전원에게 실어 나르고 있었다.

    // ClassAutoRegistrar는 CT2에서 삭제 — 인스턴스화 0건. 클래스 등록의 정본은
    // 등록 정본(RegisterReflectManual.h)의 Meta::Register<T>() 직접 호출이다.

    // Undo 계층의 init/final은 ReflectionUndo.h의 UndoSystemInitialize/Finalize로
    // 이관 — 호출처는 EngineBootstrap.h 한 곳이라 같이 고쳤다.
    inline void RegisterClassInitalize()
    {
        Registry::GetInstance();
    }

    inline void RegisterClassFinalize()
    {
        Registry::Destroy();
    }
}