#pragma once
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace editor::workspace
{
    // 2 = W6 이 `preset` 을 더했다. **v1 도 계속 읽는다** — 쓰던 사람의 배치를
    // 버전 하나 올렸다고 버리면 "복구했습니다" 한 줄과 함께 배치가 사라진다.
    // v1 에는 preset 이 없으므로 기본 preset 으로 읽는다.
    //
    // 3 = 멀티뷰포트를 켜면서 `monitors` 를 더했다. 창을 메인 밖으로 꺼내면 ImGui 가
    // `ViewportPos` 를 적는데, 그것은 **절대 데스크톱 좌표**라 이 파일의 다른 값들과
    // 성질이 다르다 — `geometry` 는 창 크기라 어디서 열어도 뜻이 같지만 뷰포트 좌표는
    // 모니터 배치가 바뀌면 **없는 화면**을 가리킨다. 그리고 안 보이는 패널은 닫을
    // 창구가 없다. 그래서 어느 화면 구성에서 적힌 좌표인지를 같이 적고, 다른 구성에서
    // 열리면 좌표만 버린다(`strip_viewport_positions`). v1·v2 파일에는 이 줄이 없고,
    // 없는 것은 "모르는 환경" 이라 같은 길로 간다.
    inline constexpr int schema_version = 3;
    inline constexpr int oldest_readable_schema = 1;
    inline constexpr std::size_t max_file_size = 4 * 1024 * 1024;
    struct document
    {
        std::string name{ "S&Box Compact" };
        std::string preset{ "sbox_compact" };   ///< layout_preset::id
        std::string viewport{ "###Editor.Scene" };
        int imgui_version{ 19280 }, theme_version{ 1 };
        float dpi{ 1.f }, width{ 1920.f }, height{ 1080.f }, tree_width{ 220.f };
        std::map<std::string, bool> panels;
        /// 이 배치가 적힌 화면 구성. `<너비>x<높이>+<x>+<y>` 를 정렬해 `;` 로 이은 것
        /// (예: `1920x1080+2880+0;2880x1620+0+0`). 사람이 읽을 수 있는 형태로 두는 이유는
        /// 뷰포트 좌표를 **왜 버렸는지**가 파일만 보고 답해져야 하기 때문이다.
        /// 비어 있으면 모르는 환경이다 — 열거에 실패했거나 v3 이전 파일이다.
        std::string monitors;
        std::string ini;
    };
    /// 이름 붙인 workspace 의 **파일 이름은 그 이름 자체**다(PHASE 21 W6-2).
    ///
    /// 표시 이름과 파일 이름을 따로 두면 둘을 잇는 표가 생기고, 그 표가 파일과
    /// 어긋나는 순간 "있다고 적힌 배치를 못 여는" 상태가 된다. 대신 이름 쪽을
    /// 좁힌다 — 파일 이름으로 쓸 수 없는 것을 **만들 때** 거절한다.
    ///
    /// 거절하는 것: 빈 이름 · 64 자 초과 · 경로 구분자와 Windows 예약 문자
    /// (`<>:"/\|?*`) · 제어 문자 · 앞뒤의 공백이나 점(탐색기가 조용히 떼어 낸다)
    /// · 장치 이름(CON·PRN·AUX·NUL·COM1~9·LPT1~9, 대소문자 무관) · `active`
    /// (활성 파일과 같은 자리를 차지한다).
    ///
    /// 통과하면 `<이름>.workspace` 가 바로 그 파일이다.
    bool valid_name(std::string_view name, std::string& error);

    struct alias { std::string old_name, stable_id; std::uint32_t old_hash{}, new_hash{}; };
    // A migration never writes the input file. The caller backs it up before publication.
    std::string migrate_ini(std::string_view ini, const std::vector<alias>& aliases);
    bool validate_ini(std::string_view ini, std::string& error);

    /// `ViewportPos`/`ViewportId` 줄을 걷는다 — 창은 메인 뷰포트로 돌아온다.
    ///
    /// ImGui 는 창이 메인 밖에 있을 때만 그 둘을 적고(imgui.cpp `ViewportId !=
    /// IMGUI_VIEWPORT_DEFAULT_ID`), 그때 `Pos` 의 뜻이 **뷰포트 로컬**로 바뀐다.
    /// 그래서 이 둘만 걷어 내면 남은 `Pos`/`Size` 는 메인 창 기준으로 다시 읽히고,
    /// 도크 배치(`DockId`)는 손대지 않으므로 도킹된 패널은 아무 영향이 없다.
    std::string strip_viewport_positions(std::string_view ini);
    std::string encode(const document& value);
    document decode(std::string_view bytes);
    std::string read_file(const std::filesystem::path& path);
    void atomic_write(const std::filesystem::path& path, std::string_view bytes);
    std::filesystem::path backup(const std::filesystem::path& path, std::string_view reason);
}
