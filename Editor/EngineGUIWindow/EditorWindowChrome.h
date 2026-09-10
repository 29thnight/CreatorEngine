#pragma once

#include <Windows.h>

#include <atomic>
#include <optional>
#include <string>

// 에디터 셸 크롬 — OS 캡션을 지우고 제목표시줄을 ImGui가 그린다.
//
// ── 왜 에디터 계층인가 ──
//
// CoreWindow는 창 생성 정책을 프로세스 호스트에게서 받기만 한다(WindowDesc).
// 그래서 캡션 제거는 엔진을 건드리지 않고, 에디터가 꽂는 메시지 가로채기와
// 에디터가 그리는 한 줄로 끝난다. Player는 이 파일을 링크하지 않으므로
// 테두리 없는 전체화면 창은 영향을 받지 않는다.
//
// ── 스레드 계약 ──
//
// HandleWindowMessage는 WndProc가 도는 메인 스레드 전용이고, DrawTitleBarTail은
// ImGui가 도는 표시 스레드 전용이다. 둘 사이에 흐르는 값은 끌기 영역 좌표
// 셋뿐이라 원자값으로 닫는다. WndProc는 ImGui 상태를 절대 읽지 않는다 —
// 읽으면 두 스레드가 같은 컨텍스트를 만진다.
//
// 제목 문자열은 아예 흐르지 않는다. 양쪽이 같은 출처(EditorSettingsStore의
// projectName)에서 각자 조립한다 — 건네주는 값이 없으면 어긋날 자리도 없다.
class EditorWindowChrome final
{
public:
    static EditorWindowChrome& Get() noexcept;

    EditorWindowChrome(const EditorWindowChrome&) = delete;
    EditorWindowChrome& operator=(const EditorWindowChrome&) = delete;

    /// 메인 스레드. 창이 만들어진 뒤 한 번 부른다. 캡션을 지운 프레임을
    /// 창에 다시 계산시킨다.
    void Attach(HWND windowHandle) noexcept;

    /// 메인 스레드 전용. 처리한 메시지만 값을 돌려준다.
    std::optional<LRESULT> HandleWindowMessage(
        HWND windowHandle, UINT message, WPARAM wParam, LPARAM lParam) noexcept;

    /// 표시 스레드 전용. 메뉴가 끝난 자리에서 이어 그린다 —
    /// 가운데 제목과 오른쪽 창 버튼 셋을 놓고 끌기 영역을 게시한다.
    void DrawTitleBarTail();

    /// 제목표시줄 한 줄의 높이(클라이언트 좌표). 표시 스레드가 게시한다.
    float GetTitleBarHeight() const noexcept
    {
        return m_titleBarHeight.load(std::memory_order_relaxed);
    }

private:
    EditorWindowChrome() = default;

    std::optional<LRESULT> HandleNonClientCalcSize(
        HWND windowHandle, WPARAM wParam, LPARAM lParam) noexcept;
    std::optional<LRESULT> HandleNonClientHitTest(
        HWND windowHandle, LPARAM lParam) noexcept;

    HWND m_windowHandle{ nullptr };

    // 표시 스레드가 쓰고 메인 스레드가 읽는다. 클라이언트 좌표.
    std::atomic<float> m_titleBarHeight{ 0.f };
    std::atomic<float> m_dragRegionLeft{ 0.f };
    std::atomic<float> m_dragRegionRight{ 0.f };
};

/// "<프로젝트 이름> - Creator Engine". 프로젝트 이름이 아직 없으면 접두어 없이
/// 엔진 이름만. FPS·해상도·백엔드는 여기 없다 — 그 값들은 씬뷰 오버레이와
/// Help > About이 각각 든다.
std::wstring ComposeEditorWindowTitle();
