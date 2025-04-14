#pragma once
#include "Core.Minimal.h"
#include "IObject.h"
#include "TypeTrait.h"
#include "HashingString.h"

class Object : public IObject, public Meta::IReflectable<Object>
{
public:
    Object() = default;
    virtual ~Object() = default;

public:
    Object(const std::string_view& name) : m_name(name.data()) {}
    Object(const Object&) = delete;
    Object(Object&&) = delete;

public:
    size_t GetInstanceID() const override final { return m_instanceID; }
    std::string ToString() const override final { return m_name.ToString(); }
    HashingString GetHashedName() const { return m_name; }

    void Destroy();
    bool IsDestroyMark() const { return m_destroyMark.load(); }
    bool IsDontDestroyOnLoad() const { return m_dontDestroyOnLoad.load(); }

    void SetDontDestroyOnLoad(Object* objPtr);

    ReflectionField(Object, PropertyOnly)
    {
        PropertyField
        ({
            meta_property(m_name)
            meta_property(m_instanceID)
        });

        ReturnReflectionPropertyOnly(Object)
    }

protected:
    size_t            m_instanceID{ TypeTrait::GUIDCreator::GetGUID() };
    HashingString     m_name{ "Object" };
    std::atomic<bool> m_destroyMark{ false };
    std::atomic<bool> m_dontDestroyOnLoad{ false };
};
