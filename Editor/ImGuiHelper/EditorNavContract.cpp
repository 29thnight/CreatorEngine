#include "EditorNavContract.h"

#include "ImGui.h"

#include <algorithm>
#include <deque>
#include <mutex>

namespace editor::nav
{
    namespace
    {
        struct announced_item
        {
            ImGuiID id{ 0 };
            const char* widget{ nullptr };
            bool interactive{ true };
            bool delegated{ false };
        };

        // ── ImGui 스레드 전용 ────────────────────────────────────────────────
        //
        // 한 프레임 동안만 산다. 잠금이 없는 이유는 신고·그리기·관측이 전부
        // 같은 스레드의 같은 프레임 안에서 일어나기 때문이다.
        std::vector<announced_item> g_frameItems;
        std::vector<announced_item> g_frameCursors;

        // ── 게시본 ───────────────────────────────────────────────────────────
        std::mutex g_mutex;
        contract_view g_published;
        std::deque<ImGuiKey> g_pendingKeys;
        ImGuiKey g_liveKey = ImGuiKey_None;

        // 주입된 포인터. 큐가 아니라 **고정 상태**다 — 이유는 헤더에 있다.
        bool g_pointerActive = false;
        float g_pointerX = 0.f;
        float g_pointerY = 0.f;
        bool g_pointerDown = false;

        void remember(std::vector<std::string>& names, const char* name)
        {
            if (nullptr == name) return;
            if (std::find(names.begin(), names.end(), name) != names.end()) return;
            if (names.size() >= kTrackedNames) return;
            names.emplace_back(name);
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

        ImGuiKey key_from_name(const std::string& name) noexcept
        {
            if ("tab" == name)    return ImGuiKey_Tab;
            if ("up" == name)     return ImGuiKey_UpArrow;
            if ("down" == name)   return ImGuiKey_DownArrow;
            if ("left" == name)   return ImGuiKey_LeftArrow;
            if ("right" == name)  return ImGuiKey_RightArrow;
            if ("enter" == name)  return ImGuiKey_Enter;
            if ("space" == name)  return ImGuiKey_Space;
            if ("escape" == name) return ImGuiKey_Escape;
            return ImGuiKey_None;
        }
    }

    void announce_item(const char* widget, ImGuiID id, bool interactive, bool delegated)
    {
        if (nullptr == widget || 0 == id) return;
        g_frameItems.push_back(announced_item{ id, widget, interactive, delegated });
    }

    void draw_cursor(const ImRect& bounds, ImGuiID id, const char* widget)
    {
        ImGui::RenderNavCursor(bounds, id);
        if (nullptr == widget || 0 == id) return;
        g_frameCursors.push_back(announced_item{ id, widget, true });
    }

    void observe_frame()
    {
        ImGuiContext* const context = ImGui::GetCurrentContext();
        if (nullptr == context)
        {
            g_frameItems.clear();
            g_frameCursors.clear();
            return;
        }

        const ImGuiID navId = context->NavId;

        const char* navWidget = nullptr;
        bool navInteractive = true;
        bool navDelegated = false;
        for (const announced_item& item : g_frameItems)
        {
            if (item.id != navId) continue;
            navWidget = item.widget;
            navInteractive = item.interactive;
            navDelegated = item.delegated;
            break;
        }

        bool cursorDrawn = false;
        for (const announced_item& cursor : g_frameCursors)
        {
            if (cursor.id != navId) continue;
            cursorDrawn = true;
            break;
        }

        const ImGuiIO& io = ImGui::GetIO();

        std::lock_guard lock(g_mutex);
        ++g_published.frames;
        g_published.keyboardEnabled =
            0 != (io.ConfigFlags & ImGuiConfigFlags_NavEnableKeyboard);
        g_published.cursorVisible = context->NavCursorVisible;
        g_published.navIdIsAlive = context->NavIdIsAlive;
        g_published.navId = static_cast<std::uint32_t>(navId);
        g_published.activeId = static_cast<std::uint32_t>(context->ActiveId);
        g_published.navWindow = (nullptr != context->NavWindow)
            ? std::string(context->NavWindow->Name) : std::string();
        g_published.navWidget = (nullptr != navWidget) ? std::string(navWidget) : std::string();
        g_published.announced += g_frameItems.size();
        g_published.cursorsDrawn += g_frameCursors.size();
        g_published.navCursorColor = pack_rgba(context->Style.Colors[ImGuiCol_NavCursor]);

        if (nullptr != navWidget)
        {
            remember(g_published.visitedWidgets, navWidget);

            // ★ 마우스를 막 쓴 뒤에는 ImGui 가 커서를 감춘다. 그 프레임에
            //   안 그리는 것은 **옳은** 동작이라 세지 않는다 — 여기를 안 가르면
            //   판정이 마우스 유무를 재게 되고, 그러면 사람이 창을 클릭했다는
            //   이유로 붉어진다.
            if (navDelegated)
            {
                // 표준 위젯이 이 칸을 통째로 그리는 프레임이다. 빼되 센다.
                ++g_published.delegatedFrames;
            }
            else if (context->NavCursorVisible && !cursorDrawn)
            {
                ++g_published.silentFrames;
                remember(g_published.silentWidgets, navWidget);
            }

            if (!navInteractive)
            {
                ++g_published.disabledFrames;
                remember(g_published.disabledWidgets, navWidget);
            }
        }

        g_frameItems.clear();
        g_frameCursors.clear();
    }

