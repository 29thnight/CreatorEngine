#pragma once
#include "Delegate.h"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
// Editor의 play-mode 정책 소유자 (E3-2 · PHASE 21 W5).
//
// 재생 진입/이탈에서 **에디터만** 신경 쓰는 일을 여기서 한다. Core의
// SceneManager는 씬 스냅샷과 시뮬레이션 phase 전이라는 런타임 primitive만 하고,
// 그 앞뒤로 SceneManager::PlayModeEvent를 던져 이 컨트롤러가 걸릴 자리를 준다.
//
// 왜 포트가 아니라 델리게이트인가
// ─────────────────────────────
// E2의 AssetAuthoringPort는 Core가 Editor의 **결과값**을 받아야 해서(파일이
// 실제로 써졌는지) 반환값 있는 함수 포인터가 필요했다. 여기는 Core가 Editor의
// 응답을 쓰지 않는 단방향 통지라, 이미 SceneManager에 선언돼 있던 PlayModeEvent를
// 쓴다. 그 이벤트는 선언만 있고 Broadcast도 구독자도 0건인 죽은 코드였다.
//
// Player는 무엇도 설치하지 않는다
// ─────────────────────────────
// Player는 이 파일을 링크하지 않는다(EngineEntry는 Editor 전용). 구독자가 없으면
// Broadcast는 아무 일도 하지 않으므로 Player의 재생 진입은 순수 런타임 동작만
// 남는다. 그것이 이 슬라이스의 목적이다 — 출하 게임이 Undo 이력을 비우지 않는다.
//
// ⚠ 여기서 던진 예외는 Core로 전파되지 않는다
// ──────────────────────────────────────────
// Core::Delegate::Broadcast는 콜백마다 try/catch로 감싸 예외를 로그로 흘리고 다음
// 콜백으로 넘어간다(Delegate.inl). 옛 코드는 이 일을 BeginPlayTransaction의 try
// 안에서 직접 했으므로 던지면 재생 진입 자체가 중단됐다 — 지금은 중단되지 않는다.
// Undo 폐기는 편의 기능이라 실패해도 재생을 막을 이유가 없어 그대로 두지만,
// **씬 무결성에 관한 일을 여기에 추가하면 안 된다.** 실패가 조용히 지나간다.
//
// ⚠ 선택 해제는 여기로 오지 않는다
// ──────────────────────────────
// EndPlayTransaction의 resetSelectedObjectEvent.Broadcast()는 이름과 달리 Editor
// 정책이 아니라 **댕글링 방지 안전장치**다. 그 한 줄을 빼고 재생 왕복을 태우면
// 에디터가 ACCESS_VIOLATION으로 죽는다(실측) — 정지 후 Scene::m_selectedEntity가
// 파괴된 엔티티를 가리키기 때문이다. AllDestroyMark 이전이라는 위치가 그 안전을
// 만든다. 여기로 옮기면 순서가 뒤로 밀려 재생 정지마다 죽는다. Core에 둔다.
// **복원**은 다른 일이다 — 해제된 뒤(Stopped 에 닿은 프레임) 재생 전에 기억해 둔
// 선택을 instanceID 로 되찾아 다시 고른다(아래 상태 머신).
//
// ── W5 상태 머신 (계획서 §6.2) ───────────────────────────────────────────
//
//   Stopped ──요청──▶ Entering ──확정──▶ PlayingPossessed ◀──모드──▶ PlayingEjected
//      ▲                 │ 실패·취소            │                          │
//      └─────────────────┘                      └──── 정지 요청 ───▶ Exiting ┘
//      ◀────────────── 복원(prior 모드·선택·포커스) ────────────────────┘
//
// 상태는 **관찰로 유도**한다 — 버튼 클릭이나 진입 통지 하나로 확정하지 않는다.
// SceneManager 가 W5 선행 1 로 낸 신호 셋(요청·진행·확정)과 ViewportHost 의
// 게시본(모드·UI 입력 상태)만 읽는다. Possessed 와 Ejected 를 가르는 것은 Host 의
// 표시 모드다: Game 모드가 곧 possess, Scene 모드가 곧 eject. 별도 버튼이 없는
// 이유는 모드 막대가 이미 그 뜻이고, 승인된 외관을 바꾸지 않기 위해서다.
//
// 입력 소유자는 프레임마다 하나다(InputManager 의 소유 관문):
//
//   game  ⇔ PlayingPossessed ∧ ¬paused ∧ 창이 전경 ∧ ¬(UI 가 글자를 받는 중)
//   editor 그 밖의 전부
//
// 이 함수는 게임 스레드에서 입력 갱신 **뒤**, 씬 틱 **앞**에 돈다. 그래야
// 같은 프레임의 스크립트가 이번 프레임의 소유권을 본다.
namespace editor::windows { enum class viewport_mode : std::uint8_t; }

