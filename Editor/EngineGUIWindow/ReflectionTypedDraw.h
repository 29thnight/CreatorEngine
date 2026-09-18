#pragma once
#include <mathematics/vector2.hpp>
#include <mathematics/vector3.hpp>
#include <mathematics/vector4.hpp>
#include "EditorObjectOperations.h"
#include "Component.h"
#include "ReflectionUndo.h" // MakeCustomChangeCommand — 에디터 층 Undo(E1-6 이관)
// 인스펙터 typed Draw (PHASE 18 CT6-c).
//
// 레거시 DrawProperties(프로퍼티당 정수 비교 선형 체인 + function/any 접근)를
// meta 서술 기반 컴파일타임 디스패치로 대체한다. 멤버를 실제 타입 T&로 만지므로
// getter/setter 왕복·any 박싱이 없고, 벡터 편집은 원본을 직접 조작한다(임시
// 벡터 복사 소멸). meta::range/displayName 속성도 여기서 소비한다.
//
// CT7 정산: 레거시 체인·A/B 토글(inspector.typeddraw)은 픽셀 동등 캡처 검수
// (CT6-c) 후 은퇴했다 — 이 typed 경로가 유일본이다. 파리티 기준은 레거시의
// "라이브" 경로였다: 포인터 특수 분기(GameObject·Texture 드래그드롭)는
// prop.typeID를 포인티 타입과 비교하는 오류로 사문이었고, 라이브 렌더링은
// 제네릭(CollapsingHeader + 재귀)이었다. 드래그드롭 복원은 별도 결정.
//
// 언두: 레거시는 Property 기반 PropertyChangeCommand — typed는 멤버 포인터를
// 캡처한 CustomChangeCommand로 동일 의미(변경 즉시 적용 + Undo/Redo 왕복).
#include "ReflectionImGuiHelper.h"
#include "InspectorDrawerList.h" // InspectorDrawer<T> 특수화 모음 — 분기보다 먼저 본다
#include "EditorPropertyRow.h"   // 공통 배치 계약 (W2-I2)
#include "ReflectionTypedYml.h" // Typed::PointeeT·RawPtrOf·EncodeMapKey 재사용
#include "InspectorControl.h"    // 접힘 머리 펼침(W2-I3 자극)
#include <cstddef>
#include <algorithm> // iter_swap — 임의 접근이 없는 컨테이너의 재배열
#include <iterator>  // prev·next·begin

static_assert(std::is_standard_layout_v<math::vector2>);
static_assert(std::is_standard_layout_v<math::vector3>);
static_assert(std::is_standard_layout_v<math::vector4>);
static_assert(std::is_standard_layout_v<math::color>);
static_assert(std::is_standard_layout_v<math::rect>);
static_assert(std::is_trivially_copyable_v<math::vector2>);
static_assert(std::is_trivially_copyable_v<math::vector3>);
static_assert(std::is_trivially_copyable_v<math::vector4>);
static_assert(std::is_trivially_copyable_v<math::color>);
static_assert(std::is_trivially_copyable_v<math::rect>);
static_assert(offsetof(math::vector2, y) == sizeof(float));
static_assert(offsetof(math::rect, height) == 3 * sizeof(float));
static_assert(offsetof(math::vector3, y) == sizeof(float));
static_assert(offsetof(math::vector3, z) == sizeof(float) * 2);
static_assert(offsetof(math::vector4, y) == sizeof(float));
static_assert(offsetof(math::vector4, z) == sizeof(float) * 2);
static_assert(offsetof(math::vector4, w) == sizeof(float) * 3);
static_assert(offsetof(math::color, a) == sizeof(float) * 3);

// 확장점이 이 번역 단위에서 실제로 보이는지 단정한다 (W2-I).
// 특수화는 쓰는 자리에서 보여야 효력이 있고, 안 보이면 아래 `if constexpr`
// 사슬이 vector3 를 그릴 분기 없이 끝까지 떨어진다 — 빌드도 검사도 붉어지지
// 않고 화면에서만 칸이 사라진다. `InspectorDrawerList.h` 의 include 가 끊기면
// 여기서 컴파일이 멈춘다.
static_assert(editor::inspector::HasInspectorDrawer<math::vector3>,
    "InspectorDrawerList.h 가 기본 드로어를 싣지 못했다");

// `m_isEnabled` 가 숨김으로 남아 있는지 단정한다 (W2-I3).
//
// 이 필드는 전용 체크박스가 담당한다 — 그쪽만 `SetEnabled` 를 거쳐
// `OnEnable`/`OnDisable` 을 보존한다. 속성이 떨어지면 리플렉션이 같은 값을 또
// 그리고, 그 칸으로 끄면 훅이 영영 불리지 않는다. 화면에는 체크박스가 하나 더
// 생길 뿐이라 눈으로는 결함으로 읽히지 않는다.
//
// 예전에는 이 규칙이 드로어 안의 문자열 비교였다. 그때는 선언 쪽에 단정을 걸
// 자리가 없었다.
namespace Meta::TypedDraw
{
    consteval bool ObjectHidesEnabledFlag()
    {
        return std::apply([](const auto&... ms)
        {
            return ((std::remove_cvref_t<decltype(ms)>::identifier ==
                        std::string_view{ "m_isEnabled" } &&
                     std::remove_cvref_t<decltype(ms)>::template
                        has_attribute<meta::hidden_attr>()) || ...);
        }, meta::schema_of<Object>.fields);
    }
}
static_assert(Meta::TypedDraw::ObjectHidesEnabledFlag(),
    "Object::m_isEnabled 에서 meta::hidden() 이 떨어졌다 — 인스펙터가 활성 플래그를 두 번 그린다");

