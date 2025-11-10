#pragma once
#ifndef UNUSE_MONO_LIB

#include "FormIntPtr.h"
#include "RectTransformComponent.h"
#include "GameObject.h"
#include "TypeTrait.h"

#include <mono/metadata/object.h>

#include <algorithm>
#include <cstdint>
#include <type_traits>

namespace
{
    constexpr AnchorPreset ClampAnchorPreset(int32_t value) noexcept
    {
        using Underlying = std::underlying_type_t<AnchorPreset>;
        const auto clamped = std::clamp(static_cast<Underlying>(value), static_cast<Underlying>(AnchorPreset::TopLeft), static_cast<Underlying>(AnchorPreset::StretchAll));
        return static_cast<AnchorPreset>(clamped);
    }

    extern "C"
    {
        static Mathf::Vector2 ICall_RectTransform_GetAnchorMin(MonoObject* _this, intptr_t nativePtr) noexcept
        {
            if (auto* self = FromIntPtr<RectTransformComponent>(_this, nativePtr))
            {
                return self->GetAnchorMin();
            }
            return {};
        }

        static void ICall_RectTransform_SetAnchorMin(MonoObject* _this, intptr_t nativePtr, Mathf::Vector2 anchorMin) noexcept
        {
            if (auto* self = FromIntPtr<RectTransformComponent>(_this, nativePtr))
            {
                self->SetAnchorMin(anchorMin);
            }
        }

        static Mathf::Vector2 ICall_RectTransform_GetAnchorMax(MonoObject* _this, intptr_t nativePtr) noexcept
        {
            if (auto* self = FromIntPtr<RectTransformComponent>(_this, nativePtr))
            {
                return self->GetAnchorMax();
            }
            return {};
        }

        static void ICall_RectTransform_SetAnchorMax(MonoObject* _this, intptr_t nativePtr, Mathf::Vector2 anchorMax) noexcept
        {
            if (auto* self = FromIntPtr<RectTransformComponent>(_this, nativePtr))
            {
                self->SetAnchorMax(anchorMax);
            }
        }

        static Mathf::Vector2 ICall_RectTransform_GetAnchoredPosition(MonoObject* _this, intptr_t nativePtr) noexcept
        {
            if (auto* self = FromIntPtr<RectTransformComponent>(_this, nativePtr))
            {
                return self->GetAnchoredPosition();
            }
            return {};
        }

        static void ICall_RectTransform_SetAnchoredPosition(MonoObject* _this, intptr_t nativePtr, Mathf::Vector2 anchoredPosition) noexcept
        {
            if (auto* self = FromIntPtr<RectTransformComponent>(_this, nativePtr))
            {
                self->SetAnchoredPosition(anchoredPosition);
            }
        }

        static Mathf::Vector2 ICall_RectTransform_GetSizeDelta(MonoObject* _this, intptr_t nativePtr) noexcept
        {
            if (auto* self = FromIntPtr<RectTransformComponent>(_this, nativePtr))
            {
                return self->GetSizeDelta();
            }
            return {};
        }

        static void ICall_RectTransform_SetSizeDelta(MonoObject* _this, intptr_t nativePtr, Mathf::Vector2 sizeDelta) noexcept
        {
            if (auto* self = FromIntPtr<RectTransformComponent>(_this, nativePtr))
            {
                self->SetSizeDelta(sizeDelta);
            }
        }

        static Mathf::Vector2 ICall_RectTransform_GetPivot(MonoObject* _this, intptr_t nativePtr) noexcept
        {
            if (auto* self = FromIntPtr<RectTransformComponent>(_this, nativePtr))
            {
                return self->GetPivot();
            }
            return {};
        }

        static void ICall_RectTransform_SetPivot(MonoObject* _this, intptr_t nativePtr, Mathf::Vector2 pivot) noexcept
        {
            if (auto* self = FromIntPtr<RectTransformComponent>(_this, nativePtr))
            {
                self->SetPivot(pivot);
            }
        }

        static Mathf::Rect ICall_RectTransform_GetWorldRect(MonoObject* _this, intptr_t nativePtr) noexcept
        {
            if (auto* self = FromIntPtr<RectTransformComponent>(_this, nativePtr))
            {
                return self->GetWorldRect();
            }
            return {};
        }

        static void ICall_RectTransform_SetAnchorPreset(MonoObject* _this, intptr_t nativePtr, int32_t preset) noexcept
        {
            if (auto* self = FromIntPtr<RectTransformComponent>(_this, nativePtr))
            {
                self->SetAnchorPreset(ClampAnchorPreset(preset));
            }
        }

        static void ICall_RectTransform_SetAnchorsPivotKeepWorld(MonoObject* _this,
            intptr_t nativePtr,
            Mathf::Vector2 newAnchorMin,
            Mathf::Vector2 newAnchorMax,
            Mathf::Vector2 newPivot,
            Mathf::Rect newParentRect) noexcept
        {
            if (auto* self = FromIntPtr<RectTransformComponent>(_this, nativePtr))
            {
                self->SetAnchorsPivotKeepWorld(newAnchorMin, newAnchorMax, newPivot, newParentRect);
            }
        }

        static void ICall_RectTransform_SetParentKeepWorldPosition(MonoObject* _this, intptr_t nativePtr, intptr_t parentPtr) noexcept
        {
            if (auto* self = FromIntPtr<RectTransformComponent>(_this, nativePtr))
            {
                auto* parent = reinterpret_cast<GameObject*>(parentPtr);
                self->SetParentKeepWorldPosition(parent);
            }
        }

        static mono_bool ICall_RectTransform_IsDirty(MonoObject* _this, intptr_t nativePtr) noexcept
        {
            if (auto* self = FromIntPtr<RectTransformComponent>(_this, nativePtr))
            {
                return self->IsDirty() ? 1 : 0;
            }
            return 0;
        }

        static uint64_t ICall_RectTransform_GetTypeID() noexcept
        {
            return static_cast<uint64_t>(type_guid(RectTransformComponent));
        }

        static intptr_t ICall_RectTransform_GetFromGameObject(intptr_t gameObjectPtr) noexcept
        {
            if (auto* gameObject = reinterpret_cast<GameObject*>(gameObjectPtr))
            {
                if (auto* rect = gameObject->GetComponent<RectTransformComponent>())
                {
                    return reinterpret_cast<intptr_t>(rect);
                }
            }
            return 0;
        }
    }
}

