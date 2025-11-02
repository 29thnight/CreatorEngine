#pragma once
#ifndef UNUSE_MONO_LIB

#include <mono/jit/jit.h>
#include <mono/metadata/assembly.h>
#include <mono/metadata/mono-config.h>
#include <mono/metadata/object.h>

#include "Component.h"
#include "GameObject.h"
#include "TypeTrait.h"
#include <cstdint>
#include <exception>

namespace
{
    inline Component* FromIntPtr(MonoObject* /*instance*/, intptr_t nativePtr) noexcept
    {
        return reinterpret_cast<Component*>(nativePtr);
    }

    extern "C"
    {
        static intptr_t ICall_Component_GetOwner(MonoObject* _this, intptr_t nativePtr) noexcept
        {
            if (auto* self = FromIntPtr(_this, nativePtr))
            {
                if (auto* owner = self->GetOwner())
                {
                    return reinterpret_cast<intptr_t>(owner);
                }
            }
            return 0;
        }

        static intptr_t ICall_Component_GetComponent(MonoObject* _this, intptr_t nativePtr, uint64_t typeGuid) noexcept
        {
            if (auto* self = FromIntPtr(_this, nativePtr))
            {
                try
                {
                    Component& component = self->GetComponent(HashedGuid{ static_cast<size_t>(typeGuid) });
                    return reinterpret_cast<intptr_t>(&component);
                }
                catch (const std::exception&)
                {
                }
            }
            return 0;
        }
    }
}

inline void Register_Component_ICalls()
{
    mono_add_internal_call("CreatorEngine.Component::ICall_GetOwner", (const void*)ICall_Component_GetOwner);
    mono_add_internal_call("CreatorEngine.Component::ICall_GetComponent", (const void*)ICall_Component_GetComponent);
}

#endif // !UNUSE_MONO_LIB
