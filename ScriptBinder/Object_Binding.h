#pragma once
#define UNUSE_MONO_LIB // 모노 관련 기능 트리거
#ifndef UNUSE_MONO_LIB

// Object_Bindings.cpp
#include <mono/jit/jit.h>
#include <mono/metadata/assembly.h>
#include <mono/metadata/mono-config.h>
#include <mono/metadata/object.h>
#include "Object.h"          // 네가 준 Object 선언
#include "TypeTrait.h"

namespace
{
    // --- String helpers ---
    inline std::string MonoToUTF8(MonoString* mstr)
    {
        if (!mstr) return {};
        char* utf8 = mono_string_to_utf8(mstr);
        std::string out = utf8 ? utf8 : "";
        if (utf8) mono_free(utf8);
        return out;
    }

    inline MonoString* UTF8ToMono(const std::string& s)
    {
        return mono_string_new(mono_domain_get(), s.c_str());
    }

    // --- Safe cast ---
    inline Object* FromIntPtr(MonoObject* /*dummyInstance*/, intptr_t nativePtr)
    {
        return reinterpret_cast<Object*>(nativePtr);
    }

    // -------- ICalls (extern "C" 권장) --------
    extern "C"
    {
        // ulong GetInstanceID(IntPtr self)
        static uint64_t ICall_Object_GetInstanceID(MonoObject* _this, intptr_t nativePtr)
        {
            if (auto* self = FromIntPtr(_this, nativePtr))
                return static_cast<uint64_t>(self->GetInstanceID());
            return 0ull;
        }

        // ulong GetTypeID(IntPtr self)
        static uint64_t ICall_Object_GetTypeID(MonoObject* _this, intptr_t nativePtr)
        {
            if (auto* self = FromIntPtr(_this, nativePtr))
                return static_cast<uint64_t>(self->GetTypeID());
            return 0ull;
        }

        // string ToString(IntPtr self)
        static MonoString* ICall_Object_ToString(MonoObject* _this, intptr_t nativePtr)
        {
            if (auto* self = FromIntPtr(_this, nativePtr))
                return UTF8ToMono(self->ToString());
            return UTF8ToMono(std::string{});
        }

        // string GetName(IntPtr self)
        static MonoString* ICall_Object_GetName(MonoObject* _this, intptr_t nativePtr)
        {
            if (auto* self = FromIntPtr(_this, nativePtr))
                return UTF8ToMono(self->GetHashedName().ToString());
            return UTF8ToMono(std::string{});
        }

        // void SetName(IntPtr self, string name)
        static void ICall_Object_SetName(MonoObject* _this, intptr_t nativePtr, MonoString* mname)
        {
            if (auto* self = FromIntPtr(_this, nativePtr))
            {
                const std::string name = MonoToUTF8(mname);
                self->m_name = HashingString{ name.c_str() };
            }
        }

        // bool GetEnabled(IntPtr self)
        static mono_bool ICall_Object_GetEnabled(MonoObject* _this, intptr_t nativePtr)
        {
            if (auto* self = FromIntPtr(_this, nativePtr))
                return self->IsEnabled() ? 1 : 0;
            return 0;
        }

        // void SetEnabled(IntPtr self, bool v)
        static void ICall_Object_SetEnabled(MonoObject* _this, intptr_t nativePtr, mono_bool v)
        {
            if (auto* self = FromIntPtr(_this, nativePtr))
                self->SetEnabled(v != 0);
        }

        // void Destroy(IntPtr self)
        static void ICall_Object_Destroy(MonoObject* _this, intptr_t nativePtr)
        {
            if (auto* self = FromIntPtr(_this, nativePtr))
                Object::Destroy(self);
        }

        // void SetDontDestroyOnLoad(IntPtr self)
        static void ICall_Object_SetDontDestroyOnLoad(MonoObject* _this, intptr_t nativePtr)
        {
            if (auto* self = FromIntPtr(_this, nativePtr))
                Object::SetDontDestroyOnLoad(self);
        }

        // IntPtr Instantiate(IntPtr original, string newName)
        static intptr_t ICall_Object_Instantiate(MonoObject* /*klass*/, intptr_t originalPtr, MonoString* mname)
        {
            auto* original = reinterpret_cast<const Object*>(originalPtr);
            if (!original) return 0;

            const std::string newName = MonoToUTF8(mname);
            Object* cloned = Object::Instantiate(original, newName);
            return reinterpret_cast<intptr_t>(cloned);
        }
    }
} // anonymous namespace

// --- 등록 함수 (도메인 초기화 시 한 번 호출) ---
void Register_Object_ICalls()
{
    // C# 정규명: "네임스페이스.타입::메서드명"
    mono_add_internal_call("CreatorEngine.Object::ICall_GetInstanceID", (const void*)ICall_Object_GetInstanceID);
    mono_add_internal_call("CreatorEngine.Object::ICall_GetTypeID", (const void*)ICall_Object_GetTypeID);
    mono_add_internal_call("CreatorEngine.Object::ICall_ToString", (const void*)ICall_Object_ToString);
    mono_add_internal_call("CreatorEngine.Object::ICall_GetName", (const void*)ICall_Object_GetName);
    mono_add_internal_call("CreatorEngine.Object::ICall_SetName", (const void*)ICall_Object_SetName);
    mono_add_internal_call("CreatorEngine.Object::ICall_GetEnabled", (const void*)ICall_Object_GetEnabled);
    mono_add_internal_call("CreatorEngine.Object::ICall_SetEnabled", (const void*)ICall_Object_SetEnabled);
    mono_add_internal_call("CreatorEngine.Object::ICall_Destroy", (const void*)ICall_Object_Destroy);
    mono_add_internal_call("CreatorEngine.Object::ICall_SetDontDestroyOnLoad", (const void*)ICall_Object_SetDontDestroyOnLoad);
    mono_add_internal_call("CreatorEngine.Object::ICall_Instantiate", (const void*)ICall_Object_Instantiate);
}
#endif // !UNUSE_MONO_LIB

