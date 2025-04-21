#pragma once
#include "Core.Minimal.h"
#include "IObject.h"
#include "TypeTrait.h"
#include "HashingString.h"
#include "Object.generated.h"

class RenderScene;
class SceneManager;
class Object : public IObject
{
public:
   ReflectObject
    [[Serializable]]
    Object() = default;
    virtual ~Object() = default;

public:
    Object(const std::string_view& name) : m_name(name.data()) {}
	Object(const std::string_view& name, size_t instanceID) : m_name(name.data()), m_instanceID(instanceID) 
    {
		TypeTrait::GUIDCreator::InsertGUID(m_instanceID);
    }
    Object(const Object&) = delete;
    Object(Object&&) = delete;

public:
    size_t GetInstanceID() const override final { return m_instanceID.m_ID_Data; }
	HashedGuid GetTypeID() const override final { return m_typeID; }
    std::string ToString() const override final { return m_name.ToString(); }
    HashingString GetHashedName() const { return m_name; }

    void Destroy();
    bool IsDestroyMark() const { return m_destroyMark.load(); }
    bool IsDontDestroyOnLoad() const { return m_dontDestroyOnLoad.load(); }

    void SetDontDestroyOnLoad(Object* objPtr);

    //ReflectionField(Object)
    //{
    //    PropertyField
    //    ({
    //        meta_property(m_name)
    //        meta_property(m_instanceID)
    //    });

    //    FieldEnd(Object, PropertyOnly)
    //}

protected:
    friend class SceneManager;
    friend class RenderScene;
	HashedGuid        m_typeID{ TypeTrait::GUIDCreator::GetTypeID<Object>() };
    [[Property]]
    HashingString     m_name{ "Object" };
    [[Property]]
    HashedGuid        m_instanceID{ TypeTrait::GUIDCreator::MakeGUID() };
    std::atomic<bool> m_destroyMark{ false };
    std::atomic<bool> m_dontDestroyOnLoad{ false };
};
