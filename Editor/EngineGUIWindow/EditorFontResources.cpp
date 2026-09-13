// 에디터 폰트 자원 (PHASE 21 W1). 왜 이 파일이 있는지는 헤더에 적었다.

#include "EditorFontResources.h"

// `GetWindowsDirectoryW` 하나 때문에 든다. 폰트 디렉터리를 하드코딩하거나
// 환경 변수에 묻지 않고 OS 에 묻는 이유는 아래 `fonts_directory` 에 적었다.
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include "ImGui.h"
#include "EditorIcons.h"
#include "EditorIconAlignment.h"
#include "EditorTextFallback.h"
#include "PathFinder.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace editor::fonts
{
    namespace
    {
        // 하위 경로는 배포된 엔진 자원, 파일 이름은 Windows Fonts의 후보다.
        // Inter가 빠진 배포에서도 시스템 폰트와 ImGui 기본 폰트로 이어진다.
        constexpr const char* kBodyCandidates[] = {
            "Fonts/Inter-Regular.ttf",
            "Verdana.ttf",
            "segoeui.ttf",
            "tahoma.ttf",
            "arial.ttf",
        };

        // 맑은 고딕은 "언어 기능" 으로 빠질 수 있다. 굴림·바탕은 오래된 설치본에
        // 남아 있고, Segoe UI 는 한글 글리프가 없지만 **죽지 않는 것**이 먼저다.
        constexpr const char* kKoreanCandidates[] = {
            "malgun.ttf",
            "malgunsl.ttf",
            "gulim.ttc",
            "batang.ttc",
            "segoeui.ttf",
        };

        // UI private-use slots belong to icons, including Inter's PUA alternates.
        // ImGui 1.92 caps GlyphExcludeRanges at 64 values; use one persistent range.
        constexpr ImWchar kIconExclusions[]{ 0xe000, 0xf8ff, 0 };
        static_assert(std::size(kIconExclusions) <= 64);

        std::vector<loaded_font>& store()
        {
            static std::vector<loaded_font> value;
            return value;
        }

        // Windows 폰트 디렉터리를 **OS 에 묻는다.**
        //
        // 넷 있던 옛 자리는 모두 경로를 손으로 적었다. 그 문자열은 두 가지로
        // 틀릴 수 있다 — 설치 드라이브가 C: 가 아닌 경우, 그리고 편집 중에
        // 이스케이프가 하나 먹히는 경우다. 후자는 가상의 걱정이 아니다:
        // 2026-09-11 에 이 파일을 만들면서 정확히 그렇게 깨뜨렸고
        // (`"\\Windows\\"` 가 `"\Windows\"` 가 되어 `C:\WINDOWSFonts\` 가 됐다),
        // 해상이 통째로 빈 것을 돌려줘 기본 폰트로 내려갔다.
        //
        // `std::filesystem::path` 로 이으면 구분자를 손으로 적을 일이 없어진다.
        std::filesystem::path fonts_directory()
        {
            wchar_t buffer[MAX_PATH]{};
            const UINT written = ::GetWindowsDirectoryW(buffer, MAX_PATH);
            if (0 == written || MAX_PATH <= written)
            {
                return {};
            }
            return std::filesystem::path(buffer, buffer + written) / L"Fonts";
        }

        loaded_font add_font(std::string_view role,
                             std::span<const char* const> candidates,
                             float size_pixels,
                             bool required)
        {
            loaded_font result;
            result.role.assign(role);
            result.size_pixels = size_pixels;
            ImGuiIO& io = ImGui::GetIO();
            for (const char* candidate : candidates)
            {
                ++result.candidates_tried;
                const std::filesystem::path path = expand_font_candidate(candidate);
                std::error_code error;
                if (path.empty() || !std::filesystem::is_regular_file(path, error))
                {
                    continue;
                }
                // filesystem 검사는 native 경로, ImGui 파일 API는 UTF-8이다.
                const std::u8string utf8 = path.u8string();
                const std::string filename(utf8.begin(), utf8.end());
                ImFontConfig config;
                config.Flags |= ImFontFlags_NoLoadError;
                config.GlyphExcludeRanges = kIconExclusions;
                result.font = io.Fonts->AddFontFromFileTTF(
                    filename.c_str(), size_pixels, &config);
                if (nullptr != result.font)
                {
                    result.resolved_path = filename;
                    break;
                }
            }

            // 경로를 미리 확인하고도 적재가 실패할 수 있다(깨진 파일·권한).
            // 그 경우까지 같은 갈래로 내린다.
            if (nullptr == result.font)
            {
                result.resolved_path.clear();
                if (required)
                {
                    ImFontConfig config;
                    config.SizePixels = size_pixels;
                    config.GlyphExcludeRanges = kIconExclusions;
                    result.font = io.Fonts->AddFontDefault(&config);
                    result.used_fallback = true;
                }
            }

            // A separate Korean font used by menus does not cover the body's
            // SerializeField labels. Each text face needs its own fallback source.
            if (result.font && !result.font->IsGlyphInFont(0xAC00))
            {
                loaded_font fallback;
                fallback.role = std::string(role) + "-korean-fallback";
                fallback.size_pixels = size_pixels;
                for (const char* candidate : kKoreanCandidates)
                {
                    ++fallback.candidates_tried;
                    const auto path = expand_font_candidate(candidate);
                    std::error_code error;
                    if (!std::filesystem::is_regular_file(path, error)) continue;
                    const auto utf8 = path.u8string();
                    const std::string filename(utf8.begin(), utf8.end());
                    if (!merge_korean_fallback(*io.Fonts, filename.c_str(), size_pixels)) continue;
                    fallback.resolved_path = filename;
                    fallback.font = result.font;
                    break;
                }
                store().push_back(std::move(fallback));
            }
            store().push_back(result);
            return result;
        }
    }

    std::span<const char* const> body_candidates()
    {
        return std::span<const char* const>(kBodyCandidates);
    }

    std::span<const char* const> korean_candidates()
    {
        return std::span<const char* const>(kKoreanCandidates);
    }

    std::filesystem::path expand_font_candidate(const char* candidate)
    {
        if (nullptr == candidate || '\0' == *candidate)
        {
            return {};
        }
        const std::filesystem::path relative{ candidate };
        if (relative.has_parent_path())
        {
            return PathFinder::EngineResourcePath(candidate);
        }
        const std::filesystem::path system_fonts = fonts_directory();
        return system_fonts.empty() ? std::filesystem::path{} : system_fonts / relative;
    }

    std::string resolve_font_path(std::span<const char* const> candidates)
    {
        std::error_code ignored;
        for (const char* candidate : candidates)
        {
            const std::filesystem::path path = expand_font_candidate(candidate);
            if (path.empty())
            {
                continue;
            }
            if (std::filesystem::is_regular_file(path, ignored))
            {
                const std::u8string utf8 = path.u8string();
                return std::string(utf8.begin(), utf8.end());
            }
        }
        return std::string{};
    }

    loaded_font add_required_font(std::string_view role,
                                  std::span<const char* const> candidates,
                                  float size_pixels)
    {
        return add_font(role, candidates, size_pixels, true);
    }

    loaded_font add_optional_font(std::string_view role,
                                  std::span<const char* const> candidates,
                                  float size_pixels)
    {
        auto result = add_font(role, candidates, size_pixels, false);
        if (result.font) result.icon_merged = merge_icon_font(size_pixels);
        return result;
    }

    bool merge_icon_font(float size_pixels, float baseline_offset_pixels)
    {
        ImGuiIO& io = ImGui::GetIO();

        // ★ 이 한 줄이 빈 폰트 목록에 병합하다 나는 ACCESS_VIOLATION 을 막는다.
        //
        // `MergeMode` 는 **직전 폰트**에 병합한다. 직전 폰트가 없으면
        // `ImFontAtlas::AddFont` 가 빈 목록의 끝을 읽는다. 본문 폰트 적재가
        // 실패한 채 여기까지 오면 정확히 그 일이 일어났다(2026-09-11 실측:
        // 0xC0000005, 읽기 시도 주소 0xFFFFFFFFFFFFFFF8).
        if (io.Fonts->Fonts.Size <= 0)
        {
            return false;
        }

        ImFont* const destination = io.Fonts->Fonts.back();

        const std::filesystem::path path = expand_font_candidate(EditorIcon::FontPath);
        std::error_code error;
        if (!std::filesystem::is_regular_file(path, error)) return false;
        const std::u8string utf8 = path.u8string();
        const std::string filename(utf8.begin(), utf8.end());
        // This static subset contains only the selected PUA codepoints, no text/ligatures.
        // FA is not merged: the two families assign different symbols to the same codepoints.
        const bool merged = nullptr != merge_aligned_icons(*io.Fonts,
            filename.c_str(), size_pixels, baseline_offset_pixels);
        if (merged)
        {
            for (auto& loaded : store()) if (loaded.font == destination) loaded.icon_merged = true;
            loaded_font icon;
            icon.role = "icons";
            icon.resolved_path = filename;
            icon.size_pixels = size_pixels;
            icon.candidates_tried = 1;
            icon.icon_merged = true;
            icon.font = destination;
            store().push_back(std::move(icon));
        }
        return merged;
    }

    const std::vector<loaded_font>& loaded_fonts()
    {
        return store();
    }

    void clear_loaded_fonts()
    {
        store().clear();
    }

    bool fallback_probe_ok()
    {
        // 후보가 하나도 없는 경우를 살아 있는 에디터에서 재는 자리다. 실제 폰트를
        // 지울 수는 없으므로 해상 함수만 태운다 — 적재가 아니라 **해상의 negative
        // 경로**가 대상이다.
        static const char* const absent[] = {
            "CreatorEditorNoSuchFont-A.ttf",
            "CreatorEditorNoSuchFont-B.ttf",
        };
        return resolve_font_path(std::span<const char* const>(absent)).empty();
    }
}