    void deliver_pending_key()
    {
        ImGuiIO& io = ImGui::GetIO();
        std::lock_guard lock(g_mutex);

        // 누른 프레임과 뗀 프레임을 가른다. 한 프레임에 둘을 같이 넣으면
        // `IsKeyPressed` 가 보는 전이가 프레임 경계 안에서 상쇄되어, nav 가
        // 움직이지 않는데 키는 소비된 것처럼 보인다.
        if (ImGuiKey_None != g_liveKey)
        {
            io.AddKeyEvent(g_liveKey, false);
            g_liveKey = ImGuiKey_None;
            ++g_published.keysDelivered;
        }
        else if (!g_pendingKeys.empty())
        {
            g_liveKey = g_pendingKeys.front();
            g_pendingKeys.pop_front();
            io.AddKeyEvent(g_liveKey, true);
        }

        g_published.keysPending = static_cast<std::uint64_t>(g_pendingKeys.size()) +
            ((ImGuiKey_None != g_liveKey) ? 1u : 0u);
    }

    void request_pointer(pointer_action action, float x, float y)
    {
        std::lock_guard lock(g_mutex);
        g_pointerActive = true;
        switch (action)
        {
        case pointer_action::move:    g_pointerX = x; g_pointerY = y; break;
        case pointer_action::press:   g_pointerDown = true;  break;
        case pointer_action::release: g_pointerDown = false; break;
        }
    }

    void apply_injected_pointer()
    {
        std::lock_guard lock(g_mutex);
        if (!g_pointerActive) return;

        // `NewFrame` 이 이벤트를 다 푼 **뒤**다. 여기서 덮으면 이번 프레임의
        // 위젯이 전부 이 좌표를 본다. 누름은 `io.MouseDown` 만 세우면 되는데,
        // 다음 프레임의 `UpdateMouseInputs` 가 "직전에 안 눌려 있었다" 를 보고
        // `MouseClicked` 를 스스로 세우기 때문이다 — 전이는 그때 생긴다.
        ImGuiIO& io = ImGui::GetIO();
        io.MousePos = ImVec2(g_pointerX, g_pointerY);
        io.MouseDown[0] = g_pointerDown;
    }

    bool request_keys(const std::vector<std::string>& keys, std::string& outError)
    {
        outError.clear();
        if (keys.empty())
        {
            outError = "키를 하나도 주지 않았다";
            return false;
        }

        std::vector<ImGuiKey> resolved;
        resolved.reserve(keys.size());
        for (const std::string& name : keys)
        {
            const ImGuiKey key = key_from_name(name);
            if (ImGuiKey_None == key)
            {
                // ★ 하나라도 모르면 **아무것도** 넣지 않는다. 절반만 들어가면
                //   그 다음 판정이 무엇을 잰 것인지 알 수 없다.
                outError = "모르는 키: " + name;
                return false;
            }
            resolved.push_back(key);
        }

        std::lock_guard lock(g_mutex);
        for (const ImGuiKey key : resolved) g_pendingKeys.push_back(key);
        g_published.keysPending = static_cast<std::uint64_t>(g_pendingKeys.size()) +
            ((ImGuiKey_None != g_liveKey) ? 1u : 0u);
        return true;
    }

    contract_view read()
    {
        std::lock_guard lock(g_mutex);
        return g_published;
    }

    void reset_counts()
    {
        std::lock_guard lock(g_mutex);
        g_published.frames = 0;
        g_published.announced = 0;
        g_published.cursorsDrawn = 0;
        g_published.silentFrames = 0;
        g_published.silentWidgets.clear();
        g_published.delegatedFrames = 0;
        g_published.disabledFrames = 0;
        g_published.disabledWidgets.clear();
        g_published.visitedWidgets.clear();
        g_published.keysDelivered = 0;
    }
}
