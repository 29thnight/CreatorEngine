// 에디터 크롬 스냅샷 — 게시·읽기와 감사·덤프 (PHASE 21 W0 전반).
//
// 이 TU 는 ImGui 를 모른다. 값을 채우는 것은 `EditorChromeProbe.cpp` 하나다.
//
// include 는 이 TU 가 직접 소유한다(유니티에서 빠져 있다).

#include "EditorChromeSnapshot.h"
#include "EditorWindowRegistry.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <unordered_map>

namespace editor
{
    namespace
    {
        std::mutex&      snapshot_mutex()    { static std::mutex m; return m; }
        chrome_snapshot& snapshot_storage()  { static chrome_snapshot s; return s; }

        std::atomic<bool>& capture_requested()
        {
            static std::atomic<bool> requested{ true };
            return requested;
        }

        // ini 에 있어도 선언 표에 없는 것이 정상인 이름 둘.
        //
        //   `Main DockSpace Window` — 도크스페이스를 담는 틀이다. 선언된 창이
        //     아니라 `EditorRenderer::BeginRender` 가 매 프레임 직접 여는 host 다.
        //   `Debug##Default` — ImGui 가 스스로 만드는 기본 디버그 창이다.
        //
        // 이 둘을 고아로 세면 감사가 언제나 2를 들고 있어 숫자가 뜻을 잃는다.
        // 목록을 더 늘리고 싶어지면 그 창이 왜 선언 밖에 있는지를 먼저 적어라.
        bool is_shell_owned_window(std::string_view name) noexcept
        {
            return ("Main DockSpace Window" == name) || ("Debug##Default" == name);
        }

        std::string hex_color(std::uint32_t packed)
        {
            char buffer[16]{};
            std::snprintf(buffer, sizeof(buffer), "#%08X", packed);
            return std::string{ buffer };
        }

        std::string joined(const std::vector<std::string>& items, std::size_t limit = 6)
        {
            std::string out;
            for (std::size_t index = 0; index < items.size() && index < limit; ++index)
            {
                if (!out.empty()) out += ", ";
                out += items[index];
            }
            if (items.size() > limit) out += ", ...";
            return out;
        }

        /// `표시 이름###안정 ID` 에서 오른쪽(= 안정 ID)을 뽑는다.
        ///
        /// 셸이 `Begin` 에 넘기는 이름이 그 꼴이고(`EditorWindowHost::compose_title`),
        /// `ImGuiWindow::Name` 에는 그 전체가 들어 있다. 선언 표가 든 것은 오른쪽
        /// 이므로 전체를 그대로 맞대면 그려지는 창이 전부 유령으로 보고된다 —
        /// 첫 실측에서 실제로 여섯이 그렇게 나왔다.
        std::string_view stable_part(std::string_view name) noexcept
        {
            const std::size_t marker = name.rfind("###");
            if (std::string_view::npos == marker) return name;
            return name.substr(marker + 3);
        }

        /// `[Window][NAME]` 줄에서 NAME 을 뽑는다. 없으면 빈 값이다.
        std::string window_entry_name(const std::string& line)
        {
            static constexpr std::string_view prefix{ "[Window][" };
            if (line.size() <= prefix.size()) return {};
            if (0 != line.compare(0, prefix.size(), prefix)) return {};
            const std::size_t close = line.rfind(']');
            if (std::string::npos == close || close <= prefix.size()) return {};
            return line.substr(prefix.size(), close - prefix.size());
        }
    }

    void publish_chrome_snapshot(chrome_snapshot&& snapshot)
    {
        std::lock_guard<std::mutex> guard(snapshot_mutex());
        snapshot_storage() = std::move(snapshot);
    }

    chrome_snapshot read_chrome_snapshot()
    {
        std::lock_guard<std::mutex> guard(snapshot_mutex());
        return snapshot_storage();
    }

    void request_chrome_snapshot()
    {
        // 첫 프레임에 한 번은 떠야 하므로 초기값이 참이다.
        capture_requested().store(true, std::memory_order_release);
    }

    bool consume_chrome_snapshot_request()
    {
        return capture_requested().exchange(false, std::memory_order_acq_rel);
    }

    // ── 감사 ──────────────────────────────────────────────────────────────

