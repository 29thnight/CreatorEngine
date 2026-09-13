#include "EditorPlayModeController.h"
#include "EditorSessionState.h"
#include "InputManager.h"
#include "ReflectionUndo.h"
#include "Render/Scene/EnhancedSceneRenderer.h"
#include "Scene.h"
#include "SceneManager.h"
#include "ViewportHostWindow.h"

#include <Windows.h>
#include <mutex>

namespace Editor
{
    namespace
    {
        using ::editor::windows::viewport_mode;

        // 게시본. 쓰는 쪽은 게임 스레드(Tick) 하나, 읽는 쪽은 CLI(게임 스레드)와
        // 제목표시줄(PresentationThread)이다. 열거 둘은 원자로 따로 두어 그리는
        // 쪽이 잠금 없이 읽는다 — 프레임마다 도는 자리에 뮤텍스를 물리지 않는다.
        std::mutex statusMutex;
        PlayModeStatus published{};
        std::atomic<PlayState>  publishedState{ PlayState::Stopped };
        std::atomic<InputOwner> publishedOwner{ InputOwner::Editor };
        std::atomic<int>        foregroundOverride{ -1 };
    }

    const char* PlayStateName(PlayState state) noexcept
    {
        switch (state)
        {
        case PlayState::Stopped:          return "Stopped";
        case PlayState::Entering:         return "Entering";
        case PlayState::PlayingPossessed: return "PlayingPossessed";
        case PlayState::PlayingEjected:   return "PlayingEjected";
        case PlayState::Exiting:          return "Exiting";
        }
        return "?";
    }

    const char* InputOwnerName(InputOwner owner) noexcept
    {
        return InputOwner::Game == owner ? "game" : "editor";
    }

    PlayModeStatus PlayModeController::Status()
    {
        std::lock_guard lock(statusMutex);
        return published;
    }

    PlayState PlayModeController::CurrentState() noexcept
    {
        return publishedState.load(std::memory_order_acquire);
    }

    InputOwner PlayModeController::CurrentOwner() noexcept
    {
        return publishedOwner.load(std::memory_order_acquire);
    }

