#include "ImGui.h"
#include "OutputLogWindow.h"
#include "EditorTheme.h"
#include "EditorIcons.h"
#include "EditorPlatform.h"
#include "LogSystem.h"

#include <algorithm>
#include <string>
#include <string_view>

namespace
{
    const char* LevelIcon(spdlog::level::level_enum level)
    {
        switch (level)
        {
        case spdlog::level::warn:     return EditorIcon::Warning;
        case spdlog::level::err:
        case spdlog::level::critical: return EditorIcon::Error;
        default:                      return EditorIcon::Info;
        }
    }

    ImVec4 LevelColor(spdlog::level::level_enum level)
    {
        switch (level)
        {
        case spdlog::level::info:     return editor::ThemeColorValue(editor::ThemeColor::Text);
        case spdlog::level::warn:     return editor::ThemeColorValue(editor::ThemeColor::Warning);
        case spdlog::level::err:
        case spdlog::level::critical: return editor::ThemeColorValue(editor::ThemeColor::Error);
        default:                      return editor::ThemeColorValue(editor::ThemeColor::TextMuted);
        }
    }

    const char* LevelName(spdlog::level::level_enum level)
    {
        switch (level)
        {
        case spdlog::level::trace:    return "Trace";
        case spdlog::level::debug:    return "Debug";
        case spdlog::level::info:     return "Info";
        case spdlog::level::warn:     return "Warning";
        case spdlog::level::err:      return "Error";
        case spdlog::level::critical: return "Critical";
        default:                      return "Off";
        }
    }

    // ImGui 가 지금 쓰는 글꼴로 그 문자열이 차지하는 폭. 이것이 옛 화면에
    // 없던 자다 — 거기서는 UTF-8 **바이트 수**를 120 과 견줬다.
    float MeasureText(std::string_view text)
    {
        if (text.empty()) return 0.0f;
        return ImGui::CalcTextSize(text.data(), text.data() + text.size()).x;
    }

    // 메시지 안의 Windows 경로. 옛 화면은 아무 줄이나 **한 번 누르면** 이것을
    // 찾아 파일을 열었다 — 읽으려고 고른 것만으로 편집기가 떴다. 여기서는
    // 상세 영역의 버튼으로만 연다.
    std::string FindWindowsPath(std::string_view message)
    {
        for (std::size_t index = 0; index + 2 < message.size(); ++index)
        {
            const char drive = message[index];
            const bool isDrive = (drive >= 'A' && drive <= 'Z') || (drive >= 'a' && drive <= 'z');
            if (!isDrive || message[index + 1] != ':' || message[index + 2] != '\\') continue;
            std::size_t end = index;
            while (end < message.size() && message[end] != '\n' && message[end] != '\r' &&
                message[end] != '\t' && message[end] != '\0')
                ++end;
            while (end > index && message[end - 1] == ' ') --end;
            return std::string(message.substr(index, end - index));
        }
        return std::string();
    }
}

namespace editor
{
    void OutputLogWindow::Draw()
    {
        // 한글 폰트는 선택이다. 없으면 `PushFont(nullptr, 0.0f)` 이 "지금 폰트를
        // 그대로" 라서 부르는 자리가 분기하지 않아도 된다(W1 에서 정한 규약).
        ImGui::PushFont(m_font, 0.0f);

        DrawToolbar();
        PumpStore();
        ImGui::Separator();

        const float available = ImGui::GetContentRegionAvail().y;
        const float spacing = ImGui::GetStyle().ItemSpacing.y;
        const bool hasDetail = !m_view.Selection().IsEmpty();
        const float detailHeight = hasDetail
            ? (std::min)(available * 0.45f, ImGui::GetTextLineHeightWithSpacing() * 8.0f)
            : 0.0f;
        const float listHeight = available - detailHeight - (hasDetail ? spacing : 0.0f);

        DrawList((std::max)(listHeight, ImGui::GetTextLineHeightWithSpacing()));
        if (hasDetail) DrawDetail(detailHeight);

        ImGui::PopFont();
    }