// `Object` 의 정체성 필드 둘이 손에 닿지 않는지 단정한다 (W2-I).
//
// `m_name` 은 컴포넌트에서 타입 이름 사본이고 `m_instanceID` 는 레지스트리
// 키다. 둘 다 유일하게 옳은 값이 이미 정해져 있는데, 표시 속성이 붙기 전에는
// `m_name` 이 편집 가능한 글상자로 나와 있었다 — 고치면 씬 파일에 그대로
// 적혔다.
//
// 검사가 아니라 `static_assert` 인 이유. 속성이 떨어져도 화면은 멀쩡해 보인다.
// 글상자가 하나 생길 뿐이고, 그것이 잘못이라는 것은 값의 뜻을 알아야 보인다.
// 런타임 검사는 그 프레임을 그려 봐야 알지만 이쪽은 빌드가 선다.
namespace Meta::TypedDraw
{
    template<class Attr>
    consteval bool ObjectFieldMarked(std::string_view field)
    {
        return std::apply([field](const auto&... ms)
        {
            return ((std::remove_cvref_t<decltype(ms)>::identifier == field &&
                     std::remove_cvref_t<decltype(ms)>::template
                        has_attribute<Attr>()) || ...);
        }, meta::schema_of<Object>.fields);
    }
}
static_assert(Meta::TypedDraw::ObjectFieldMarked<meta::readonly_attr>("m_name"),
    "Object::m_name 에서 meta::readonly() 가 떨어졌다 — 컴포넌트의 타입 이름 사본이 다시 편집 가능해진다");
static_assert(Meta::TypedDraw::ObjectFieldMarked<meta::debug_only_attr>("m_name"),
    "Object::m_name 에서 meta::debugOnly() 가 떨어졌다 — 컴포넌트 머리글과 같은 이름이 줄마다 겹쳐 나온다");
static_assert(Meta::TypedDraw::ObjectFieldMarked<meta::readonly_attr>("m_instanceID"),
    "Object::m_instanceID 에서 meta::readonly() 가 떨어졌다 — 레지스트리 키를 손으로 고칠 수 있게 된다");
static_assert(Meta::TypedDraw::ObjectFieldMarked<meta::debug_only_attr>("m_instanceID"),
    "Object::m_instanceID 에서 meta::debugOnly() 가 떨어졌다 — 내부 식별자가 기본 인스펙터에 샌다");

namespace Meta::TypedDraw
{
    // DrawFn·Registry·FindDraw는 ReflectionImGuiHelper.h(디스패치 지점)에 있다.

    // 멤버 변경을 언두 스택에 싣는다 — 레거시 MakePropChangeCommand 등가.
    template<class Owner, class MemberT, auto MP>
    inline void CommitMemberChange(Owner* obj, const MemberT& prevValue, const MemberT& newValue, const char* field)
    {
        if constexpr (std::is_base_of_v<Component, Owner>)
        {
            const auto* type = Meta::Find(obj->GetTypeID().m_ID_Data);
            if (obj->GetOwner() && type)
            {
                obj->*MP = prevValue;
                auto before = Meta::SerializeDocument(obj, *type);
                obj->*MP = newValue;
                EditorObjectOperations::CommitProperty(*obj, field, std::move(before));
                return;
            }
        }
        Meta::MakeCustomChangeCommand(
            [obj, prevValue]() { obj->*MP = prevValue; },
            [obj, newValue]() { obj->*MP = newValue; });
    }

    // 컨테이너의 접힘 머리. 명령은 클릭할 수 없으므로 `editor.inspector expand on` 이
    // 켜져 있으면 연다 — 켜지 않으면 안쪽 줄은 어떤 검사에도 자극되지 않는다.
    inline bool ContainerHeader(const char* label)
    {
        if (editor::windows::inspector_expand_all())
        {
            ImGui::SetNextItemOpen(true, ImGuiCond_Always);
        }
        return ImGui::CollapsingHeader(label);
    }

    // 컨테이너 원소 한 줄 (W2-I3). 이름은 순번(`[0]`)이고 라벨·값 열은 공통 배치가 놓는다.
    //
    // 예전에는 원소 위젯이 라벨 없이 ImGui 기본 폭(창의 2/3)으로 서고, 가변 배열은 그
    // 오른쪽에 `^`·`v` 버튼을 더 붙였다. 좁은 인스펙터에서 합성 자극물이 240 폭 323 px ·
    // 320 폭 143 px 넘쳤고, 원소 24 개가 공통 줄을 지나지 않았다.
    //
    // ★ W2-I5 — 배치를 **이 줄에서 다시 잰다.** 두 가지가 틀려 있었다.
    //   ① 버튼 몫을 판정 **뒤에** 뺐다. 그래서 값 칸이 최소 가독 폭 아래로 눌려도
    //      줄이 내려가지 않았다 — 240 폭에서 원소 칸이 **1 px** 이었다. 오른쪽 끝은
    //      작업 영역 안이라 넘침은 0 이고, 화면에는 눌린 칸만 남는다.
    //   ② 바깥 본문에서 잰 배치를 그대로 썼다. 원소는 접힘 머리 안이라 들여쓰기만큼
    //      좁은데, 라벨 열은 바깥 폭 기준이라 값이 그 차이만큼 더 줄었다.
    // 버튼 수를 `aux_button_count` 로 넘기면 전환 판정이 그 몫을 알고 내려간다.
    inline float BeginElementLine(int index, int trailingButtons = 0)
    {
        char label[16];
        snprintf(label, sizeof(label), "[%d]", index);

        // 원소 줄끼리 한 판정을 쓴다 — 같은 배열의 원소가 서로 다른 모드로 서면
        // 줄마다 값 칸의 자리가 달라진다(`LayoutScope` 와 같은 이유).
        static editor::widgets::property_layout_state state{};
        const editor::widgets::property_layout_metrics metrics =
            editor::widgets::measure_property_layout(
                editor::widgets::property_layout_inputs_now(trailingButtons,
                    ImGui::CalcTextSize(label).x), state);
        return ImMax(editor::widgets::begin_property_line(label, metrics), 1.f);
    }

