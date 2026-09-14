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
    };

    /// 프레임 머리에서 한 번. 재스캔 예산을 되돌린다.
    void browser_cache_begin_frame();

    /// 폴더 하나의 목록. 캐시에 있고 낡지 않았으면 **디스크를 만지지 않는다.**
    const browser_directory_listing& browser_cache_listing(const browser_fs::path& directory);

    /// 이 창이 만든 변경 뒤에 부른다(새 폴더·삭제·이름 변경·자산 생성).
    /// 다음 프레임은 예산을 무시하고 필요한 만큼 다시 훑는다.
    void browser_cache_invalidate();

    browser_cache_stats browser_cache_get_stats();
}
