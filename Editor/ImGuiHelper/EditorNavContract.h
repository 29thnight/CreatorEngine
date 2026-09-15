#pragma once
// PHASE 21 W2-1 — 키보드 탐색 계약을 **잴 수 있게** 만든다.
//
// 계획서 §7.1 의 판정문은 이것이다:
//
//   *"custom widget은 ImGui ID, nav, focus, disabled, clipping, tooltip,
//     testability를 보존해야 하며 별도 input framework를 만들지 않는다."*
//
// 그런데 그 문장의 어느 절도 **밖에서 읽을 수단이 없었다.** nav 상태를 내보내는
// 표면이 0 이고, 키를 주입할 표면도 0 이다. 그 위에서 "키보드 탐색이 유지된다"
// 를 말하면 빈 집합을 성공으로 읽는 것이다 — W8-3 에서 검증 레이어가 같은 모양
// 이었다(레이어는 켜 놓고 아무도 큐를 읽지 않았다).
//
// ── 무엇이 실제로 틀려 있었나 ─────────────────────────────────────────────
//
// `ImGui::RenderNavCursor` 는 위젯이 **명시적으로** 불러야 한다. `ItemAdd` 가
// 대신 그려 주지 않는다(표준 `ButtonEx` 도 `RenderNavCursor` → `RenderFrame`
// 순서로 직접 부른다). 그런데 이 저장소의 custom widget 넷 중
// `EditorPropertyRow` 하나만 그것을 불렀다. 나머지는 `ItemAdd` + `ButtonBehavior`
// 를 하므로 **키보드로 닿고 Enter 로 눌리기까지 하는데 어디에 서 있는지 보이지
// 않았다.** 테마는 `ImGuiCol_NavCursor` 를 `Primary` 로 이미 정해 두었다 —
// 색은 있고 그리는 곳이 없었다.
//
// ── 왜 장부인가 ───────────────────────────────────────────────────────────
//
// "커서를 그렸는가" 는 소스에서도 셀 수 있다. 그러나 소스 대조만으로는
// **nav 가 실제로 그 위젯에 닿는지** 모른다 — 부르는 줄이 있어도 조건이 틀려
// 한 번도 실행되지 않을 수 있고, 그것이 이 저장소가 여러 번 겪은 죽은 분기다
// (`EditorSectionHeader` 의 hover 색이 그랬다). 그래서 런타임으로 잰다.
//
//   ① 위젯이 `ItemAdd` 성공 직후 `announce_item` 으로 자기 아이템을 신고한다.
//   ② nav 커서는 `draw_cursor` 로만 그린다 — 그린 사실이 장부에 남는다.
//   ③ 프레임 끝에 `observe_frame` 이 ImGui 의 `NavId` 를 읽어 ①과 맞댄다.
//      nav 가 신고된 아이템 위에 섰는데 ②가 그 id 로 불리지 않았으면 **그
//      프레임을 센다.** 그 수가 판정이다.
//
// 이러면 "부르는 줄이 있다" 가 아니라 "실제로 그 상황에서 그렸다" 를 잰다.
//
// ── 자극이 없으면 0 은 아무 뜻이 없다 ────────────────────────────────────
//
// nav 가 custom widget 위에 한 번도 서지 않으면 위 수는 당연히 0 이다. 그래서
// **키 주입**(`request_keys`)이 같은 장부에 있다. CLI 가 Tab·화살표를 예약하고
// `deliver_pending_key` 가 `io.AddKeyEvent` 로 넣는다. 그 호출은 프레임 **안**에서
// 해도 된다 — 이벤트는 `g.InputEventsQueue` 에 쌓였다가 다음 `NewFrame` 이 푼다.
// 그래서 관측과 같은 자리(프레임 끝)에 두고, 반영은 한 프레임 뒤다.
// 판정하는 쪽은 `visitedWidgets` 로 **자극이 닿았는지** 먼저 확인한다.
//
// ── 스레드 ────────────────────────────────────────────────────────────────
//
// ①②③은 전부 ImGui(Presentation) 스레드다. 그 셋이 공유하는 프레임 버퍼는
// 잠금이 없다. `request_keys`·`read`·`reset_counts` 는 게임 스레드의 CLI 가
// 부르므로 게시본만 잠금으로 가른다 — `EditorWorkspaceStore` 의 mailbox 와 같은
// 규약이고, 명령은 예약하고 게시된 값을 읽을 뿐 ImGui 를 만지지 않는다.

// ★ 이 헤더는 **ImGui 를 끌어오지 않는다.** CLI(게임 스레드)가 장부를 읽어야
//   하는데, 명령 TU 에 `imgui_internal.h` 가 딸려 들어오면 "명령은 ImGui 를
//   만지지 않는다" 는 규약이 헤더 수준에서 무너진다. 위젯 쪽 인자 둘만 전방
//   선언으로 받는다.
struct ImRect;
typedef unsigned int ImGuiID;

#include <cstdint>
#include <string>
#include <vector>

namespace editor::nav
{
    /// 이름을 담아 두는 자리의 수. 한 번 어긋나면 프레임마다 같은 이름이
    /// 쌓이므로 전부 담으면 장부가 로그가 된다(W8-3 의 `retained` 와 같은 이유).
    inline constexpr std::size_t kTrackedNames = 16;

