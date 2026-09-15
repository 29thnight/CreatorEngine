// Numeric activation is adapted from Dear ImGui 1.92.8 (MIT).
// Copyright (c) 2014-2026 Omar Cornut
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
#include "EditorPropertyRow.h"
#include "EditorNavContract.h"
#include "EditorClipContract.h"
#include "EditorStateContract.h"

#include "ImGui.h"
#include "EditorTheme.h"

#include <array>
#include <cstring>

namespace editor::widgets
{
    void compact_property_number(char* text) noexcept
    {
        if (!text || !*text) return;
        char* dot = nullptr;
        char* end = text + (*text == '-' || *text == '+');
        bool digits = false;
        for (; *end; ++end)
        {
            if (*end == '.' && !dot) dot = end;
            else if (*end >= '0' && *end <= '9') digits = true;
            else return;
        }
        if (!dot || !digits) return;
        while (end > dot + 1 && end[-1] == '0') --end;
        if (end == dot + 1) --end;
        *end = '\0';
        if (std::strcmp(text, "-0") == 0 || std::strcmp(text, "+0") == 0)
        {
            text[0] = '0';
            text[1] = '\0';
        }
    }

    bool drag_property_float(const char* label, float* value, float speed,
        float min, float max, const char* format, int flags, bool joined_left)
    {
        if (!InspectorStyleActive())
            return ImGui::DragFloat(label, value, speed, min, max, format, flags);

        // Layout/activation follows Dear ImGui 1.92.8 DragScalar. DragBehavior
        // and TempInputScalar still own editing, rounding, navigation and input.
        // Only the idle/drag value renderer differs: compact and left aligned.
        // Dear ImGui: Copyright (c) 2014-2026 Omar Cornut, MIT license
        // (vcpkg_installed/.../share/imgui/copyright).
        using namespace ImGui;
        ImGuiWindow* window = GetCurrentWindow();
        if (window->SkipItems) return false;
        ImGuiContext& g = *GImGui;
        const ImGuiStyle& style = g.Style;
        const ImGuiID id = window->GetID(label);
        const char* label_end = FindRenderedTextEnd(label);
        const ImVec2 label_size = CalcTextSize(label, label_end);
        const ImRect frame(window->DC.CursorPos,
            window->DC.CursorPos + ImVec2(CalcItemWidth(), GetFrameHeight()));
        const ImRect total(frame.Min, frame.Max + ImVec2(
            label_size.x > 0.f ? style.ItemInnerSpacing.x + label_size.x : 0.f, 0.f));
        const bool input_allowed = !(flags & ImGuiSliderFlags_NoInput);
        ItemSize(total, style.FramePadding.y);
        if (!ItemAdd(total, id, &frame, input_allowed ? ImGuiItemFlags_Inputable : 0))
            return false;
        if (!format) format = "%.3f";
        const bool hovered = ItemHoverable(frame, id, g.LastItemData.ItemFlags);
        bool input = input_allowed && TempInputIsActive(id);
        if (!input)
        {
            const bool clicked = hovered && IsMouseClicked(0, ImGuiInputFlags_None, id);
            const bool twice = hovered && g.IO.MouseClickedCount[0] == 2 &&
                TestKeyOwner(ImGuiKey_MouseLeft, id);
            const bool activate = clicked || twice || g.NavActivateId == id;
            if (activate && (clicked || twice)) SetKeyOwner(ImGuiKey_MouseLeft, id);
            if (activate && input_allowed)
                input = (clicked && g.IO.KeyCtrl) || twice ||
                    (g.NavActivateId == id && (g.NavActivateFlags & ImGuiActivateFlags_PreferInput));
            if (g.IO.ConfigDragClickToInputText && input_allowed && !input &&
                g.ActiveId == id && hovered && g.IO.MouseReleased[0] &&
                !IsMouseDragPastThreshold(0, g.IO.MouseDragThreshold * 0.5f))
            {
                g.NavActivateId = id;
                g.NavActivateFlags = ImGuiActivateFlags_PreferInput;
                input = true;
            }
            if (activate) std::memcpy(&g.ActiveIdValueOnActivation, value, sizeof(float));
            if (activate && !input)
            {
                SetActiveID(id, window);
                SetFocusID(id, window);
                FocusWindow(window);
                g.ActiveIdUsingNavDirMask = (1 << ImGuiDir_Left) | (1 << ImGuiDir_Right);
            }
        }
        // W2-3: 상태 행렬.
        //
        // `focus` 와 `nav` 를 같은 자리에서 읽되 **다른 것으로** 적는다.
        // `focus` 는 "이 아이템이 키보드 입력을 받는 자리다"(`NavId` 가 나다)이고,
        // `nav` 는 거기에 "키보드로 와서 커서가 보인다"(`NavCursorVisible`)가
        // 더해진 것이다. 둘을 같은 식으로 적으면 이름만 둘이고 뜻은 하나가 된다.
        //
        // 값 줄은 편집 중(active)과 손댈 수 없음(disabled)을 모두 갖는다.
        ::editor::state::declare("EditorPropertyRow.drag",
            ::editor::state::hover | ::editor::state::active |
            ::editor::state::focus | ::editor::state::nav | ::editor::state::disabled,
            ::editor::state::mixed | ::editor::state::error,
            "mixed 는 Inspector 가 m_selectedEntity 하나만 그려 값이 갈리는 상황이 오지 않고(W2-I3 의 몫), error 는 값 검증이라는 원천이 아직 없다");

        if (input)
        {
            const bool clamp = (flags & ImGuiSliderFlags_ClampOnInput) &&
                (min < max || (min == max && (min != 0.f || (flags & ImGuiSliderFlags_ClampZeroRange))));
            // W2-1: 이 프레임의 그리기를 표준 위젯에 **넘긴다.** Tab 으로
            // `Inputable` 아이템에 들어가면 ImGui 가 `PreferInput` 으로 편집
            // 모드에 넣고, 그때부터 `InputTextEx` 가 이 칸과 nav 커서를 함께
            // 그린다 — 여기서 또 그리면 두 벌이다. 장부에는 넘겼다고 적는다.
            ::editor::nav::announce_item("EditorPropertyRow.drag", id, input_allowed, true);
            // W2-3: 그리기는 넘겼어도 이 아이템은 그 프레임에 **존재하고
            // 포커스를 쥐고 있다.** 여기서 신고하지 않으면 Tab 이 닿는 순간
            // 위젯이 행렬에서 사라져 `focus`·`nav` 가 영영 미관측으로 남는다 —
            // 자극이 도착하는 바로 그 경로가 장부의 눈을 가리는 셈이다.
            // `active` 는 여기서 "편집 중" 을 뜻한다.
            ::editor::state::announce("EditorPropertyRow.drag",
                (hovered ? ::editor::state::hover : 0u) |
                ((g.ActiveId == id) ? ::editor::state::active : 0u) |
                ((g.NavId == id) ? ::editor::state::focus : 0u) |
                ((g.NavId == id && g.NavCursorVisible) ? ::editor::state::nav : 0u) |
                ((input_allowed && 0 == (g.CurrentItemFlags & ImGuiItemFlags_Disabled))
                     ? 0u : ::editor::state::disabled),
                frame);
            return TempInputScalar(frame, id, label, ImGuiDataType_Float, value,
                format, clamp ? &min : nullptr, clamp ? &max : nullptr);
        }
        const ImU32 background = GetColorU32(g.ActiveId == id ? ImGuiCol_FrameBgActive :
            hovered ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg);
        window->DrawList->AddRectFilled(frame.Min, frame.Max, background,
            style.FrameRounding, joined_left ? ImDrawFlags_RoundCornersRight : ImDrawFlags_RoundCornersAll);
        RenderFrameBorder(frame.Min, frame.Max, style.FrameRounding);
        // 여기부터는 이 위젯이 직접 그린다 — 그리기 책임이 확정된 자리에서
        // 신고한다. `ItemAdd` 직후에 신고하면 위의 위임 경로까지 "내가 그린다"
        // 로 적히고, 그러면 판정이 옳은 동작을 위반으로 읽는다.
        ::editor::state::announce("EditorPropertyRow.drag",
            (hovered ? ::editor::state::hover : 0u) |
            ((g.ActiveId == id) ? ::editor::state::active : 0u) |
            ((g.NavId == id) ? ::editor::state::focus : 0u) |
            ((g.NavId == id && g.NavCursorVisible) ? ::editor::state::nav : 0u) |
            // disabled 는 둘의 합집합이다 — `NoInput` 플래그와 바깥의
            // `BeginDisabled`. 앞의 것만 읽으면 인스펙터가 실제로 쓰는
            // 기제를 통째로 못 본다.
            ((input_allowed && 0 == (g.CurrentItemFlags & ImGuiItemFlags_Disabled))
                 ? 0u : ::editor::state::disabled),
            frame);
        ::editor::nav::announce_item("EditorPropertyRow.drag", id, input_allowed);
        // `RenderNavCursor` 를 직접 부르지 않는다 — 그러면 그린 사실이 장부에
        // 남지 않아 판정이 "안 그렸다" 로 읽는다. 소스 대조 게이트가 직접
        // 호출을 막는다(verify-editor-keyboard-nav).
        ::editor::nav::draw_cursor(frame, id, "EditorPropertyRow.drag");
        const bool changed = DragBehavior(id, ImGuiDataType_Float, value,
            speed, &min, &max, format, flags);
        if (changed) MarkItemEdited(id);
        char buffer[64];
        DataTypeFormatString(buffer, IM_COUNTOF(buffer), ImGuiDataType_Float, value, format);
        compact_property_number(buffer);
        if (g.LogEnabled) LogSetNextTextDecoration("{", "}");
        RenderTextClipped(frame.Min + ImVec2(style.FramePadding.x, 0.f),
            frame.Max - ImVec2(style.FramePadding.x, 0.f), buffer, nullptr,
            nullptr, ImVec2(0.f, 0.5f), &frame);
        if (label_size.x > 0.f)
            RenderText(ImVec2(frame.Max.x + style.ItemInnerSpacing.x,
                frame.Min.y + style.FramePadding.y), label, label_end, false);
        IMGUI_TEST_ENGINE_ITEM_INFO(id, label, g.LastItemData.StatusFlags |
            (input_allowed ? ImGuiItemStatusFlags_Inputable : 0));
        return changed;
    }

