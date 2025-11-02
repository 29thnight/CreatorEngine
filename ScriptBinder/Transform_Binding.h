#pragma once
#ifndef UNUSE_MONO_LIB

#include "FormIntPtr.h"
#include "Transform.h"

#include <mono/metadata/object.h>

namespace
{
    inline Mathf::Vector3 ToVector3(const Mathf::Vector4& value) noexcept
    {
        return { value.x, value.y, value.z };
    }

    inline Mathf::Quaternion ToQuaternion(const Mathf::Vector4& value) noexcept
    {
        return { value.x, value.y, value.z, value.w };
    }

    extern "C"
    {
        static Mathf::Vector3 ICall_Transform_GetLocalPosition(MonoObject* _this, intptr_t nativePtr) noexcept
        {
            if (auto* self = FromIntPtr<Transform>(_this, nativePtr))
            {
                return ToVector3(self->position);
            }
            return {};
        }

        static void ICall_Transform_SetLocalPosition(MonoObject* _this, intptr_t nativePtr, Mathf::Vector3 position) noexcept
        {
            if (auto* self = FromIntPtr<Transform>(_this, nativePtr))
            {
                self->SetPosition(position);
            }
        }

        static Mathf::Quaternion ICall_Transform_GetLocalRotation(MonoObject* _this, intptr_t nativePtr) noexcept
        {
            if (auto* self = FromIntPtr<Transform>(_this, nativePtr))
            {
                return ToQuaternion(self->rotation);
            }
            return {};
        }

        static void ICall_Transform_SetLocalRotation(MonoObject* _this, intptr_t nativePtr, Mathf::Quaternion rotation) noexcept
        {
            if (auto* self = FromIntPtr<Transform>(_this, nativePtr))
            {
                self->SetRotation(rotation);
            }
        }

        static Mathf::Vector3 ICall_Transform_GetLocalScale(MonoObject* _this, intptr_t nativePtr) noexcept
        {
            if (auto* self = FromIntPtr<Transform>(_this, nativePtr))
            {
                return ToVector3(self->scale);
            }
            return {};
        }

        static void ICall_Transform_SetLocalScale(MonoObject* _this, intptr_t nativePtr, Mathf::Vector3 scale) noexcept
        {
            if (auto* self = FromIntPtr<Transform>(_this, nativePtr))
            {
                self->SetScale(scale);
            }
        }

        static Mathf::Vector3 ICall_Transform_GetWorldPosition(MonoObject* _this, intptr_t nativePtr) noexcept
        {
            if (auto* self = FromIntPtr<Transform>(_this, nativePtr))
            {
                return { self->GetWorldPosition() };
            }
            return {};
        }

        static void ICall_Transform_SetWorldPosition(MonoObject* _this, intptr_t nativePtr, Mathf::Vector3 position) noexcept
        {
            if (auto* self = FromIntPtr<Transform>(_this, nativePtr))
            {
                self->SetWorldPosition(position);
            }
        }

        static Mathf::Quaternion ICall_Transform_GetWorldRotation(MonoObject* _this, intptr_t nativePtr) noexcept
        {
            if (auto* self = FromIntPtr<Transform>(_this, nativePtr))
            {
                return { self->GetWorldQuaternion() };
            }
            return {};
        }

        static void ICall_Transform_SetWorldRotation(MonoObject* _this, intptr_t nativePtr, Mathf::Quaternion rotation) noexcept
        {
            if (auto* self = FromIntPtr<Transform>(_this, nativePtr))
            {
                self->SetWorldRotation(rotation);
            }
        }

        static Mathf::Vector3 ICall_Transform_GetWorldScale(MonoObject* _this, intptr_t nativePtr) noexcept
        {
            if (auto* self = FromIntPtr<Transform>(_this, nativePtr))
            {
                return { self->GetWorldScale() };
            }
            return {};
        }

        static void ICall_Transform_SetWorldScale(MonoObject* _this, intptr_t nativePtr, Mathf::Vector3 scale) noexcept
        {
            if (auto* self = FromIntPtr<Transform>(_this, nativePtr))
            {
                self->SetWorldScale(scale);
            }
        }

        static Mathf::Vector3 ICall_Transform_GetForward(MonoObject* _this, intptr_t nativePtr) noexcept
        {
            if (auto* self = FromIntPtr<Transform>(_this, nativePtr))
            {
                return self->GetForward();
            }
            return {};
        }

        static Mathf::Vector3 ICall_Transform_GetRight(MonoObject* _this, intptr_t nativePtr) noexcept
        {
            if (auto* self = FromIntPtr<Transform>(_this, nativePtr))
            {
                return self->GetRight();
            }
            return {};
        }

        static Mathf::Vector3 ICall_Transform_GetUp(MonoObject* _this, intptr_t nativePtr) noexcept
        {
            if (auto* self = FromIntPtr<Transform>(_this, nativePtr))
            {
                return self->GetUp();
            }
            return {};
        }

        static intptr_t ICall_Transform_GetOwner(MonoObject* _this, intptr_t nativePtr) noexcept
        {
            if (auto* self = FromIntPtr<Transform>(_this, nativePtr))
            {
                if (auto* owner = self->GetOwner())
                {
                    return reinterpret_cast<intptr_t>(owner);
                }
            }
            return 0;
        }
    }
}

inline void Register_Transform_ICalls()
{
    mono_add_internal_call("CreatorEngine.Transform::ICall_GetLocalPosition", (const void*)ICall_Transform_GetLocalPosition);
    mono_add_internal_call("CreatorEngine.Transform::ICall_SetLocalPosition", (const void*)ICall_Transform_SetLocalPosition);
    mono_add_internal_call("CreatorEngine.Transform::ICall_GetLocalRotation", (const void*)ICall_Transform_GetLocalRotation);
    mono_add_internal_call("CreatorEngine.Transform::ICall_SetLocalRotation", (const void*)ICall_Transform_SetLocalRotation);
    mono_add_internal_call("CreatorEngine.Transform::ICall_GetLocalScale", (const void*)ICall_Transform_GetLocalScale);
    mono_add_internal_call("CreatorEngine.Transform::ICall_SetLocalScale", (const void*)ICall_Transform_SetLocalScale);
    mono_add_internal_call("CreatorEngine.Transform::ICall_GetWorldPosition", (const void*)ICall_Transform_GetWorldPosition);
    mono_add_internal_call("CreatorEngine.Transform::ICall_SetWorldPosition", (const void*)ICall_Transform_SetWorldPosition);
    mono_add_internal_call("CreatorEngine.Transform::ICall_GetWorldRotation", (const void*)ICall_Transform_GetWorldRotation);
    mono_add_internal_call("CreatorEngine.Transform::ICall_SetWorldRotation", (const void*)ICall_Transform_SetWorldRotation);
    mono_add_internal_call("CreatorEngine.Transform::ICall_GetWorldScale", (const void*)ICall_Transform_GetWorldScale);
    mono_add_internal_call("CreatorEngine.Transform::ICall_SetWorldScale", (const void*)ICall_Transform_SetWorldScale);
    mono_add_internal_call("CreatorEngine.Transform::ICall_GetForward", (const void*)ICall_Transform_GetForward);
    mono_add_internal_call("CreatorEngine.Transform::ICall_GetRight", (const void*)ICall_Transform_GetRight);
    mono_add_internal_call("CreatorEngine.Transform::ICall_GetUp", (const void*)ICall_Transform_GetUp);
    mono_add_internal_call("CreatorEngine.Transform::ICall_GetOwner", (const void*)ICall_Transform_GetOwner);
}

#endif // !UNUSE_MONO_LIB
