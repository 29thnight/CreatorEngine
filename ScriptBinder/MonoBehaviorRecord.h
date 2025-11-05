#pragma once
#define UNUSE_MONO_LIB
#ifndef UNUSE_MONO_LIB
#include <mono/metadata/object.h>
#include "Delegate.h"

using MonoBehaviorHandle = uint32_t;

enum class MonoBehaviorEvent
{
    Awake,
    OnEnable,
    Start,
    FixedUpdate,
    Update,
    LateUpdate,
    OnDisable,
    OnDestroy,
};

struct MethodHandlePair
{
    MonoMethod*          method{ nullptr };
    Core::DelegateHandle handle{};
    MonoBehaviorEvent    event{ MonoBehaviorEvent::Awake };
};

// C# MonoBehavior 타입별 메서드 포인터 정보
struct MonoBehaviorInfo
{
    MonoClass* klass;
    MonoMethod* start = nullptr;
    MonoMethod* update = nullptr;
    MonoMethod* onDisable = nullptr;
    // 필요시 FixedUpdate, LateUpdate, OnDestroy 등 추가
};

struct MonoBehaviorRecord
{
    MonoBehaviorHandle            id{};
    MonoObject*                   instance{ nullptr };
    std::vector<MethodHandlePair> events;
};

#endif // UNUSE_MONO_LIB
