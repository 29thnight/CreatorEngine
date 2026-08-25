#pragma once
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
#include "ReflectionTypedYml.h" // Typed::PointeeT·RawPtrOf 재사용
#include <cstddef>

static_assert(std::is_standard_layout_v<math::vector4>);
static_assert(std::is_trivially_copyable_v<math::vector4>);
static_assert(offsetof(math::vector4, y) == sizeof(float));
static_assert(offsetof(math::vector4, z) == sizeof(float) * 2);
static_assert(offsetof(math::vector4, w) == sizeof(float) * 3);

namespace Meta::TypedDraw
{
    // DrawFn·Registry·FindDraw는 ReflectionImGuiHelper.h(디스패치 지점)에 있다.

    // 멤버 변경을 언두 스택에 싣는다 — 레거시 MakePropChangeCommand 등가.
    template<class Owner, class MemberT, auto MP>
    inline void CommitMemberChange(Owner* obj, const MemberT& prevValue, const MemberT& newValue)
    {
        Meta::MakeCustomChangeCommand(
            [obj, prevValue]() { obj->*MP = prevValue; },
            [obj, newValue]() { obj->*MP = newValue; });
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

    // 스칼라 원소 벡터 편집기 — 레거시 블록(Add/Remove/개별 위젯/^v 재배열)과
    // 동일한 조작을 원본 벡터에 직접 한다(접힘 조기 검사 포함, CT1 규칙 유지).
    template<class E, class WidgetFn>
    inline void DrawVectorEditor(const char* idName, const char* label,
        std::vector<E>& vec, E defaultValue, WidgetFn&& widget)
    {
        ImGui::PushID(idName);
        if (ImGui::CollapsingHeader(label))
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
            for (int i = 0; i < size; ++i)
            {
                ImGui::PushID(i);
                widget(i, vec[static_cast<size_t>(i)]);

                if (size > 0)
                {
                    ImGui::SameLine();
                    if (ImGui::Button("^") && i > 0)
                    {
                        std::swap(vec[static_cast<size_t>(i)], vec[static_cast<size_t>(i) - 1]);
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("v") && i < size - 1)
                    {
                        std::swap(vec[static_cast<size_t>(i)], vec[static_cast<size_t>(i) + 1]);
                    }
                }
                ImGui::PopID();
            }
        }
        ImGui::PopID();
    }

    template<meta::reflectable T>
    void DrawTypedObject(T& obj);

