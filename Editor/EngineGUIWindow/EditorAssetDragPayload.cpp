#include "EditorAssetDragPayload.h"
#include "BrowserDirectorySnapshot.h"
#include "PathFinder.h"
#include "LogSystem.h"
#include "ImGui.h"
#include <string>
#include <system_error>

namespace
{
    // 경로를 싣는 payload 타입의 **정본**. ImGui 는 payload 를 타입 이름 하나로만
    // 구분하므로, 여기 없는 이름이 나르는 바이트는 경로가 아니다. 도킹 창을 끌 때
    // ImGui 가 세우는 `_IMWINDOW`(ImGuiWindow* 8바이트)도, 구조체를 그대로 싣는
    // `ASSET_ENTRY` 도 여기 없다 — 그 바이트를 경로로 읽으면 크래시한다(아래 참조).
    //
    // 브라우저 타일은 `FileTypeToString` 이 돌려준 이름으로 payload 를 싣는다
    // (ContentsBrowserWindow.cpp). 그 표에 유형을 더하면서 이 표를 안 고치면
    // `set_payload` 가 싣기를 **거부하고 이유를 남긴다** — 조용히 어긋나지 않는다.
    constexpr std::string_view kPathPayloadTypes[] = {
        "Unknown", "Model", "Texture", "MaterialTexture", "TerrainTexture",
        "Shader", "CppScript", "CSharpScript", "Prefab", "Sound", "HDR",
        "VolumeProfile", "Font", "SPRITESHEET", "UI_TEXTURE",
    };

    bool is_path_payload_type(std::string_view type)
    {
        for (const std::string_view known : kPathPayloadTypes)
            if (known == type) return true;
        return false;
    }

    // `std::filesystem::path` 는 무효 UTF-8 바이트열을 만나면 빈 경로를 주는 대신
    // `system_error` 를 **던진다**(_Convert_narrow_to_wide). payload 는 남이 실은
    // 바이트일 수 있으므로 던지기 전에 여기서 막는다. MultiByteToWideChar 가
    // MB_ERR_INVALID_CHARS 로 거절하는 것들을 같이 거절한다 — 과장 표기, 서러게이트,
    // U+10FFFF 초과, 끊긴 연속 바이트.
    bool is_valid_utf8(const char* bytes, size_t length)
    {
        for (size_t i = 0; i < length; )
        {
            const unsigned char lead = static_cast<unsigned char>(bytes[i]);
            size_t extra = 0;
            unsigned int codepoint = 0;
            unsigned int lowest = 0;
            if (lead < 0x80)                            { ++i; continue; }
            else if (0xC2 <= lead && lead <= 0xDF)      { extra = 1; codepoint = lead & 0x1Fu; lowest = 0x80u; }
            else if (0xE0 <= lead && lead <= 0xEF)      { extra = 2; codepoint = lead & 0x0Fu; lowest = 0x800u; }
            else if (0xF0 <= lead && lead <= 0xF4)      { extra = 3; codepoint = lead & 0x07u; lowest = 0x10000u; }
            else return false;

            if (i + extra >= length) return false;
            for (size_t k = 1; k <= extra; ++k)
            {
                const unsigned char continuation = static_cast<unsigned char>(bytes[i + k]);
                if (0x80 != (continuation & 0xC0)) return false;
                codepoint = (codepoint << 6) | (continuation & 0x3Fu);
            }
            if (codepoint < lowest) return false;
            if (0xD800 <= codepoint && codepoint <= 0xDFFF) return false;
            if (codepoint > 0x10FFFF) return false;
            i += extra + 1;
        }
        return true;
    }
}

namespace editor::asset_drag
{
    void set_payload(const char* type, const std::filesystem::path& path)
    {
        if (!type || !is_path_payload_type(type))
        {
            std::string message("asset_drag: '");
            message += type ? type : "(null)";
            message += "' is not in the path payload table — add it in EditorAssetDragPayload.cpp";
            Debug::PrintLog(spdlog::level::err, message);
            return;
        }
        const std::u8string text = path.lexically_normal().u8string();
        // NUL 까지 싣는다 — 받는 쪽은 끝 바이트로 잘림을 가린다.
        ImGui::SetDragDropPayload(type, text.c_str(), text.size() + 1);
    }

    bool carries_path(const ImGuiPayload& payload)
    {
        return '\0' != payload.DataType[0] && is_path_payload_type(payload.DataType);
    }

    std::filesystem::path path_of(const ImGuiPayload& payload)
    {
        if (!payload.Data || payload.DataSize < 2)
            return {};
        const char* bytes = static_cast<const char*>(payload.Data);
        const size_t length = static_cast<size_t>(payload.DataSize) - 1;
        if (bytes[length] != '\0')
            return {};
        // ★ 9-18 크래시(CreatorEditor_20260918_153849.dmp): 이 검사가 없어서 도킹 창
        //   드래그의 `_IMWINDOW` payload — 포인터 8바이트 `60 9c 28 a5 fa 01 00 00` —
        //   가 여기까지 왔다. 상위 바이트가 0 이라 위의 NUL 검사를 통과했고, `0x9c` 는
        //   선두 없는 연속 바이트라 path 생성자가 던졌으며, 그리는 스레드에는 그것을
        //   받을 자리가 없어 terminate 했다. 무효 바이트열은 예외가 아니라 빈 경로다.
        if (!is_valid_utf8(bytes, length))
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
