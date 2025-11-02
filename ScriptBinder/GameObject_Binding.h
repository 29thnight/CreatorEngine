#pragma once
#ifndef UNUSE_MONO_LIB

#include "FormIntPtr.h"
#include "GameObject.h"
#include "Component.h"
#include "TypeTrait.h"

#include <cstdint>
#include <string>

namespace
{
    inline std::string MonoToUTF8(MonoString* mstr)
    {
        if (!mstr)
        {
            return {};
        }

        char* utf8 = mono_string_to_utf8(mstr);
        std::string out = utf8 ? utf8 : "";
        if (utf8)
        {
            mono_free(utf8);
        }
        return out;
    }

    inline MonoString* UTF8ToMono(const std::string& value)
    {
        return mono_string_new(mono_domain_get(), value.c_str());
    }

    extern "C"
    {
        static intptr_t ICall_GameObject_Find(MonoObject* /*klass*/, MonoString* mname)
        {
            const std::string name = MonoToUTF8(mname);
            if (name.empty())
            {
                return 0;
            }

            if (GameObject* found = GameObject::Find(name))
            {
                return reinterpret_cast<intptr_t>(found);
            }
            return 0;
        }

        static intptr_t ICall_GameObject_FindIndex(MonoObject* /*klass*/, int32_t index)
        {
            if (index < 0)
            {
                return 0;
            }

            if (GameObject* found = GameObject::FindIndex(index))
            {
                return reinterpret_cast<intptr_t>(found);
            }
            return 0;
        }

        static intptr_t ICall_GameObject_GetComponent(MonoObject* _this, intptr_t nativePtr, uint32_t typeId) noexcept
        {
            if (auto* self = FromIntPtr<GameObject>(_this, nativePtr))
            {
                if (auto component = self->GetComponentByTypeID(typeId))
                {
                    return reinterpret_cast<intptr_t>(component.get());
                }
            }
            return 0;
        }

        static intptr_t ICall_GameObject_GetTransform(MonoObject* _this, intptr_t nativePtr) noexcept
        {
            if (auto* self = FromIntPtr<GameObject>(_this, nativePtr))
            {
                return reinterpret_cast<intptr_t>(&self->m_transform);
            }
            return 0;
        }

        static MonoString* ICall_GameObject_GetTag(MonoObject* _this, intptr_t nativePtr)
        {
            if (auto* self = FromIntPtr<GameObject>(_this, nativePtr))
            {
                return UTF8ToMono(self->m_tag.ToString());
            }
            return UTF8ToMono(std::string{});
        }

        static void ICall_GameObject_SetTag(MonoObject* _this, intptr_t nativePtr, MonoString* mtag)
        {
            if (auto* self = FromIntPtr<GameObject>(_this, nativePtr))
            {
                const std::string tag = MonoToUTF8(mtag);
                self->SetTag(tag);
            }
        }

        static MonoString* ICall_GameObject_GetLayer(MonoObject* _this, intptr_t nativePtr)
        {
            if (auto* self = FromIntPtr<GameObject>(_this, nativePtr))
            {
                return UTF8ToMono(self->m_layer.ToString());
            }
            return UTF8ToMono(std::string{});
        }

        static void ICall_GameObject_SetLayer(MonoObject* _this, intptr_t nativePtr, MonoString* mlayer)
        {
            if (auto* self = FromIntPtr<GameObject>(_this, nativePtr))
            {
                const std::string layer = MonoToUTF8(mlayer);
                self->SetLayer(layer);
            }
        }

        static mono_bool ICall_GameObject_GetStatic(MonoObject* _this, intptr_t nativePtr)
        {
            if (auto* self = FromIntPtr<GameObject>(_this, nativePtr))
            {
                return self->IsStatic() ? 1 : 0;
            }
            return 0;
        }

        static void ICall_GameObject_SetStatic(MonoObject* _this, intptr_t nativePtr, mono_bool value)
        {
            if (auto* self = FromIntPtr<GameObject>(_this, nativePtr))
            {
                self->SetStatic(value != 0);
            }
        }
    }
}

inline void Register_GameObject_ICalls()
{
    mono_add_internal_call("CreatorEngine.GameObject::ICall_Find", (const void*)ICall_GameObject_Find);
    mono_add_internal_call("CreatorEngine.GameObject::ICall_FindIndex", (const void*)ICall_GameObject_FindIndex);
    mono_add_internal_call("CreatorEngine.GameObject::ICall_GetComponent", (const void*)ICall_GameObject_GetComponent);
    mono_add_internal_call("CreatorEngine.GameObject::ICall_GetTransform", (const void*)ICall_GameObject_GetTransform);
    mono_add_internal_call("CreatorEngine.GameObject::ICall_GetTag", (const void*)ICall_GameObject_GetTag);
    mono_add_internal_call("CreatorEngine.GameObject::ICall_SetTag", (const void*)ICall_GameObject_SetTag);
    mono_add_internal_call("CreatorEngine.GameObject::ICall_GetLayer", (const void*)ICall_GameObject_GetLayer);
    mono_add_internal_call("CreatorEngine.GameObject::ICall_SetLayer", (const void*)ICall_GameObject_SetLayer);
    mono_add_internal_call("CreatorEngine.GameObject::ICall_GetStatic", (const void*)ICall_GameObject_GetStatic);
    mono_add_internal_call("CreatorEngine.GameObject::ICall_SetStatic", (const void*)ICall_GameObject_SetStatic);
}

#endif // !UNUSE_MONO_LIB
