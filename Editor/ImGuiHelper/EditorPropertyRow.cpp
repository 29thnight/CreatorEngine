#include "EditorPropertyRow.h"

#include "ImGui.h"
#include "EditorTheme.h"

#include <array>

namespace editor::widgets
{
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
            changed |= ImGui::DragFloat(kPropertyFieldIds[static_cast<std::size_t>(index)],
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
        constexpr float kLabelMaxLogical = 160.f;

        // 가용 폭 중 라벨이 가져가는 비율. 상한에 닿기 전까지만 쓴다.
        constexpr float kLabelRatio = 0.40f;

        // 값 최소 폭을 재는 대표 문자열. **현재 값으로 재지 않는다.**
        // 현재 숫자로 재면 0 에서 -1234.567 로 바뀌는 순간 열이 움직이고,
        // 그 움직임이 다시 모드 판정을 흔든다. 계획서가 "현재 숫자 값이나 매
        // 프레임 라벨 최대값 변화로 열과 모드가 흔들리지 않게 한다" 고 한 자리다.
        constexpr const char* kValueSample = "-0000.000";

        // 축 칸 하나의 최소 폭을 재는 문자열. 행 전체의 것보다 짧다 — 축은
        // 셋이 한 줄을 나눠 쓰므로 같은 잣대를 대면 넉넉한 폭에서도 세로로
        // 떨어진다. `%.3f` 한 칸이 보이는 만큼이 기준이다.
        constexpr const char* kAxisSample = "-0.000";

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

    float property_layout_label_max_logical() noexcept
    {
        return kLabelMaxLogical;
    }

    float property_layout_label_ratio() noexcept
    {
        return kLabelRatio;
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
        inputs.label_max = ThemePixels(kLabelMaxLogical);
        inputs.label_ratio = kLabelRatio;

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
        // 비율과 상한이 위를 막고, 힌트가 있으면 거기까지만 쓴다. 힌트는
        // 이 구간의 고정 라벨이 실제로 필요한 폭이라, 짧은 이름만 있는 구간이
        // 가용 폭의 40% 를 통째로 버리는 것을 막는다.
        float label_col = ImMin(inputs.available * inputs.label_ratio, inputs.label_max);
        if (inputs.label_hint > 0.f)
        {
            label_col = ImMin(label_col, inputs.label_hint);
        }

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
        if (!inputs.editing)
        {
            if (state.axis_stacked)
            {
                if (slot >= axis_need + inputs.hysteresis)
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

        ImGui::AlignTextToFramePadding();
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
