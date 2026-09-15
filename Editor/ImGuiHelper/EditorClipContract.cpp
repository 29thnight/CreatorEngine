#include "EditorClipContract.h"

#include "ImGui.h"

#include <algorithm>
#include <mutex>

namespace editor::clipping
{
    namespace
    {
        // ── 게시본 ───────────────────────────────────────────────────────────
        //
        // 신고는 ImGui 스레드, 읽기는 게임 스레드의 CLI 다. `editor::nav` 와 달리
        // 프레임 안에서 맞대 볼 것이 없다 — 판정이 신고 그 자리에서 끝나므로
        // 프레임 지역 버퍼가 필요 없고, 게시본 하나만 잠금으로 가른다.
        std::mutex g_mutex;
        contract_view g_published;

        // `enter_widget` 이 남긴 깊이. 위젯은 겹쳐 그려질 수 있으므로(패널 안의
        // 속성 줄) 스택으로 둔다.
        struct open_widget
        {
            const char* widget{ nullptr };
            int depth{ 0 };
        };
        std::vector<open_widget> g_openWidgets;

        void remember(std::vector<std::string>& names, const char* name)
        {
            if (nullptr == name) return;
            if (std::find(names.begin(), names.end(), name) != names.end()) return;
            if (names.size() >= kTrackedNames) return;
            names.emplace_back(name);
        }
    }

    void announce_text(const char* widget, float text_width, float column_width,
        bool clipped, bool tooltip)
    {
        if (nullptr == widget) return;

        std::lock_guard<std::mutex> guard(g_mutex);
        ++g_published.announced;
        remember(g_published.measuredWidgets, widget);

        // 폭 비교에 여유를 준다. `CalcTextSize` 와 실제 배치가 소수점 아래에서
        // 갈리는 프레임이 있고, 그 반올림을 위반으로 세면 판정이 온통 붉어진다.
        // 1 픽셀 안쪽은 겹치지 않는다.
        constexpr float kSlack = 1.f;
        const bool overflows = (text_width > column_width + kSlack);

        if (!overflows)
        {
            // 넘치지 않으면 자르든 말든 보이는 것은 같다. 셀 것이 없다.
            return;
        }

        if (!clipped)
        {
            // 옆 칸 위로 그려진다. 글자는 대개 오른쪽 컨트롤보다 **나중에**
            // 그려지므로 덮는 쪽이다.
            ++g_published.overflowFrames;
            remember(g_published.overflowWidgets, widget);
            return;
        }

        if (!tooltip)
        {
            // 잘라서 옆 칸은 지켰는데, 잘린 이름을 돌려줄 길이 없다. 눈에 띄는
            // 고장이 아니라 **조용히 사라지는 정보**라 더 오래 남는다.
            ++g_published.silentFrames;
            remember(g_published.silentWidgets, widget);
            return;
        }

        ++g_published.truncated;
        remember(g_published.truncatedWidgets, widget);
    }

    void enter_widget(const char* widget, int clip_stack_depth)
    {
        if (nullptr == widget) return;
        g_openWidgets.push_back(open_widget{ widget, clip_stack_depth });
    }

    void leave_widget(const char* widget, int clip_stack_depth)
    {
        if (nullptr == widget || g_openWidgets.empty()) return;

        const open_widget opened = g_openWidgets.back();
        g_openWidgets.pop_back();
        if (opened.depth == clip_stack_depth) return;

        // 들어올 때와 나갈 때의 깊이가 다르다 — 어딘가에서 push 하고 pop 하지
        // 않았거나(그 뒤 전부가 좁은 사각형에 갇힌다) 남의 것을 pop 했다.
        std::lock_guard<std::mutex> guard(g_mutex);
        ++g_published.unbalanced;
        remember(g_published.unbalancedWidgets, opened.widget);
    }

    void observe_frame()
    {
        // 프레임을 넘기며 열린 채 남은 위젯이 있으면 그것도 균형이 아니다.
        // 다음 프레임의 신고와 섞이지 않게 여기서 비운다.
        if (!g_openWidgets.empty())
        {
            std::lock_guard<std::mutex> guard(g_mutex);
            for (const open_widget& opened : g_openWidgets)
            {
                ++g_published.unbalanced;
                remember(g_published.unbalancedWidgets, opened.widget);
            }
            g_openWidgets.clear();
        }

        std::lock_guard<std::mutex> guard(g_mutex);
        ++g_published.frames;
    }

    contract_view read()
    {
        std::lock_guard<std::mutex> guard(g_mutex);
        return g_published;
    }

    void reset_counts()
    {
        std::lock_guard<std::mutex> guard(g_mutex);
        g_published = contract_view{};
    }
}
