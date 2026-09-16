#include "EditorAssetDragPayload.h"
#include "BrowserDirectorySnapshot.h"
#include "PathFinder.h"
#include "LogSystem.h"
#include "ImGui.h"
#include <string>
#include <system_error>

namespace editor::asset_drag
{
    void set_payload(const char* type, const std::filesystem::path& path)
    {
        const std::u8string text = path.lexically_normal().u8string();
        // NUL 까지 싣는다 — 받는 쪽은 끝 바이트로 잘림을 가린다.
        ImGui::SetDragDropPayload(type, text.c_str(), text.size() + 1);
    }

    std::filesystem::path path_of(const ImGuiPayload& payload)
    {
        if (!payload.Data || payload.DataSize < 2)
            return {};
        const char* bytes = static_cast<const char*>(payload.Data);
        const size_t length = static_cast<size_t>(payload.DataSize) - 1;
        if (bytes[length] != '\0')
            return {};
        return std::filesystem::path(std::u8string(
            reinterpret_cast<const char8_t*>(bytes), length));
    }

    bool lives_in(const std::filesystem::path& path, std::string_view folder, std::string_view consumer)
    {
        // 브라우저 코드는 `equivalent` 를 쓰지 않는다(verify-browser-filesystem-contract).
        // 놓는 순간 한 번 양쪽을 정규형으로 펴고 어휘로 맞댄다 — 프레임마다가 아니다.
        std::error_code pathError;
        std::error_code folderError;
        const std::filesystem::path parent = path.empty() ? std::filesystem::path{}
            : editor::browser_canonical(path.parent_path(), pathError);
        const std::filesystem::path expected = editor::browser_canonical(PathFinder::Relative(folder), folderError);
        if (!path.empty() && !pathError && !folderError && editor::browser_same_directory(parent, expected))
            return true;

        // 이 소비자는 이름만 저장하고 `folder` 에서 다시 찾는다. 다른 폴더의 파일을
        // 받으면 같은 이름의 **다른 파일**이 묶이므로 받지 않는다.
        std::string message(consumer);
        message += ": only assets directly inside '";
        message += folder;
        message += "' can be dropped here (got '";
        message += reinterpret_cast<const char*>(path.u8string().c_str());
        message += "')";
        Debug::PrintLog(spdlog::level::err, message);
        return false;
    }
}
