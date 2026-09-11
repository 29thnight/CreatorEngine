// 크롬 스냅샷을 뜨는 쪽 (PHASE 21 W0 전반) — ImGui 를 아는 유일한 자리.
//
// ── 내부 API 를 읽는다는 것 ──────────────────────────────────────────────
//
// 도크 노드와 창의 도크 소속은 `imgui.h` 의 공개 면에 없다. `imgui_internal.h`
// 의 `ImGuiDockNode`·`ImGuiWindow` 를 직접 읽어야 한다. 그 구조체는 판마다
// 바뀌므로 판 번호를 스냅샷에 싣고 감사가 대조한다
// (`expected_imgui_version_num`). 컴파일이 통과하면서 읽는 값만 어긋나는
// 종류의 사고를 그것이 막는다.
//
// ── 노드를 재귀로 훑지 않는 이유 ─────────────────────────────────────────
//
// `DockContext.Nodes` 가 ID → 노드 맵이라 **살아 있는 노드 전부**가 거기 있다.
// 뿌리에서 자식으로 내려가면 떠 있는 노드(도크스페이스에 안 붙은 것)를 놓친다.
// 그리고 `ImGuiStorage` 는 키로 정렬돼 있어 순회가 결정적이다 — 골든이 흔들리지
// 않아야 한다는 §1.3-4 의 요구를 여기서도 지킨다.
//
// include 는 이 TU 가 직접 소유한다(유니티에서 빠져 있다).

#include "EditorChromeProbe.h"

#include "EditorChromeSnapshot.h"
#include "EditorWindowRegistry.h"
#include "EditorWindowNames.h"
#include "EditorSettingsStore.h"

#include "ImGui.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <system_error>

namespace
{
    /// 라벨의 코드포인트 중 현재 폰트에 **없는** 것의 수.
    ///
    /// 1.92 의 폰트는 동적이라 아직 래스터되지 않은 글리프가 흔하다. 그래서
    /// `FindGlyphNoFallback` 이 아니라 `IsGlyphInFont` 를 묻는다 — 앞의 것은
    /// "아직 안 구웠다"와 "폰트에 없다"를 구분하지 못해 멀쩡한 아이콘을
    /// 누락으로 셀 수 있다.
    int count_missing_glyphs(const std::string& label)
    {
        ImFont* const font = ImGui::GetFont();
        if (nullptr == font) return 0;

        int missing = 0;
        const char* cursor = label.c_str();
        const char* const end = cursor + label.size();
        while (cursor < end)
        {
            unsigned int codepoint = 0;
            const int bytes = ImTextCharFromUtf8(&codepoint, cursor, end);
            if (0 == bytes) break;
            cursor += bytes;

            // 공백은 폰트에 글리프가 없어도 정상으로 그려진다.
            if ((0 == codepoint) || (' ' == codepoint)) continue;
            if (!font->IsGlyphInFont(static_cast<ImWchar>(codepoint))) ++missing;
        }
        return missing;
    }

    std::uint32_t pack_rgba(const ImVec4& color) noexcept
    {
        const auto channel = [](float value) -> std::uint32_t
        {
            const float clamped = (value < 0.f) ? 0.f : ((value > 1.f) ? 1.f : value);
            return static_cast<std::uint32_t>(clamped * 255.f + 0.5f);
        };
        return (channel(color.x) << 24) | (channel(color.y) << 16) |
               (channel(color.z) << 8)  |  channel(color.w);
    }

    bool same_color(const ImVec4& a, const ImVec4& b) noexcept
    {
        return pack_rgba(a) == pack_rgba(b);
    }
}

namespace editor
{
    void capture_chrome_draw_totals(double ui_cpu_ms)
    {
        const ImDrawData* const drawData = ImGui::GetDrawData();
        // `Render()` 앞이거나 이 프레임이 그려지지 않았으면 유효하지 않다.
        // 그때 0 을 얹으면 "UI 가 아무것도 안 그렸다" 로 읽히므로 얹지 않는다.
        if ((nullptr == drawData) || (!drawData->Valid)) return;

        std::int64_t commands = 0;
        for (int index = 0; index < drawData->CmdListsCount; ++index)
        {
            commands += drawData->CmdLists[index]->CmdBuffer.Size;
        }
        amend_chrome_draw_totals(drawData->TotalVtxCount,
                                 drawData->TotalIdxCount,
                                 commands,
                                 ui_cpu_ms);
    }

