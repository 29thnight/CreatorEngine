#include "Paklib.hpp"

#include <Windows.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    namespace fs = std::filesystem;

    struct InputFile
    {
        std::string virtualPath;
        fs::path sourcePath;
    };

    struct WindowsPathLess
    {
        bool operator()(const std::wstring& lhs, const std::wstring& rhs) const
        {
            return CSTR_LESS_THAN == CompareStringOrdinal(
                lhs.data(), static_cast<int>(lhs.size()),
                rhs.data(), static_cast<int>(rhs.size()), TRUE);
        }
    };

    [[noreturn]] void Fail(const std::string& message)
    {
        throw std::runtime_error(message);
    }

    std::string ToUtf8(const fs::path& path)
    {
        const auto value = path.generic_u8string();
        return std::string(value.begin(), value.end());
    }

    bool StartsWithOrdinalIgnoreCase(std::wstring_view value, std::wstring_view prefix)
    {
        return value.size() >= prefix.size() && CSTR_EQUAL == CompareStringOrdinal(
            value.data(), static_cast<int>(prefix.size()),
            prefix.data(), static_cast<int>(prefix.size()), TRUE);
    }

    void RejectUnsafeWin32PathSyntax(const fs::path& path, const char* label)
    {
        const std::wstring raw = path.native();
        if (raw.empty()) Fail(std::string(label) + " path is empty");
        if (StartsWithOrdinalIgnoreCase(raw, LR"(\\?\)") ||
            StartsWithOrdinalIgnoreCase(raw, LR"(\\.\)") ||
            StartsWithOrdinalIgnoreCase(raw, LR"(\??\)"))
        {
            Fail(std::string(label) + " uses a device/extended path namespace: " +
                ToUtf8(path));
        }

        const fs::path absolute = fs::absolute(path).lexically_normal();
        const std::wstring absoluteText = absolute.native();
        if (StartsWithOrdinalIgnoreCase(absoluteText, LR"(\\)"))
        {
            Fail(std::string(label) + " must use a local DOS drive path: " +
                ToUtf8(path));
        }
        for (const fs::path& component : absolute.relative_path())
        {
            const std::wstring text = component.native();
            if (text.find(L':') != std::wstring::npos ||
                (!text.empty() && (text.back() == L' ' || text.back() == L'.')))
            {
                Fail(std::string(label) + " contains an unsafe Win32 path component: " +
                    ToUtf8(path));
            }
        }
    }

    void RequireNonReparsePath(const fs::path& path, const char* label)
    {
        const DWORD attributes = GetFileAttributesW(path.c_str());
        if (INVALID_FILE_ATTRIBUTES == attributes)
        {
            Fail(std::string(label) + " path is unavailable (Win32 " +
                std::to_string(GetLastError()) + "): " + ToUtf8(path));
        }
        if (0 != (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
        {
            Fail(std::string(label) + " reparse point is not allowed: " + ToUtf8(path));
        }
    }

    void RejectReparsePathIfExists(const fs::path& path, const char* label)
    {
        const DWORD attributes = GetFileAttributesW(path.c_str());
        if (INVALID_FILE_ATTRIBUTES == attributes)
        {
            const DWORD error = GetLastError();
            if (ERROR_FILE_NOT_FOUND == error || ERROR_PATH_NOT_FOUND == error) return;
            Fail(std::string("failed to inspect ") + label + " (Win32 " +
                std::to_string(error) + "): " + ToUtf8(path));
        }
        if (0 != (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
        {
            Fail(std::string(label) + " reparse point is not allowed: " + ToUtf8(path));
        }
    }

    void RejectReparseAncestors(const fs::path& path, const char* label)
    {
        fs::path current = fs::absolute(path).lexically_normal();
        while (!current.empty())
        {
            RejectReparsePathIfExists(current, label);
            const fs::path parent = current.parent_path();
            if (parent == current) break;
            current = parent;
        }
    }

    fs::path FinalExistingDirectoryPath(const fs::path& path, const char* label)
    {
        const HANDLE handle = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
            OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        if (INVALID_HANDLE_VALUE == handle)
        {
            Fail(std::string("failed to open ") + label + " for final-path resolution (Win32 " +
                std::to_string(GetLastError()) + "): " + ToUtf8(path));
        }

        struct HandleCloser
        {
            HANDLE value;
            ~HandleCloser() { if (INVALID_HANDLE_VALUE != value) CloseHandle(value); }
        } closer{ handle };

        std::wstring buffer(32768, L'\0');
        const DWORD length = GetFinalPathNameByHandleW(handle, buffer.data(),
            static_cast<DWORD>(buffer.size()), FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
        if (0 == length || length >= buffer.size())
        {
            Fail(std::string("failed to resolve final path for ") + label + " (Win32 " +
                std::to_string(GetLastError()) + "): " + ToUtf8(path));
        }
        buffer.resize(length);
        if (StartsWithOrdinalIgnoreCase(buffer, LR"(\\?\UNC\)"))
        {
            buffer = L"\\\\" + buffer.substr(8);
        }
        else if (StartsWithOrdinalIgnoreCase(buffer, LR"(\\?\)"))
        {
            buffer.erase(0, 4);
        }
        return fs::path(buffer).lexically_normal();
    }

    fs::path ResolveDirectoryIdentity(const fs::path& path, const char* label,
        bool mustExist)
    {
        RejectUnsafeWin32PathSyntax(path, label);
        fs::path probe = fs::absolute(path).lexically_normal();
        std::vector<fs::path> suffix;
        for (;;)
        {
            const DWORD attributes = GetFileAttributesW(probe.c_str());
            if (INVALID_FILE_ATTRIBUTES != attributes)
            {
                if (0 == (attributes & FILE_ATTRIBUTE_DIRECTORY))
                {
                    Fail(std::string(label) + " ancestor is not a directory: " +
                        ToUtf8(probe));
                }
                break;
            }

            const DWORD error = GetLastError();
            if (ERROR_FILE_NOT_FOUND != error && ERROR_PATH_NOT_FOUND != error)
            {
                Fail(std::string("failed to inspect ") + label + " (Win32 " +
                    std::to_string(error) + "): " + ToUtf8(probe));
            }
            const fs::path leaf = probe.filename();
            const fs::path parent = probe.parent_path();
            if (leaf.empty() || parent.empty() || parent == probe)
            {
                Fail(std::string(label) + " has no existing directory ancestor: " +
                    ToUtf8(path));
            }
            suffix.push_back(leaf);
            probe = parent;
        }
        if (mustExist && !suffix.empty())
        {
            Fail(std::string(label) + " directory is missing: " + ToUtf8(path));
        }

        fs::path result = FinalExistingDirectoryPath(probe, label);
        for (auto it = suffix.rbegin(); it != suffix.rend(); ++it) result /= *it;
        result = result.lexically_normal();
        RejectUnsafeWin32PathSyntax(result, label);
        return result;
    }

    bool PathHasPrefix(const fs::path& candidate, const fs::path& root)
    {
        std::wstring candidateText = candidate.lexically_normal().native();
        std::wstring rootText = root.lexically_normal().native();
        while (rootText.size() > 3 &&
            (rootText.back() == L'\\' || rootText.back() == L'/'))
        {
            rootText.pop_back();
        }
        if (candidateText.size() < rootText.size()) return false;
        if (CSTR_EQUAL != CompareStringOrdinal(candidateText.data(),
            static_cast<int>(rootText.size()), rootText.data(),
            static_cast<int>(rootText.size()), TRUE))
        {
            return false;
        }
        return candidateText.size() == rootText.size() ||
            (!rootText.empty() &&
                (rootText.back() == L'\\' || rootText.back() == L'/')) ||
            candidateText[rootText.size()] == L'\\' ||
            candidateText[rootText.size()] == L'/';
    }

    bool IsSameOrDescendant(const fs::path& candidate, const fs::path& root)
    {
        return PathHasPrefix(fs::absolute(candidate), fs::absolute(root));
    }

    // SerializationPlan D1(Y-4): 네이티브 스크립트 소스는 배포물에 실리지 않는다.
    //
    // ★ `.hlsl`/`.hlsli`는 여기 없고, 앞으로도 넣으면 안 된다. 현재 패키지는 셰이더
    //   **소스**와 Slang/DXC DLL을 싣고 Player가 런타임에 컴파일한다
    //   (BuildPipelinePlan B3). 셰이더를 "소스 파일"로 묶어 빼면 게임이 아무것도
    //   그리지 못한다 — 필터의 과잉이 누락보다 위험한 자리다.
    //
    // ★ `.meta`도 없다. D5 매니페스트가 Player의 GUID 해석을 대체하기 전까지
    //   sidecar가 필요하다(계획서 D1 본문의 별표).
    bool IsExcludedSourceExtension(const fs::path& path)
    {
        static const std::wstring_view kExcluded[] = { L".cpp", L".h", L".hpp" };

        std::wstring extension = path.extension().native();
        // 대소문자 무시 — `.CPP`로 저장된 파일이 필터를 지나가면 안 된다.
        for (wchar_t& c : extension)
        {
            if (c >= L'A' && c <= L'Z') c = static_cast<wchar_t>(c - L'A' + L'a');
        }

        for (const std::wstring_view candidate : kExcluded)
        {
            if (extension == candidate) return true;
        }
        return false;
    }

    void CollectFiles(const fs::path& root, const char* mountName,
        std::vector<InputFile>& files)
    {
        RejectReparseAncestors(root, "source ancestor");
        RequireNonReparsePath(root, "source root");
        std::error_code error{};
        if (!fs::is_directory(root, error) || error)
        {
            Fail("source directory not found: " + ToUtf8(root));
        }

        const fs::path normalizedRoot = fs::weakly_canonical(root, error);
        if (error) Fail("failed to resolve source directory: " + error.message());

        fs::recursive_directory_iterator it(
            normalizedRoot, fs::directory_options::none, error);
        const fs::recursive_directory_iterator end{};
        if (error) Fail("failed to enumerate source directory: " + error.message());

        for (; it != end; it.increment(error))
        {
            if (error) Fail("failed while enumerating source directory: " + error.message());

            const fs::directory_entry& entry = *it;
            RequireNonReparsePath(entry.path(), "source entry");
            const fs::file_status status = entry.symlink_status(error);
            if (error) Fail("failed to inspect source entry: " + error.message());
            if (fs::is_symlink(status))
            {
                Fail("symbolic links are not valid package inputs: " + ToUtf8(entry.path()));
            }
            if (!fs::is_regular_file(status)) continue;

            // D1: 제외는 reparse/symlink 거부 **뒤**에 온다. 순서를 바꾸면 악성 심볼릭
            // 링크가 `.cpp` 확장자를 달고 검사를 건너뛴다.
            if (IsExcludedSourceExtension(entry.path())) continue;

            const fs::path relative = fs::relative(entry.path(), normalizedRoot, error);
            if (error || relative.empty() || *relative.begin() == "..")
            {
                Fail("source entry escapes package root: " + ToUtf8(entry.path()));
            }

            const fs::path virtualPath = fs::u8path(mountName) / relative;
            files.push_back({ ToUtf8(virtualPath), entry.path() });
        }
        if (error) Fail("failed while enumerating source directory: " + error.message());
    }

    struct Arguments
    {
        fs::path assets;
        fs::path settings;
        fs::path output;
        // D1 게이트용. pak은 결정적이지 않다 — 같은 입력을 두 번 패킹해도 SHA-256이
        // 다르다. 그래서 바이트 비교로는 "무엇이 실렸는지"를 단정할 수 없고, 목록을
        // 내보내는 창구가 필요하다. entry 수만 세면 "빠져야 할 것이 들어가고 실려야
        // 할 것이 빠진" 같은 수의 pak을 통과시킨다.
        bool listEntries = false;
    };

    Arguments ParseArguments(int argc, wchar_t* argv[])
    {
        Arguments result{};
        for (int i = 1; i < argc; ++i)
        {
            const std::wstring_view arg{ argv[i] };
            if ((arg == L"--assets" || arg == L"--settings" || arg == L"--output") &&
                i + 1 >= argc)
            {
                Fail("missing value after command-line option");
            }

            if (arg == L"--assets") result.assets = argv[++i];
            else if (arg == L"--settings") result.settings = argv[++i];
            else if (arg == L"--output") result.output = argv[++i];
            else if (arg == L"--list-entries") result.listEntries = true;
            else if (arg == L"--help" || arg == L"-h")
            {
                std::wcout << L"AssetPacker --assets <dir> --settings <dir> --output <pak>\n";
                std::exit(0);
            }
            else
            {
                Fail("unknown command-line option: " + Pak::wide_to_utf8(arg));
            }
        }

        if (result.assets.empty() || result.settings.empty() || result.output.empty())
        {
            Fail("required: --assets <dir> --settings <dir> --output <pak>");
        }
        return result;
    }

    void PromoteCandidate(const fs::path& candidate, const fs::path& output)
    {
        if (!MoveFileExW(candidate.c_str(), output.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            Fail("failed to promote pak candidate (Win32 " +
                std::to_string(GetLastError()) + ")");
        }
    }
}

int wmain(int argc, wchar_t* argv[])
{
    fs::path candidate{};
    try
    {
        const Arguments args = ParseArguments(argc, argv);
        std::error_code error{};
        RejectUnsafeWin32PathSyntax(args.assets, "assets root");
        RejectUnsafeWin32PathSyntax(args.settings, "settings root");
        RejectUnsafeWin32PathSyntax(args.output, "output");
        const fs::path lexicalAssets = fs::absolute(args.assets).lexically_normal();
        const fs::path lexicalSettings = fs::absolute(args.settings).lexically_normal();
        const fs::path lexicalOutput = fs::absolute(args.output).lexically_normal();
        if (lexicalOutput.filename().empty()) Fail("output file name is empty");

        RequireNonReparsePath(lexicalAssets, "assets root");
        RequireNonReparsePath(lexicalSettings, "settings root");
        RejectReparseAncestors(lexicalAssets, "assets ancestor");
        RejectReparseAncestors(lexicalSettings, "settings ancestor");
        RejectReparseAncestors(lexicalOutput.parent_path(), "output ancestor");
        const fs::path assets = ResolveDirectoryIdentity(
            lexicalAssets, "assets root", true);
        const fs::path settings = ResolveDirectoryIdentity(
            lexicalSettings, "settings root", true);
        const fs::path outputParentBefore = ResolveDirectoryIdentity(
            lexicalOutput.parent_path(), "output parent", false);
        fs::path output = outputParentBefore / lexicalOutput.filename();
        if (IsSameOrDescendant(output, assets) || IsSameOrDescendant(output, settings))
        {
            Fail("output must be outside package input roots: " + ToUtf8(output));
        }
        RejectReparsePathIfExists(output, "output");
        fs::create_directories(output.parent_path(), error);
        if (error) Fail("failed to create output directory: " + error.message());
        RejectReparseAncestors(output.parent_path(), "output ancestor");
        const fs::path outputParentAfter = ResolveDirectoryIdentity(
            output.parent_path(), "output parent", true);
        if (!PathHasPrefix(outputParentAfter, outputParentBefore) ||
            !PathHasPrefix(outputParentBefore, outputParentAfter))
        {
            Fail("output parent identity changed during creation");
        }
        output = outputParentAfter / lexicalOutput.filename();
        if (IsSameOrDescendant(output, assets) || IsSameOrDescendant(output, settings))
        {
            Fail("output must be outside package input roots after final resolution: " +
                ToUtf8(output));
        }

        std::vector<InputFile> files;
        CollectFiles(assets, "Assets", files);
        CollectFiles(settings, "ProjectSetting", files);
        std::sort(files.begin(), files.end(), [](const InputFile& lhs, const InputFile& rhs)
        {
            return lhs.virtualPath < rhs.virtualPath;
        });

        std::set<std::string> uniquePaths;
        std::set<std::wstring, WindowsPathLess> uniqueWindowsPaths;
        for (const InputFile& file : files)
        {
            if (!uniquePaths.insert(file.virtualPath).second)
            {
                Fail("duplicate virtual path: " + file.virtualPath);
            }
            if (!uniqueWindowsPaths.insert(Pak::utf8_to_wide(file.virtualPath)).second)
            {
                Fail("case-insensitive Windows path collision: " + file.virtualPath);
            }
        }
        if (files.empty()) Fail("package input is empty");

        candidate = output;
        candidate += L".candidate." + std::to_wstring(GetCurrentProcessId());
        RejectReparsePathIfExists(candidate, "pak candidate");
        fs::remove(candidate, error);
        error.clear();

        Pak::BuildOptions options{};
        Pak::Builder builder(candidate, options);
        for (const InputFile& file : files)
        {
            builder.addFile(file.virtualPath, file.sourcePath);
        }
        builder.finish();

        // Re-open before promotion. This validates the header/index hash and makes a
        // partial candidate impossible to publish as the canonical package.
        const Pak::Archive archive(candidate);
        const auto archivedFiles = archive.list();
        if (archivedFiles.size() != files.size())
        {
            Fail("pak entry count mismatch after reopen");
        }
        for (std::size_t i = 0; i < files.size(); ++i)
        {
            if (archivedFiles[i].path != files[i].virtualPath ||
                archivedFiles[i].size != fs::file_size(files[i].sourcePath))
            {
                Fail("pak index differs from sorted input at: " + files[i].virtualPath);
            }
        }

        PromoteCandidate(candidate, output);
        if (args.listEntries)
        {
            // reopen 검증을 통과한 archivedFiles를 찍는다 — 빌더에 넣은 목록이 아니라
            // **실제로 pak에서 다시 읽힌** 목록이라야 게이트가 산출물을 본 것이 된다.
            for (const auto& archived : archivedFiles)
            {
                std::cout << "[PAK-ENTRY] " << archived.path << '\n';
            }
        }
        std::cout << "[PAK] packaged " << files.size() << " sorted entries: "
            << ToUtf8(output) << '\n';
        return 0;
    }
    catch (const std::exception& exception)
    {
        if (!candidate.empty())
        {
            std::error_code ignored{};
            fs::remove(candidate, ignored);
        }
        std::cerr << "[PAK] failed: " << exception.what() << '\n';
        return 1;
    }
}