    template<class E>
    inline void DrawEnumCombo(const char* label, const char* idName, E& value)
    {
        // CT9-b: magic_enum → 자급 표(meta::enum_entries). 같은 스캔 원리·
        // 동일 범위라 이름·순서가 레거시 콤보와 그대로 일치한다.
        constexpr auto& entries = meta::enum_entries<E>;

        int currentIndex = 0;
        std::vector<const char*> items;
        items.reserve(entries.size());
        for (size_t i = 0; i < entries.size(); ++i)
        {
            items.push_back(entries[i].name.data());
            if (entries[i].value == value)
            {
                currentIndex = static_cast<int>(i);
            }
        }

        ImGui::PushID(idName);
        if (ImGui::Combo(label, &currentIndex, items.data(), static_cast<int>(items.size())))
        {
            value = entries[currentIndex].value; // 레거시도 enum은 언두 없이 즉시 대입
        }
        ImGui::PopID();
    }

    // 스칼라 원소 시퀀스 편집기 — 레거시 블록(Add/Remove/개별 위젯/^v 재배열)과
    // 동일한 조작을 원본 컨테이너에 직접 한다(접힘 조기 검사 포함, CT1 규칙 유지).
    //
    // 예전에는 서명이 `std::vector<E>&` 였다. 컨테이너 이름을 서명에 박으면
    // deque·list 는 같은 조작을 할 수 있는데도 못 들어온다 — 직렬화기가 range
    // 일반화로 걷어 낸 것과 같은 제약이 여기 남아 있었다.
    template<class Container, class E, class WidgetFn>
    inline void DrawSequenceEditor(const char* idName, const char* label,
        Container& vec, E defaultValue, WidgetFn&& widget)
    {
        ImGui::PushID(idName);
        if (ContainerHeader(label))
        {
            if (ImGui::Button("Add"))
            {
                vec.push_back(defaultValue);
            }
            ImGui::SameLine();
            if (ImGui::Button("Remove"))
            {
                if (!vec.empty())
                {
                    vec.pop_back();
                }
            }

            const int size = static_cast<int>(vec.size());
            const float button = ImGui::GetFrameHeight();
            const float gap = ImGui::GetStyle().ItemInnerSpacing.x;
            // 임의 접근이 없는 컨테이너(list)도 같은 조작을 받아야 하므로
            // 인덱스가 아니라 반복자로 걷는다.
            auto cursor = std::begin(vec);
            for (int i = 0; i < size; ++i, ++cursor)
            {
                ImGui::PushID(i);
                ImGui::SetNextItemWidth(BeginElementLine(i, 2));
                widget(i, *cursor);

                auto neighbour = cursor;
                ImGui::SameLine(0.f, gap);
                if (ImGui::Button("^", ImVec2(button, button)) && i > 0)
                {
                    std::iter_swap(cursor, std::prev(neighbour));
                }
                ImGui::SameLine(0.f, gap);
                if (ImGui::Button("v", ImVec2(button, button)) && i < size - 1)
                {
                    std::iter_swap(cursor, std::next(neighbour));
                }
                ImGui::PopID();
            }
        }
        ImGui::PopID();
    }

    // ── 컨테이너 원소 하나의 값 위젯 ──────────────────────────────────────
    //
    // 멤버와 **같은 문**으로 들어간다: `InspectorDrawer<E>` 특수화가 먼저고,
    // 없으면 기본 위젯이다. 원소만 다른 문을 쓰면, 포크가 자기 타입에 붙인
    // 드로어가 컨테이너 안에서만 조용히 무시된다 — 확장점의 계약이 반쪽이 된다.
    //
    // 기존 다섯 갈래(string·int·float·vector2·vector3)의 위젯은 그대로 둔다.
    // 실측상 등록된 필드가 있는 것은 `vector<float>`(3건)·`vector<std::string>`
    // (2건) 둘뿐이고 그 둘에는 드로어가 없으므로, 이 문을 앞에 세워도 실제로
    // 그려지는 화면은 바뀌지 않는다.
    template<class E>
    inline bool DrawContainerElement(const char* id, E& value)
    {
        if constexpr (editor::inspector::HasInspectorDrawer<E>)
        {
            return editor::inspector::InspectorDrawer<E>::Draw(id, value);
        }
        else if constexpr (std::is_same_v<E, std::string>)
        {
            char buffer[128];
            strncpy_s(buffer, sizeof(buffer), value.c_str(), _TRUNCATE);
            buffer[sizeof(buffer) - 1] = '\0';
            if (ImGui::InputText(id, buffer, sizeof(buffer)))
            {
                value = std::string(buffer);
                return true;
            }
            return false;
        }
        else if constexpr (std::is_same_v<E, HashingString>)
        {
            char buffer[128];
            strncpy_s(buffer, sizeof(buffer), value.ToString().c_str(), _TRUNCATE);
            buffer[sizeof(buffer) - 1] = '\0';
            if (ImGui::InputText(id, buffer, sizeof(buffer)))
            {
                value = HashingString(std::string_view{ buffer });
                return true;
            }
            return false;
        }
        else if constexpr (std::is_same_v<E, bool>)
        {
            return ImGui::Checkbox(id, &value);
        }
        else if constexpr (std::is_same_v<E, int>)
        {
            return ImGui::InputInt(id, &value);
        }
        else if constexpr (std::is_same_v<E, float>)
        {
            return ImGui::InputFloat(id, &value);
        }
        else if constexpr (std::is_enum_v<E>)
        {
            DrawEnumCombo(id, id, value);
            return false; // enum 콤보는 멤버 쪽과 같게 즉시 대입한다
        }
        else if constexpr (std::is_same_v<E, math::vector2>)
        {
            return ImGui::InputFloat2(id, &value.x);
        }
        else if constexpr (std::is_same_v<E, math::vector3>)
        {
            return ImGui::InputFloat3(id, &value.x);
        }
        else if constexpr (std::is_same_v<E, math::vector4>
            || std::is_same_v<E, math::quaternion>)
        {
            return ImGui::InputFloat4(id, &value.x);
        }
        else if constexpr (std::is_same_v<E, math::color>)
        {
            return ImGui::ColorEdit4(id, &value.r);
        }
        else if constexpr (std::is_same_v<E, HashedGuid>)
        {
            // 손으로 고칠 값이 아니다 — 보여 주기만 한다. `[no widget]` 보다
            // 낫다: `vector<HashedGuid>`(BTBuildNode::Children)는 그 자리에
            // 무엇이 걸려 있는지가 읽을 거리의 전부다.
            ImGui::TextDisabled("%zu", value.m_ID_Data);
            return false;
        }
        else if constexpr (std::is_arithmetic_v<E>)
        {
            // uint16_t·uint32_t·size_t 처럼 폭만 다른 정수들. 예전에는 갈래가
            // 없어 `m_keywordSelections`(vector<uint16_t>) 같은 필드가 인스펙터에
            // 아예 나타나지 않았다.
            double scratch = static_cast<double>(value);
            if (ImGui::InputDouble(id, &scratch))
            {
                value = static_cast<E>(scratch);
                return true;
            }
            return false;
        }
        else
        {
            ImGui::TextDisabled("[no widget]");
            return false;
        }
    }