    void PlayModeController::SetForegroundOverride(int mode) noexcept
    {
        foregroundOverride.store(mode < 0 ? -1 : (mode ? 1 : 0), std::memory_order_release);
    }

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
                //
                // ★ W5 선행 1 뒤로 이 통지는 스냅샷이 뜬 **뒤**에 온다. 그 전에는
                //   통지가 먼저라 스냅샷이 실패한 전이가 이력부터 죽였다 — 게이트의
                //   실패 주입 구간이 "실패하면 이력이 남는다" 를 단정한다.
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
        // 종료 경로에서도 소유권과 커서를 에디터로 되돌린다 — 게임이 커서를
        // 숨긴 채 재생 중에 창을 닫아도 OS 커서가 사라진 채 남지 않는다.
        InputManagement->SetGameInputOwned(false);
        InputManagement->ShowCursor();
        if (!m_handle.IsValid()) return;
        SceneManagers->PlayModeEvent -= m_handle;
        m_handle.Reset();
    }

    bool PlayModeController::ForegroundNow() const noexcept
    {
        const int mode = foregroundOverride.load(std::memory_order_acquire);
        if (mode >= 0) return 0 != mode;
        const HWND window = InputManagement->WindowHandle();
        return nullptr != window && ::GetForegroundWindow() == window;
    }

    void PlayModeController::RememberPrior()
    {
        const auto demand = ::editor::windows::read_viewport_demand();
        m_priorMode = demand.mode;
        m_priorHostFocused = demand.hostFocused;
        m_priorSelection.clear();
        m_priorPrimary = 0;
        m_priorSceneId = 0;
        if (Scene* scene = SceneManagers->GetActiveScene())
        {
            m_priorSceneId = scene->GetSceneId();
            for (const Entity* entity : scene->m_selectedEntities)
                if (entity) m_priorSelection.push_back(entity->GetInstanceID());
            if (const Entity* primary = scene->m_selectedEntity)
                m_priorPrimary = primary->GetInstanceID();
        }
        m_hasPrior = true;
    }

    void PlayModeController::RestorePrior()
    {
        if (!m_hasPrior) return;
        m_hasPrior = false;

        // 문서 — Host 모드. 요청은 다음 Host 본문이 소비하므로 그때까지 요청값을
        // 유효 모드로 본다(m_modeRequest).
        ::editor::windows::request_viewport_mode(m_priorMode);
        m_modeRequest = m_priorMode;

        // 포커스 — 재생 전에 Host 가 포커스를 갖고 있었으면 되돌린다. 게임
        // 캔버스가 포커스를 가져간 채 정지하면 키보드 탐색이 엉뚱한 창에 남는다.
        if (m_priorHostFocused) ::editor::windows::request_viewport_focus();

        // 선택 — 정지의 안전장치(resetSelectedObjectEvent)가 비운 뒤라 여기서
        // instanceID 로 되찾는다. 백업 YAML 이 m_instanceID 를 싣고 복원이 그것을
        // 그대로 쓰므로 같은 논리 신원이 살아 있다. Undo 항목은 만들지 않는다 —
        // 정지는 편집이 아니다.
        Scene* scene = SceneManagers->GetActiveScene();
        if (nullptr == scene || scene->GetSceneId() != m_priorSceneId) return;
        if (m_priorSelection.empty() && 0 == m_priorPrimary) return;

        const auto find = [scene](std::size_t instanceId) -> Entity*
        {
            if (0 == instanceId) return nullptr;
            for (const auto& entity : scene->m_Entities)
                if (entity && !entity->IsDestroyMark() && entity->GetInstanceID() == instanceId)
                    return entity.get();
            return nullptr;
        };
        scene->ClearSelectedEntities();
        for (const std::size_t id : m_priorSelection)
            if (Entity* entity = find(id)) scene->AddSelectedEntity(entity);
        scene->m_selectedEntity = find(m_priorPrimary);
        if (nullptr == scene->m_selectedEntity && !scene->m_selectedEntities.empty())
            scene->m_selectedEntity = scene->m_selectedEntities.front();
        if (Entity* selected = scene->m_selectedEntity)
            EditorSessionState::Get().SelectionHistory().Observe(
                scene->GetSceneId(), scene->HandleOf(selected->m_index));
    }

    void PlayModeController::Tick()
    {
        SceneManager& sceneManager = *SceneManagers;
        const bool requested = sceneManager.IsGameStart();
        const bool committed = sceneManager.IsPlayCommitted();
        const bool pending   = sceneManager.HasPendingSceneStructureChange();
        const bool paused    = sceneManager.IsGamePaused();

        const auto demand = ::editor::windows::read_viewport_demand();
        if (m_modeRequest && demand.mode == *m_modeRequest) m_modeRequest.reset();

        // 게임 캔버스 클릭은 possess 요청이다 — Ejected 에서 Game Preview 를
        // 누르거나, Possessed 에서 글자 입력을 끊고 캔버스로 돌아올 때.
        if (demand.gameCanvasClicks != m_seenCanvasClicks)
        {
            m_seenCanvasClicks = demand.gameCanvasClicks;
            if (PlayState::PlayingEjected == m_state)
            {
                ::editor::windows::request_viewport_mode(viewport_mode::game);
                m_modeRequest = viewport_mode::game;
            }
        }
        const viewport_mode effectiveMode = m_modeRequest ? *m_modeRequest : demand.mode;

        switch (m_state)
        {
        case PlayState::Stopped:
            if (requested)
            {
                RememberPrior();
                m_state = PlayState::Entering;
            }
            break;

        case PlayState::Entering:
            if (committed)
            {
                // 확정 뒤에만 Game 캔버스와 게임 입력으로 간다(§6.2). 요청은 다음
                // Host 본문이 소비하고 그때까지 요청값이 유효 모드다.
                ::editor::windows::request_viewport_mode(viewport_mode::game);
                m_modeRequest = viewport_mode::game;
                m_state = PlayState::PlayingPossessed;
            }
            else if (!requested)
            {
                // 실패(SceneManager 가 요청을 되돌렸다) 또는 취소. 아무것도
                // 바뀌지 않았으므로 되돌릴 것도 없다 — prior 만 버린다.
                m_hasPrior = false;
                m_state = PlayState::Stopped;
            }
            break;

        case PlayState::PlayingPossessed:
        case PlayState::PlayingEjected:
            if (!requested || !committed)
            {
                m_state = PlayState::Exiting;
            }
            else
            {
                m_state = viewport_mode::game == effectiveMode
                    ? PlayState::PlayingPossessed : PlayState::PlayingEjected;
            }
            break;

        case PlayState::Exiting:
            if (committed && requested)
            {
                // 복원 전에 다시 재생을 눌렀다. prior 는 첫 재생 전의 것이 남는다.
                m_state = viewport_mode::game == effectiveMode
                    ? PlayState::PlayingPossessed : PlayState::PlayingEjected;
            }
            else if (!committed && !pending && !requested)
            {
                // 씬이 복원된 뒤다. 게임이 남긴 커서 요청은 게임과 함께 사라진다.
                InputManagement->ShowCursor();
                RestorePrior();
                m_state = PlayState::Stopped;
            }
            break;
        }

        const bool foreground = ForegroundNow();
        const bool gameOwns = PlayState::PlayingPossessed == m_state
            && !paused && foreground && !demand.uiWantTextInput;
        InputManagement->SetGameInputOwned(gameOwns);

        const EnhancedLiveDisplaySnapshot display = EnhancedSceneRenderer::GetLiveDisplaySnapshot();
        const bool gameTargetReady = display.Get(EnhancedLiveDisplayTarget::Game).active;
        Publish(paused, foreground, demand.uiWantTextInput, gameTargetReady);
    }

    void PlayModeController::Publish(bool paused, bool foreground, bool uiTextInput, bool gameTargetReady)
    {
        SceneManager& sceneManager = *SceneManagers;
        const InputOwner owner = InputManagement->IsGameInputOwned() ? InputOwner::Game : InputOwner::Editor;
        publishedState.store(m_state, std::memory_order_release);
        publishedOwner.store(owner, std::memory_order_release);

        std::lock_guard lock(statusMutex);
        published.state = m_state;
        published.owner = owner;
        published.requested = sceneManager.IsGameStart();
        published.committed = sceneManager.IsPlayCommitted();
        published.pending = sceneManager.HasPendingSceneStructureChange();
        published.paused = paused;
        published.foreground = foreground;
        published.foregroundOverride = foregroundOverride.load(std::memory_order_acquire);
        published.uiTextInput = uiTextInput;
        published.viewportGame = viewport_mode::game ==
            (m_modeRequest ? *m_modeRequest : ::editor::windows::get_viewport_mode());
        published.gameTargetReady = gameTargetReady;
        published.cursorHidden = InputManagement->IsCursorHidden();
        published.cursorHideRequested = InputManagement->IsCursorHideRequested();
        published.failureCount = sceneManager.PlayFailureCount();
        published.lastFailure = sceneManager.LastPlayFailure();
        ++published.ticks;
    }
}
