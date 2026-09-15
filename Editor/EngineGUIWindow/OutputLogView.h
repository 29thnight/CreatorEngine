#pragma once

// Output Log 3단계 — 화면이 쓰는 상태. ImGui 를 알지 못한다.
//
// 왜 갈라놓는가. 옛 로그 창은 저장소에서 받은 목록을 그 자리에서 포맷하고
// 자르고 그렸다. 그래서 "무엇을 보여줄 것인가" 가 ImGui 호출 사이에 흩어져
// 있었고, 라이브 에디터를 띄우지 않고는 한 줄도 확인할 수 없었다. 여기에
// 모아 두면 필터 · 검색 · Collapse · 선택 · 말줄임을 `verify-log-storage` 의
// 독립 프로브가 그대로 잰다. 남는 쪽(OutputLogWindow)은 행을 그리는 일만 한다.
//
// ── 옛 화면이 눈대중으로 하던 것 두 가지를 여기서 없앤다 ──────────────────
//
//   ① `WordWrapText(text, 120)` — 공백으로 단어를 나누고 **바이트 길이**를
//      120 과 견줬다. 한글은 UTF-8 에서 글자당 3 바이트라 같은 칸을 세 배로
//      쳐서 너무 일찍 접혔고, 공백 없는 긴 경로는 아예 접히지 않았다. 창 폭도
//      글꼴도 보지 않았다. 여기서는 **실제 글꼴 폭**을 재는 함수를 받아
//      글자 경계에서 자른다(`EllipsizeToWidth`).
//   ② `35 * 개행 수` 로 행 높이를 잡았다 — 글꼴·DPI 와 무관한 숫자였다.
//      목록은 이제 한 줄 고정이고(높이는 창이 글꼴에서 얻는다), 여러 줄
//      본문은 상세 영역이 원문 그대로 보여 준다.
//
// ── 원문은 건드리지 않는다 ────────────────────────────────────────────────
//
// 저장소가 개행 · 탭 · 연속 공백 · NUL 을 원문대로 들고 있다(1단계 계약).
// 목록 행은 한 줄이어야 하므로 **표시용으로만** 평탄화하고(`FlattenForRow`),
// 검색 · 복사 · 상세는 언제나 원문을 본다. 평탄화한 문자열을 저장소나 그룹에
// 되돌려 쓰지 않는다.

#include "LogStore.h"

