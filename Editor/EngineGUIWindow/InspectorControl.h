#pragma once

// PHASE 21 W2-I — Inspector 를 밖에서 읽고 폭을 정하는 창구.
//
// 왜 있는가. 계획서 §W2-I 의 완료 판정은 *"가용 content 폭 240/320/480/720 logical px
// 에서 가로 잘림·겹침·0-size 입력칸은 실패"* 와 *"미이관 표면 0"* 이다. 착수 시점에
// 인스펙터 폭을 정할 수단이 없었다 — 도크는 창을 좁혀도 절대 폭을 지키고, 도킹
// 저장본의 `SizeRef` 를 고치는 길은 값을 정확히 맞추지 못한다. 그리고 전용 드로어가
// 공통 배치를 지나는지는 소스 셈으로만 읽혔다(파일 단위 셈이 12 를 0 으로 읽은 적이
// 있다). 그래서 둘을 연다.
//
//   ① 폭: 요청한 논리 폭의 영역 안에서 본문을 그린다(끄면 창 그대로).
//   ② 본문마다: 공통 배치 줄(`begin_property_line`) 수와 오른쪽 끝, 넘침 px.
//   ③ 펼침: 접힘 머리로 시작하는 본문(Import Settings 의 맵·배열)을 모두 펼친다.
//      명령은 클릭할 수 없으므로, 켜지 않으면 안쪽 줄은 자극되지 않는다.
//   ④ 자극물(W2-I3): 리플렉션 경로의 모든 모양(중첩 구조체·배열·맵)을 담은 합성 값을
//      엔티티 본문 끝에 `ReflectionFixture` 로 그린다(`InspectorLayoutFixture.h`).
//
// ── 스레드 ─────────────────────────────────────────────────────────────────
//
// 창은 PresentationThread 에서 그려지고 CLI 는 게임 스레드에서 돈다. 브라우저
// 창구(`ContentBrowserControl.h`)와 같은 모양 — 폭은 원자 값, 사본은 프레임 끝에
// 게시한다. 요청 직후의 읽기는 아직 옛 상태다(게이트는 `wait` 를 끼운다).
//
// 이 헤더는 ImGui 를 모른다. 명령 쪽이 이것만 include 한다.

#include <cstdint>
#include <string>
#include <vector>

namespace editor::windows
{
    /// 이번 프레임에 그린 본문 하나. 좌표는 ImGui 화면 좌표(px).
    struct inspector_body
    {
        std::string type{};           ///< 컴포넌트 타입 이름(상단 셋은 드로어 이름)
        std::uint32_t instance{};     ///< 컴포넌트 인스턴스 ID. 상단 셋은 0
        bool open{};                  ///< 본문이 그려졌는가(접힌 패널은 거짓)
        bool enabledToggle{};         ///< 머리줄이 개별 활성 체크박스를 냈는가(W2-I1 정책)
        std::uint64_t propertyLines{};///< 이 본문이 지난 `begin_property_line` 수
        std::uint64_t fields{};       ///< 이 본문이 그린 값 위젯 수(W2-I5)
        float minLineValue{};         ///< 줄이 값에 준 폭의 최소(px)
        float minFieldWidth{};        ///< 값 위젯이 받은 폭의 최소(px)
        std::uint32_t fieldDigest{};  ///< 값 위젯 ID 를 그린 차례대로 섞은 값
        std::string narrowestField{}; ///< 가장 좁은 칸이 있던 줄 이름
        std::uint64_t axisFields{};   ///< 줄을 여럿이 나눠 쓰는 칸의 수
        float minAxisWidth{};         ///< 그 칸이 받은 폭의 최소(px)
        std::string narrowestAxis{};  ///< 그 칸이 있던 줄 이름
        /// 이 본문이 처음 그린 값 칸의 화면 사각형(px). 밖에서 그 칸을 끌 수 있다.
        float firstFieldX{}, firstFieldY{}, firstFieldW{}, firstFieldH{};
        float minX{}, maxX{};         ///< 본문 항목이 차지한 가로 범위
        float height{};
        float overflow{};             ///< 오른쪽 끝이 작업 영역을 넘은 px(0 이면 안 넘침)
    };

    struct inspector_snapshot
    {
        std::uint64_t frames{};       ///< 창 본문이 돈 프레임 수
        std::string entity{};         ///< 선택된 엔티티 이름, 없으면 빈 문자열
        float uiScale{};              ///< `ThemePixels(1)`
        float requestedWidth{};       ///< 요청한 논리 폭, 0 이면 창 그대로
        bool expandAll{};             ///< 접힘 머리를 모두 펼치고 있는가
        bool fixture{};               ///< 리플렉션 자극물을 그리고 있는가
        float contentWidth{};         ///< 본문이 실제로 받은 작업 영역 폭(px)
        float contentMaxX{};          ///< 작업 영역 오른쪽 끝(px)

        /// ★ 실제로 **보이는** 폭. 요청한 폭이 도크 패널보다 넓으면 본문은 그
        /// 폭으로 배치되지만 패널 밖은 잘려 화면에 없다 — 작업 영역만 읽으면
        /// 자가 "보이지 않는 배치" 를 재고도 초록이다(W2-I5 에서 실측: 요청
        /// 1080 px 중 505 px 만 보였다). 이 값이 `contentWidth` 보다 작으면
        /// 그만큼은 화면 밖이다.
        float visibleWidth{};

        // 최소 가독 폭(W2-I5). 값 칸이 이보다 좁으면 숫자를 읽을 수 없다.
        // 폰트와 배율에서 나오므로 검사가 리터럴 대신 이 값과 견준다.
        float valueMin{};             ///< 일반 값 칸의 하한
        float axisValueMin{};         ///< 축 값 칸의 하한(badge 제외)

        /// 지금 편집 중인 ImGui 아이템. 폭을 바꾸는 동안 이 값이 살아 있어야
        /// *"편집 중 ID/Undo 손실"* 이 없다고 말할 수 있다.
        std::uint32_t activeId{};
        std::vector<inspector_body> bodies{};
    };

    /// 본문 폭을 논리 px 로 정한다. 0 이하면 끈다.
    void set_inspector_width(float logicalWidth) noexcept;

    /// 접힘 머리를 모두 펼친다. 그리는 쪽은 `inspector_expand_all` 로 읽는다.
    void set_inspector_expand_all(bool expand) noexcept;
    bool inspector_expand_all() noexcept;

    /// 리플렉션 모양 자극물을 엔티티 본문 끝에 그린다.
    void set_inspector_fixture(bool enabled) noexcept;

    /// 마지막으로 게시된 사본. 창이 한 번도 안 그려졌으면 `frames == 0`.
    inspector_snapshot read_inspector();
}