    /// 원소를 제자리에서 편집할 수 있는 타입인가(그릴 수만 있는 것과 구분한다).
    template<class E>
    inline constexpr bool kElementHasWidget =
        editor::inspector::HasInspectorDrawer<E>
        || std::is_arithmetic_v<E> || std::is_enum_v<E>
        || std::is_same_v<E, std::string> || std::is_same_v<E, HashingString>
        || std::is_same_v<E, HashedGuid>
        || std::is_same_v<E, math::vector2> || std::is_same_v<E, math::vector3>
        || std::is_same_v<E, math::vector4> || std::is_same_v<E, math::quaternion>
        || std::is_same_v<E, math::color>;

    template<meta::reflectable T>
    void DrawTypedObject(T& obj);

    // 값 위젯이 쓰는 숨은 ID. 라벨은 공통 배치 계층이 이미 그렸으므로 위젯
    // 자신은 이름을 내지 않는다. `PushID(name)` 안이라 같은 문자열이 여러 줄에
    // 쓰여도 ID 가 겹치지 않는다.
    inline constexpr const char* kValueId = "##v";

    // 이 멤버의 표시 이름. `meta::displayName` 이 붙어 있으면 그것이고,
    // 없으면 식별자에서 유도한다(W2-I3).
    //
    // 유도를 기본으로 둔 이유는 실측이다 — 저장소의 필드 416개 중
    // `displayName` 선언은 0건이었다. 손으로 붙이는 길은 안 붙인 필드가 조용히
    // `m_nearPlane` 인 채로 나오는 것을 막을 수단이 없다.
    template<class MI>
    inline const char* MemberLabel(const MI& mi)
    {
        if constexpr (std::remove_cvref_t<MI>::template has_attribute<meta::display_name_attr>())
        {
            return mi.template attribute<meta::display_name_attr>().value.data();
        }
        else
        {
            return editor::widgets::display_label(
                std::remove_cvref_t<MI>::identifier.data());
        }
    }

    // 이 타입의 고정 라벨 중 가장 넓은 폭. 라벨 열이 필요 이상 넓어지지 않게
    // 한다. 식별자에서 유도한 이름은 컴파일 시 정해진 문자열에서만 나오므로
    // 프레임마다 달라지지 않는다 — 계획서가 금지한 "라벨 최대값 변화로 열이
    // 흔들리는" 상태가 되지 않는다.
    // 상속 체인을 `DrawFrame` 과 같은 모양으로 거슬러 오른다.
    //
    // 처음에는 `meta::fields<T>()` 로 체인 전체를 한 번에 받으려 했는데
    // MSVC 가 "이니셜라이저가 너무 많이 중첩되었습니다" 로 막았다 — 그 질의가
    // 체인을 `tuple_cat` 으로 물질화하기 때문이다. 그리는 쪽이 이미 재귀로
    // 내려가므로 힌트도 같은 모양으로 맞춘다.
    // 읽기 전용 구간. 그리는 분기가 20 갈래라 각 분기가 스스로 짝을 맞추면
    // 하나만 빠져도 ImGui 의 비활성 스택이 어긋난 채 프레임이 끝난다.
    struct DisabledScope
    {
        bool active{ false };

        explicit DisabledScope(bool on) : active(on)
        {
            if (active)
            {
                ImGui::BeginDisabled();
            }
        }

        ~DisabledScope()
        {
            if (active)
            {
                ImGui::EndDisabled();
            }
        }

        DisabledScope(const DisabledScope&) = delete;
        DisabledScope& operator=(const DisabledScope&) = delete;
    };

    template<meta::reflectable T>
    inline float LabelHintOf()
    {
        using Desc = std::remove_cvref_t<decltype(meta::schema_of<T>)>;

        float widest = 0.f;
        if constexpr (Desc::has_base)
        {
            widest = LabelHintOf<typename Desc::base_type>();
        }
        std::apply([&](const auto&... ms)
        {
            ((widest = ImMax(widest,
                ImGui::CalcTextSize(MemberLabel(ms)).x)), ...);
        }, meta::schema_of<T>.fields);
        return widest;
    }

