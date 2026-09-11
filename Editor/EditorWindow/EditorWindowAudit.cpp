// 창 배선 감사 (PHASE 21 M4 4단계).
//
// 이 TU도 유니티에서 뺀다 — EditorWindowRegistry.cpp와 같은 이유.

#include "EditorWindowAudit.h"

#include "EditorWindowRegistry.h"
#include "EditorWindowSurface.h"
#include "Windows/EditorWindowBody.h"
#include "EditorWindowNames.h"

#include <algorithm>

namespace editor
{
    namespace
    {
        // 본문 보관소를 쓰지 않고 선언이 자유 함수를 직접 부르는 창들이다.
        //
        // 보관소는 본문이 객체의 것이라 `this`를 물어야 할 때 필요한 장치인데,
        // 애니메이터 셋의 본문은 자유 함수라 걸 것이 없다. 그래서 "선언은
        // 있는데 본문이 안 걸렸다"가 이 셋에서는 정상이다.
        //
        // ★ 목록이 여기 손으로 적혀 있는 것이 요점이다. 자유 함수로 그리는
        //   창을 새로 만들고 여기 적지 않으면 감사가 붉어지고, 고치는 방법은
        //   "의도를 적는 것"이다. 자동으로 알아내면 오타로 죽은 창과 구분이
        //   되지 않는다 — 둘 다 "보관소에 없다"로 똑같이 보이기 때문이다.
        // PHASE 21 W3 이 이 목록을 늘린다. 창이 드는 것이 UI 지역 상태뿐이면
        // 소유자를 둘 이유가 없어 자유 함수와 TU 지역 상태로 옮기고, 그러면
        // 보관소에 걸 일이 없어진다. 여덟이 다 옮겨 가면 보관소에 남는 것은
        // 남의 객체 안에 사는 본문 열넷뿐이고, 그때 이 검사는 방향이 뒤집힌다 —
        // "목록에 없는 것이 예외" 가 된다. 그 전환은 열넷이 정리된 뒤에 한다.
        constexpr std::string_view kSelfDrawingWindows[]
        {
            EditorWindowName::kAnimatorEvent,
            EditorWindowName::kAnimationControllers,
            EditorWindowName::kAvatarMask,
            // W3 에서 옮긴 것들.
            EditorWindowName::kScene,
            EditorWindowName::kGame,
            EditorWindowName::kContentBrowser,
            EditorWindowName::kHierarchy,
            EditorWindowName::kInspector,
            EditorWindowName::kAssetBundle,
            EditorWindowName::kResourceCounter,
            EditorWindowName::kRenderPass,
        };

        bool draws_without_store(std::string_view stable_id)
        {
            for (std::string_view one : kSelfDrawingWindows)
            {
                if (one == stable_id) return true;
            }
            return false;
        }

        bool contains(const std::vector<std::string_view>& names, std::string_view one)
        {
            return names.end() != std::find(names.begin(), names.end(), one);
        }
    }

    window_audit audit_declared_windows(const window_table& table)
    {
        window_audit audit;

        const std::vector<window_entry>& entries = table.entries;
        const std::vector<std::string_view> bodies = windows::bound_window_bodies();

        audit.declared = entries.size();
        audit.bound = bodies.size();

        // ① 걸렸는데 선언이 없다. 그 본문은 영영 불리지 않는다.
        for (std::string_view name : bodies)
        {
            if (nullptr == find_window_of(name))
            {
                audit.orphan_bodies.push_back(name);
            }
        }

        // ② 선언은 있는데 걸릴 자리가 비었다. 자유 함수로 그리는 창은 뺀다.
        // ③ 같은 이름이 두 번 선언됐다.
        std::vector<std::string_view> seen;
        seen.reserve(entries.size());
        for (const window_entry& entry : entries)
        {
            if (!draws_without_store(entry.stable_id) && !contains(bodies, entry.stable_id))
            {
                audit.bodyless_windows.push_back(entry.stable_id);
            }

            if (contains(seen, entry.stable_id))
            {
                if (!contains(audit.duplicate_ids, entry.stable_id))
                {
                    audit.duplicate_ids.push_back(entry.stable_id);
                }
            }
            else
            {
                seen.push_back(entry.stable_id);
            }
        }

        // ④ 아무 창도 가지 않는 도킹 자리. 배치가 빈 노드를 만든다.
        for (dock_slot slot : all_dock_slots)
        {
            if (dock_slot::floating == slot) continue;

            const bool filled = entries.end() != std::find_if(entries.begin(), entries.end(),
                [slot](const window_entry& entry) { return entry.dock == slot; });
            if (!filled)
            {
                audit.empty_dock_slots.push_back(to_string(slot));
            }
        }

        return audit;
    }

    namespace
    {
        void append_list(std::string& out, const char* label,
                         const std::vector<std::string_view>& names)
        {
            out += label;
            out += '=';
            out += std::to_string(names.size());
            if (!names.empty())
            {
                out += " [";
                for (std::size_t i = 0; i < names.size(); ++i)
                {
                    if (0 != i) out += ", ";
                    out.append(names[i]);
                }
                out += ']';
            }
            out += '\n';
        }
    }

    std::string dump_window_audit()
    {
        const window_audit audit = audit_declared_windows();

        std::string out;
        out += "declared=" + std::to_string(audit.declared) +
               "  bound_bodies=" + std::to_string(audit.bound) + '\n';
        append_list(out, "orphan_bodies", audit.orphan_bodies);
        append_list(out, "bodyless_windows", audit.bodyless_windows);
        append_list(out, "duplicate_ids", audit.duplicate_ids);
        append_list(out, "empty_dock_slots", audit.empty_dock_slots);
        out += audit.clean() ? "audit=clean\n" : "audit=dirty\n";
        return out;
    }
}