    struct contract_view
    {
        /// 키보드 탐색이 **실제로** 켜져 있는가. 이것이 거짓이면 아래 수가 전부
        /// 0 인 것은 계약이 지켜졌다는 뜻이 아니라 축이 없다는 뜻이다.
        bool keyboardEnabled{ false };

        /// ImGui 가 지금 nav 커서를 보일 상태인가. 마우스를 쓰면 거짓이 된다 —
        /// 그 프레임에 커서를 안 그리는 것은 **옳은** 동작이라 판정에서 뺀다.
        bool cursorVisible{ false };

        /// 이번 프레임에 nav 아이템을 실제로 봤는가.
        bool navIdIsAlive{ false };

        std::uint32_t navId{ 0 };
        std::uint32_t activeId{ 0 };
        std::string navWindow;   ///< nav 가 선 창의 ImGui 이름
        std::string navWidget;   ///< 그 아이템을 신고한 custom widget(없으면 빈 문자열)

        std::uint64_t frames{ 0 };        ///< 장부가 관측한 프레임 수
        std::uint64_t announced{ 0 };     ///< custom widget 아이템 신고 총수
        std::uint64_t cursorsDrawn{ 0 };  ///< `draw_cursor` 호출 총수

        /// ★ 판정 ① — nav 가 custom widget 위에 섰고 커서를 보여야 하는데
        ///   그 프레임에 아무도 커서를 그리지 않은 횟수. 0 이어야 한다.
        std::uint64_t silentFrames{ 0 };
        std::vector<std::string> silentWidgets;

        /// nav 가 선 아이템의 그리기를 **표준 위젯에 넘긴** 프레임 수.
        ///
        /// Tab 으로 `ImGuiItemFlags_Inputable` 아이템에 들어가면 ImGui 가
        /// `ImGuiActivateFlags_PreferInput` 으로 편집 모드에 넣고, 그때부터
        /// `TempInputScalar`(= `InputTextEx`)가 그 칸을 통째로 그린다 — nav
        /// 커서도 그쪽 몫이다. 그 프레임에 custom widget 이 커서를 또 그리면
        /// 두 벌이 된다.
        ///
        /// ★ 판정에서 **빼되 수는 남긴다.** 이 수가 `frames` 에 가깝게 크면
        ///   판정 ①이 사실상 아무것도 안 보고 있는 것이다 — 면제가 대상을
        ///   통째로 비우는 것이 이 저장소가 겪은 눈먼 초록의 한 양식이다.
        std::uint64_t delegatedFrames{ 0 };

        /// ★ 판정 ② — nav 가 **손댈 수 없는** 아이템 위에 선 횟수. 0 이어야 한다.
        ///   `ItemAdd` 에 `ImGuiItemFlags_Disabled` 를 주지 않으면 탐색이 죽은
        ///   버튼에 멈추고, 사용자는 눌러도 아무 일이 없는 자리에 갇힌다.
        std::uint64_t disabledFrames{ 0 };
        std::vector<std::string> disabledWidgets;

        /// nav 가 머문 custom widget 들. **자극이 닿았는지**를 이것으로 본다.
        std::vector<std::string> visitedWidgets;

        /// 테마가 정한 nav 커서 색(`0xRRGGBBAA`). 그리는 곳이 없으면 이 값이
        /// 옳아도 화면에는 아무것도 없다 — 실제로 그런 상태였다.
        std::uint32_t navCursorColor{ 0 };

        std::uint64_t keysPending{ 0 };    ///< 아직 넣지 않은 예약 키
        std::uint64_t keysDelivered{ 0 };  ///< io 에 넣고 뗀 키의 수
    };

    /// custom widget 이 이번 프레임의 **그리기 책임이 확정된 뒤** 부른다.
    /// `widget` 은 정적 리터럴이어야 한다(장부가 포인터만 든다).
    /// `interactive` 는 이 아이템이 이번 프레임에 실제로 눌릴 수 있는가다.
    /// `delegated` 는 이 프레임의 그리기를 표준 위젯에 넘겼다는 뜻이다 —
    /// 그러면 nav 커서도 그쪽 몫이라 판정 ①에서 뺀다.
    void announce_item(const char* widget, ImGuiID id, bool interactive,
        bool delegated = false);

    /// nav 커서를 그리는 **유일한 길.** `ImGui::RenderNavCursor` 를 custom widget
    /// 에서 직접 부르지 않는다 — 그러면 그린 사실이 장부에 남지 않아 판정 ①이
    /// 거짓으로 붉어진다. 소스 대조 게이트가 그 직접 호출을 막는다.
    void draw_cursor(const ImRect& bounds, ImGuiID id, const char* widget);

    /// 프레임 끝(크롬 스냅샷을 뜨는 자리)에서 부른다.
    void observe_frame();

    /// 프레임마다 한 번. 예약된 키 하나를 누르거나 뗀다(누름과 뗌은 서로 다른
    /// 프레임이다). 이벤트는 큐에 쌓여 **다음** `NewFrame` 에서 처리된다.
    void deliver_pending_key();

    /// CLI 가 키를 예약한다. 이름은 tab · up · down · left · right · enter ·
    /// space · escape. 하나라도 모르면 아무것도 넣지 않고 거짓을 돌려준다.
    bool request_keys(const std::vector<std::string>& keys, std::string& outError);

    contract_view read();

    /// 수만 비운다 — 켜짐 여부(`keyboardEnabled`)는 이 실행의 성질이다.
    void reset_counts();
}