    dock_audit audit_dock_tree(const chrome_snapshot& snapshot)
    {
        dock_audit audit{};
        audit.nodes = snapshot.nodes.size();

        for (const dock_node_view& node : snapshot.nodes)
        {
            if (node.is_leaf)    ++audit.leaf_nodes;
            if (node.is_central) ++audit.central_nodes;
        }

        // 선언이 자리를 준 창이 실제로 노드에 들어갔는가.
        //
        // 도크되지 않는 것이 정상인 창(`dock_exempt`)은 제외한다. 그 판단은
        // 게시하는 쪽이 도크 빌더와 같은 조건으로 해서 보낸다 — 여기서 다시
        // 판단하면 같은 규칙이 두 군데가 되어 갈릴 수 있다.
        for (const window_placement_view& placement : snapshot.placements)
        {
            if (placement.dock_exempt) continue;

            // ★ ImGui 가 모르는 창은 **아직 한 번도 그려지지 않은 창**이다.
            //   닫힌 채로 부팅한 창이 그렇고(Resource Counter·Behavior Tree
            //   Editor·BlackBoard Editor 가 실측으로 그랬다), 그것은 결함이
            //   아니다. 도크 빌더는 그래도 자리를 적어 두므로 창이 열리면
            //   제자리로 간다. 결함의 모양은 "그려지고 있는데 자리를 잃은 창"
            //   이고, 그것이 §1.4 가 기록한 Content Browser 사고의 꼴이다.
            if (!placement.known_to_imgui) continue;

            if (0 != placement.dock_node)
            {
                ++audit.docked_windows;
                continue;
            }
            audit.undocked_slots.push_back(placement.stable_id);
        }

        // 노드가 들고 있는데 선언 표에 없는 이름.
        for (const dock_tab_view& tab : snapshot.tabs)
        {
            const std::string_view identity = stable_part(tab.window);
            if (is_shell_owned_window(identity)) continue;
            if (window_declared(identity)) continue;
            audit.ghost_tabs.push_back(tab.window);
        }

        audit.imgui_version_num = snapshot.imgui_version_num;
        audit.internal_api_version_known =
            (expected_imgui_version_num == snapshot.imgui_version_num);

        return audit;
    }

    theme_audit audit_theme(const chrome_snapshot& snapshot)
    {
        theme_audit audit{};
        audit.colors  = snapshot.style_colors.size();
        audit.scalars = snapshot.style_scalars.size();
        audit.colors_differing_from_default = snapshot.colors_differing_from_default;
        audit.font_global_scale = snapshot.font_global_scale;
        audit.preference_scale  = snapshot.preference_scale;

        // 에디터 스킨이 실제로 적용됐는가. `ApplyEditorStyle` 이 40 항목 넘게
        // 덮어쓰므로 한 자리 숫자면 돌지 않은 것이다. 임계를 10 으로 둔 이유는
        // 스킨을 다시 써도 그 아래로 내려갈 일은 없고, 아예 안 도는 경우와는
        // 확실히 갈리기 때문이다.
        audit.style_applied = (10 <= snapshot.colors_differing_from_default);

        // 배율 출처가 하나인가. 적용 경로가 둘이라 한쪽만 고치면 갈린다.
        const float difference = snapshot.font_global_scale - snapshot.preference_scale;
        audit.scale_matches = (difference > -0.0001f) && (difference < 0.0001f);

        return audit;
    }

    layout_audit audit_layout(const chrome_snapshot& snapshot)
    {
        layout_audit audit{};
        audit.ini_path   = snapshot.ini_path;
        audit.ini_exists = snapshot.ini_exists;
        audit.ini_bytes  = snapshot.ini_bytes;

        if (!audit.ini_exists) return audit;

        std::ifstream stream(std::filesystem::path{ snapshot.ini_path },
                             std::ios::binary);
        if (!stream)
        {
            // 있다고 게시됐는데 열리지 않는다. 읽을 수 없는 것을 "항목 0" 으로
            // 내면 빈 집합이 성공으로 읽힌다.
            audit.ini_exists = false;
            return audit;
        }

        std::unordered_map<std::string, int> seen;
        std::string line;
        while (std::getline(stream, line))
        {
            if (!line.empty() && '\r' == line.back()) line.pop_back();
            const std::string name = window_entry_name(line);
            if (name.empty()) continue;

            ++audit.ini_entries;
            const int count = ++seen[name];
            if (2 == count) audit.duplicate_entries.push_back(name);

            if (is_shell_owned_window(name))   continue;
            if (window_declared(name))         { ++audit.matched; continue; }
            audit.orphan_entries.push_back(name);
        }

        std::sort(audit.duplicate_entries.begin(), audit.duplicate_entries.end());
        std::sort(audit.orphan_entries.begin(), audit.orphan_entries.end());
        return audit;
    }

