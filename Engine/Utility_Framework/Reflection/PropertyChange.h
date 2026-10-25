#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
namespace Meta
{
    enum class PropertyChangeSource : std::uint8_t
    {
        Reflection, Prefab, Inspector, Undo, Redo, Native, Script, Physics
    };
    inline thread_local unsigned g_propertyWriteSuppression{};
    struct ScopedPropertyWriteSuppression
    {
        ScopedPropertyWriteSuppression(){++g_propertyWriteSuppression;}
        ~ScopedPropertyWriteSuppression(){--g_propertyWriteSuppression;}
        ScopedPropertyWriteSuppression(const ScopedPropertyWriteSuppression&)=delete;
        ScopedPropertyWriteSuppression& operator=(const ScopedPropertyWriteSuppression&)=delete;
    };
    inline thread_local std::span<const std::string> g_propertyChangeFields{};
    inline thread_local const void* g_propertyChangeFilterOwner{};
    class ScopedPropertyChangeFields
    {
        std::span<const std::string> previous_;
        const void* previousOwner_;
    public:
        ScopedPropertyChangeFields(std::span<const std::string> fields, const void* owner) : previous_(g_propertyChangeFields), previousOwner_(g_propertyChangeFilterOwner) {g_propertyChangeFields=fields;g_propertyChangeFilterOwner=owner;}
        ~ScopedPropertyChangeFields(){g_propertyChangeFields=previous_;g_propertyChangeFilterOwner=previousOwner_;}
        ScopedPropertyChangeFields(const ScopedPropertyChangeFields&)=delete;
        ScopedPropertyChangeFields& operator=(const ScopedPropertyChangeFields&)=delete;
    };
    inline bool ShouldNotifyProperty(std::string_view field, const void* owner)
    {
        if(owner!=g_propertyChangeFilterOwner || g_propertyChangeFields.empty())return true;
        for(const auto& selected:g_propertyChangeFields)if(selected==field)return true;
        return false;
    }
    inline thread_local const void* g_propertyNotificationOwner = nullptr;
    inline thread_local PropertyChangeSource g_propertyChangeSource = PropertyChangeSource::Reflection;
    class ScopedPropertyChangeSource
    {
    public:
        explicit ScopedPropertyChangeSource(PropertyChangeSource source) : previous_(g_propertyChangeSource) {g_propertyChangeSource=source;}
        ~ScopedPropertyChangeSource(){g_propertyChangeSource=previous_;}
        ScopedPropertyChangeSource(const ScopedPropertyChangeSource&)=delete;
        ScopedPropertyChangeSource& operator=(const ScopedPropertyChangeSource&)=delete;
    private: PropertyChangeSource previous_;
    };
    inline PropertyChangeSource CurrentPropertyChangeSource(){return g_propertyChangeSource;}
}
