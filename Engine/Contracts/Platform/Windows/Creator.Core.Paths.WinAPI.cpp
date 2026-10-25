#if !defined(CE_CONTRACT_STANDALONE) || !defined(CE_PATHS_PLATFORM_SOURCE_IMPLEMENTED)
#define CE_PATHS_PLATFORM_SOURCE_IMPLEMENTED
#if defined(CE_CONTRACT_STANDALONE)
#include "../../../../Tools/build/ModuleAdapters/Creator.Core.Paths.h"
#include "../../../Platform/WindowsSDK/Creator.WinAPI.h"
#define CE_PATHS_PLATFORM_DEFINITION inline
#else
module Creator.Core.Paths;
import "Creator.WinAPI.h";
#define CE_PATHS_PLATFORM_DEFINITION
#endif

namespace ce::paths
{
    CE_PATHS_PLATFORM_DEFINITION bool roots::has_valid_runtime_ownership() const noexcept
    {
        try
        {
            const auto normalizeAbsolute =
                [](const std::filesystem::path& value, std::filesystem::path& output) noexcept
            {
                try
                {
                    if (value.empty() || !value.is_absolute())
                    {
                        return false;
                    }
                    output = value.lexically_normal();
                    return !output.empty() && !output.root_path().empty();
                }
                catch (...)
                {
                    return false;
                }
            };
            const auto samePath = [](const std::filesystem::path& left, const std::filesystem::path& right) noexcept
            { return 0 == _wcsicmp(left.c_str(), right.c_str()); };

            std::filesystem::path owner;
            std::filesystem::path process;
            std::filesystem::path content;
            std::filesystem::path data;
            std::filesystem::path project;
            std::filesystem::path assets;
            if (!normalizeAbsolute(runtime_owner_root, owner) || !normalizeAbsolute(runtime_process_root, process) ||
                !normalizeAbsolute(runtime_content_root, content) || !normalizeAbsolute(runtime_data_root, data) ||
                !normalizeAbsolute(project_root, project) || !normalizeAbsolute(assets_root, assets))
            {
                return false;
            }

            // Runtime cleanup must never be able to select a drive root or one of
            // its immediate children as the shared owner capability.
            if (samePath(owner, owner.root_path()) || samePath(owner.parent_path(), owner.root_path()))
            {
                return false;
            }

            return samePath(process.parent_path(), owner) && samePath(content, process / L"RuntimeContent") &&
                   samePath(data, process / L"RuntimeData") && !samePath(content, data) && samePath(project, content) &&
                   samePath(assets, content / L"Assets");
        }
        catch (...)
        {
            return false;
        }
    }

    CE_PATHS_PLATFORM_DEFINITION std::filesystem::path resolve_process_executable_directory()
    {
        std::array<wchar_t, MAX_PATH> buffer{};
        const DWORD length =
            GetModuleFileNameW(GetModuleHandleW(nullptr), buffer.data(), static_cast<DWORD>(buffer.size()));
        if (0 == length || length >= buffer.size())
        {
            return {};
        }
        // remove_filename()은 후행 구분자를 남긴다. 그 값을 다시 parent_path() 하면
        // 일부 구현에서 같은 디렉터리가 반환되어 앱 폴더와 구성 루트가 합쳐진다.
        return std::filesystem::path(buffer.data()).parent_path().lexically_normal();
    }

    CE_PATHS_PLATFORM_DEFINITION std::filesystem::path resolve_runtime_directory()
    {
        auto root = resolve_process_executable_directory();
        for (int depth = 0; depth <= 2 && !root.empty(); ++depth)
        {
            std::error_code error;
            if (std::filesystem::is_regular_file(root / L"Runtime" / L"layout.version", error))
            {
                return root / L"Runtime";
            }
            root = root.parent_path();
        }
        return {};
    }
} // namespace ce::paths

#undef CE_PATHS_PLATFORM_DEFINITION

#endif