    // 리플렉션 한 프레임의 배치를 세우고 나갈 때 되돌린다.
    //
    // 중첩 구조체는 들여쓰기로 가용 폭이 달라지므로 진입할 때 다시 잰다.
    // 되돌리지 않으면 안쪽에서 잰 좁은 열이 바깥 줄에 그대로 남는다.
    struct LayoutScope
    {
        editor::widgets::property_layout_metrics saved{};

        explicit LayoutScope(float labelHint)
        {
            // 폭 전환의 직전 모드. 리플렉션 경로 전체가 한 판정을 쓴다 —
            // 줄마다 따로 두면 같은 프레임 안에서 어떤 줄은 inline, 어떤 줄은
            // stacked 가 된다.
            static editor::widgets::property_layout_state state{};
            saved = editor::widgets::push_property_layout(
                editor::widgets::measure_property_layout(
                    editor::widgets::property_layout_inputs_now(0, labelHint), state));
        }

        ~LayoutScope()
        {
            editor::widgets::push_property_layout(saved);
        }

        LayoutScope(const LayoutScope&) = delete;
        LayoutScope& operator=(const LayoutScope&) = delete;
    };

    // 멤버 하나 — 컴파일타임 카테고리 디스패치.
    template<class Owner, class MI>
    inline void DrawOneMember(Owner& obj, const MI& mi)
    {
        constexpr auto MP = std::remove_cvref_t<MI>::pointer;
        using MemberT = std::remove_cvref_t<decltype(obj.*MP)>;

        const char* name = std::remove_cvref_t<MI>::identifier.data();

        // 숨김은 속성이 정한다 (W2-I3).
        //
        // 예전에는 여기서 필드 이름을 문자열로 비교해 `m_isEnabled` 를 걸렀다.
        // 필드를 옮기거나 이름을 바꾸면 그 규칙이 조용히 풀리는 자리였고,
        // 다른 내부 필드를 숨길 길도 없었다. 규칙을 선언 쪽으로 옮긴다 —
        // `Object.h` 의 `meta::hidden()` 이 그 자리다.
        if constexpr (std::remove_cvref_t<MI>::template has_attribute<meta::hidden_attr>())
        {
            return;
        }

        // 디버그 전용은 모드가 켜졌을 때만 그린다.
        //
        // `hidden` 과 달리 판정이 런타임이다. 속성이 붙었는지는 컴파일 시에
        // 정해지므로 `if constexpr` 로 감싼다 — 붙지 않은 필드에는 이 검사
        // 코드가 아예 생성되지 않는다.
        if constexpr (std::remove_cvref_t<MI>::template has_attribute<meta::debug_only_attr>())
        {
            if (!editor::widgets::property_debug_mode())
            {
                return;
            }
        }

        // 읽기 전용이면 그리되 손이 닿지 않는다. 비활성 위젯은 언제나 거짓을
        // 돌려주므로 아래 `changed` 경로와 언두는 저절로 닫힌다.
        const DisabledScope disabled{
            std::remove_cvref_t<MI>::template has_attribute<meta::readonly_attr>() };

        // CT6-b 속성 소비
        const char* label = MemberLabel(mi);
        bool hasRange = false;
        float rangeMin = 0.0f;
        float rangeMax = 0.0f;
        if constexpr (std::remove_cvref_t<MI>::template has_attribute<meta::range_attr<float>>())
        {
            const auto r = mi.template attribute<meta::range_attr<float>>();
            hasRange = true;
            rangeMin = r.min;
            rangeMax = r.max;
        }

        MemberT& value = obj.*MP;

        // 넓은 줄은 두 갈래로 정해진다 — 필드에 붙은 `meta::wide()` 와 값
        // 타입의 드로어가 선언한 `kWideMode`. s&box 의 `[WideMode]` 와
        // `ControlWidget.IsWideMode` 가 같은 짝이다. 둘 다 컴파일 시에
        // 정해지므로 넓히지 않는 줄은 사본조차 만들지 않는다.
        constexpr bool kWideRow =
            std::remove_cvref_t<MI>::template has_attribute<meta::wide_attr>() ||
            editor::inspector::DrawerWantsWideRow<MemberT>;

        const editor::widgets::property_layout_metrics layout = []
        {
            const auto& section = editor::widgets::current_property_layout();
            if constexpr (kWideRow)
            {
                return editor::widgets::widen_property_line(section);
            }
            else
            {
                return section;
            }
        }();

        // ── 값 타입 커스텀 드로어가 먼저다 (W2-I) ──────────────────────────
        //
        // `InspectorDrawer<MemberT>` 특수화가 있으면 아래 분기 사슬을 전부
        // 건너뛴다. 엔진 분기를 고치지 않고 타입을 더할 수 있는 자리이며,
        // 엔진이 싣는 기본 드로어도 같은 문으로 들어온다
        // (`InspectorDrawerList.h`). 특수화가 없으면 이 `if` 는 컴파일에서
        // 통째로 사라지므로 런타임 비용도 간접 호출도 없다.
        if constexpr (editor::inspector::HasInspectorDrawer<MemberT>)
        {
            MemberT v = value;
            ImGui::PushID(name);
            ImGui::SetNextItemWidth(editor::widgets::begin_property_line(label, layout));
            if (editor::inspector::InspectorDrawer<MemberT>::Draw(kValueId, v))
            {
                CommitMemberChange<Owner, MemberT, MP>(&obj, value, v, name);
                value = v;
            }
            ImGui::PopID();
        }
        else if constexpr (std::is_same_v<MemberT, int>)
        {
            int v = value;
            ImGui::PushID(name);
            ImGui::SetNextItemWidth(editor::widgets::begin_property_line(label, layout));
            const bool changed = hasRange
                ? ImGui::SliderInt(kValueId, &v, static_cast<int>(rangeMin), static_cast<int>(rangeMax))
                : ImGui::DragInt(kValueId, &v);
            if (changed)
            {
                CommitMemberChange<Owner, MemberT, MP>(&obj, value, v, name);
                value = v;
            }
            ImGui::PopID();
        }
        else if constexpr (std::is_same_v<MemberT, unsigned int>)
        {
            unsigned int v = value;
            ImGui::PushID(name);
            ImGui::SetNextItemWidth(editor::widgets::begin_property_line(label, layout));
            if (ImGui::DragScalar(kValueId, ImGuiDataType_U32, &v))
            {
                CommitMemberChange<Owner, MemberT, MP>(&obj, value, v, name);
                value = v;
            }
            ImGui::PopID();
        }
        else if constexpr (std::is_same_v<MemberT, long long>)
        {
            long long v = value;
            ImGui::PushID(name);
            ImGui::SetNextItemWidth(editor::widgets::begin_property_line(label, layout));
            if (ImGui::DragScalar(kValueId, ImGuiDataType_S64, &v))
            {
                CommitMemberChange<Owner, MemberT, MP>(&obj, value, v, name);
                value = v;
            }
            ImGui::PopID();
        }
        else if constexpr (std::is_same_v<MemberT, float>)
        {
            float v = value;
            ImGui::PushID(name);
            ImGui::SetNextItemWidth(editor::widgets::begin_property_line(label, layout));
            const bool changed = hasRange
                ? ImGui::SliderFloat(kValueId, &v, rangeMin, rangeMax)
                : editor::widgets::drag_property_float(kValueId, &v);
            if (changed)
            {
                CommitMemberChange<Owner, MemberT, MP>(&obj, value, v, name);
                value = v;
            }
            ImGui::PopID();
        }
        else if constexpr (std::is_same_v<MemberT, bool>)
        {
            bool v = value;
            ImGui::PushID(name);
            editor::widgets::begin_property_line(label, layout);
            if (ImGui::Checkbox(kValueId, &v))
            {
                CommitMemberChange<Owner, MemberT, MP>(&obj, value, v, name);
                value = v;
            }
            ImGui::PopID();
        }
        else if constexpr (std::is_same_v<MemberT, std::string>)
        {
            std::string v = value;
            ImGui::PushID(name);
            ImGui::SetNextItemWidth(editor::widgets::begin_property_line(label, layout));
            if (ImGui::InputText(kValueId, v.data(), v.size() + 1,
                ImGuiInputTextFlags_CallbackResize, Meta::InputTextCallback,
                static_cast<void*>(&v)))
            {
                if (InputManagement->IsKeyPressed(VK_RETURN))
                {
                    CommitMemberChange<Owner, MemberT, MP>(&obj, value, v, name);
                    value = v;
                }
            }
            ImGui::PopID();
        }
        else if constexpr (std::is_same_v<MemberT, HashingString>)
        {
            // PHASE 15 H-a. 예전에는 HashingString 복사본을 만들어 그 내부 버퍼를
            // ImGui에 직접 넘겼다. 셋이 한꺼번에 깨져 있었다 — ImGui가 버퍼에
            // 써도 m_hash가 갱신되지 않아 문자열과 해시가 어긋났고, 버퍼 크기로
            // 현재 길이를 넘겨 이름을 늘릴 수 없었으며, 늘리면 범위 밖 쓰기였다.
            //
            // 바로 위 std::string 브랜치와 같은 모양으로 맞춘다. 편집은
            // std::string 버퍼에서 하고(리사이즈 콜백이 성장을 감당한다),
            // 확정할 때 HashingString을 새로 만들어 setter로 커밋한다 —
            // 그래야 해시가 다시 계산된다.
            std::string buffer = value.ToString();
            ImGui::PushID(name);
            ImGui::SetNextItemWidth(editor::widgets::begin_property_line(label, layout));
            if (ImGui::InputText(kValueId, buffer.data(), buffer.size() + 1,
                ImGuiInputTextFlags_CallbackResize, Meta::InputTextCallback,
                static_cast<void*>(&buffer)))
            {
                // 빈 이름 커밋은 저작 UX 판단으로 막는다. HashingString 자체는
                // 빈 문자열을 합법으로 다루지만(H5 결정), 이름이 비면 하이어라키에서
                // 그 오브젝트를 다시 고를 수 없다 — 되돌리기 어려운 편집이다.
                if (InputManagement->IsKeyPressed(VK_RETURN) && !buffer.empty())
                {
                    HashingString committed(std::string_view{ buffer });
                    CommitMemberChange<Owner, MemberT, MP>(&obj, value, committed, name);
                    value = committed;
                }
            }
            ImGui::PopID();
        }
        else if constexpr (std::is_same_v<MemberT, math::vector2>)
        {
            MemberT v = value;
            ImGui::PushID(name);
            ImGui::SetNextItemWidth(editor::widgets::begin_property_line(label, layout));
            if (editor::widgets::drag_property_floats(kValueId, &v.x, 2))
            {
                CommitMemberChange<Owner, MemberT, MP>(&obj, value, v, name);
                value = v;
            }
            ImGui::PopID();
        }
        else if constexpr (std::is_same_v<MemberT, math::vector4>
            || std::is_same_v<MemberT, math::quaternion>)
        {
            MemberT v = value;
            ImGui::PushID(name);
            ImGui::SetNextItemWidth(editor::widgets::begin_property_line(label, layout));
            if (editor::widgets::drag_property_floats(kValueId, &v.x, 4, 0.1f))
            {
                CommitMemberChange<Owner, MemberT, MP>(&obj, value, v, name);
                value = v;
            }
            ImGui::PopID();
        }
        else if constexpr (std::is_same_v<MemberT, math::color>)
        {
            math::color v = value;
            ImGui::PushID(name);
            ImGui::SetNextItemWidth(editor::widgets::begin_property_line(label, layout));
            if (ImGui::ColorEdit4(kValueId, &v.r))
            {
                CommitMemberChange<Owner, MemberT, MP>(&obj, value, v, name);
                value = v;
            }
            ImGui::PopID();
        }
        else if constexpr (std::is_same_v<MemberT, math::rect>)
        {
            math::rect v = value;
            ImGui::PushID(name);
            ImGui::SetNextItemWidth(editor::widgets::begin_property_line(label, layout));
            if (editor::widgets::drag_property_floats(kValueId, &v.x, 4))
            {
                CommitMemberChange<Owner, MemberT, MP>(&obj, value, v, name);
                value = v;
            }
            ImGui::PopID();
        }
        else if constexpr (std::is_enum_v<MemberT>)
        {
            ImGui::SetNextItemWidth(editor::widgets::begin_property_line(label, layout));
            DrawEnumCombo(kValueId, name, value);
        }
        // ── 컨테이너 (range 일반화) ────────────────────────────────────────
        //
        // 예전에는 `std::vector<E>` 다섯 벌을 구체 타입 비교로 적어 두었다.
        // 실측하면 그중 등록된 필드가 있는 것은 float·string 둘뿐이고 나머지
        // 셋(int·vector2·vector3)은 **죽은 분기**였다. 그리고 목록 밖의
        // 컨테이너는 마지막 else 로 떨어져 조용히 안 그려졌다 — `vector<uint16_t>`
        // 도, `vector<AssetEntry>` 처럼 reflect() 원소를 담은 벡터 일곱 종도
        // 인스펙터에 아예 나타나지 않았다.
        //
        // 직렬화기와 같은 판정(meta::container)으로 갈아 끼운다. 이름이 아니라
        // 능력으로 고르므로 deque·list·array·set 이 같은 길로 들어온다.
        else if constexpr (meta::container::SequenceRange<MemberT>
            && !editor::inspector::HasInspectorDrawer<MemberT>)
        {
            using E = meta::container::ValueT<MemberT>;

            if constexpr (std::is_pointer_v<E> || is_shared_ptr_v<E> || is_unique_ptr_v<E>)
            {
                // 포인터 원소 목록(Entity::m_components 등)은 여기 몫이 아니다 —
                // 소유자 인스펙터가 따로 그린다. 종전과 같이 넘어간다.
            }
            else if constexpr (meta::container::ConstElementRange<MemberT>)
            {
                // std::set 계열은 원소를 const 로만 내준다. 제자리 편집이
                // 성립하지 않으므로(키를 고치면 정렬이 깨진다) 읽기로만 낸다.
                ImGui::PushID(name);
                if (ContainerHeader(label))
                {
                    int index = 0;
                    for (const E& element : value)
                    {
                        ImGui::PushID(index);
                        E scratch = element;
                        const DisabledScope readOnly{ true };
                        ImGui::SetNextItemWidth(BeginElementLine(index));
                        DrawContainerElement(kValueId, scratch);
                        ImGui::PopID();
                        ++index;
                    }
                }
                ImGui::PopID();
            }
            else if constexpr (kElementHasWidget<E>
                && meta::container::BackInsertable<MemberT>)
            {
                DrawSequenceEditor(name, label, value, E{},
                    [](int, E& element)
                    {
                        DrawContainerElement(kValueId, element);
                    });
            }
            else
            {
                // 크기가 고정된 배열이거나(Add/Remove 가 성립하지 않는다),
                // 원소가 reflect() 타입이라 한 줄 위젯으로 안 접히는 경우.
                ImGui::PushID(name);
                if (ContainerHeader(label))
                {
                    int index = 0;
                    for (auto& element : value)
                    {
                        ImGui::PushID(index);
                        if constexpr (kElementHasWidget<E>)
                        {
                            ImGui::SetNextItemWidth(BeginElementLine(index));
                            DrawContainerElement(kValueId, element);
                        }
                        else if constexpr (meta::reflectable<E>)
                        {
                            if (editor::widgets::property_group_header(
                                std::to_string(index).c_str()))
                            {
                                DrawTypedObject(element);
                            }
                        }
                        else
                        {
                            ImGui::TextDisabled("[%d] [no widget]", index);
                        }
                        ImGui::PopID();
                        ++index;
                    }
                }
                ImGui::PopID();
            }
        }
        else if constexpr (meta::container::KeyedRange<MemberT>
            && !editor::inspector::HasInspectorDrawer<MemberT>)
        {
            using M = meta::container::MappedT<MemberT>;

            // 키는 줄의 이름이 된다. 키 자체를 고치는 길은 두지 않는다 —
            // 맵에서 키를 바꾸는 것은 항목을 지우고 새로 넣는 일이라, 같은
            // 프레임에 순회를 무너뜨린다. 항목 추가·삭제도 같은 이유로 없다.
            ImGui::PushID(name);
            if (ContainerHeader(label))
            {
                int index = 0;
                for (auto& entry : value)
                {
                    ImGui::PushID(index++);
                    const std::string keyLabel =
                        Meta::Typed::EncodeMapKey(entry.first);
                    if constexpr (kElementHasWidget<M>)
                    {
                        ImGui::SetNextItemWidth(
                            editor::widgets::begin_property_line(keyLabel.c_str(), layout));
                        DrawContainerElement(kValueId, entry.second);
                    }
                    else if constexpr (meta::reflectable<M>)
                    {
                        if (editor::widgets::property_group_header(keyLabel.c_str()))
                        {
                            DrawTypedObject(entry.second);
                        }
                    }
                    else
                    {
                        ImGui::TextDisabled("%s: [no widget]", keyLabel.c_str());
                    }
                    ImGui::PopID();
                }
            }
            ImGui::PopID();
        }
        else if constexpr (std::is_pointer_v<MemberT> || is_shared_ptr_v<MemberT>)
        {
            using U = Meta::Typed::PointeeT<MemberT>;
            auto* p = Meta::Typed::RawPtrOf(value);

            if (nullptr != p)
            {
                if constexpr (meta::reflectable<U>)
                {
                    ImGui::PushID(name);
                    if (editor::widgets::property_group_header(label))
                    {
                        DrawTypedObject(*p);
                    }
                    ImGui::PopID();
                }
                else
                {
                    ImGui::Text("%s: [Unregistered Type For GUI Debug]", name);
                }
            }
            else
            {
                ImGui::Text("%s: nullptr [For GUI Debug]", name);
            }
        }
        else if constexpr (meta::reflectable<MemberT>)
        {
            ImGui::PushID(name);
            if (editor::widgets::property_group_header(label))
            {
                DrawTypedObject(value);
            }
            ImGui::PopID();
        }
        else
        {
            // 레거시 파리티: 어느 분기에도 없던 타입은 조용히 넘어갔다
            // (double·미등록 구조체 등). 같은 침묵을 유지한다.
        }
    }

