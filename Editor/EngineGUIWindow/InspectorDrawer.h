#pragma once
#include <concepts>

// 인스펙터 값 타입 커스텀 드로어 확장점 (PHASE 21 W2-I).
//
// 배경 — 왜 이것이 필요한가.
// 리플렉션 경로(ReflectionTypedDraw.h)는 타입마다 `if constexpr` 분기로 그린다.
// 그 분기 목록은 엔진 소스 안에 있으므로, 엔진을 포크한 쪽이 자기 값 타입을
// 자기 방식으로 그리려면 엔진 파일을 손으로 고쳐야 했다. 포크가 upstream을
// 따라갈 때마다 그 분기가 충돌 지점이 된다.
//
// 해결 — 타입 단위 특수화.
// `InspectorDrawer<T>`를 특수화하면 리플렉션 드로어가 그것을 **먼저** 쓴다.
// 특수화는 자기 타입 옆(자기 헤더)에 쓰고, `InspectorDrawerList.h`에 include
// 한 줄만 더한다. 엔진의 분기는 건드리지 않는다.
//
// 왜 자기 등록(self-registration)이 아니라 특수화인가.
// 엔진 모듈이 전부 StaticLibrary라 정적 초기화 자기 등록자는 링크에서 조용히
// 떨어진다(이 저장소가 이미 겪은 실패 양식). 그래서 어느 방식을 택하든 "명시적
// 목록 파일" 한 줄은 피할 수 없다. 그렇다면 런타임 비용도 간접 호출도 없는
// 컴파일타임 특수화가 낫다.
//
// 쓰는 법 — 자기 타입 옆에 이렇게 쓴다.
//
//     #include "InspectorDrawer.h"
//
//     template<>
//     struct editor::inspector::InspectorDrawer<my::Curve>
//     {
//         // value를 제자리에서 편집하고, 바뀌었으면 true를 돌려준다.
//         // ImGui 전부를 그대로 쓸 수 있다 — 팝업·드래그드롭·ID 스택 포함.
//         static bool Draw(const char* label, my::Curve& value) { ... }
//     };
//     static_assert(editor::inspector::HasInspectorDrawer<my::Curve>);
//
// 마지막 `static_assert`는 관례다. 이름이나 서명을 잘못 쓰면 특수화가 조용히
// 무시되고 기본 위젯이 그려지는데, 그 어긋남은 빌드도 검사도 붉게 만들지
// 않는다. 단정 한 줄이 그것을 컴파일 오류로 바꾼다.
//
// 언두는 드로어가 신경 쓰지 않는다. 리플렉션 드로어가 사본을 넘기고, true가
// 돌아오면 원본과 사본의 차이를 언두 스택에 싣는다.
namespace editor::inspector
{
    // 기본형은 비어 있다 — `Draw`가 없으므로 아래 개념이 거짓이 된다.
    // (선언만 두는 방식도 되지만, 불완전 타입에 대한 SFINAE는 컴파일러마다
    //  경계가 다르다. 비어 있는 정의가 어디서나 같게 판정된다.)
    template<class T>
    struct InspectorDrawer
    {
    };

    template<class T>
    concept HasInspectorDrawer = requires(const char* label, T& value)
    {
        { InspectorDrawer<T>::Draw(label, value) } -> std::same_as<bool>;
    };
}
