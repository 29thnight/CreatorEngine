#pragma once
#include <mono/metadata/object.h>
#include "Delegate.h"

using MonoBehaviorHandle = uint32_t;

struct MethodHandlePair
{
    MonoMethod*          method{ nullptr };
    Core::DelegateHandle handle{};
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
    MonoBehaviorHandle              id{};
    MonoObject*                     instance{ nullptr };
    std::vector<MethodHandlePair>   events;
};
