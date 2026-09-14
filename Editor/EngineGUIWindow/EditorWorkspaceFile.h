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
    inline constexpr int schema_version = 2;
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
    std::string encode(const document& value);
    document decode(std::string_view bytes);
    std::string read_file(const std::filesystem::path& path);
    void atomic_write(const std::filesystem::path& path, std::string_view bytes);
    std::filesystem::path backup(const std::filesystem::path& path, std::string_view reason);
}