    bool drag_property_floats(const char* label, float* values, int count,
        float speed, float min, float max, const char* format, int flags)
    {
        if (ImGui::GetCurrentWindow()->SkipItems) return false;
        bool changed = false;
        ImGui::BeginGroup();
        ImGui::PushID(label);
        ImGui::PushMultiItemsWidths(count, ImGui::CalcItemWidth());
        for (int i = 0; i < count; ++i)
        {
            ImGui::PushID(i);
            if (i) ImGui::SameLine(0.f, ImGui::GetStyle().ItemInnerSpacing.x);
            changed |= drag_property_float("", values + i, speed, min, max, format, flags);
            ImGui::PopID();
            ImGui::PopItemWidth();
        }
        ImGui::PopID();
        const char* end = ImGui::FindRenderedTextEnd(label);
        if (label != end)
        {
            ImGui::SameLine(0.f, ImGui::GetStyle().ItemInnerSpacing.x);
            ImGui::TextEx(label, end);
        }
        ImGui::EndGroup();
        return changed;
    }

    bool property_group_header(const char* label)
    {
        if (!InspectorStyleActive())
            return ImGui::CollapsingHeader(label, ImGuiTreeNodeFlags_DefaultOpen);
        // No extra ID push: child field IDs remain the same as CollapsingHeader.
        return ImGui::TreeNodeEx(label, ImGuiTreeNodeFlags_DefaultOpen |
            ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanAvailWidth |
            ImGuiTreeNodeFlags_FramePadding);
    }

