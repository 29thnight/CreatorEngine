#pragma once
// 에디터 폰트 자원의 유일한 자리 (PHASE 21 W1).
//
// ── 왜 한 자리로 모으는가 ─────────────────────────────────────────────────
//
// 폰트를 적재하는 자리가 넷이었고 넷 다 `"C:\Windows\Fonts\..."` 를 손으로
// 적었다(`EditorRenderer`·`MenuBarWindow`·`EditorAssetPresentation` ×2). 그 넷이
// 같은 결함을 공유했다 — **파일이 없으면 에디터가 죽는다.**
//
// 실측(2026-09-11, Release):
//
//   [imgui-error] In window 'NULL': Could not load font file!
//   [크래시] ACCESS_VIOLATION (0xC0000005) · 읽기 시도 주소 0xFFFFFFFFFFFFFFF8
//     #00 ImFontAtlas::AddFont      (imgui_draw.cpp:3084)
//     #01 ImFontAtlas::AddFontFromMemoryCompressedTTF
//     #02 EditorRenderer::AddEditorFonts
//
// 죽는 자리는 본문 폰트가 아니라 **그 다음의 아이콘 폰트**다.
// `AddFontFromFileTTF` 는 실패하면 오류를 적고 NULL 을 돌려주는데(1.92 의 오류
// 복구가 Release 에서는 어서션 대신 로그를 남긴다), 이어지는 아이콘 폰트는
// `MergeMode = true` 라 **직전 폰트에 병합**한다. 직전 폰트가 없으므로 빈 폰트
// 목록의 끝을 읽는다. Debug 도 Release 도 같은 자리에서 죽는다.
//
// 그래서 두 가지를 여기서 보장한다.
//
//   ① 본문 폰트는 **반드시 선다.** 후보를 앞에서부터 훑고 하나도 없으면 ImGui
//      기본 폰트로 내려간다. `nullptr` 을 돌려주지 않는다.
//   ② 아이콘 병합은 **병합할 폰트가 있을 때만** 한다. 없으면 아무 일도 하지
//      않고 거짓을 돌려준다 — 위 크래시의 자리가 이 한 줄이다.
//
// 선택 폰트(작은 글씨용)는 비어 있어도 된다. `PushFont(nullptr, 0.0f)` 이
// "지금 폰트를 그대로" 라서(imgui.h:516) 호출자가 분기하지 않아도 안전하다.

#include <span>
#include <string>
#include <vector>

struct ImFont;

namespace editor::fonts
{
    /// 적재 결과 하나. 보고 표면(`editor.theme`)이 이것을 그대로 읽는다.
    struct loaded_font
    {
        std::string role;              ///< "body" · "korean" · "small" · ...
        std::string resolved_path;     ///< 빈 것이면 후보가 하나도 없었다
        float       size_pixels{ 0.f };
        int          candidates_tried{ 0 };
        bool        used_fallback{ false };  ///< ImGui 기본 폰트로 내려갔는가
        bool        icon_merged{ false };
        ImFont*     font{ nullptr };
    };

    /// 본문 폰트 후보. 앞에서부터 **실재하는** 첫 것을 쓴다.
    std::span<const char* const> body_candidates();

    /// 한글 폰트 후보. 맑은 고딕이 없는 설치본(언어 기능 제거)이 있다.
    std::span<const char* const> korean_candidates();

    /// 후보 하나를 절대 경로로 펼친다. `%SystemRoot%` 를 읽어 쓰므로
    /// `C:\Windows` 가 아닌 설치본에서도 맞는다.
    std::string expand_font_candidate(const char* candidate);

    /// 후보 중 실재하는 첫 절대 경로. 하나도 없으면 빈 문자열.
    std::string resolve_font_path(std::span<const char* const> candidates);

    /// 본문 폰트를 반드시 세운다(①). 돌려주는 `font` 는 `nullptr` 이 아니다.
    loaded_font add_required_font(std::string_view role,
                                  std::span<const char* const> candidates,
                                  float size_pixels);

    /// 있으면 쓰고 없으면 비워 둔다. 비어도 호출자가 분기할 필요가 없다.
    loaded_font add_optional_font(std::string_view role,
                                  std::span<const char* const> candidates,
                                  float size_pixels);

    /// 아이콘 폰트를 직전 폰트에 병합한다(②). 병합할 폰트가 없으면 거짓.
    bool merge_icon_font(float size_pixels);

    /// 이번 실행에서 적재한 폰트 전부. 보고 표면이 읽는다.
    const std::vector<loaded_font>& loaded_fonts();

    /// 아틀라스를 비울 때 함께 비운다(배율 변경 시 재적재 경로).
    void clear_loaded_fonts();

    /// 후보가 하나도 없는 경우에 해상이 **빈 것을 돌려주는가.** 부팅에서 한 번
    /// 돌려 보고를 남긴다 — 이 저장소는 "게이트가 도는 세트에 없으면 없는 것"
    /// 으로 두 번 데었으므로, 되돌아갈 길이 없는 negative 경로를 살아 있는
    /// 에디터에서 재는 자리를 둔다.
    bool fallback_probe_ok();
}
