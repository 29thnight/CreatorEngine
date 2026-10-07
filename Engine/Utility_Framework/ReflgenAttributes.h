#pragma once
// 엔진 속성 이름공간 creator — 필드·메서드에 [[creator::debug_only]] 처럼 단다(Directory.Build.targets 의
// ReflgenAttributeScopes). reflgen 의 주입 header 가 맨 먼저 include 한다(ReflgenAttributeHeaders) — 생성된 서술이 이
// 타입들을 그대로 싣는다(field_descriptor 의 속성 튜플, 런타임 서술자의 attributes()).
//
// 읽는 곳:
//   debug_only · wide            인스펙터 typed Draw(ReflectionTypedDraw.h) — 디버그 모드에서만 그린다 · 줄을 넓힌다
//   read_only_in_inspector ·     인스펙터 메서드 UI(ReflectionImGuiHelper.h DrawMethods) — 인자 없는 메서드를 매 프레임
//   hide_in_inspector              불러 결과만 보인다 · 메서드를 그리지 않는다
//   units                        단위 꼬리표 — 아직 읽는 곳이 없다(저작 표기만 받는다)
//
// reflgen 도입 P5: 엔진 스키마(MetaSchema.h)의 속성 타입(meta::units_attr 등)의 별명이던 것을 독립 타입으로 옮겼다 —
// 엔진 스키마와 reflgen 다리(meta::of<T>)는 걷었다. 문자열은 reflgen::static_string 이다(구조적 타입 — C++26 주석
// 값이 될 수 있다, reflgen::display_name 과 같은 이유).
#include <reflgen/core/static_string.h>
#include <string_view>

namespace creator
{
    struct units
    {
        reflgen::static_string value;

        constexpr explicit units(std::string_view text) noexcept : value(text) {}
    };

    struct debug_only
    {
    };

    struct wide
    {
    };

    // 메서드 속성 — 인스펙터의 호출 UI.
    struct read_only_in_inspector
    {
    };

    struct hide_in_inspector
    {
    };

    // 엔진 바인딩 생성기의 명시적 opt-in. reflection/private 접근 허용과는 별개다.
    // 이름은 기존 API 표의 도메인 접두사이고, 각 메서드도 script_export 를 가져야 한다.
    struct script_component
    {
        reflgen::static_string prefix;

        constexpr explicit script_component(std::string_view value) noexcept : prefix(value) {}
    };

    struct script_export
    {
        reflgen::static_string property;
        int slot;

        // slot 0 은 존재 질의다. 나머지 순번은 ABI 이므로 선언의 이동으로 바뀌지 않는다.
        constexpr script_export(std::string_view name, int abiSlot) noexcept : property(name), slot(abiSlot) {}
    };
}