    namespace
    {
        // 값 열의 ImGui ID. 원본은 `"##x"`·`"##y"` 였고 열이 둘로 고정이라
        // 성립했다. 인덱스로 고정한 표를 쓰면 열이 늘어도 앞선 열의 ID 가
        // 그대로라, 같은 줄에서 vec2 를 vec3 로 바꿔도 ImGui 의 편집 상태가
        // 딴 칸으로 옮겨 가지 않는다.
        //
        // 매 프레임 문자열을 만들지 않는 것도 이유다 — 이 줄은 인스펙터가
        // 열려 있는 동안 계속 도는 자리고, 성능 계약(§8.2)이 정적 경로의
        // frame 당 heap 할당 0 을 목표로 둔다.
        constexpr std::array<const char*, 4> kPropertyFieldIds{ "##f0", "##f1", "##f2", "##f3" };
    }

    int property_row_field_limit() noexcept
    {
        return static_cast<int>(kPropertyFieldIds.size());
    }

    const char* property_row_field_id(int index) noexcept
    {
        if (index < 0 || index >= static_cast<int>(kPropertyFieldIds.size()))
        {
            return nullptr;
        }
        return kPropertyFieldIds[static_cast<std::size_t>(index)];
    }

    bool draw_property_row(const property_row_request& request)
    {
        IM_ASSERT(nullptr != request.label && "속성 줄은 이름이 ID 의 씨앗이다");
        IM_ASSERT(nullptr != request.values && "값 없는 줄은 그릴 것이 없다");
        IM_ASSERT(request.count >= 1 && request.count <= property_row_field_limit() &&
            "값 열 수가 필드 ID 표를 벗어났다");

        if (nullptr == request.label || nullptr == request.values)
        {
            return false;
        }
        if (request.count < 1 || request.count > property_row_field_limit())
        {
            return false;
        }

        bool changed = false;
        ImGui::TableNextRow();

        // 0번 열 — 라벨. 원본의 규약 그대로다. 입력 위젯과 수직으로 맞추고
        // (`AlignTextToFramePadding`), 열이 좁아지면 잘리는 대신 접힌다.
        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + ImGui::GetContentRegionAvail().x);
        ImGui::TextUnformatted(request.label);
        ImGui::PopTextWrapPos();

