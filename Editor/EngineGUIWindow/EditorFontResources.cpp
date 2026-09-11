// 에디터 폰트 자원 (PHASE 21 W1). 왜 이 파일이 있는지는 헤더에 적었다.

#include "EditorFontResources.h"

// `GetWindowsDirectoryW` 하나 때문에 든다. 폰트 디렉터리를 하드코딩하거나
// 환경 변수에 묻지 않고 OS 에 묻는 이유는 아래 `fonts_directory` 에 적었다.
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include "ImGui.h"
#include "IconsFontAwesome6.h"
#include "fa.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace editor::fonts
{
    namespace
    {
        // 폰트 디렉터리 아래의 **파일 이름만** 적는다. 절대 경로를 적으면
        // Windows 가 C: 가 아닌 설치본에서 틀린다.
        constexpr const char* kBodyCandidates[] = {
            "Verdana.ttf",   // 지금까지 쓰던 것 — 판정이 바뀌지 않도록 맨 앞에 둔다
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
                // 여기서 예외를 던지면 폰트 하나 때문에 부팅이 죽는다.
                return std::filesystem::path(L"C:/Windows") / L"Fonts";
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
            result.candidates_tried = static_cast<int>(candidates.size());
            result.resolved_path = resolve_font_path(candidates);

            ImGuiIO& io = ImGui::GetIO();
            if (!result.resolved_path.empty())
            {
                result.font = io.Fonts->AddFontFromFileTTF(
                    result.resolved_path.c_str(), size_pixels);
            }

            // 경로를 미리 확인하고도 적재가 실패할 수 있다(깨진 파일·권한).
            // 그 경우까지 같은 갈래로 내린다.
            if (nullptr == result.font)
            {
                result.resolved_path.clear();
                if (required)
                {
                    result.font = io.Fonts->AddFontDefault();
                    result.used_fallback = true;
                }
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

    std::string expand_font_candidate(const char* candidate)
    {
        if (nullptr == candidate || '\0' == *candidate)
        {
            return std::string{};
        }
        return (fonts_directory() / candidate).string();
    }

    std::string resolve_font_path(std::span<const char* const> candidates)
    {
        std::error_code ignored;
        for (const char* candidate : candidates)
        {
            const std::string path = expand_font_candidate(candidate);
            if (path.empty())
            {
                continue;
            }
            if (std::filesystem::exists(path, ignored))
            {
                return path;
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
        return add_font(role, candidates, size_pixels, false);
    }

    bool merge_icon_font(float size_pixels)
    {
        // ★ 아이콘 범위는 **적재하는 블롭과 같은 판**이어야 한다.
        //
        // `IconsFontAwesome4.h` 와 `6.h` 가 `ICON_MIN_FA`/`ICON_MAX_FA` 를 서로
        // 다른 값으로 정의한다(FA4 0xf000~0xf2e0 · FA6 0xe005~0xf8ff). 이
        // 프로젝트는 유니티 빌드라 같은 blob 안의 다른 TU 가 FA4 를 들이면 여기
        // 값이 조용히 좁아지고, 그러면 0xf000 아래의 FA6 아이콘(예: Hierarchy 의
        // `ICON_FA_BARS_STAGGERED` U+e0d7)이 아틀라스에 실리지 않아 **네모로
        // 그려진다.** 실행해도 예외가 나지 않으므로 컴파일 시점에 막는다.
        //
        // FA4 헤더는 은퇴시켰다(2026-09-11, W1). 이 단정은 그것이 되돌아오는 것을
        // 막는 자물쇠다 — `static_assert` 라서 Release 에서도 사라지지 않는다.
        static_assert(0xe005 == ICON_MIN_FA && 0xf8ff == ICON_MAX_FA,
            "아이콘 범위가 FA6 의 값이 아니다. 같은 유니티 blob 안의 TU 가 "
            "IconsFontAwesome4.h 를 들였을 가능성이 높다 — 폰트 블롭은 FA6 하나뿐이므로 "
            "헤더도 IconsFontAwesome6.h 하나로 맞춰라");

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

        // ★ 대상이 **암묵 참조 크기**면 크기를 넘기면 안 된다.
        //
        // 1.92.8 의 표(imgui_draw.cpp:3111):
        //
        //                 | 대상 암묵 | 대상 명시 |
        //   더하기 암묵   | OK        | OK        |
        //   더하기 명시   | **KO**    | OK        |
        //
        // 암묵 크기를 세우는 것은 `AddFontDefault` 뿐이다. 즉 이 조합은 본문
        // 폰트가 후보를 다 놓쳐 기본 폰트로 내려갔을 때 정확히 일어난다 —
        // **대비 경로로 내려가는 순간 어서션에서 죽는 대비**였다. 2026-09-11 에
        // 실측으로 걸렸다(imgui_draw.cpp:3115). 대비가 서려면 이 분기가 있어야
        // 한다.
        ImFont* const destination = io.Fonts->Fonts.back();
        const bool destination_is_implicit =
            (nullptr != destination) &&
            (0 != (destination->Flags & ImFontFlags_ImplicitRefSize));
        const float merge_size = destination_is_implicit ? 0.0f : size_pixels;

        static const ImWchar icons_ranges[] = { ICON_MIN_FA, ICON_MAX_FA, 0 };
        ImFontConfig icons_config;
        icons_config.MergeMode = true;
        const bool merged = nullptr != io.Fonts->AddFontFromMemoryCompressedTTF(
            FA_compressed_data, FA_compressed_size, merge_size,
            &icons_config, icons_ranges);

        if (merged && !store().empty())
        {
            store().back().icon_merged = true;
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