    // ── 덤프 ──────────────────────────────────────────────────────────────

    std::string dump_dock_tree(const chrome_snapshot& snapshot)
    {
        std::string out{ "node\tparent\tx0\ty0\tx1\ty1\taxis\ttabs\tcentral\tleaf\tvisible\n" };
        char buffer[256]{};
        for (const dock_node_view& node : snapshot.nodes)
        {
            std::snprintf(buffer, sizeof(buffer),
                "%u\t%u\t%.0f\t%.0f\t%.0f\t%.0f\t%d\t%d\t%d\t%d\t%d\n",
                node.id, node.parent,
                node.rect[0], node.rect[1], node.rect[2], node.rect[3],
                node.split_axis, node.tab_count,
                node.is_central ? 1 : 0, node.is_leaf ? 1 : 0,
                node.is_visible ? 1 : 0);
            out += buffer;
        }

        // 어느 노드에 무엇이 들어앉았는가. 유령 탭이 감사 한 줄에만 나오면
        // 사람이 자리를 못 본다 — 변이 증명에서 이 목록을 눈으로 확인해야 했다.
        out += "\nnodeTab\twindow\n";
        for (const dock_tab_view& tab : snapshot.tabs)
        {
            std::snprintf(buffer, sizeof(buffer), "%u\t", tab.node);
            out += buffer;
            out += tab.window;
            out += '\n';
        }

        out += "\nwindow\tnode\tx0\ty0\tx1\ty1\tknown\tdockActive\tdockExempt\n";
        for (const window_placement_view& placement : snapshot.placements)
        {
            out += placement.stable_id;
            std::snprintf(buffer, sizeof(buffer),
                "\t%u\t%.0f\t%.0f\t%.0f\t%.0f\t%d\t%d\t%d\n",
                placement.dock_node,
                placement.rect[0], placement.rect[1],
                placement.rect[2], placement.rect[3],
                placement.known_to_imgui ? 1 : 0,
                placement.dock_active ? 1 : 0,
                placement.dock_exempt ? 1 : 0);
            out += buffer;
        }
        return out;
    }

    std::string dump_theme(const chrome_snapshot& snapshot)
    {
        std::string out{ "styleColor\tvalue\n" };
        for (const std::pair<std::string, std::uint32_t>& color : snapshot.style_colors)
        {
            out += color.first;
            out += '\t';
            out += hex_color(color.second);
            out += '\n';
        }

        out += "\nstyleScalar\tvalue\n";
        char buffer[128]{};
        for (const std::pair<std::string, float>& scalar : snapshot.style_scalars)
        {
            out += scalar.first;
            std::snprintf(buffer, sizeof(buffer), "\t%.3f\n", scalar.second);
            out += buffer;
        }
        return out;
    }

    std::string dump_layout(const chrome_snapshot& snapshot)
    {
        const layout_audit audit = audit_layout(snapshot);

        std::string out{ "iniKey\tvalue\n" };
        out += "path\t";
        out += audit.ini_path;
        out += '\n';

        char buffer[128]{};
        std::snprintf(buffer, sizeof(buffer),
            "exists\t%d\nbytes\t%llu\nwindowEntries\t%zu\nmatched\t%zu\n",
            audit.ini_exists ? 1 : 0,
            static_cast<unsigned long long>(audit.ini_bytes),
            audit.ini_entries, audit.matched);
        out += buffer;

        // 활성 workspace·preset·schema version 은 아직 없다. 있는 척하지 않고
        // 없음을 적는다 — W3 이 그것을 세울 때 이 줄이 값으로 바뀐다.
        out += "workspace\t(none — W3)\npreset\t(none — W6)\nschemaVersion\t0\n";

        out += "\niniOrphan\n";
        for (const std::string& orphan : audit.orphan_entries)
        {
            out += orphan;
            out += '\n';
        }
        return out;
    }

