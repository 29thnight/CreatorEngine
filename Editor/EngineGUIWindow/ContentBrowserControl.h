#pragma once

// PHASE 21 W2-B — Content Browser 를 밖에서 읽고 모는 창구.
//
// 왜 있는가. 계획서 §W2-B 의 완료 판정은 *"같은 프레임의 현재 위치·검색 범위·
// 이력 버튼 상태·결과 신원·선택"* 을 관측하고 *"이력 분기 누락·동명 payload"*
// 를 변이로 주입해 실패를 확인하라고 적었다. 착수 시점에 그 값을 밖으로 내는
// 명령이 **0 개**였고, 탐색을 일으킬 수단도 사람의 클릭뿐이었다. 그 상태의
// 게이트는 기본 위치를 두 번 읽고 초록이 된다.
//
// ── 스레드 ─────────────────────────────────────────────────────────────────
//
// 창은 PresentationThread 에서 그려지고 CLI 는 게임 스레드에서 돈다. 그래서
// `editor.viewport` 와 같은 모양을 쓴다 — 명령은 **요청함**에 넣고, 창이 프레임
// 머리에서 비우며, 프레임 끝에 **사본**을 게시한다. 요청을 넣은 직후의 읽기는
// 아직 옛 상태다(게이트는 `wait` 를 끼운다).
//
// 이 헤더는 ImGui 를 모른다. 명령 쪽이 이것만 include 한다.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace editor::windows
{
    /// 탐색 범위. 계획서 §W2-B 계약 5 — *"최근 항목과 전체 자산은 검색 범위다"*.
    enum class content_browser_scope : std::uint8_t
    {
        folder,      ///< 실제 폴더 하나
        recent,      ///< 열기·선택에 성공한 자산, 최근 순
        everything,  ///< 프로젝트 Assets 아래 전부
    };

    enum class content_browser_request_kind : std::uint8_t
    {
        go_folder,      ///< text = Assets 기준 상대 경로(빈 문자열이면 뿌리)
        go_recent,
        go_everything,
        back,
        forward,
        up,
        search,         ///< text = 검색어(빈 문자열이면 지운다)
        select,         ///< text = Assets 기준 상대 경로. **지금 보이는 결과** 안에 있어야 한다
        scroll,         ///< value = 목록의 세로 스크롤(px)
        create_folder,          ///< text = 이름. 지금 폴더 안에 만든다 — 대화상자의 Create 와 같은 길
        create_volume_profile,  ///< text = 이름(확장자 없이). VolumeProfile 폴더에서만
    };

    struct content_browser_request
    {
        content_browser_request_kind kind{};
        std::string text{};
        float value{};
    };

    /// W2-B1 — 트리·분할선·본문의 **같은 프레임** 배치. 좌표는 ImGui 화면 좌표(px),
    /// 선호 폭만 논리 px 이다. 분할선 끌기·배율 행렬·좁은 창 판정이 이 값을 본다.
    struct content_browser_layout
    {
        bool treeVisible{};
        float uiScale{};                   ///< `ThemePixels(1)` — 사용자 배율 × DPI
        float availableWidth{};            ///< 트리+분할선+본문이 나눠 쓰는 폭
        float preferredTreeWidth{};        ///< 저장되는 선호 폭(논리 px, 140~600)
        float appliedTreeWidth{};          ///< 이번 프레임에 준 폭 — 좁으면 본문 최소 폭에 잘린다
        float treeMinX{}, treeMaxX{};
        float splitterMinX{}, splitterMaxX{}, splitterMinY{}, splitterMaxY{};
        float bodyMinX{}, bodyMaxX{};
        bool splitterHovered{}, splitterActive{};
        float viewportX{}, viewportY{};    ///< 주 뷰포트 원점 — 창 클라이언트 (0,0) 의 화면 좌표
        /// 이 프레임에 ImGui 가 본 포인터. 운영체제 메시지가 백엔드를 지나 여기까지
        /// 닿았는지를 결과(폭)가 아니라 **경계 통과**로 판정하려고 낸다.
        float mouseX{}, mouseY{};
        bool mouseDown{};
    };

    struct content_browser_snapshot
    {
        std::uint64_t frames{};            ///< 창 본문이 돈 프레임 수(게시가 살아 있는가)
        std::uint64_t requestsApplied{};
        std::uint64_t requestsRejected{};
        std::string lastRejection{};

        content_browser_scope scope{};
        std::string directory{};           ///< Assets 기준 상대 경로, 뿌리는 "."
        std::string search{};
        std::string selected{};            ///< Assets 기준 상대 경로, 없으면 빈 문자열
        std::string error{};

        std::size_t historyIndex{};
        std::size_t historySize{};
        std::vector<std::string> history{}; ///< "folder:<상대 경로>" · "recent" · "everything"
        bool canBack{};
        bool canForward{};
        bool canUp{};
        bool canCreate{};                  ///< New 가 눌리는가 — 가상 위치에서는 거짓이어야 한다
        bool canCreateVolumeProfile{};     ///< 폴더 메뉴·타일 메뉴가 같은 술어로 연다

        std::size_t resultCount{};
        std::vector<std::string> results{}; ///< 보이는 결과의 상대 경로(앞에서 최대 256)
        float scrollY{};
        float scrollMaxY{};

        std::size_t everythingFolders{};   ///< 전체 순회가 목록을 확보한 폴더 수
        std::size_t everythingPending{};   ///< 아직 확보하지 못한 폴더 수
        bool everythingComplete{};
        std::size_t recentCount{};
        /// 결과를 **다시 모은** 횟수(누계). 목록·검색·범위가 그대로인 프레임에는 오르지
        /// 않아야 한다 — 전체 자산 4,274 개에서 매 프레임 모으고 정렬하던 비용이 3.25ms 였다.
        std::uint64_t resultRebuilds{};
        content_browser_layout layout{};
    };

    /// 요청을 넣는다. 함이 가득 차면(32) 거짓.
    bool request_content_browser(content_browser_request request);

    /// 마지막으로 게시된 사본. 창이 한 번도 안 그려졌으면 `frames == 0`.
    content_browser_snapshot read_content_browser();

    const char* content_browser_scope_name(content_browser_scope scope);
}
