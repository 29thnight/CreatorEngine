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
            if (!isEntering) return;

            // 재생 진입은 편집 이력을 버린다.
            //
            // 이 두 줄은 SceneManager::BeginPlayTransaction 안에 있었다. 그 함수는
            // Player도 타므로(Player의 유일한 재생 진입 경로다) 출하 게임이 매번
            // Undo 스택을 비우고 있었다 — 애초에 쌓지도 않는 스택을.
            //
            // 정지에서 되돌리지 않는다. Clear는 버리는 것이지 물러 두는 것이 아니고,
            // 그것이 옛 동작이다(회귀 게이트 verify-play-selection-undo.ps1의 판정 E).
            Meta::UndoManager::GetInstance()->ClearGameMode();
            Meta::UndoManager::GetInstance()->Clear();
        });
    }

    void PlayModeController::Shutdown()
    {
        if (!m_handle.IsValid()) return;
        SceneManagers->PlayModeEvent -= m_handle;
        m_handle.Reset();
    }
}
