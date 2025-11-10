#pragma once
#ifndef UNUSE_MONO_LIB
#include "Object_Binding.h"
#include "FormIntPtr.h"
#include "CSharpScriptComponent.h"

#include <string>

namespace
{
    extern "C"
    {
        static MonoString* ICall_CSharpScriptComponent_GetScriptName(MonoObject* _this, intptr_t nativePtr) noexcept
        {
            if (auto* self = FromIntPtr<CSharpScriptComponent>(_this, nativePtr))
            {
                return UTF8ToMono(self->GetScriptName());
            }

            return UTF8ToMono(std::string{});
        }

        static MonoString* ICall_CSharpScriptComponent_GetScriptGuid(MonoObject* _this, intptr_t nativePtr) noexcept
        {
            if (auto* self = FromIntPtr<CSharpScriptComponent>(_this, nativePtr))
            {
                FileGuid guid = self->GetScriptGuid();
                return UTF8ToMono(guid.ToString());
            }

            return UTF8ToMono(std::string{});
        }

        static mono_bool ICall_CSharpScriptComponent_HasManagedInstance(MonoObject* _this, intptr_t nativePtr) noexcept
        {
            if (auto* self = FromIntPtr<CSharpScriptComponent>(_this, nativePtr))
            {
                return self->HasManagedInstance() ? 1 : 0;
            }

            return 0;
        }

        static uint32_t ICall_CSharpScriptComponent_GetBehaviorHandle(MonoObject* _this, intptr_t nativePtr) noexcept
        {
            if (auto* self = FromIntPtr<CSharpScriptComponent>(_this, nativePtr))
            {
                return self->GetBehaviorHandle();
            }

            return 0u;
        }
    }
}

inline void Register_CSharpScriptComponent_ICalls()
{
    mono_add_internal_call("CreatorEngine.CSharpScriptComponent::ICall_GetScriptName", (const void*)ICall_CSharpScriptComponent_GetScriptName);
    mono_add_internal_call("CreatorEngine.CSharpScriptComponent::ICall_GetScriptGuid", (const void*)ICall_CSharpScriptComponent_GetScriptGuid);
    mono_add_internal_call("CreatorEngine.CSharpScriptComponent::ICall_HasManagedInstance", (const void*)ICall_CSharpScriptComponent_HasManagedInstance);
    mono_add_internal_call("CreatorEngine.CSharpScriptComponent::ICall_GetBehaviorHandle", (const void*)ICall_CSharpScriptComponent_GetBehaviorHandle);
}

#endif // !UNUSE_MONO_LIB