    // 서술 프레임 하나 — 레거시 DrawObject의 PushID(type.name)·부모 우선·
    // 멤버·메서드 순서를 그대로 따른다.
    template<meta::reflectable T, class Owner>
    inline void DrawFrame(Owner& obj)
    {
        // CT8→CT9: 정적 desc 사본 대신 canonical 물질화(meta::schema_of<T>)
        // 직접 참조 — DrawFrame<T, Owner>가 Owner 조합마다 서술자를 중복
        // 물질화하던 지점이었다. 부모는 로컬 스키마의 base_type 체인.
        using Desc = std::remove_cvref_t<decltype(meta::schema_of<T>)>;

        ImGui::PushID(Desc::identifier.data());

        if constexpr (Desc::has_base)
        {
            DrawFrame<typename Desc::base_type>(obj);
        }

        std::apply([&](const auto&... ms)
        {
            (DrawOneMember(obj, ms), ...);
        }, meta::schema_of<T>.fields);

        // 메서드는 레거시 DrawMethods 재사용 (파라미터 UI 파리티) — 프레임의
        // 런타임 Type을 넘긴다.
        Meta::DrawMethods(static_cast<void*>(&obj), Meta::TypeOf<T>());

        ImGui::PopID();
    }

    // 자기 프레임 필드만 보는 힌트. `DrawOwnMembers` 가 부모를 그리지 않으므로
    // 그쪽은 이 값을 쓴다.
    template<meta::reflectable T>
    inline float OwnLabelHintOf()
    {
        float widest = 0.f;
        std::apply([&](const auto&... ms)
        {
            ((widest = ImMax(widest,
                ImGui::CalcTextSize(MemberLabel(ms)).x)), ...);
        }, meta::schema_of<T>.fields);
        return widest;
    }