namespace Editor
{
    enum class PlayState : std::uint8_t
    {
        Stopped,
        Entering,           ///< 요청은 섰고 전이는 아직(스냅샷 전)
        PlayingPossessed,   ///< 확정 · Host 가 Game 모드 · 게임이 입력 후보
        PlayingEjected,     ///< 확정 · Host 가 Scene 모드 · 편집 도구가 입력
        Exiting,            ///< 정지 요청 뒤 복원까지
    };

    enum class InputOwner : std::uint8_t { Editor, Game };

    const char* PlayStateName(PlayState state) noexcept;
    const char* InputOwnerName(InputOwner owner) noexcept;

    /// 밖(CLI · 제목표시줄)이 읽는 사본. 게임 스레드가 Tick 마다 게시한다.
    struct PlayModeStatus
    {
        PlayState     state{ PlayState::Stopped };
        InputOwner    owner{ InputOwner::Editor };
        bool          requested{};        ///< SceneManager::IsGameStart
        bool          committed{};        ///< SceneManager::IsPlayCommitted
        bool          pending{};          ///< HasPendingSceneStructureChange
        bool          paused{};
        bool          foreground{};       ///< 에디터 창이 OS 전경인가(override 반영)
        int           foregroundOverride{ -1 }; ///< -1 auto · 0 off · 1 on (검증 손잡이)
        bool          uiTextInput{};      ///< ImGui 가 글자를 받는 중(게시본)
        bool          viewportGame{};     ///< Host 가 Game 모드인가(요청 반영)
        bool          gameTargetReady{};  ///< 게임 카메라가 있어 Game 타깃이 서는가
        bool          cursorHidden{};
        bool          cursorHideRequested{};
        std::uint32_t failureCount{};
        std::string   lastFailure;
        std::uint64_t ticks{};
    };

    class PlayModeController
    {
    public:
        // SceneManager::PlayModeEvent를 구독한다. 씬이 만들어지기 전에 불러도 된다.
        void Initialize();
        void Shutdown();

        /// 게임 스레드 · 입력 갱신 뒤 · 씬 틱 앞. 상태를 유도하고 소유권을 적용한다.
        void Tick();

        static PlayModeStatus Status();
        static PlayState  CurrentState() noexcept;
        static InputOwner CurrentOwner() noexcept;
        /// 검증 손잡이. 게이트는 창을 숨겨 띄우므로 OS 전경을 만들 수 없다 —
        /// 전경 조건을 강제로 켜고 꺼서 소유권 규칙 자체를 잰다.
        static void SetForegroundOverride(int mode) noexcept;

    private:
        bool ForegroundNow() const noexcept;
        void RememberPrior();
        void RestorePrior();
        void Publish(bool paused, bool foreground, bool uiTextInput, bool gameTargetReady);

        Core::DelegateHandle m_handle{};
        PlayState m_state{ PlayState::Stopped };
        /// Host 에 요청했고 게시본에 아직 보이지 않는 모드. 게시본은 한 프레임
        /// 늦으므로, 요청한 프레임에 옛 모드를 읽고 상태가 한 번 되돌아가는 것을
        /// 막는다.
        std::optional<::editor::windows::viewport_mode> m_modeRequest;
        std::uint64_t m_seenCanvasClicks{};

        // 재생 전의 문서·포커스·선택. Stop 이 되돌린다.
        ::editor::windows::viewport_mode m_priorMode{};
        bool m_priorHostFocused{};
        std::uint32_t m_priorSceneId{};
        std::vector<std::size_t> m_priorSelection;   ///< instanceID — 백업이 보존한다
        std::size_t m_priorPrimary{};
        bool m_hasPrior{};
    };
}
