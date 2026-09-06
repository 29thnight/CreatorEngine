#include "EditorPlayModeController.h"
#include "SceneManager.h"
#include "ReflectionUndo.h"

namespace Editor
{
    void PlayModeController::Initialize()
    {
        if (m_handle.IsValid()) return;

        m_handle = SceneManagers->PlayModeEvent.AddLambda([](bool isEntering)
        {
            Meta::UndoManager* undo = Meta::UndoManager::GetInstance();

            if (isEntering)
            {
                // 재생 진입은 편집 이력을 버린다.
                //
                // 이 두 줄은 SceneManager::BeginPlayTransaction 안에 있었다. 그 함수는
                // Player도 타므로(Player의 유일한 재생 진입 경로다) 출하 게임이 매번
                // Undo 스택을 비우고 있었다 — 애초에 쌓지도 않는 스택을.
                //
                // 정지에서 되돌리지 않는다. Clear는 버리는 것이지 물러 두는 것이 아니고,
                // 그것이 옛 동작이다(회귀 게이트 verify-play-selection-undo.ps1의 판정 E).
                undo->ClearGameMode();
                undo->Clear();
            }
            else
            {
                // 정지는 게임 스택을 버린다. GUI 버튼이 전이 직전에 하던 일이다.
                undo->ClearGameMode();
            }

            // ★ LC6(§9): **재생 상태를 따라가는 값의 주인을 여기 하나로 모은다.**
            //
            //   이 대입은 `MenuBarWindow` 의 Play 버튼 안에 인라인으로 있었고,
            //   저장소 전체에서 그 한 줄이 유일한 쓰기였다. 그래서 CLI·서비스로
            //   재생하면 `m_isGameMode` 가 **영원히 false** 였다 — 이름과 달리
            //   이 필드는 "게임 모드"가 아니라 "에디터 UI 의 Play 버튼을 눌렀는가"
            //   였다는 뜻이다(ReflectionUndo.h 의 경고가 그것을 적어 두었다).
            //
            //   결과는 조용한 의미 분기였다. 서비스로 재생한 뒤의 편집은 게임
            //   스택이 아니라 **편집 스택**에 쌓인다 — 사람이 GUI 로 한 것과
            //   에이전트가 HTTP 로 한 것이 같은 조작인데 다른 곳에 기록된다.
            //   §9 가 "GUI Play 와 서비스 play 가 같은 규약으로 전이" 하라고 한
            //   자리가 정확히 이것이다.
            //
            //   이제 GUI 든 CLI 든 `SetGameStart` 를 지나면 이 이벤트가 오므로
            //   한 곳에서 정해진다. 앞선 게이트가 이 결함을 "고치면 붉어지게"
            //   못 박아 두었고(verify-play-selection-undo.ps1 판정 C), 그 줄이
            //   이번에 붉어진다.
            undo->m_isGameMode = isEntering;
        });
    }

    void PlayModeController::Shutdown()
    {
        if (!m_handle.IsValid()) return;
        SceneManagers->PlayModeEvent -= m_handle;
        m_handle.Reset();
    }
}