        ImGui::PushID(request.label);
        for (int index = 0; index < request.count; ++index)
        {
            // 값 열은 1번부터다. 표가 그보다 적은 열로 세워졌으면
            // `TableSetColumnIndex` 가 거짓을 돌려주므로 그 칸은 건너뛴다 —
            // 없는 칸에 그리면 ImGui 가 어서션으로 죽는다.
            if (!ImGui::TableSetColumnIndex(index + 1))
            {
                continue;
            }
            ImGui::SetNextItemWidth(-FLT_MIN); // 셀 가용폭 전부
            changed |= drag_property_float(kPropertyFieldIds[static_cast<std::size_t>(index)],
                request.values + index, request.speed,
                request.min, request.max, request.format);
        }
        ImGui::PopID();

        return changed;
    }

    // ── 배치 계약 (W2-I2) ────────────────────────────────────────────────

    namespace
    {
        // 라벨 열 상한. 논리 픽셀이라 `ThemePixels` 를 한 번 통과한다.
        // 이 값보다 긴 라벨은 잘리고 tooltip 이 전체 이름을 준다 — 라벨이
        // 길다고 값 열을 먹게 두면 긴 이름 하나가 그 컴포넌트 전체의 열을
        // 밀어낸다.
        constexpr float kLabelMinLogical = 140.f;

        // 값 최소 폭을 재는 대표 문자열. **현재 값으로 재지 않는다.**
        // 현재 숫자로 재면 0 에서 -1234.567 로 바뀌는 순간 열이 움직이고,
        // 그 움직임이 다시 모드 판정을 흔든다. 계획서가 "현재 숫자 값이나 매
        // 프레임 라벨 최대값 변화로 열과 모드가 흔들리지 않게 한다" 고 한 자리다.
        constexpr const char* kValueSample = "-0000.000";

        // Minimum readable width for compact axis values, not the edit format.
        // Keep this fixed so changing a value cannot rearrange the three axes.
        constexpr const char* kAxisSample = "-0.00";

        // 유도한 표시 이름을 담는 버퍼. 매 프레임 필드마다 도는 자리라
        // std::string 을 새로 만들지 않는다(§8.2 의 "정적 경로 frame 당 heap
        // 할당 0").
        //
        // 한 칸이 아니라 **고리**인 이유: 중첩 구조체를 그릴 때 바깥 필드의
        // 라벨이 아직 쓰이는 중에 안쪽 필드가 같은 버퍼를 덮을 수 있다. 고리를
        // 두면 가까운 몇 개가 동시에 살아 있어 그 덮어쓰기가 일어나지 않는다.
        // 돌려준 포인터의 수명이 "다음 몇 번의 호출까지" 라는 뜻이기도 하다.
        constexpr int kLabelCapacity = 128;
        constexpr int kLabelSlots = 4;
        char g_labelRing[kLabelSlots][kLabelCapacity]{};
        int g_labelSlot = 0;

        bool is_upper(char c) noexcept { return c >= 'A' && c <= 'Z'; }
        bool is_lower(char c) noexcept { return c >= 'a' && c <= 'z'; }
        bool is_digit(char c) noexcept { return c >= '0' && c <= '9'; }
    }

    int display_label_capacity() noexcept
    {
        return kLabelCapacity;
    }

    const char* display_label(const char* identifier) noexcept
    {
        g_labelSlot = (g_labelSlot + 1) % kLabelSlots;
        char* const buffer = g_labelRing[g_labelSlot];

        if (nullptr == identifier)
        {
            buffer[0] = '\0';
            return buffer;
        }

        // `m_` 접두를 뗀다. `m` 하나만 있고 뒤가 대문자인 경우(`mPosition`)도
        // 같은 뜻이라 함께 뗀다.
        const char* source = identifier;
        if ('m' == source[0] && '_' == source[1])
        {
            source += 2;
        }
        else if ('m' == source[0] && is_upper(source[1]))
        {
            source += 1;
        }

        int out = 0;
        char previous = '\0';
        for (int index = 0; '\0' != source[index] && out < kLabelCapacity - 1; ++index)
        {
            const char current = source[index];

            if ('_' == current)
            {
                // 남은 밑줄은 칸으로 바꾼다. 연달아 나오면 한 칸만 넣는다.
                if (out > 0 && ' ' != buffer[out - 1])
                {
                    buffer[out++] = ' ';
                }
                previous = current;
                continue;
            }

            // 소문자·숫자 뒤의 대문자 앞에서 끊는다. 연속 대문자는 끊지 않아
            // 약어가 쪼개지지 않는다 — `m_fovDegrees` 는 "Fov Degrees" 지만
            // `m_useHDR` 은 "Use HDR" 로 남는다.
            const bool boundary = out > 0 && is_upper(current) &&
                (is_lower(previous) || is_digit(previous));
            if (boundary && ' ' != buffer[out - 1] && out < kLabelCapacity - 1)
            {
                buffer[out++] = ' ';
            }

            if (out < kLabelCapacity - 1)
            {
                // 첫 글자는 대문자로 올린다. 그 밖은 원문 그대로다.
                buffer[out] = (0 == out && is_lower(current))
                    ? static_cast<char>(current - 'a' + 'A')
                    : current;
                ++out;
            }
            previous = current;
        }

        // 꼬리 공백을 턴다. `m_foo_` 처럼 밑줄로 끝나는 이름이 남기는 것이다.
        while (out > 0 && ' ' == buffer[out - 1])
        {
            --out;
        }
        buffer[out] = '\0';

        // 전부 떨어져 나갔으면 원시 이름을 돌려준다. 빈 라벨은 ImGui 에서
        // ID 가 비는 것과 같아 같은 프레임의 다른 위젯과 충돌한다.
        return 0 == out ? identifier : buffer;
    }

    float property_layout_label_min_logical() noexcept
    {
        return kLabelMinLogical;
    }

    const char* property_layout_value_sample() noexcept
    {
        return kValueSample;
    }

    const char* property_layout_axis_sample() noexcept
    {
        return kAxisSample;
    }

    float property_layout_label_hint(const char* const* labels, int count)
    {
        if (nullptr == labels || count <= 0)
        {
            return 0.f;
        }

        float widest = 0.f;
        for (int index = 0; index < count; ++index)
        {
            const char* const label = labels[index];
            if (nullptr == label)
            {
                continue;
            }
            widest = ImMax(widest, ImGui::CalcTextSize(label,
                ImGui::FindRenderedTextEnd(label)).x);
        }
        return widest;
    }

    property_layout_inputs property_layout_inputs_now(int aux_button_count,
        float label_hint)
    {
        const ImGuiStyle& style = ImGui::GetStyle();

        property_layout_inputs inputs{};
        inputs.available = ImGui::GetContentRegionAvail().x;
        inputs.label_min = ThemePixels(kLabelMinLogical);

        // 폰트에서 잰 값은 이미 배율이 반영돼 있다. `ThemePixels` 를 다시
        // 곱하면 DPI 2 에서 두 번 커진다.
        inputs.value_min = ImGui::CalcTextSize(kValueSample).x + style.FramePadding.x * 2.f;
        inputs.axis_value_min = ImGui::CalcTextSize(kAxisSample).x + style.FramePadding.x * 2.f;
        inputs.label_hint = label_hint;

        // badge 폭의 정본은 `EditorAxisField3` 다. 같은 식을 여기에 다시 적는
        // 대신 같은 근거(`GetFrameHeight`)를 쓴다 — 둘이 갈리면 축 줄이
        // 최소 폭을 넘겼다고 판정해 놓고 실제로는 칸이 1px 로 눌린다.
        inputs.badge_width = ImGui::GetFrameHeight() * 0.62f;

        inputs.gap = ThemePixels(EditorThemeTokens::ItemGapX);
        inputs.axis_gap = style.ItemInnerSpacing.x;
        inputs.aux_reserve = aux_button_count > 0
            ? ImGui::GetFrameHeight() * static_cast<float>(aux_button_count)
            : 0.f;
        inputs.hysteresis = inputs.gap;
        inputs.editing = ImGui::IsAnyItemActive();
        return inputs;
    }

    property_layout_metrics measure_property_layout(const property_layout_inputs& inputs,
        property_layout_state& state) noexcept
    {
        // ── 라벨 열 (s&box 규약) ─────────────────────────────────────────
        //
        // 비율이 없다. 최소 폭과 이 구간이 실제로 그릴 라벨 폭 중 **큰 쪽**을
        // 쓰고, 늘어나는 열은 값 쪽 하나다. 가용 폭이 인자로 들어오지 않는
        // 것이 요점이다 — 창을 넓혀도 라벨 열은 그대로고 넓어진 만큼이 전부
        // 값으로 간다.
        float label_col = ImMax(inputs.label_min, inputs.label_hint);

        // 편집 중 보류가 inline 을 붙들고 있는 동안 창이 라벨 열보다 좁아질
        // 수 있다. 그때 열을 그대로 두면 값 칸이 가용 폭 밖으로 나간다.
        label_col = ImMin(label_col,
            ImMax(inputs.available - inputs.gap - inputs.aux_reserve, 1.f));

        // inline 일 때 값이 받게 될 폭.
        const float inline_value = inputs.available - label_col - inputs.gap - inputs.aux_reserve;

        // ── 줄 전환. 완충 폭이 한쪽 방향에만 붙는다 ──────────────────────
        //
        // stacked 에서 inline 으로 돌아올 때만 `value_min + hysteresis` 를
        // 요구한다. 양쪽에 걸면 임계값 자체가 둘로 갈려 어느 쪽도 만족하지
        // 않는 구간이 생긴다.
        if (!inputs.editing)
        {
            if (property_layout_mode::inline_row == state.mode)
            {
                if (inline_value < inputs.value_min)
                {
                    state.mode = property_layout_mode::stacked;
                }
            }
            else if (inline_value >= inputs.value_min + inputs.hysteresis)
            {
                state.mode = property_layout_mode::inline_row;
            }
        }

        property_layout_metrics metrics{};
        metrics.mode = state.mode;
        metrics.gap = inputs.gap;
        metrics.aux_reserve = inputs.aux_reserve;

        if (property_layout_mode::stacked == state.mode)
        {
            metrics.label_col = inputs.available;
            metrics.value_col = ImMax(inputs.available - inputs.aux_reserve, 1.f);
        }
        else
        {
            metrics.label_col = label_col;
            metrics.value_col = ImMax(inline_value, 1.f);
        }

        // ── 축 전환. 값 열을 셋으로 나눈 뒤에 판정한다 ───────────────────
        // 축 위젯이 실제로 쓰는 식과 같아야 한다 — 간격도 그쪽의
        // `ItemInnerSpacing` 이고, 최소 폭도 축 전용 대표 문자열이다.
        const float slot = (metrics.value_col - inputs.axis_gap * 2.f) / 3.f;
        const float axis_need = inputs.badge_width + inputs.axis_value_min;
        metrics.axis_need = axis_need;
        metrics.axis_gap = inputs.axis_gap;
        if (!inputs.editing)
        {
            if (state.axis_stacked)
            {
                // Hysteresis is a row-width margin, not a margin per axis.
                if (slot >= axis_need + inputs.hysteresis / 3.f)
                {
                    state.axis_stacked = false;
                }
            }
            else if (slot < axis_need)
            {
                state.axis_stacked = true;
            }
        }
        metrics.axis_stacked = state.axis_stacked;

        return metrics;
    }

    property_layout_metrics widen_property_line(
        const property_layout_metrics& metrics) noexcept
    {
        // 이미 내려간 줄은 그대로다. 한 번 더 넓히면 `label_col` 이 가용 폭에
        // `gap + value_col` 을 더한 값이 되어 값 칸이 창 밖으로 나간다.
        if (property_layout_mode::stacked == metrics.mode)
        {
            return metrics;
        }

        property_layout_metrics widened = metrics;
        const float full = metrics.label_col + metrics.gap + metrics.value_col;
        widened.mode = property_layout_mode::stacked;
        widened.label_col = full;
        widened.value_col = full;

        // 축 판정을 다시 한다. 넓힌 값 열은 언제나 원래보다 넓으므로 이 재판정은
        // 세로 → 가로 한 방향으로만 간다 — 넓혔는데 축이 세로로 남는 것만
        // 막고, 반대로 뒤집지는 않는다. 완충 폭을 여기 다시 걸지 않는 이유다.
        if (widened.axis_need > 0.f)
        {
            const float slot = (widened.value_col - widened.axis_gap * 2.f) / 3.f;
            if (slot >= widened.axis_need)
            {
                widened.axis_stacked = false;
            }
        }
        return widened;
    }

    namespace
    {
        // 기본은 꺼짐이다. 켜 두면 `meta::debugOnly()` 로 표시한 내부 식별자가
        // 기본 인스펙터에 샌다 — 표시 속성을 붙인 뜻이 사라진다.
        constexpr bool kDebugModeDefault = false;

        // 인스펙터 디버그 모드. 창이 아니라 사람의 상태라 한 칸이다.
        bool g_debugMode = kDebugModeDefault;
    }

    bool property_debug_mode() noexcept
    {
        return g_debugMode;
    }

    bool property_debug_mode_default() noexcept
    {
        return kDebugModeDefault;
    }

    void set_property_debug_mode(bool enabled) noexcept
    {
        g_debugMode = enabled;
    }

    namespace
    {
        // 지금 그리는 줄의 배치. 즉시 모드 한 스레드라 한 칸이면 되고,
        // 중첩은 호출자가 직전 값을 되돌려 감당한다.
        property_layout_metrics g_currentLayout{};
    }

    const property_layout_metrics& current_property_layout() noexcept
    {
        return g_currentLayout;
    }

    property_layout_metrics push_property_layout(
        const property_layout_metrics& metrics) noexcept
    {
        const property_layout_metrics previous = g_currentLayout;
        g_currentLayout = metrics;
        return previous;
    }

    float begin_property_line(const char* label, const property_layout_metrics& metrics)
    {
        IM_ASSERT(nullptr != label && "속성 줄은 이름이 있어야 한다");
        if (nullptr == label)
        {
            return metrics.value_col;
        }

        const float line_start = ImGui::GetCursorPosX();
        const ImVec2 screen = ImGui::GetCursorScreenPos();
        const float height = ImGui::GetFrameHeight();

        // `##` 뒤는 ID 다. 표준 ImGui 규약대로 표시하지 않는다.
        const char* const end = ImGui::FindRenderedTextEnd(label);
        const float text_width = ImGui::CalcTextSize(label, end).x;
        const bool clipped = text_width > metrics.label_col;

        // W2-2: 이 위젯이 규칙의 정본이다 — 재고, 넘치면 자르고, 자른 줄은
        // tooltip 으로 전체를 준다. 형제 셋이 여기를 베꼈다. 신고만 붙인다.
        ::editor::clipping::announce_text("EditorPropertyRow.label",
            text_width, metrics.label_col, clipped, true);

        ImGui::AlignTextToFramePadding();
        if (InspectorStyleActive())
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        if (clipped)
        {
            // 자른다. 잘린 줄은 그 자리에 tooltip 으로 전체 이름을 준다.
            // `IsItemHovered` 를 쓰지 않는 이유는 항목 사각형이 잘리기 전의
            // 글자 폭이라 열 밖까지 hover 로 잡히기 때문이다.
            ImGui::PushClipRect(screen, ImVec2(screen.x + metrics.label_col, screen.y + height), true);
            ImGui::TextUnformatted(label, end);
            ImGui::PopClipRect();
        }
        else
        {
            ImGui::TextUnformatted(label, end);
        }
        if (InspectorStyleActive()) ImGui::PopStyleColor();

        if (property_layout_mode::stacked == metrics.mode)
        {
            // 값은 다음 줄이다. 들여쓰기를 더하지 않는다 — 좁아서 내린 줄에
            // 들여쓰기를 주면 값 열이 또 줄어든다.
            if (clipped && ImGui::IsMouseHoveringRect(screen,
                ImVec2(screen.x + metrics.label_col, screen.y + height)))
            {
                ImGui::SetTooltip("%.*s", static_cast<int>(end - label), label);
            }
            return metrics.value_col;
        }

        // 값 커서를 **절대 위치**로 세운다. `SameLine` 의 기본 간격에 맡기면
        // 라벨 길이마다 값 열의 시작이 달라진다 — 지금 Transform 이 공백
        // 문자열로 맞추고 있는 것이 바로 그 어긋남을 손으로 메운 것이다.
        ImGui::SameLine(0.f, 0.f);
        ImGui::SetCursorPosX(line_start + metrics.label_col + metrics.gap);

        if (clipped && ImGui::IsMouseHoveringRect(screen,
            ImVec2(screen.x + metrics.label_col, screen.y + height)))
        {
            ImGui::SetTooltip("%.*s", static_cast<int>(end - label), label);
        }
        return metrics.value_col;
    }
}
