#pragma once
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace editor::workspace
{
    inline constexpr int schema_version = 1;
    inline constexpr std::size_t max_file_size = 4 * 1024 * 1024;
    struct document
    {
        std::string name{ "S&Box Compact" };
        std::string viewport{ "###Editor.Scene" };
        int imgui_version{ 19280 }, theme_version{ 1 };
        float dpi{ 1.f }, width{ 1920.f }, height{ 1080.f }, tree_width{ 220.f };
        std::map<std::string, bool> panels;
        std::string ini;
    };
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
