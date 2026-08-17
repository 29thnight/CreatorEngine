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

class ComponentFactory;
namespace Meta
{
    // TypeCaster(any/void* 캐스터)는 구조 리뷰(8-17)에서 삭제 — 레거시
    // 직렬화 워크·인스펙터 체인이 은퇴하며 ToVoidPtr/MakeAnyFromRaw 소비가
    // 0건이 됐는데 등록만 맵을 채우고 있었다(좀비).

    class Registry : public DLLCore::Singleton<Registry>
    {
    private:
        Registry() = default;
        ~Registry() = default;
        friend DLLCore::Singleton<Registry>;
        friend class ::ComponentFactory;
    public:
        // 구조 리뷰(8-17): 값 복사 2벌 -> 포인터 인덱스. 정본은 adapt<T>()의
        // 함수-로컬 static Type(프로그램 수명)이라 참조 수명이 보장된다 —
        // 사본 3벌(adapt+이름맵+해시맵)이 1벌로 줄고, 두 맵의 탈동기화
        // 여지도 원천 소멸한다.
        void Register(const std::string& name, const Type& type)
        {
            if (map.find(name) == map.end())
            {
                map[name] = &type;
            }

            if (hashMap.find(type.typeID) == hashMap.end())
            {
                hashMap[type.typeID] = &type;
            }
        }

        // ScriptRegister/UnRegister(스크립트 핫리로드용 갱신·해제)는 CT2에서
        // 삭제 — 호출처 0건. 덕분에 이름 맵·해시 맵의 탈동기화 창구(분석 F-4)도
        // 등록 단일 경로로 좁혀졌다.

        const Type* Find(const std::string& name)
        {
            auto it = map.find(name);
            return it != map.end() ? it->second : nullptr;
        }

        const Type* Find(size_t typeID)
        {
            auto it = hashMap.find(typeID);
            return it != hashMap.end() ? it->second : nullptr;
        }

        // 등록 타입 전수 열람(PHASE 18 CT0 — reflect.golden). 복사본을 돌려주는
        // 이유: 호출자는 정렬해 쓰는데, 맵 내부 참조를 내주면 순회 중 등록/해제와
        // 겹칠 때의 안전을 호출자가 증명해야 한다.
        std::vector<std::string> GetAllTypeNames() const
        {
            std::vector<std::string> names;
            names.reserve(map.size());
            for (const auto& [name, type] : map)
            {
                names.push_back(name);
            }
            return names;
        }

    private:
        std::unordered_map<std::string, const Type*> map;
        std::unordered_map<size_t, const Type*> hashMap;
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