    // 멤버 하나 — 컴파일타임 카테고리 디스패치.
    template<class Owner, class MI>
    inline void DrawOneMember(Owner& obj, const MI& mi)
    {
        constexpr auto MP = std::remove_cvref_t<MI>::pointer;
        using MemberT = std::remove_cvref_t<decltype(obj.*MP)>;

        const char* name = std::remove_cvref_t<MI>::identifier.data();

        // 레거시 스킵 규칙 파리티: m_isEnabled는 전용 체크박스가 담당
        // (SetEnabled 경유 — OnEnable/OnDisable 훅 보존).
        if (std::strcmp(name, "m_isEnabled") == 0)
        {
            return;
        }

        // CT6-b 속성 소비
        const char* label = name;
        bool hasRange = false;
        float rangeMin = 0.0f;
        float rangeMax = 0.0f;
        if constexpr (std::remove_cvref_t<MI>::template has_attribute<meta::display_name_attr>())
        {
            label = mi.template attribute<meta::display_name_attr>().value.data();
        }
        if constexpr (std::remove_cvref_t<MI>::template has_attribute<meta::range_attr<float>>())
        {
            const auto r = mi.template attribute<meta::range_attr<float>>();
            hasRange = true;
            rangeMin = r.min;
            rangeMax = r.max;
        }

        MemberT& value = obj.*MP;

        if constexpr (std::is_same_v<MemberT, int>)
        {
            int v = value;
            ImGui::PushID(name);
            const bool changed = hasRange
                ? ImGui::SliderInt(label, &v, static_cast<int>(rangeMin), static_cast<int>(rangeMax))
                : ImGui::DragInt(label, &v);
            if (changed)
            {
                CommitMemberChange<Owner, MemberT, MP>(&obj, value, v);
                value = v;
            }
            ImGui::PopID();
        }
        else if constexpr (std::is_same_v<MemberT, unsigned int>)
        {
            unsigned int v = value;
            ImGui::PushID(name);
            if (ImGui::DragScalar(label, ImGuiDataType_U32, &v))
            {
                CommitMemberChange<Owner, MemberT, MP>(&obj, value, v);
                value = v;
            }
            ImGui::PopID();
        }
        else if constexpr (std::is_same_v<MemberT, long long>)
        {
            long long v = value;
            ImGui::PushID(name);
            if (ImGui::DragScalar(label, ImGuiDataType_S64, &v))
            {
                CommitMemberChange<Owner, MemberT, MP>(&obj, value, v);
                value = v;
            }
            ImGui::PopID();
        }
        else if constexpr (std::is_same_v<MemberT, float>)
        {
            float v = value;
            ImGui::PushID(name);
            const bool changed = hasRange
                ? ImGui::SliderFloat(label, &v, rangeMin, rangeMax)
                : ImGui::DragFloat(label, &v);
            if (changed)
            {
                CommitMemberChange<Owner, MemberT, MP>(&obj, value, v);
                value = v;
            }
            ImGui::PopID();
        }
        else if constexpr (std::is_same_v<MemberT, bool>)
        {
            bool v = value;
            ImGui::PushID(name);
            if (ImGui::Checkbox(label, &v))
            {
                CommitMemberChange<Owner, MemberT, MP>(&obj, value, v);
                value = v;
            }
            ImGui::PopID();
        }
        else if constexpr (std::is_same_v<MemberT, std::string>)
        {
            std::string v = value;
            ImGui::PushID(name);
            if (ImGui::InputText(label, v.data(), v.size() + 1,
                ImGuiInputTextFlags_CallbackResize, Meta::InputTextCallback,
                static_cast<void*>(&v)))
            {
                if (InputManagement->IsKeyPressed(VK_RETURN))
                {
                    CommitMemberChange<Owner, MemberT, MP>(&obj, value, v);
                    value = v;
                }
            }
            ImGui::PopID();
        }
        else if constexpr (std::is_same_v<MemberT, HashingString>)
        {
            HashingString v = value;
            ImGui::PushID(name);
            if (ImGui::InputText(label, v.data(), v.size() + 1))
            {
                CommitMemberChange<Owner, MemberT, MP>(&obj, value, v);
                value = v;
            }
            ImGui::PopID();
        }
        else if constexpr (std::is_same_v<MemberT, Mathf::Vector2>)
        {
            Mathf::Vector2 v = value;
            ImGui::PushID(name);
            if (ImGui::DragFloat2(label, &v.x))
            {
                CommitMemberChange<Owner, MemberT, MP>(&obj, value, v);
                value = v;
            }
            ImGui::PopID();
        }
        else if constexpr (std::is_same_v<MemberT, Mathf::Vector3>)
        {
            Mathf::Vector3 v = value;
            ImGui::PushID(name);
            if (ImGui::DragFloat3(label, &v.x))
            {
                CommitMemberChange<Owner, MemberT, MP>(&obj, value, v);
                value = v;
            }
            ImGui::PopID();
        }
        else if constexpr (std::is_same_v<MemberT, Mathf::Vector4>
            || std::is_same_v<MemberT, Mathf::Quaternion>
            || std::is_same_v<MemberT, math::vector4>
            || std::is_same_v<MemberT, math::quaternion>)
        {
            MemberT v = value;
            ImGui::PushID(name);
            if (ImGui::DragFloat4(label, &v.x, 0.1f))
            {
                CommitMemberChange<Owner, MemberT, MP>(&obj, value, v);
                value = v;
            }
            ImGui::PopID();
        }
        else if constexpr (std::is_same_v<MemberT, Mathf::Color4>)
        {
            Mathf::Color4 v = value;
            ImGui::PushID(name);
            if (ImGui::ColorEdit4(label, &v.x))
            {
                CommitMemberChange<Owner, MemberT, MP>(&obj, value, v);
                value = v;
            }
            ImGui::PopID();
        }
        else if constexpr (std::is_same_v<MemberT, Mathf::Rect>)
        {
            Mathf::Rect v = value;
            ImGui::PushID(name);
            if (ImGui::DragFloat4(label, &v.x))
            {
                CommitMemberChange<Owner, MemberT, MP>(&obj, value, v);
                value = v;
            }
            ImGui::PopID();
        }
        else if constexpr (std::is_enum_v<MemberT>)
        {
            DrawEnumCombo(label, name, value);
        }
        else if constexpr (std::is_same_v<MemberT, std::vector<std::string>>)
        {
            DrawVectorEditor(name, label, value, std::string{},
                [](int i, std::string& s)
                {
                    char buf[128];
                    strncpy_s(buf, sizeof(buf), s.c_str(), _TRUNCATE);
                    buf[sizeof(buf) - 1] = '\0';
                    if (ImGui::InputText(("##" + std::to_string(i)).c_str(), buf, sizeof(buf)))
                    {
                        s = std::string(buf);
                    }
                });
        }
        else if constexpr (std::is_same_v<MemberT, std::vector<int>>)
        {
            DrawVectorEditor(name, label, value, 0,
                [](int i, int& v)
                {
                    ImGui::InputInt(("##" + std::to_string(i)).c_str(), &v);
                });
        }
        else if constexpr (std::is_same_v<MemberT, std::vector<float>>)
        {
            DrawVectorEditor(name, label, value, 0.0f,
                [](int i, float& v)
                {
                    ImGui::InputFloat(("##" + std::to_string(i)).c_str(), &v);
                });
        }
        else if constexpr (std::is_same_v<MemberT, std::vector<Mathf::Vector2>>)
        {
            DrawVectorEditor(name, label, value, Mathf::Vector2{ 0.f, 0.f },
                [](int i, Mathf::Vector2& v)
                {
                    ImGui::InputFloat2(("##" + std::to_string(i)).c_str(), &v.x);
                });
        }
        else if constexpr (std::is_same_v<MemberT, std::vector<Mathf::Vector3>>)
        {
            DrawVectorEditor(name, label, value, Mathf::Vector3{ 0.f, 0.f, 0.f },
                [](int i, Mathf::Vector3& v)
                {
                    ImGui::InputFloat3(("##" + std::to_string(i)).c_str(), &v.x);
                });
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
                    if (ImGui::CollapsingHeader(label, ImGuiTreeNodeFlags_DefaultOpen))
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
            if (ImGui::CollapsingHeader(label, ImGuiTreeNodeFlags_DefaultOpen))
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

    template<meta::reflectable T>
    void DrawTypedObject(T& obj)
    {
        DrawFrame<T>(obj);
    }

    // 레거시 DrawProperties 등가 — 자기 프레임 멤버만(부모·메서드 제외).
    // PassSetting 패널·서브 구조체 패널처럼 "이 타입의 프로퍼티만" 그리던
    // 직접 호출자들의 대체다 (CT7).
    template<meta::reflectable T>
    void DrawOwnMembers(T& obj)
    {
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
