#pragma once

// PHASE 21 W7-1 — Content Browser 의 디렉터리 스냅샷.
//
// 고치는 것. 브라우저는 **프레임마다 파일시스템을 훑고 있었다.**
// `ShowDirectoryTree` 가 열린 폴더 하나마다 `directory_iterator` 를 돌리고 재귀
// 하고, `ShowCurrentDirectoryFiles` 가 또 한 번 훑은 뒤 항목마다 `is_directory`
// (stat)를 부르며 정렬까지 했다. 캐시가 하나도 없었다.
//
// 실측(Release · 창 2400x1400 · docs/analysis/EditorPanelCostBaselineW7.md):
// 폴더 트리가 씬과 무관하게 **매 프레임 스캔 24 회**, 엔티티 1,000 에서
// Hierarchy 의 5.8 배(4.60 vs 0.80 ms)였다.
//
// ── 무효화를 어떻게 다루는가 ──────────────────────────────────────────────
//
// 계획서 §8.3 은 "invalidation 근거가 없으면 매 frame 전체 cache를 믿지 말고
// fail-safe rebuild한다" 고 적었다. 여기서는 근거 셋을 쓴다.
//
//   ① **자기 변경** — 이 창이 폴더를 만들거나 지우면 `invalidate()` 를 부른다.
//      즉시 반영돼야 하는 유일한 경우다(사람이 방금 한 일이다).
//   ② **나이** — 스캔한 지 `kRevalidateMs` 가 지난 목록은 낡은 것으로 본다.
//      밖에서 파일이 바뀌는 것을 이 경로가 알 방법이 지금은 없다.
//   ③ **프레임 예산** — 낡았다고 한 프레임에 전부 다시 훑지 않는다. 프레임당
//      `kRescanBudget` 개만 훑는다. 그러지 않으면 초당 몇 번씩 24 회 스캔이
//      몰려 p95 가 오히려 나빠진다 — 비용을 줄이려다 스파이크를 만드는 꼴이다.
//
// 그래서 정상 상태의 비용은 **프레임당 스캔 1 회 이하**이고, 폴더 24 개면 각
// 폴더가 1 초에 한 번쯤 갱신된다(200fps 에서 프레임당 0.12 회). 감시자
// (`EditorDirectoryWatcher`)를 붙이면 ②가 필요 없어져 idle 스캔이 0 이 되는데,
// 그것은 이 조각의 범위 밖이다 — 여기서는 **재는 축이 먼저 서 있는지**가
// 중요하고, 그 축은 `editor.panelcost` 의 `scans` 다.
//
// 캐시는 정책을 모른다. 지원 확장자 · 검색 · 유형 필터 · 정렬은 창이 소유한다
// (W2-B 의 몫이다). 여기가 주는 것은 **디스크를 만지지 않고 얻는 목록**뿐이다.