#include <algorithm>
#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace editor
{
    // 목록 한 줄. 본문을 담지 않는다 — 행은 clipper 때문에 매 프레임 수천 개가
    // 오갈 수 있고, 본문은 그룹이 하나씩만 들고 있으면 된다.
    struct OutputLogRow
    {
        std::uint64_t groupId{};
        // Collapse 를 끈 목록에서만 유효하다. 켠 목록에서는 0 이다.
        std::uint64_t sequence{};
        spdlog::level::level_enum level{};
        // Collapse 행이 오른쪽에 내보이는 배지. 1 이면 배지를 그리지 않는다.
        std::uint64_t repeatCount{ 1 };
        spdlog::log_clock::time_point timestamp{};
    };

    struct OutputLogFilter
    {
        spdlog::level::level_enum minimumLevel{ spdlog::level::trace };
        bool collapse{ true };
        // 본문 원문과 로거명을 본다. ASCII 만 대소문자를 무시한다 — 한글에는
        // 대소문자가 없고, 로케일을 끌어들이면 같은 입력이 기계마다 달라진다.
        std::string search;

        // 멤버로 둔다. 네임스페이스에 두면 클래스 안에서 부르는 자리가
        // 선언을 못 본다.
        bool operator==(const OutputLogFilter&) const = default;
    };

    // 선택은 배열 인덱스가 아니라 id 다. 퇴거·Clear 로 목록이 흔들려도 다른
    // 로그가 선택된 것처럼 보이지 않는다(1단계에서 배운 것).
    struct OutputLogSelection
    {
        std::uint64_t groupId{};
        std::uint64_t sequence{};

        bool IsEmpty() const { return groupId == 0; }
    };

    // UTF-8 이어지는 바이트(10xxxxxx)를 건너뛰어 글자 경계를 찾는다.
    inline bool IsUtf8ContinuationByte(char byte)
    {
        return (static_cast<unsigned char>(byte) & 0xC0u) == 0x80u;
    }

    inline std::size_t PreviousUtf8Boundary(std::string_view text, std::size_t index)
    {
        while (index > 0 && IsUtf8ContinuationByte(text[index - 1])) --index;
        return index > 0 ? index - 1 : 0;
    }

    inline std::size_t NextUtf8Boundary(std::string_view text, std::size_t index)
    {
        if (index >= text.size()) return text.size();
        ++index;
        while (index < text.size() && IsUtf8ContinuationByte(text[index])) ++index;
        return index;
    }

    // 목록 행에 쓸 한 줄. 개행·탭·연속 공백을 공백 하나로 접고 NUL 도 공백으로
    // 바꾼다. 앞뒤 공백은 떼어 아이콘과의 간격이 본문마다 달라지지 않게 한다.
    inline std::string FlattenForRow(std::string_view text)
    {
        std::string flattened;
        flattened.reserve(text.size());
        bool pendingSpace = false;
        for (const char character : text)
        {
            const bool isBlank = character == '\n' || character == '\r' ||
                character == '\t' || character == ' ' || character == '\0';
            if (isBlank)
            {
                pendingSpace = !flattened.empty();
                continue;
            }
            if (pendingSpace)
            {
                flattened.push_back(' ');
                pendingSpace = false;
            }
            flattened.push_back(character);
        }
        return flattened;
    }

    // 실제 폭으로 자른다. `measure` 는 그 문자열이 차지하는 픽셀 폭을 돌려주는
    // 것이면 무엇이든 된다(창은 ImGui::CalcTextSize 를 넘긴다).
    //
    // 글자 경계에서만 자르므로 한글이 반 토막 나 깨진 네모로 그려지지 않는다.
    // 말줄임표를 붙인 결과가 한계 폭을 넘지 않는 것까지 지킨다 — 붙이고 나서
    // 넘치면 자르는 의미가 없다.
    template <typename Measure>
    std::string EllipsizeToWidth(std::string_view text, float maxWidth, Measure measure)
    {
        const char* const ellipsis = "...";
        if (text.empty() || maxWidth <= 0.0f) return std::string();
        if (measure(text) <= maxWidth) return std::string(text);

        const float ellipsisWidth = measure(std::string_view(ellipsis));
        if (ellipsisWidth > maxWidth) return std::string();

        // 글자 단위 이분 탐색. 폭은 글자 수에 단조가 아닐 수 있으므로(커닝)
        // 경계 후보를 좁힌 뒤 마지막에 한 글자씩 물러서며 확정한다.
        std::vector<std::size_t> boundaries;
        for (std::size_t index = 0; index < text.size(); index = NextUtf8Boundary(text, index))
            boundaries.push_back(index);
        boundaries.push_back(text.size());

        std::size_t low = 0, high = boundaries.size() - 1, best = 0;
        while (low <= high)
        {
            const std::size_t middle = low + (high - low) / 2;
            const std::string_view candidate = text.substr(0, boundaries[middle]);
            if (measure(candidate) + ellipsisWidth <= maxWidth)
            {
                best = middle;
                low = middle + 1;
            }
            else
            {
                if (middle == 0) break;
                high = middle - 1;
            }
        }
        while (best > 0 &&
            measure(text.substr(0, boundaries[best])) + ellipsisWidth > maxWidth)
            --best;

        std::string result(text.substr(0, boundaries[best]));
        result += ellipsis;
        return result;
    }

    // 저장소의 변경분을 받아 화면이 읽을 상태를 들고 있는다. 프레임마다 전체를
    // 복사하지 않는 것이 2단계에서 `ReadDeltaSince` 를 더한 이유다.
    class OutputLogView
    {
    public:
        // 커서를 그대로 저장소에 되돌려 준다. 변경이 없으면 저장소가 빈
        // optional 을 주므로 이 함수는 불리지 않는다.
        void Apply(const LogDelta& delta)
        {
            if (delta.resynchronized)
            {
                m_groups.clear();
                m_occurrences.clear();
            }
            for (const std::uint64_t groupId : delta.removedGroups) m_groups.erase(groupId);
            for (const LogGroup& group : delta.changedGroups) m_groups[group.groupId] = group;
            for (const LogOccurrence& occurrence : delta.appendedOccurrences)
                m_occurrences.push_back(occurrence);
            while (!m_occurrences.empty() &&
                m_occurrences.front().sequence < delta.oldestRetainedSequence)
                m_occurrences.pop_front();
            m_cursor = delta.cursor;
            m_evictedEntries = delta.evictedEntries;
            m_rejectedEntries = delta.rejectedEntries;
            m_levelTotals = delta.levelTotals;
            m_dirty = true;
        }

        void Clear()
        {
            m_groups.clear();
            m_occurrences.clear();
            m_rows.clear();
            m_selection = {};
            m_cursor = {};
            m_evictedEntries = 0;
            m_rejectedEntries = 0;
            m_levelTotals = {};
            m_dirty = true;
        }

        LogCursor Cursor() const { return m_cursor; }
        std::uint64_t EvictedEntries() const { return m_evictedEntries; }
        std::uint64_t RejectedEntries() const { return m_rejectedEntries; }
        // 저장소가 센 값을 그대로 물린다. 창이 따로 세면 상태 표시줄과 두 벌이
        // 되어 한쪽만 맞는 일이 생긴다.
        const LogLevelTotals& LevelTotals() const { return m_levelTotals; }
        const std::vector<OutputLogRow>& Rows() const { return m_rows; }
        // 목록이 빈 이유를 가른다 — 아직 아무것도 안 찍혔나, 걸러졌나.
        bool HasAnyGroup() const { return !m_groups.empty(); }
        const OutputLogSelection& Selection() const { return m_selection; }

        const LogGroup* FindGroup(std::uint64_t groupId) const
        {
            const auto found = m_groups.find(groupId);
            return found == m_groups.end() ? nullptr : &found->second;
        }

        // 선택은 **있는 것만** 받는다. 사라진 그룹을 가리키는 선택은 비운다.
        void Select(const OutputLogSelection& selection)
        {
            m_selection = m_groups.count(selection.groupId) != 0 ? selection : OutputLogSelection{};
        }

        // 필터가 바뀌었거나 변경분이 들어온 뒤에만 행을 다시 세운다. clipper 는
        // **평탄한 목록**을 요구하므로 여기서 한 벌로 만들어 둔다.
        void Rebuild(const OutputLogFilter& filter)
        {
            if (!m_dirty && filter == m_filter) return;
            m_filter = filter;
            m_dirty = false;
            m_rows.clear();

            const std::string needle = ToAsciiLower(filter.search);
            if (filter.collapse)
            {
                // 첫 발생 순서(= groupId 오름차순)로 둔다. 최근 순으로 다시
                // 정렬하면 읽는 도중 행이 튀어 같은 줄을 두 번 읽게 된다.
                for (const auto& [groupId, group] : m_groups)
                {
                    if (!Passes(group, filter.minimumLevel, needle)) continue;
                    m_rows.push_back(OutputLogRow{ group.groupId, 0, group.level,
                        group.totalCount, group.lastTimestamp });
                }
            }
            else
            {
                for (const LogOccurrence& occurrence : m_occurrences)
                {
                    const auto found = m_groups.find(occurrence.groupId);
                    if (found == m_groups.end()) continue;
                    if (!Passes(found->second, filter.minimumLevel, needle)) continue;
                    m_rows.push_back(OutputLogRow{ occurrence.groupId, occurrence.sequence,
                        found->second.level, 1, occurrence.timestamp });
                }
            }

            if (!m_selection.IsEmpty() && m_groups.count(m_selection.groupId) == 0)
                m_selection = {};
        }

        // 화면이 행을 그릴 때 쓰는 본문. 원문은 그룹에 그대로 있다.
        std::string RowText(const OutputLogRow& row) const
        {
            const LogGroup* const group = FindGroup(row.groupId);
            return group == nullptr ? std::string() : FlattenForRow(group->message);
        }

        static std::string ToAsciiLower(std::string_view text)
        {
            std::string lowered(text);
            for (char& character : lowered)
            {
                if (character >= 'A' && character <= 'Z')
                    character = static_cast<char>(character - 'A' + 'a');
            }
            return lowered;
        }

    private:
        static bool Passes(const LogGroup& group, spdlog::level::level_enum minimumLevel,
            const std::string& loweredNeedle)
        {
            // 옛 화면은 `level != trace && level < 필터` 라 **Trace 를 언제나**
            // 보여 줬다 — 고르는 값이 무엇이든 새어 나왔다. 여기서는 고른 값
            // 이상만 지난다.
            if (group.level < minimumLevel) return false;
            if (loweredNeedle.empty()) return true;
            return ToAsciiLower(group.message).find(loweredNeedle) != std::string::npos ||
                ToAsciiLower(group.loggerName).find(loweredNeedle) != std::string::npos;
        }

        std::map<std::uint64_t, LogGroup> m_groups;
        std::deque<LogOccurrence> m_occurrences;
        std::vector<OutputLogRow> m_rows;
        OutputLogSelection m_selection;
        OutputLogFilter m_filter;
        LogCursor m_cursor;
        std::uint64_t m_evictedEntries{};
        LogLevelTotals m_levelTotals;
        std::uint64_t m_rejectedEntries{};
        bool m_dirty{ true };
    };
}
