#include "EditorStateContract.h"

#include "ImGui.h"

#include <algorithm>
#include <cstring>
#include <mutex>

namespace editor::state
{
    namespace
    {
        struct named_bit { std::uint32_t bit; const char* name; };

        // 계획서가 적은 순서 그대로. 출력도 이 순서를 따른다.
        constexpr named_bit kBits[] = {
            { hover,    "hover" },
            { active,   "active" },
            { focus,    "focus" },
            { nav,      "nav" },
            { disabled, "disabled" },
            { mixed,    "mixed" },
            { error,    "error" },
        };

        std::mutex g_mutex;
        contract_view g_published;

        widget_view& entry(const char* widget)
        {
            for (widget_view& view : g_published.widgets)
            {
                if (view.widget == widget) return view;
            }
            g_published.widgets.push_back(widget_view{});
            g_published.widgets.back().widget = widget;
            return g_published.widgets.back();
        }
    }

    const char* name_of(std::uint32_t bit) noexcept
    {
        for (const named_bit& named : kBits)
        {
            if (named.bit == bit) return named.name;
        }
        return nullptr;
    }

    std::uint32_t bit_of(const char* name) noexcept
    {
        if (nullptr == name) return 0u;
        for (const named_bit& named : kBits)
        {
            if (0 == std::strcmp(named.name, name)) return named.bit;
        }
        return 0u;
    }

    void declare(const char* widget, std::uint32_t declared,
        std::uint32_t notApplicable, const char* reason)
    {
        if (nullptr == widget) return;

        // 선언이 스스로 모순이면(같은 상태를 구분한다면서 올 수 없다고도 하면)
        // 판정이 무엇을 요구하는지 알 수 없다. 겹치는 비트는 **선언 쪽을
        // 버린다** — 올 수 없다는 쪽이 더 강한 주장이고, 게이트가 이유를 읽는다.
        declared &= ~notApplicable;

        std::lock_guard<std::mutex> guard(g_mutex);
        widget_view& view = entry(widget);
        view.declared = declared;
        view.notApplicable = notApplicable;
        view.reason = (nullptr != reason) ? reason : "";
    }

    void announce(const char* widget, std::uint32_t states, const ImRect& bounds)
    {
        if (nullptr == widget) return;

        std::lock_guard<std::mutex> guard(g_mutex);
        ++g_published.announced;
        widget_view& view = entry(widget);
        view.observed |= (states & all);
        ++view.frames;
        view.x0 = bounds.Min.x; view.y0 = bounds.Min.y;
        view.x1 = bounds.Max.x; view.y1 = bounds.Max.y;
    }

    void observe_frame()
    {
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
        // ★ 선언은 남기고 관측만 비운다. 선언은 이 실행의 성질이라 구간마다
        //   달라지지 않으며, 지워 버리면 첫 구간에서 행렬이 통째로 비어 보인다
        //   — 그 빈 표를 "위반 0" 으로 읽는 것이 이 저장소가 여러 번 겪은
        //   눈먼 초록이다.
        g_published.frames = 0;
        g_published.announced = 0;
        for (widget_view& view : g_published.widgets)
        {
            view.observed = 0;
            view.frames = 0;
        }
    }
}
