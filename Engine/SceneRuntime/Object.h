#pragma once
#include "Reflection.hpp" // CT3: was transitive via Core.Minimal.h
#include "MetaPolymorphic.h"
#include "Core.Minimal.h"
#include "IObject.h"
#include "TypeTrait.h"
#include "HashingString.h"

namespace Meta { struct EditorObjectIdentity; }
class RenderScene;
class SceneManager;
class Object : public IObject, public meta::polymorphic
{
   public:
   static consteval auto reflect()
   {
       using Self = Object;
       return meta::schema<Self>(
           // 컴포넌트에서 이 값은 **타입 이름 사본**이다. 씬 파일이
           // `m_name: CameraComponent` 로 적어 두고 로드가 되읽는다. 타입을
           // 확정하는 것은 노드 키와 `m_typeUUID` 라 이 값을 고쳐도 로드는
           // 깨지지 않지만, 유일하게 옳은 값이 하나뿐인 필드가 편집 가능한
           // 글상자로 나와 있었다 — 고치면 그대로 디스크에 적혔다.
           //
           // 엔티티의 이름은 여전히 편집 대상이다. 다만 그 경로는 리플렉션이
           // 아니라 인스펙터 상단의 이름 칸이고, 그쪽은
           // `EditorObjectOperations::Rename` 을 거쳐 씬의 이름 유일성까지
           // 맞춘다. 리플렉션 경로에서 직접 쓰면 그 단계를 건너뛴다.
           meta::field<&Self::m_name>.with(meta::readonly(), meta::debugOnly()),
           // 인스턴스 식별자. `GUIDCreator` 의 전역 집합과 시스템 레지스트리
           // (`AnimationJob::m_animators` 등)의 키다. 손으로 고치면 등록은 옛
           // 키로 남고 해제는 새 키로 가서 레지스트리에 죽은 항목이 남는다.
           meta::field<&Self::m_instanceID>.with(meta::readonly(), meta::debugOnly()),
           // 전용 체크박스가 담당한다 — 인스펙터는 `SetEnabled` 를 거쳐야
           // `OnEnable`/`OnDisable` 이 보존된다. 리플렉션이 직접 그리면 그 훅을
           // 건너뛴다.
           meta::field<&Self::m_isEnabled>.with(meta::hidden()));
   }
private:
    friend struct Meta::EditorObjectIdentity;
    friend class SceneManager;
    friend class RenderScene;
    friend class Prefab;
    friend class PrefabUtility;
public:
    Object() = default;
    virtual ~Object() = default;

public:
    // 복사하면 instanceID까지 복사되어 GUID 레지스트리에 없는 유령이 생긴다.
    // 오브젝트는 값이 아니다 — 복제는 Instantiate가 새 ID를 발급하는 경로로만 한다.
    Object(const Object&) = delete;
    Object& operator=(const Object&) = delete;

    Object(std::string_view name) : m_name(name) {}
	Object(std::string_view name, size_t instanceID) : m_name(name), m_instanceID(instanceID)
    {
		TypeTrait::GUIDCreator::InsertGUID(m_instanceID);
    }

public:
    size_t GetInstanceID() const override final { return m_instanceID.m_ID_Data; }
    void MakeInstanceID() { TypeTrait::GUIDCreator::EraseGUID(m_instanceID); m_instanceID = make_guid(); }
	HashedGuid GetTypeID() const override final { return m_typeID; }
    std::string ToString() const override final { return m_name.ToString(); }
    const HashingString& GetHashedName() const noexcept { return m_name; }

    virtual void Destroy();
    bool IsDestroyMark() const { return m_destroyMark; }
    bool IsDontDestroyOnLoad() const { return m_dontDestroyOnLoad; }

    bool IsEnabled() const { return m_isEnabled; }
	virtual void SetEnabled(bool able) { m_isEnabled = able; }

    static void Destroy(Object* objPtr);
    static void SetDontDestroyOnLoad(Object* objPtr);
    static Object* Instantiate(const Object* original, std::string_view newName);

public:
    HashingString           m_name{ "Object" };
protected:
	HashedGuid              m_typeID{ type_guid(Object) };
    HashedGuid              m_instanceID{ make_guid() };
	bool                    m_destroyMark{ false };
	bool                    m_dontDestroyOnLoad{ false };

    // SetEnabled를 거치지 않으면 OnEnable/OnDisable이 호출되지 않는다 —
    // 훅이 전이 시점에 불리게 바뀐 뒤로(PHASE 9-2) 이 필드를 밖에서 직접 쓰는 것은
    // 곧 생명주기를 건너뛰는 것이다. 인스펙터 체크박스가 실제로 그러고 있었다.
    bool                    m_isEnabled{ true };
};
