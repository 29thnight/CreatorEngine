#include "../../Editor/EngineGUIWindow/EditorWorkspaceFile.h"
#include "../../Editor/EngineGUIWindow/EditorWindowNames.h"

// Exercises the production codec without a window or the Win32 atomic-save
// adapter. The native editor_workspace_probe retains the atomic-save checks.
int main(int argc, char** argv)
{
    using namespace editor::workspace;
    int checks = 0;
    const auto check = [&](bool passed, const char* contract)
    {
        if (!passed) throw std::runtime_error(contract);
        ++checks;
    };
    try
    {
        check(argc == 2, "pass the real imgui-ini fixture directory");
        ce::dynamic_array<alias> aliases;
        for (const auto& item : EditorWindowName::legacy_names)
        {
            ce::utf8_string old(utf8_view(item.old_name));
            ce::utf8_string stable(utf8_view(item.stable_id));
            aliases.push_back({old, stable, 0, 0});
            old.insert(0, "###");
            aliases.push_back({std::move(old), std::move(stable), 0, 0});
        }
        const std::filesystem::path fixtures(argv[1]);
        std::size_t fixtureCount = 0;
        for (const auto& entry : std::filesystem::directory_iterator(fixtures))
        {
            if (entry.path().extension() != ".ini") continue;
            const auto original = read_file(entry.path());
            const auto migrated = migrate_ini(original, aliases);
            ce::utf8_string error;
            const bool valid = validate_ini(migrated, error);
            const bool damaged = entry.path().filename().string().starts_with("damaged-");
            check(valid != damaged, "real fixture acceptance changed");
            check(migrated == migrate_ini(migrated, aliases), "migration is not idempotent");
            check(original == read_file(entry.path()), "migration modified its source");
            ++fixtureCount;
        }
        check(fixtureCount == 6, "all six legacy fixtures must be present");

        document value;
        value.ini = migrate_ini(read_file(fixtures / "current-debug.ini"), aliases);
        value.name.assign("\xed\x95\x9c\xea\xb8\x80 \xf0\x9f\x9a\x80 \"layout\" \\");
        value.monitors.assign("1920x1080+0+0;2560x1440+-2560+0");
        value.panels.emplace("###Editor.Inspector", false);
        value.panels.emplace("###Editor.Hierarchy", true);
        value.tree_width = 316.f;
        const auto encoded = encode(value);
        const auto loaded = decode(encoded);
        check(loaded.name == value.name, "UTF-8 name/quote/backslash roundtrip failed");
        check(loaded.ini == value.ini && loaded.monitors == value.monitors,
              "INI or monitor bytes changed during serialization");
        check(loaded.panels == value.panels && loaded.tree_width == 316.f,
              "panel state or layout geometry changed during serialization");
        check(encode(loaded) == encoded, "workspace serialization is not byte-stable");
        for (int schema : {1, 2})
        {
            std::string legacy = std_string(encoded);
            legacy.replace(0, std::string("CreatorWorkspace 3").size(),
                           "CreatorWorkspace " + std::to_string(schema));
            for (std::string_view field : {std::string_view("monitors "), std::string_view("preset ")})
            {
                if (schema == 2 && field == "preset ") continue;
                const auto begin = legacy.find("\n" + std::string(field));
                const auto end = legacy.find('\n', begin + 1);
                legacy.erase(begin, end - begin);
            }
            const auto restored = decode(utf8_view(legacy));
            check(restored.name == value.name && restored.ini == value.ini && restored.monitors.empty(),
                  "v1/v2 workspace compatibility changed");
            check(restored.preset == "sbox_compact", "legacy preset fallback changed");
        }

        const std::string padded = "###Editor.InspectorTAIL";
        const ce::utf8_string_view bounded(padded.data(), padded.size() - 4);
        check(!value.panels.at(bounded), "transparent panel lookup ignored view bounds");
        ce::string edit;
        check(bool(ce::to_utf16(value.name, edit)), "UTF-8 workspace name cannot populate UTF-16 editor");
        ce::utf8_string committed;
        check(bool(ce::to_utf8(edit.view(), committed)) && committed == value.name,
              "UTF-16 editing boundary changed workspace name bytes");

        ce::utf8_string error;
        for (const ce::utf8_string_view name : {ce::utf8_string_view("Layout"),
             ce::utf8_string_view("\xed\x95\x9c\xea\xb8\x80"), ce::utf8_string_view("\xf0\x9f\x9a\x80")})
            check(valid_name(name, error) && error.empty(), "valid Unicode name rejected");
        for (const ce::utf8_string_view name : {ce::utf8_string_view(""), ce::utf8_string_view(" CON"),
             ce::utf8_string_view("CON"), ce::utf8_string_view("cOm9"), ce::utf8_string_view("active"),
             ce::utf8_string_view("Layout."), ce::utf8_string_view("a/b"), ce::utf8_string_view("a\0b", 3),
             ce::utf8_string_view("\xc0\xaf", 2), ce::utf8_string_view("\xed\xa0\x80", 3),
             ce::utf8_string_view("\xf0\x9f\x9a", 3)})
            check(!valid_name(name, error) && !error.empty(), "invalid workspace name accepted");
        ce::utf8_string maximum;
        maximum.resize(64, 'a');
        check(valid_name(maximum, error), "64-byte name limit changed");
        maximum.push_back('a');
        check(!valid_name(maximum, error), "65-byte name accepted");

        const auto stripped = strip_viewport_positions("[Window][###Editor.Viewport]\r\n"
            "ViewportPos=-1920,0\r\nViewportId=0x42\r\nDockId=0x5\r\n");
        check(stripped == "[Window][###Editor.Viewport]\r\nDockId=0x5\r\n",
              "viewport migration changed surviving CRLF bytes");
        ce::dynamic_array<alias> selectedAliases;
        selectedAliases.push_back({ce::utf8_string("Old"), ce::utf8_string("###Editor.Test"), 0x12, 0x34});
        const auto selected = migrate_ini("[Window][Old]\nSize=20,20\n[Docking][Data]\nSelected=0x00000012\n",
                                          selectedAliases);
        check(selected.find("[Window][###Editor.Test]") != ce::utf8_string_view::npos &&
              selected.find("Selected=0x00000034") != ce::utf8_string_view::npos,
              "window identity or selected dock hash did not migrate");
        ce::utf8_string duplicate(value.ini);
        duplicate.append("\n[Window][Renamed###Editor.Inspector]\nSize=20,20\n");
        const auto deduped = migrate_ini(duplicate, aliases);
        const auto id = ce::utf8_string_view("[Window][###Editor.Inspector]");
        const auto first = deduped.find(id);
        check(first != ce::utf8_string_view::npos &&
              deduped.find(id, first + id.size()) == ce::utf8_string_view::npos,
              "relabelled window produced duplicate identity");
        check(validate_ini(deduped, error), "deduplicated workspace no longer validates");

        const auto rejects = [](ce::utf8_string_view bytes)
        {
            try { (void)decode(bytes); return false; }
            catch (const std::exception&) { return true; }
        };
        check(rejects(encoded.view().substr(0, 30)), "truncated workspace accepted");
        auto invalid = encoded;
        invalid.insert(0, "future ");
        check(rejects(invalid), "invalid schema accepted");
        invalid = encoded;
        const auto sizeAt = invalid.find("Size=");
        const auto sizeEnd = invalid.find("\n", sizeAt);
        invalid.erase(sizeAt, sizeEnd - sizeAt);
        invalid.insert(sizeAt, "Size=0,0");
        check(rejects(invalid), "invalid window geometry accepted");
        check(!validate_ini(ce::utf8_string_view("a\0b", 3), error), "embedded NUL layout accepted");
        // The view can be a non-null-terminated slice of a much larger file.
        ce::utf8_string withTail(encoded);
        withTail.append("THIS TAIL IS NOT PART OF THE DOCUMENT");
        check(encode(decode(withTail.view().substr(0, encoded.size()))) == encoded,
              "workspace decoder read beyond the supplied byte range");

        const auto temporary = std::filesystem::temp_directory_path() /
            ("creator-workspace-string-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        check(std::filesystem::create_directory(temporary), "cannot create isolated I/O test directory");
        struct cleanup
        {
            std::filesystem::path path;
            ~cleanup() { std::error_code ignored; std::filesystem::remove_all(path, ignored); }
        } cleanupFiles{temporary};
        const auto path = temporary / std::filesystem::path(u8"\ud55c\uae00.workspace");
        std::string binary(8195, 'x');
        binary[4095] = '\0';
        binary.replace(8189, 6, "\xed\x95\x9c\xea\xb8\x80");
        {
            std::ofstream stream(path, std::ios::binary);
            stream.write(binary.data(), static_cast<std::streamsize>(binary.size()));
            check(bool(stream), "cannot write binary I/O fixture");
        }
        check(read_file(path) == utf8_view(binary), "chunked file read lost Unicode or embedded NUL bytes");
        const auto firstBackup = backup(path, "before-test"), secondBackup = backup(path, "before-test");
        check(firstBackup != secondBackup && read_file(firstBackup) == utf8_view(binary) &&
              read_file(secondBackup) == utf8_view(binary), "backup names collided or changed bytes");
        const auto oversized = temporary / "oversized.workspace";
        { std::ofstream stream(oversized, std::ios::binary); stream.seekp(max_file_size); stream.put('x'); }
        bool sizeRejected = false;
        try { (void)read_file(oversized); }
        catch (const std::exception&) { sizeRejected = true; }
        check(sizeRejected, "workspace file size limit changed");

        std::printf("Workspace UTF-8 contracts PASS: %d checks, %zu real ini fixtures\n", checks, fixtureCount);
        return 0;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "Workspace UTF-8 contracts FAILED after %d checks: %s\n", checks, error.what());
        return 1;
    }
}