    bool capture_chrome_snapshot()
    {
        const std::chrono::steady_clock::time_point began =
            std::chrono::steady_clock::now();

        ImGuiContext* const context = ImGui::GetCurrentContext();
        if (nullptr == context) return false;

        // 주기 + 요청. 매 프레임 뜨면 진단 장치가 0.47 ms(Debug 실측)를 물고
        // 가므로 간격을 둔다. 요청이 있으면 간격을 기다리지 않는다.
        static int framesSinceCapture = kCaptureIntervalFrames;
        const bool onRequest = consume_chrome_snapshot_request();
        if (!onRequest && (++framesSinceCapture < kCaptureIntervalFrames)) return false;
        framesSinceCapture = 0;

        chrome_snapshot snapshot{};
        snapshot.valid            = true;
        snapshot.frame            = static_cast<std::uint64_t>(ImGui::GetFrameCount());
        snapshot.imgui_version    = IMGUI_VERSION;
        snapshot.imgui_version_num = IMGUI_VERSION_NUM;

        // ── 도크 노드 ─────────────────────────────────────────────────────
        const ImVector<ImGuiStoragePair>& nodeMap = context->DockContext.Nodes.Data;
        snapshot.nodes.reserve(static_cast<std::size_t>(nodeMap.Size));
        for (int index = 0; index < nodeMap.Size; ++index)
        {
            ImGuiDockNode* const node =
                static_cast<ImGuiDockNode*>(nodeMap[index].val_p);
            if (nullptr == node) continue;

            dock_node_view view{};
            view.id         = static_cast<std::uint32_t>(node->ID);
            view.parent     = (nullptr != node->ParentNode)
                            ? static_cast<std::uint32_t>(node->ParentNode->ID) : 0u;
            view.rect[0]    = node->Pos.x;
            view.rect[1]    = node->Pos.y;
            view.rect[2]    = node->Pos.x + node->Size.x;
            view.rect[3]    = node->Pos.y + node->Size.y;
            view.split_axis = node->IsSplitNode() ? static_cast<int>(node->SplitAxis) : -1;
            view.tab_count  = node->Windows.Size;
            view.is_central = node->IsCentralNode();
            view.is_leaf    = node->IsLeafNode();
            view.is_visible = node->IsVisible;
            snapshot.nodes.push_back(view);

            for (int slot = 0; slot < node->Windows.Size; ++slot)
            {
                const ImGuiWindow* const docked = node->Windows[slot];
                if (nullptr == docked || nullptr == docked->Name) continue;
                snapshot.tabs.push_back(dock_tab_view{
                    static_cast<std::uint32_t>(node->ID), std::string{ docked->Name } });
            }
        }

        // ── 선언된 창의 배치 ──────────────────────────────────────────────
        //
        // 도크 빌더가 건너뛰는 조건과 **같은 조건**을 여기서 쓴다
        // (`EditorRenderer::BuildInitialDockLayout`). 조건이 갈리면 감사가
        // 정상을 결함으로 보고한다. 지금 그 조건은 "떠 있는 창" 하나뿐이다 —
        // Content Browser 의 서랍 예외가 스타일 분기와 함께 사라졌다.
        const std::vector<window_entry>& entries = window_entries_of();
        snapshot.placements.reserve(entries.size());
        for (const window_entry& entry : entries)
        {
            window_placement_view view{};
            view.stable_id = std::string{ entry.stable_id };
            view.label = std::string{ entry.label };
            view.dock_exempt = (dock_slot::floating == entry.dock);
            view.missing_glyphs = count_missing_glyphs(view.label);

            if (ImGuiWindow* const window = ImGui::FindWindowByName(view.stable_id.c_str()))
            {
                view.known_to_imgui = true;
                view.dock_node      = static_cast<std::uint32_t>(window->DockId);
                view.dock_active    = window->DockIsActive;
                view.rect[0]        = window->Pos.x;
                view.rect[1]        = window->Pos.y;
                view.rect[2]        = window->Pos.x + window->Size.x;
                view.rect[3]        = window->Pos.y + window->Size.y;
            }
            snapshot.placements.push_back(std::move(view));
        }

        // ── 스타일 ────────────────────────────────────────────────────────
        //
        // 기본 생성한 `ImGuiStyle` 은 ImGui 의 기본 다크 스킨이다(생성자가
        // `StyleColorsDark` 를 부른다). 그것과 다른 색의 수가 "에디터 스킨이
        // 실제로 적용됐는가" 의 관측이 된다 — 적용을 빼먹으면 0 이 된다.
        const ImGuiStyle& style = ImGui::GetStyle();
        const ImGuiStyle  defaults{};

        snapshot.style_colors.reserve(static_cast<std::size_t>(ImGuiCol_COUNT));
        for (int slot = 0; slot < ImGuiCol_COUNT; ++slot)
        {
            snapshot.style_colors.emplace_back(
                std::string{ ImGui::GetStyleColorName(slot) },
                pack_rgba(style.Colors[slot]));
            if (!same_color(style.Colors[slot], defaults.Colors[slot]))
            {
                ++snapshot.colors_differing_from_default;
            }
        }

        // 스칼라는 손으로 고른 목록이다. 개수를 단정하지 않으므로 늘려도
        // 게이트가 깨지지 않는다 — 덤프가 한 줄 늘어날 뿐이다.
        const auto scalar = [&snapshot](const char* name, float value)
        {
            snapshot.style_scalars.emplace_back(std::string{ name }, value);
        };
        scalar("Alpha", style.Alpha);
        scalar("DisabledAlpha", style.DisabledAlpha);
        scalar("WindowPadding.x", style.WindowPadding.x);
        scalar("WindowPadding.y", style.WindowPadding.y);
        scalar("WindowRounding", style.WindowRounding);
        scalar("WindowBorderSize", style.WindowBorderSize);
        scalar("ChildRounding", style.ChildRounding);
        scalar("ChildBorderSize", style.ChildBorderSize);
        scalar("PopupRounding", style.PopupRounding);
        scalar("FramePadding.x", style.FramePadding.x);
        scalar("FramePadding.y", style.FramePadding.y);
        scalar("FrameRounding", style.FrameRounding);
        scalar("FrameBorderSize", style.FrameBorderSize);
        scalar("ItemSpacing.x", style.ItemSpacing.x);
        scalar("ItemSpacing.y", style.ItemSpacing.y);
        scalar("ItemInnerSpacing.x", style.ItemInnerSpacing.x);
        scalar("ItemInnerSpacing.y", style.ItemInnerSpacing.y);
        scalar("CellPadding.x", style.CellPadding.x);
        scalar("CellPadding.y", style.CellPadding.y);
        scalar("IndentSpacing", style.IndentSpacing);
        scalar("ScrollbarSize", style.ScrollbarSize);
        scalar("ScrollbarRounding", style.ScrollbarRounding);
        scalar("GrabMinSize", style.GrabMinSize);
        scalar("GrabRounding", style.GrabRounding);
        scalar("TabRounding", style.TabRounding);
        scalar("TabBorderSize", style.TabBorderSize);
        scalar("WindowTitleAlign.x", style.WindowTitleAlign.x);
        scalar("WindowTitleAlign.y", style.WindowTitleAlign.y);

        const ImGuiIO& io = ImGui::GetIO();
        snapshot.font_scale_main = style.FontScaleMain;
        snapshot.preference_scale  =
            EditorSettingsStore::Get().Preferences().GetImGuiScale();

        // ── ini ───────────────────────────────────────────────────────────
        //
        // `PathFinder::ConfigPath` 를 다시 조립하지 않고 **ImGui 가 실제로 쓰는
        // 값**을 싣는다. 둘이 같다는 것은 계획서 §1.2 의 주장이고, 주장을
        // 베끼면 어긋난 순간을 못 본다.
        if (nullptr != io.IniFilename)
        {
            snapshot.ini_path = io.IniFilename;
            std::error_code error{};
            const std::filesystem::path path{ snapshot.ini_path };
            snapshot.ini_exists = std::filesystem::exists(path, error) && !error;
            if (snapshot.ini_exists)
            {
                const std::uintmax_t size = std::filesystem::file_size(path, error);
                snapshot.ini_bytes = error ? 0u : static_cast<std::uint64_t>(size);
            }
        }

        const std::chrono::steady_clock::time_point ended =
            std::chrono::steady_clock::now();
        snapshot.capture_ms =
            std::chrono::duration<double, std::milli>(ended - began).count();

        publish_chrome_snapshot(std::move(snapshot));
        return true;
    }
}