    void OutputLogWindow::PumpStore()
    {
        // 종료 단계에서는 전역 Debug 가 죽은 인스턴스를 가리킬 수 있다.
        if (Log::IsAlive())
        {
            if (auto delta = Debug->GetLogDeltaSince(m_view.Cursor())) m_view.Apply(*delta);
        }
        m_filter.minimumLevel = static_cast<spdlog::level::level_enum>(m_levelChoice);
        m_filter.search = m_search;
        m_view.Rebuild(m_filter);
    }

    void OutputLogWindow::DrawToolbar()
    {
        if (ImGui::Button(EditorIcon::Label<EditorIcon::Delete, " Clear">))
        {
            if (Log::IsAlive()) Debug->Clear();
            m_view.Clear();
            m_lastRowCount = 0;
        }
        ImGui::SameLine();
        ImGui::Checkbox("Collapse", &m_filter.collapse);
        ImGui::SameLine();
        ImGui::Checkbox("Auto Scroll", &m_autoScroll);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 7.0f);
        ImGui::Combo("##OutputLogLevel", &m_levelChoice,
            "Trace\0Debug\0Info\0Warning\0Error\0Critical\0");
        ImGui::SameLine();
        ImGui::SetNextItemWidth((std::max)(ImGui::GetContentRegionAvail().x,
            ImGui::GetFontSize() * 6.0f));
        ImGui::InputTextWithHint("##OutputLogSearch",
            EditorIcon::Label<EditorIcon::Search, " Search">, m_search, sizeof(m_search));

