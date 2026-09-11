#include "EditorPropertyRow.h"

#include "ImGui.h"

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
}