    // 타입 이름을 리플렉션에서 직접 읽어 한 줄로 놓는다 (디버그 모드 전용).
    //
    // 컴포넌트의 `Object::m_name` 은 타입 이름 **사본**이다. 씬 파일이
    // `m_name: CameraComponent` 로 적어 두고 로드가 되읽는 값이라, 사람이
    // 고치면 그대로 디스크에 남는다. 사본을 읽기 전용으로 돌린 것과 별개로,
    // 디버그 모드에서 보는 이름은 사본이 아니라 **정본**이어야 한다.
    //
    // 여기 있는 값은 `meta::schema_of<T>` 의 식별자다. 컴파일 시 상수이고
    // 직렬화되지 않으므로 고칠 자리 자체가 없다 — 읽기 전용으로 "막은" 것이
    // 아니라 쓸 수 있는 저장소가 없는 것이다.
    template<meta::reflectable T>
    inline void DrawTypeIdentityRow()
    {
        if (!editor::widgets::property_debug_mode())
        {
            return;
        }

        using Desc = std::remove_cvref_t<decltype(meta::schema_of<T>)>;

        const DisabledScope disabled{ true };
        const auto& layout = editor::widgets::current_property_layout();
        (void)editor::widgets::begin_property_line("Type", layout);

        // 길이를 함께 넘긴다. 식별자는 `string_view` 라 널로 끝난다는 보장이 없다.
        ImGui::TextUnformatted(Desc::identifier.data(),
            Desc::identifier.data() + Desc::identifier.size());
    }