        const std::uint64_t evicted = m_view.EvictedEntries();
        const std::uint64_t rejected = m_view.RejectedEntries();
        if (evicted != 0 || rejected != 0)
        {
            ImGui::TextDisabled("History limit: %llu expired, %llu oversized. See log file for full history.",
                static_cast<unsigned long long>(evicted), static_cast<unsigned long long>(rejected));
        }
    }

    void OutputLogWindow::DrawList(float height)
    {
        ImGui::BeginChild("OutputLogRows", ImVec2(0.0f, height), ImGuiChildFlags_None,
            ImGuiWindowFlags_AlwaysVerticalScrollbar);

        const std::vector<OutputLogRow>& rows = m_view.Rows();
        const float rowHeight = ImGui::GetTextLineHeightWithSpacing();
        const float iconWidth = MeasureText(EditorIcon::Warning) + ImGui::GetStyle().ItemSpacing.x;

        // 행 사이 간격을 0 으로 둔다. `Selectable` 이 차지하는 높이와 다음 행이
        // 시작하는 자리를 둘 다 `rowHeight` 로 맞추기 위해서다 — clipper 에게
        // 알려 준 보폭과 실제 보폭이 갈리면 **스크롤해야만** 어긋남이 드러난다.
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
            ImVec2(ImGui::GetStyle().ItemSpacing.x, 0.0f));

        // 보이는 행만 낸다. 평탄한 목록이라야 clipper 가 성립한다 — 접힘을
        // ImGui 가 기억하는 트리와는 양립하지 않는다(W7-3 에서 배운 것).
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(rows.size()), rowHeight);
        while (clipper.Step())
        {
            for (int index = clipper.DisplayStart; index < clipper.DisplayEnd; ++index)
            {
                const OutputLogRow& row = rows[static_cast<std::size_t>(index)];
                const LogGroup* const group = m_view.FindGroup(row.groupId);
                if (group == nullptr) continue;

                std::string badge;
                if (m_filter.collapse && row.repeatCount > 1)
                    badge = std::to_string(row.repeatCount);
                const float badgeWidth = badge.empty()
                    ? 0.0f : MeasureText(badge) + ImGui::GetStyle().ItemSpacing.x * 2.0f;

                const std::string flat = m_view.RowText(row);
                const float textWidth = ImGui::GetContentRegionAvail().x - iconWidth - badgeWidth;
                const std::string shown = EllipsizeToWidth(flat, textWidth, MeasureText);

                const OutputLogSelection& selection = m_view.Selection();
                const bool selected = selection.groupId == row.groupId &&
                    selection.sequence == row.sequence;

                ImGui::PushID(index);
                const float rowStartX = ImGui::GetCursorPosX();
                // 라벨을 비워 둔다. `Selectable` 은 라벨 안의 `##` 를 식별자
                // 구분자로 읽어 그 앞까지만 보여 주는데, 로그 본문에 `##` 가
                // 들어오는 일은 실제로 있다. 본문은 아래에서 원문 그대로 낸다.
                if (ImGui::Selectable("##row", selected, ImGuiSelectableFlags_None,
                    ImVec2(0.0f, rowHeight)))
                    m_view.Select({ row.groupId, row.sequence });
                const bool hovered = ImGui::IsItemHovered();

                // 본문을 그 위에 겹치려면 같은 줄로 돌아와야 한다. `SetCursorPos`
                // 로 돌아오면 ImGui 가 **창 경계를 늘리려는 시도**로 읽어 매
                // 프레임 오류를 낸다("Code uses SetCursorPos() to extend
                // window/parent boundaries"). `SameLine` 은 앞 줄의 높이를 그대로
                // 물려받으므로 경계도 보폭도 건드리지 않는다.
                ImGui::SameLine(rowStartX);
                ImGui::PushStyleColor(ImGuiCol_Text, LevelColor(row.level));
                ImGui::TextUnformatted(LevelIcon(row.level));
                ImGui::SameLine();
                ImGui::TextUnformatted(shown.c_str(), shown.c_str() + shown.size());
                ImGui::PopStyleColor();
                if (!badge.empty())
                {
                    ImGui::SameLine(ImGui::GetContentRegionMax().x - badgeWidth);
                    ImGui::TextDisabled("%s", badge.c_str());
                }

                // 잘린 줄은 머물면 전문을 보여 준다. 포맷을 태우지 않는다 —
                // 본문의 `%` 가 서식 지정자로 읽히면 안 된다.
                if (hovered && shown.size() != flat.size())
                {
                    ImGui::BeginTooltip();
                    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 40.0f);
                    ImGui::TextUnformatted(flat.c_str(), flat.c_str() + flat.size());
                    ImGui::PopTextWrapPos();
                    ImGui::EndTooltip();
                }
                ImGui::PopID();
            }
        }

        // 늘었을 때만 민다. 매 프레임 밀면 위로 올려 읽는 중에도 끌려 내려간다.
        if (m_autoScroll && rows.size() != m_lastRowCount) ImGui::SetScrollHereY(1.0f);
        m_lastRowCount = rows.size();

        ImGui::PopStyleVar();
        ImGui::EndChild();
    }

    void OutputLogWindow::DrawDetail(float height)
    {
        const OutputLogSelection selection = m_view.Selection();
        const LogGroup* const group = m_view.FindGroup(selection.groupId);
        if (group == nullptr) return;

        ImGui::BeginChild("OutputLogDetail", ImVec2(0.0f, height), ImGuiChildFlags_None);

        if (ImGui::Button("Copy"))
        {
            // 클립보드는 NUL 로 끝나는 문자열이라 본문에 NUL 이 있으면 거기서
            // 끊긴다. 저장소의 원문은 그대로 남는다.
            ImGui::SetClipboardText(group->message.c_str());
        }
        const std::string path = FindWindowsPath(group->message);
        if (!path.empty())
        {
            ImGui::SameLine();
            if (ImGui::Button("Open File")) EditorPlatform::Get().OpenFile(path);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%s · %s · total %llu · retained %llu",
            LevelName(group->level),
            group->loggerName.empty() ? "(no logger)" : group->loggerName.c_str(),
            static_cast<unsigned long long>(group->totalCount),
            static_cast<unsigned long long>(group->retainedOccurrences));

        // 출처는 **없으면 없다고 적는다.** 지금 이 엔진의 로그는 전부 출처가
        // 없다(`Debug->Log*` 는 spdlog 매크로가 아니라 함수라 호출 위치를
        // 싣지 않는다). 그것을 숨기면 4단계가 무엇을 고쳐야 하는지 흐려진다.
        if (group->source.file.empty() && group->source.line == 0)
            ImGui::TextDisabled("source: none (the producer did not supply a call site)");
        else
            ImGui::TextDisabled("source: %s:%d (%s)", group->source.file.c_str(),
                group->source.line, group->source.function.c_str());

        ImGui::Separator();
        // 원문 그대로. 목록의 평탄화는 표시용이고 여기까지 오지 않는다.
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(group->message.c_str(),
            group->message.c_str() + group->message.size());
        ImGui::PopTextWrapPos();

        ImGui::EndChild();
    }
}
