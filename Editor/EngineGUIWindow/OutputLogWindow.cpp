#include "ImGui.h"
#include "OutputLogWindow.h"
#include "EditorTheme.h"
#include "EditorIcons.h"
#include "EditorPlatform.h"
#include "LogSystem.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
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

    // 카드 배경. 수준 색을 옅게 깐다 — 글자 색만으로 가르면 한 화면에 수백 줄이
    // 있을 때 심각한 것이 눈에 안 들어온다.
    ImVec4 LevelTint(spdlog::level::level_enum level)
    {
        ImVec4 color = LevelColor(level);
        switch (level)
        {
        case spdlog::level::warn:     color.w = 0.13f; break;
        case spdlog::level::err:
        case spdlog::level::critical: color.w = 0.16f; break;
        default:                      color.w = 0.045f; break;
        }
        return color;
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

    // 벽시계 시:분:초.밀리. 날짜는 안 적는다 — 한 세션 안에서 읽는 값이고,
    // 카드 둘째 줄은 좁다.
    std::string ClockText(spdlog::log_clock::time_point timestamp)
    {
        using namespace std::chrono;
        const auto since = timestamp.time_since_epoch();
        const auto milli = duration_cast<milliseconds>(since).count();
        const long long ofDay = milli % (24LL * 60LL * 60LL * 1000LL);
        char buffer[16]{};
        std::snprintf(buffer, sizeof(buffer), "%02lld:%02lld:%02lld.%03lld",
            ofDay / 3600000LL, (ofDay / 60000LL) % 60LL, (ofDay / 1000LL) % 60LL, ofDay % 1000LL);
        return std::string(buffer);
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
    bool OutputLogWindow::CountChip(const char* icon, std::uint64_t count,
        spdlog::level::level_enum level, bool active, const char* tooltip)
    {
        const std::string label = std::string(icon) + " " + std::to_string(count);
        ImGui::PushStyleColor(ImGuiCol_Text, LevelColor(level));
        ImGui::PushStyleColor(ImGuiCol_Button,
            active ? LevelTint(level) : ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, LevelTint(level));
        const bool pressed = ImGui::Button(label.c_str());
        ImGui::PopStyleColor(3);
        if (tooltip != nullptr && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tooltip);
        return pressed;
    }

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
            if (auto delta = Debug::GetLogDeltaSince(m_view.Cursor())) m_view.Apply(*delta);
        }
        m_filter.minimumLevel = static_cast<spdlog::level::level_enum>(m_levelChoice);
        m_filter.search = m_search;
        m_view.Rebuild(m_filter);
    }

    void OutputLogWindow::DrawToolbar()
    {
        const ImGuiStyle& style = ImGui::GetStyle();

        if (ImGui::Button(EditorIcon::Label<EditorIcon::Delete, " Clear">))
        {
            if (Log::IsAlive()) Debug::Clear();
            m_view.Clear();
            m_lastRowCount = 0;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("기록과 수준별 누적을 모두 비운다");

        ImGui::SameLine();
        ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
        ImGui::SameLine();

        ImGui::Checkbox("Collapse", &m_filter.collapse);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("같은 내용의 반복을 한 장으로 접고 오른쪽에 누적 수를 적는다");
        ImGui::SameLine();
        ImGui::Checkbox("Auto Scroll", &m_autoScroll);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("새 기록이 오면 바닥으로 따라간다");

        ImGui::SameLine();
        ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
        ImGui::SameLine();

        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 7.0f);
        ImGui::Combo("##OutputLogLevel", &m_levelChoice,
            "Trace\0Debug\0Info\0Warning\0Error\0Critical\0");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("이 수준 미만은 목록에서 뺀다");

        ImGui::SameLine();
        ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
        ImGui::SameLine();

        // 수준별 누적. 오른쪽 끝에 두면 창을 좁혔을 때 검색 상자가 밀어내
        // 통째로 안 보인다 — 수준 고르개 바로 옆, 검색보다 **앞**에 둔다.
        // 누르면 그 수준을 최소 수준으로 세운다: 세는 자리와 거르는 자리가
        // 같아야 "이 54 개를 보고 싶다" 가 한 번에 끝난다.
        const LogLevelTotals& totals = m_view.LevelTotals();
        if (CountChip(EditorIcon::Info, totals.messages, spdlog::level::info,
            m_levelChoice <= static_cast<int>(spdlog::level::info), "메시지 — 눌러서 이 수준부터 본다"))
            m_levelChoice = static_cast<int>(spdlog::level::trace);
        ImGui::SameLine();
        if (CountChip(EditorIcon::Warning, totals.warnings, spdlog::level::warn,
            m_levelChoice <= static_cast<int>(spdlog::level::warn), "경고 — 눌러서 이 수준부터 본다"))
            m_levelChoice = static_cast<int>(spdlog::level::warn);
        ImGui::SameLine();
        if (CountChip(EditorIcon::Error, totals.errors, spdlog::level::err,
            m_levelChoice <= static_cast<int>(spdlog::level::err), "오류 — 눌러서 이 수준부터 본다"))
            m_levelChoice = static_cast<int>(spdlog::level::err);

        // 둘째 줄: 검색. 첫 줄에 이어 붙이면 좁은 창에서 밀려 나간다.
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        ImGui::InputTextWithHint("##OutputLogSearch",
            EditorIcon::Label<EditorIcon::Search, " Search (본문 원문과 로거명)">,
            m_search, sizeof(m_search));

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

        const ImGuiStyle& style = ImGui::GetStyle();
        const std::vector<OutputLogRow>& rows = m_view.Rows();
        const float textHeight = ImGui::GetTextLineHeight();
        const float gutter = MeasureText(EditorIcon::Warning) + style.ItemSpacing.x * 2.0f;

        // 카드 하나의 높이. 두 줄(본문·메타) + 위 여백 + 카드 사이 간격이다.
        // 이 값을 clipper 에게 그대로 준다 — 실제 보폭과 갈리면 스크롤해야만
        // 드러나고, 스크롤 0 인 그림 대조로는 못 본다.
        const float headPad = ImFloor(style.FramePadding.y);
        const float cardGap = ImFloor(style.ItemSpacing.y);
        const float lineOne = ImFloor(textHeight) + headPad * 2.0f;
        const float cardHeight = lineOne + ImFloor(textHeight) + headPad + cardGap;

        // 줄 간격을 0 으로 둔다. 카드 안의 두 줄과 카드 사이 간격은 위에서
        // 계산한 값이 주는 것이고, ImGui 의 기본 간격이 거기에 더 얹히면 안 된다.
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(style.ItemSpacing.x, 0.0f));

        ImDrawList* const drawList = ImGui::GetWindowDrawList();
        const bool windowHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);

        // 보이는 행만 낸다. 평탄한 목록이라야 clipper 가 성립한다 — 접힘을
        // ImGui 가 기억하는 트리와는 양립하지 않는다(W7-3 에서 배운 것).
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(rows.size()), cardHeight);
        while (clipper.Step())
        {
            for (int index = clipper.DisplayStart; index < clipper.DisplayEnd; ++index)
            {
                const OutputLogRow& row = rows[static_cast<std::size_t>(index)];
                const LogGroup* const group = m_view.FindGroup(row.groupId);
                if (group == nullptr) continue;

                const OutputLogSelection& selection = m_view.Selection();
                const bool selected = selection.groupId == row.groupId &&
                    selection.sequence == row.sequence;

                std::string badge;
                if (m_filter.collapse && row.repeatCount > 1)
                    badge = std::to_string(row.repeatCount);
                const float badgeWidth = badge.empty()
                    ? 0.0f : MeasureText(badge) + style.FramePadding.x * 4.0f;

                ImGui::PushID(index);

                // ── 카드 배경 ────────────────────────────────────────────
                // 그리기 목록으로 깐다. W2 의 custom draw 계약이 대상으로 삼는
                // 것은 **글자를 직접 그리는 자리**(RenderText·AddText)이고,
                // 사각형은 거기 들지 않는다. 글자는 전부 표준 위젯이 낸다.
                const ImVec2 cardMin = ImGui::GetCursorScreenPos();
                const float cardWidth = ImGui::GetContentRegionAvail().x;
                const ImVec2 cardMax(cardMin.x + cardWidth, cardMin.y + cardHeight - cardGap);
                const bool hovered = windowHovered &&
                    ImGui::IsMouseHoveringRect(cardMin, cardMax);

                ImVec4 tint = LevelTint(row.level);
                if (selected) tint = ThemeColorValue(ThemeColor::Selection), tint.w = 0.38f;
                else if (hovered) tint.w += 0.06f;
                drawList->AddRectFilled(cardMin, cardMax, ImGui::GetColorU32(tint), 4.0f);
                // 왼쪽 강조 띠. 옅은 배경만으로는 info 와 trace 가 안 갈린다.
                ImVec4 accent = LevelColor(row.level);
                accent.w = selected ? 1.0f : 0.75f;
                drawList->AddRectFilled(cardMin, ImVec2(cardMin.x + 3.0f, cardMax.y),
                    ImGui::GetColorU32(accent), 4.0f, ImDrawFlags_RoundCornersLeft);
                if (selected)
                {
                    ImVec4 border = ThemeColorValue(ThemeColor::Primary);
                    drawList->AddRect(cardMin, cardMax, ImGui::GetColorU32(border), 4.0f, 0, 1.0f);
                }

                // ── 첫 줄: 히트 영역 · 아이콘 · 본문 · 배지 ───────────────
                const float textX = ImGui::GetCursorPosX();
                // 라벨을 비워 둔다. `Selectable` 은 라벨 안의 `##` 를 식별자
                // 구분자로 읽어 그 앞까지만 보여 주는데, 로그 본문에 `##` 가
                // 들어오는 일은 실제로 있다. 본문은 아래에서 원문 그대로 낸다.
                // 배경은 이미 위에서 깔았으므로 이 위젯은 자리를 잡는 일만 한다.
                ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
                ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
                ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
                if (ImGui::Selectable("##row", selected, ImGuiSelectableFlags_None,
                    ImVec2(0.0f, lineOne)))
                    m_view.Select({ row.groupId, row.sequence });
                ImGui::PopStyleColor(3);
                const bool itemHovered = ImGui::IsItemHovered();

                const std::string flat = m_view.RowText(row);
                const float messageWidth = cardWidth - gutter - badgeWidth - style.ItemSpacing.x;
                const std::string shown = EllipsizeToWidth(flat, messageWidth, MeasureText);

                // 본문을 그 위에 겹치려면 같은 줄로 돌아와야 한다. `SetCursorPos`
                // 로 돌아오면 ImGui 가 **창 경계를 늘리려는 시도**로 읽어 매
                // 프레임 오류를 낸다. `SameLine` 은 앞 줄의 높이를 그대로
                // 물려받으므로 경계도 보폭도 건드리지 않는다.
                ImGui::SameLine(textX + style.ItemSpacing.x);
                ImGui::PushStyleColor(ImGuiCol_Text, LevelColor(row.level));
                ImGui::TextUnformatted(LevelIcon(row.level));
                ImGui::SameLine(textX + gutter);
                ImGui::TextUnformatted(shown.c_str(), shown.c_str() + shown.size());
                ImGui::PopStyleColor();
                if (!badge.empty())
                {
                    ImGui::SameLine(textX + cardWidth - badgeWidth);
                    ImGui::TextDisabled("%s", badge.c_str());
                }

                // ── 둘째 줄: 메타. 자연스러운 다음 줄이라 커서를 놓지 않는다.
                ImGui::PushItemFlag(ImGuiItemFlags_NoNav, true);
                ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
                ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
                ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
                if (ImGui::Selectable("##meta", selected, ImGuiSelectableFlags_None,
                    ImVec2(0.0f, ImFloor(textHeight))))
                    m_view.Select({ row.groupId, row.sequence });
                ImGui::PopStyleColor(3);
                ImGui::PopItemFlag();
                ImGui::SameLine(textX + gutter);
                ImGui::TextDisabled("%s · %s · %s", LevelName(row.level),
                    group->loggerName.empty() ? "(no logger)" : group->loggerName.c_str(),
                    ClockText(row.timestamp).c_str());
                // 카드 사이 간격. 항목이라 ImGui 가 보폭을 세고, 뒤에 커서를
                // 놓는 호출이 남지 않는다.
                ImGui::Dummy(ImVec2(0.0f, cardGap));

                // 잘린 줄은 머물면 전문을 보여 준다. 포맷을 태우지 않는다 —
                // 본문의 `%` 가 서식 지정자로 읽히면 안 된다.
                if ((hovered || itemHovered) && shown.size() != flat.size())
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

        if (rows.empty())
        {
            ImGui::Dummy(ImVec2(0.0f, ImGui::GetTextLineHeight()));
            ImGui::Indent(gutter);
            ImGui::TextDisabled(m_view.HasAnyGroup()
                ? "걸러져서 보이는 것이 없다 — 수준이나 검색어를 바꿔라."
                : "아직 기록이 없다.");
            ImGui::Unindent(gutter);
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

        ImGui::BeginChild("OutputLogDetail", ImVec2(0.0f, height), ImGuiChildFlags_Borders);

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
            if (ImGui::Button(EditorIcon::Label<EditorIcon::Folder, " Open File">))
                EditorPlatform::Get().OpenFile(path);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", path.c_str());
        }
        ImGui::SameLine();
        ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, LevelColor(group->level));
        ImGui::TextUnformatted(LevelIcon(group->level));
        ImGui::SameLine();
        ImGui::TextUnformatted(LevelName(group->level));
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::TextDisabled("· %s · total %llu · retained %llu",
            group->loggerName.empty() ? "(no logger)" : group->loggerName.c_str(),
            static_cast<unsigned long long>(group->totalCount),
            static_cast<unsigned long long>(group->retainedOccurrences));

        // 출처는 **없으면 없다고 적는다.** `Debug::PrintLog` 가
        // `std::source_location` 기본 인자로 호출 위치를 싣게 된 뒤로는 대부분
        // 채워지지만, 래퍼를 한 겹 거쳐 들어오는 줄은 여전히 비어 있다. 빈 것을
        // 숨기면 그 래퍼들이 어디인지 알 수단이 사라진다.
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