#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace editor
{
    namespace browser_fs = std::filesystem;

    struct browser_directory_entry
    {
        browser_fs::path path{};
        std::string nameUtf8{};   ///< 표시용 파일 이름
        std::string pathUtf8{};   ///< 안정 ID·tooltip 용 전체 경로
        std::string extension{};  ///< 소문자 아님 — 원문 그대로(지원 여부 판정은 창이 한다)
        bool isDirectory{};       ///< 여기 담아 두면 정렬 비교마다 stat 하지 않는다
        bool isSymlink{};         ///< 폴더 트리는 심볼릭 링크를 타지 않는다(옛 동작 보존)
    };

    struct browser_directory_listing
    {
        std::vector<browser_directory_entry> entries{};
        std::error_code error{};
        bool valid{};
    };

    struct browser_cache_stats
    {
        std::uint64_t scans{};      ///< 실제로 디스크를 훑은 횟수(누계)
        std::uint64_t hits{};       ///< 캐시로 답한 횟수(누계)
        std::uint64_t evictions{};  ///< 무효화로 버린 목록 수(누계)
        std::size_t   cached{};     ///< 지금 들고 있는 폴더 수
        /// W7-5: **목록 스캔 밖에서** 디스크를 만진 횟수(누계). 아래 주석 참조.
        std::uint64_t probes{};
    };

    /// 프레임 머리에서 한 번. 재스캔 예산을 되돌린다.
    void browser_cache_begin_frame();

    /// 폴더 하나의 목록. 캐시에 있고 낡지 않았으면 **디스크를 만지지 않는다.**
    const browser_directory_listing& browser_cache_listing(const browser_fs::path& directory);

    /// 이 창이 만든 변경 뒤에 부른다(새 폴더·삭제·이름 변경·자산 생성).
    /// 다음 프레임은 예산을 무시하고 필요한 만큼 다시 훑는다.
    void browser_cache_invalidate();

    browser_cache_stats browser_cache_get_stats();

    // ══ PHASE 21 W7-5 — 스캔 밖에서 디스크를 만지는 자리 ═══════════════════
    //
    // W7-1 이 스캔을 24 → 0 으로 없앴는데도 `browser_tree` 가 avg 1.375 ms 였다
    // (실측 2026-09-15). **`scans` 는 0 이었다.** 세는 자가 틀린 단위를 세고
    // 있었기 때문이다 — 계약은 *"브라우저가 프레임마다 디스크를 만지지 않는다"*
    // 인데 계수기는 *"디렉터리를 훑은 횟수"* 만 셌다. 그 사이로
    // `std::filesystem::equivalent` 가 트리 노드마다 빠져나갔다(노드 24 × 경로
    // 2 = 프레임당 파일 핸들 48 회). Windows 에서 `equivalent` 는 두 경로를
    // **실제로 열어**(`CreateFile` + `GetFileInformationByHandle`) 파일 식별자를
    // 비교한다 — 목록 캐시를 경유할 수 없는 종류의 호출이다.
    //
    // 그래서 축을 하나 더 연다. `scans` 는 W7-1 의 뜻 그대로 두고(그 게이트가
    // 그 수에 걸려 있다) **`probes`** 가 "스캔이 아닌 디스크 접촉" 을 센다.
    // 판정은 idle 프레임에서 둘 다 0 이다.
    //
    // ★ 강제 단위를 맞춘 것이 핵심이다. 계약이 말하는 단위(디스크 접촉)로 세지
    //   않으면, 그 단위의 다른 모양이 계수기 옆으로 지나가도 게이트는 초록이다.

    /// ★ 디스크를 만지는 통로는 아래 둘뿐이다(`browser_canonical` ·
    ///   `browser_directory_exists`). 둘 다 `probes` 를 올린다. 경로 비교는
    ///   **어휘로** 하고, 디스크 철자가 필요하면 미리 한 번 풀어서 들고 있어라.
    ///   `equivalent` 를 다시 들이지 마라 — 게이트가 브라우저 코드에서 그 호출이
    ///   0 인 것을 단정한다.

    /// 두 경로가 같은 폴더를 가리키는가 — **디스크를 만지지 않는다.**
    /// 어휘 정규화(`lexically_normal`)로 비교한다. 트리는 심볼릭 링크를 타지
    /// 않으므로(`browser_directory_entry::isSymlink` 참조) 여기서 필요한 것은
    /// `equivalent` 의 링크 의미론이 아니라 "이 폴더가 그 폴더인가" 하나다.
    bool browser_same_directory(const browser_fs::path& a, const browser_fs::path& b);

    /// 경로를 정규형으로 편다. **디스크를 만진다**(`probes` 가 오른다).
    /// 뿌리처럼 자주 바뀌지 않는 것에만 쓰고 **결과를 들고 있어라** — 매 프레임
    /// 부르면 그것이 곧 프레임당 파일시스템 호출이고, 이 조각이 없애려는 바로
    /// 그것이다.
    browser_fs::path browser_canonical(const browser_fs::path& path, std::error_code& ec);

    /// 폴더가 실재하는가. **디스크를 만진다.** 위와 같은 규약으로 쓴다.
    bool browser_directory_exists(const browser_fs::path& path);
}