    template<meta::reflectable T>
    void DrawTypedObject(T& obj)
    {
        // 배치는 프레임 하나에 한 번 잰다. 상속 체인 전체가 같은 라벨 열에
        // 서야 하므로 힌트도 체인 전체에서 뽑는다.
        const LayoutScope scope{ LabelHintOf<T>() };
        DrawTypeIdentityRow<T>();
        DrawFrame<T>(obj);
    }

    // 레거시 DrawProperties 등가 — 자기 프레임 멤버만(부모·메서드 제외).
    // PassSetting 패널·서브 구조체 패널처럼 "이 타입의 프로퍼티만" 그리던
    // 직접 호출자들의 대체다 (CT7).
    template<meta::reflectable T>
    void DrawOwnMembers(T& obj)
    {
        const LayoutScope scope{ OwnLabelHintOf<T>() };
        std::apply([&](const auto&... ms)
        {
            (DrawOneMember(obj, ms), ...);
        }, meta::schema_of<T>.fields);
    }

    template<class T>
    void DrawThunk(void* instance)
    {
        DrawTypedObject(*static_cast<T*>(instance));
    }

    template<class T>
    inline void RegisterDraw()
    {
        Registry()[TypeTrait::GUIDCreator::GetTypeID<T>().m_ID_Data] = &DrawThunk<T>;
    }
}
