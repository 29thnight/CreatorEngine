#pragma once
// 창 본문 보관소 (PHASE 21 M4 2·3단계 → W3 · 계획서 부록 B.3).
//
// 계획서가 정한 대로 **이관은 프레임만 옮긴다.** 본문 코드는 창 클래스가 있던
// 자리에 그대로 두고 `Begin`과 `End`만 셸이 가져간다. 그 사이를 잇는 것이 여기다 —
// 본문의 주인이 자기 본문을 이름으로 걸고, 선언의 진입점이 그것을 부른다.
//
// ── 왜 진입점을 창마다 한 쌍씩 적는가 ─────────────────────────────────────
//
// 셸이 부르는 것은 함수 포인터다. 안정 식별자를 비타입 템플릿 인자로 넘길 수
// 없어서(문자열은 구조적 타입이 아니다) `draw_*`/`has_*` 한 쌍을 창마다 적는다.
// 부록 A가 상단 저장소를 둘로 나눌 수밖에 없었던 것과 같은 제약이다.
//
// ── std::function 이 여기 남는 이유 ───────────────────────────────────────
//
// 옛 `ContextRegister`가 이미 쓰던 그대로다. 이관이 타입소거를 새로 들이지
// 않는다는 뜻이고, 본문을 캡처 없는 함수로 분해하는 것은 별건이다. 표(registry)
// 쪽은 함수 포인터만 들므로 CT6-a가 걷어낸 타입소거가 되살아나지 않는다.
//
// ── W3: 바인딩의 수명을 타입이 든다 ───────────────────────────────────────
//
// 걸린 본문은 거의 모두 `this` 를 캡처한다. 그러므로 그 `this` 보다 오래 살면
// 안 된다. 이 규약을 주석으로만 적어 두었더니 지켜지지 않았다 — 실측하면
// `MenuBarWindow` 는 본문 **열 개**를 걸고 하나도 풀지 않았고 소멸자조차 없다.
// 그래서 걸기의 반환값을 `[[nodiscard]]` 핸들로 바꾼다. 핸들을 들지 않으면
// 컴파일이 서지 않고, 핸들이 죽으면 바인딩도 죽는다. 게이트가 아니라 컴파일러가
// 강제하므로 "도는 세트에 없으면 없는 것" 의 함정에 걸리지 않는다.
//
// 풀어 주는 자유 함수(`unbind_window_body`)는 없앴다. 남겨 두면 핸들이 살아
// 있는 이름을 밖에서 풀 수 있고, 그 뒤 누가 같은 이름을 다시 걸면 핸들의
// 소멸자가 **남의 바인딩을 지운다.** 같은 이유로 핸들은 이름만이 아니라 표와
// 토큰을 함께 들고, 토큰이 어긋나면 아무것도 지우지 않는다.

#include <cstdint>
#include <functional>
#include <string_view>
#include <utility>
#include <vector>

namespace editor::windows
{
    /// 바인딩 하나. 토큰은 같은 이름을 다시 걸 때마다 새로 발급된다.
    struct body_binding_record
    {
        std::string_view      stable_id{};
        std::function<void()> body{};
        std::uint64_t         token{ 0 };
    };

    /// 본문 보관소. 창 표(`editor::window_table`)와 같은 모양으로 둔 이유는
    /// 같다 — 검사가 제품 저장소를 치웠다 되돌리면 **CLI 가 도는 게임 스레드**와
    /// **PresentationThread** 사이에 잠금 없는 경합이 생긴다. 검사는 지역 표를
    /// 쓰고 제품은 `process_window_bodies()` 하나를 쓴다.
    struct body_table
    {
        std::vector<body_binding_record> entries;
    };

    /// 제품 본문 보관소. 진입점 매크로가 읽는 것이 이것이다.
    body_table& process_window_bodies();

    class window_body_binding;

    /// 본문을 이름에 건다. 반환된 핸들이 그 바인딩의 수명이다.
    [[nodiscard]] window_body_binding bind_window_body(
        body_table& table, std::string_view stable_id, std::function<void()> body);

    /// 제품 보관소에 거는 짧은 표기.
    [[nodiscard]] window_body_binding bind_window_body(
        std::string_view stable_id, std::function<void()> body);

    /// 바인딩 하나의 수명을 드는 핸들. 이동만 되고 복사는 되지 않는다.
    class window_body_binding
    {
    public:
        window_body_binding() noexcept = default;
        ~window_body_binding();

        window_body_binding(const window_body_binding&) = delete;
        window_body_binding& operator=(const window_body_binding&) = delete;

        window_body_binding(window_body_binding&& other) noexcept;
        window_body_binding& operator=(window_body_binding&& other) noexcept;

        /// 지금 이 핸들이 바인딩을 들고 있는가. 이동하고 난 쪽은 거짓이다.
        [[nodiscard]] bool bound() const noexcept { return nullptr != m_table; }

        /// 들고 있는 이름. 들고 있지 않으면 빈 것이다.
        [[nodiscard]] std::string_view stable_id() const noexcept { return m_stableId; }

        /// 지금 풀어 버린다. 주인이 `Shutdown()` 으로 수명을 끊는 싱글톤에
        /// 필요하다 — 객체가 남은 채 본문만 내려야 하는 자리가 있다.
        void reset() noexcept;

    private:
        friend window_body_binding bind_window_body(
            body_table&, std::string_view, std::function<void()>);

        window_body_binding(body_table& table, std::string_view stable_id,
                            std::uint64_t token) noexcept
            : m_table(&table), m_stableId(stable_id), m_token(token)
        {
        }

        body_table*      m_table{ nullptr };
        std::string_view m_stableId{};
        std::uint64_t    m_token{ 0 };
    };

    /// 지금 걸려 있는 이름 전부. 건 순서 그대로다.
    ///
    /// 고아 검사가 이것을 읽는다 — 표에 없는 이름에 본문이 걸려 있으면 그
    /// 본문은 영영 불리지 않는다. 오타 하나가 조용히 죽은 창을 만들던 옛
    /// `GetContext` 결함의 반대편이고, 그래서 반대 방향도 봐야 한다.
    std::vector<std::string_view> bound_window_bodies(const body_table& table);
    std::vector<std::string_view> bound_window_bodies();

    namespace detail
    {
        /// 걸린 본문을 부른다. 걸리지 않았으면 아무 일도 하지 않는다.
        void run_window_body(body_table& table, std::string_view stable_id);
        void run_window_body(std::string_view stable_id);

        /// 본문이 걸려 있는가. 선언의 `available` 술어가 이것을 답한다 —
        /// 본문의 주인이 아직 없거나 이미 사라졌으면 프레임을 열지 않는다.
        bool window_body_bound(const body_table& table, std::string_view stable_id);
        bool window_body_bound(std::string_view stable_id);
    }
}

/// 진입점 한 쌍을 정의한다. 헤더에는 두 줄을 손으로 적고(grep 가능해야 한다)
/// 정의만 이 매크로가 편다.
#define EDITOR_DEFINE_WINDOW_ENTRY(suffix, name_expr)                        \
    void draw_##suffix() { ::editor::windows::detail::run_window_body(name_expr); } \
    bool has_##suffix()  { return ::editor::windows::detail::window_body_bound(name_expr); }