inline void Register_RectTransform_ICalls()
{
    mono_add_internal_call("CreatorEngine.RectTransform::ICall_GetAnchorMin", (const void*)ICall_RectTransform_GetAnchorMin);
    mono_add_internal_call("CreatorEngine.RectTransform::ICall_SetAnchorMin", (const void*)ICall_RectTransform_SetAnchorMin);
    mono_add_internal_call("CreatorEngine.RectTransform::ICall_GetAnchorMax", (const void*)ICall_RectTransform_GetAnchorMax);
    mono_add_internal_call("CreatorEngine.RectTransform::ICall_SetAnchorMax", (const void*)ICall_RectTransform_SetAnchorMax);
    mono_add_internal_call("CreatorEngine.RectTransform::ICall_GetAnchoredPosition", (const void*)ICall_RectTransform_GetAnchoredPosition);
    mono_add_internal_call("CreatorEngine.RectTransform::ICall_SetAnchoredPosition", (const void*)ICall_RectTransform_SetAnchoredPosition);
    mono_add_internal_call("CreatorEngine.RectTransform::ICall_GetSizeDelta", (const void*)ICall_RectTransform_GetSizeDelta);
    mono_add_internal_call("CreatorEngine.RectTransform::ICall_SetSizeDelta", (const void*)ICall_RectTransform_SetSizeDelta);
    mono_add_internal_call("CreatorEngine.RectTransform::ICall_GetPivot", (const void*)ICall_RectTransform_GetPivot);
    mono_add_internal_call("CreatorEngine.RectTransform::ICall_SetPivot", (const void*)ICall_RectTransform_SetPivot);
    mono_add_internal_call("CreatorEngine.RectTransform::ICall_GetWorldRect", (const void*)ICall_RectTransform_GetWorldRect);
    mono_add_internal_call("CreatorEngine.RectTransform::ICall_SetAnchorPreset", (const void*)ICall_RectTransform_SetAnchorPreset);
    mono_add_internal_call("CreatorEngine.RectTransform::ICall_SetAnchorsPivotKeepWorld", (const void*)ICall_RectTransform_SetAnchorsPivotKeepWorld);
    mono_add_internal_call("CreatorEngine.RectTransform::ICall_SetParentKeepWorldPosition", (const void*)ICall_RectTransform_SetParentKeepWorldPosition);
    mono_add_internal_call("CreatorEngine.RectTransform::ICall_IsDirty", (const void*)ICall_RectTransform_IsDirty);
    mono_add_internal_call("CreatorEngine.RectTransform::ICall_GetTypeID", (const void*)ICall_RectTransform_GetTypeID);
    mono_add_internal_call("CreatorEngine.RectTransform::ICall_GetFromGameObject", (const void*)ICall_RectTransform_GetFromGameObject);
}

#endif // !UNUSE_MONO_LIB
