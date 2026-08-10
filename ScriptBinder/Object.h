#pragma once
#include "Core.Minimal.h"
#include "IObject.h"
#include "TypeTrait.h"
#include "HashingString.h"
#include "Object.generated.h"

class RenderScene;
class SceneManager;
class Object : public IObject, public Managed::HeapObject
{
private:
    friend class SceneManager;
    friend class RenderScene;
    friend class Prefab;
    friend class PrefabUtility;
public:
   ReflectObject
    [[Serializable]]
    Object() = default;
    virtual ~Object() = default;

public:
    Object(std::string_view name) : m_name(name.data()) {}
	Object(std::string_view name, size_t instanceID) : m_name(name.data()), m_instanceID(instanceID) 
    {
		TypeTrait::GUIDCreator::InsertGUID(m_instanceID);
    }

public:
    size_t GetInstanceID() const override final { return m_instanceID.m_ID_Data; }
    void MakeInstanceID() { m_instanceID = make_guid(); }
	HashedGuid GetTypeID() const override final { return m_typeID; }
    std::string ToString() const override final { return m_name.ToString(); }
    HashingString GetHashedName() const { return m_name; }

    virtual void Destroy();
    bool IsDestroyMark() const { return m_destroyMark; }
    bool IsDontDestroyOnLoad() const { return m_dontDestroyOnLoad; }

    bool IsEnabled() const { return m_isEnabled; }
	virtual void SetEnabled(bool able) { m_isEnabled = able; }

    static void Destroy(Object* objPtr);
    static void SetDontDestroyOnLoad(Object* objPtr);
    static Object* Instantiate(const Object* original, std::string_view newName);

public:
    [[Property]]
    HashingString           m_name{ "Object" };
protected:
	HashedGuid              m_typeID{ type_guid(Object) };
    [[Property]]
    HashedGuid              m_instanceID{ make_guid() };
	bool                    m_destroyMark{ false };
	bool                    m_dontDestroyOnLoad{ false };

    // SetEnabled를 거치지 않으면 OnEnable/OnDisable이 호출되지 않는다 —
    // 훅이 전이 시점에 불리게 바뀐 뒤로(PHASE 9-2) 이 필드를 밖에서 직접 쓰는 것은
    // 곧 생명주기를 건너뛰는 것이다. 인스펙터 체크박스가 실제로 그러고 있었다.
    [[Property]]
    bool                    m_isEnabled{ true };
};