    std::string dump_dock_audit(const chrome_snapshot& snapshot)
    {
        const dock_audit audit = audit_dock_tree(snapshot);
        char buffer[256]{};
        std::snprintf(buffer, sizeof(buffer),
            "\n[AUDIT] dock nodes=%zu leaf=%zu central=%zu docked=%zu"
            " undocked=%zu ghost=%zu frame=%llu imguiVersion=%d versionKnown=%d\n",
            audit.nodes, audit.leaf_nodes, audit.central_nodes,
            audit.docked_windows, audit.undocked_slots.size(),
            audit.ghost_tabs.size(),
            static_cast<unsigned long long>(snapshot.frame),
            audit.imgui_version_num,
            audit.internal_api_version_known ? 1 : 0);
        std::string out{ buffer };

        // 계측 자신의 비용. 매 프레임 도는 것이 자기 값을 숨기면 W0 의 성능
        // 기준선이 거짓이 된다.
        std::snprintf(buffer, sizeof(buffer),
            "[AUDIT] captureMs=%.4f\n", snapshot.capture_ms);
        out += buffer;

        if (!audit.undocked_slots.empty())
        {
            out += "[AUDIT] 자리를 선언했는데 도크되지 않은 창: " +
                   joined(audit.undocked_slots) + "\n";
        }
        if (!audit.ghost_tabs.empty())
        {
            out += "[AUDIT] 선언에 없는 이름이 도크 노드에 있다: " +
                   joined(audit.ghost_tabs) + "\n";
        }
        if (1 < audit.central_nodes)
        {
            out += "[AUDIT] central node 가 둘 이상이다\n";
        }
        if (0 == audit.central_nodes)
        {
            out += "[NOTE] central node 가 없다 — 중앙 ViewportHost 노드는 W4 가 세운다\n";
        }
        if (!audit.internal_api_version_known)
        {
            out += "[AUDIT] ImGui 판이 바뀌었다 — imgui_internal.h 구조체를 읽는 "
                   "코드를 다시 검증하고 expected_imgui_version_num 을 올려라\n";
        }
        return out;
    }

    std::string dump_theme_audit(const chrome_snapshot& snapshot)
    {
        const theme_audit audit = audit_theme(snapshot);
        char buffer[320]{};
        std::snprintf(buffer, sizeof(buffer),
            "\n[AUDIT] theme colors=%zu scalars=%zu differing=%zu"
            " fontGlobalScale=%.3f preferenceScale=%.3f applied=%d scaleMatch=%d\n",
            audit.colors, audit.scalars, audit.colors_differing_from_default,
            audit.font_global_scale, audit.preference_scale,
            audit.style_applied ? 1 : 0, audit.scale_matches ? 1 : 0);
        std::string out{ buffer };

        if (!audit.style_applied)
        {
            out += "[AUDIT] 에디터 스킨이 적용되지 않았다 — ImGui 기본값과 같다\n";
        }
        if (!audit.scale_matches)
        {
            out += "[AUDIT] 배율 출처가 갈렸다 — io.FontGlobalScale 과 설정값이 다르다\n";
        }

        // 판정에 넣지 않고 적어 두는 것. `io.FontGlobalScale` 은 1.92 에서
        // obsolete 이고 정식 경로는 `style.FontScaleMain` 이다(계획서 §1.1).
        // 그 이주는 W1 의 몫이므로 여기서 붉게 만들면 W1 착수 전까지 게이트가
        // 내내 붉어 도는 세트에 들어갈 수 없다. 판 번호를 같은 줄에 적는다 —
        // obsolete 여부가 판에 달린 판단이라 근거를 떼어 두지 않는다.
        out += "[NOTE] imgui " + snapshot.imgui_version +
               " · 배율이 obsolete io.FontGlobalScale 경로다 — W1 이 "
               "style.FontScaleMain 으로 이주한다\n";
        return out;
    }

    std::string dump_layout_audit(const chrome_snapshot& snapshot)
    {
        const layout_audit audit = audit_layout(snapshot);
        char buffer[256]{};
        std::snprintf(buffer, sizeof(buffer),
            "\n[AUDIT] layout iniExists=%d entries=%zu matched=%zu"
            " duplicate=%zu orphan=%zu\n",
            audit.ini_exists ? 1 : 0, audit.ini_entries, audit.matched,
            audit.duplicate_entries.size(), audit.orphan_entries.size());
        std::string out{ buffer };

        if (!audit.duplicate_entries.empty())
        {
            out += "[AUDIT] 같은 창 이름이 ini 에 두 번 있다: " +
                   joined(audit.duplicate_entries) + "\n";
        }
        if (audit.ini_exists && (0 == audit.matched))
        {
            out += "[AUDIT] ini 에 선언과 맞물린 항목이 하나도 없다\n";
        }
        if (!audit.orphan_entries.empty())
        {
            out += "[NOTE] 선언에 없는 ini 항목 — 옛 빌드의 흔적이다. W3 이주가 읽는다: " +
                   joined(audit.orphan_entries) + "\n";
        }
        return out;
    }
}
