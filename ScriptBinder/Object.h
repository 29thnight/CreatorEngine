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
    Object(const Object&) = default;
    Object(Object&&) noexcept = default;

public:
    size_t GetInstanceID() const override final { return m_instanceID.m_ID_Data; }
	HashedGuid GetTypeID() const override final { return m_typeID; }
    std::string ToString() const override final { return m_name.ToString(); }
    HashingString GetHashedName() const { return m_name; }

    void Destroy();
    bool IsDestroyMark() const { return m_destroyMark; }
	void SetDestroyMark() { m_destroyMark = true; }
    bool IsDontDestroyOnLoad() const { return m_dontDestroyOnLoad; }

	bool IsEnabled() const { return m_isEnabled; }
	void SetEnabled(bool able) { m_isEnabled = able; }

	static void Destroy(Object* objPtr);
    static void SetDontDestroyOnLoad(Object* objPtr);
    static Object* Instantiate(const Object* original, const std::string_view& newName);


public:
    [[Property]]
    HashingString     m_name{ "Object" };
    [[Property]]
	bool m_isEnabled{ true };

protected:
    friend class SceneManager;
    friend class RenderScene;
	HashedGuid        m_typeID{ type_guid(Object) };
    [[Property]]
    HashedGuid        m_instanceID{ make_guid() };
	bool m_destroyMark{ false };
	bool m_dontDestroyOnLoad{ false };
};
