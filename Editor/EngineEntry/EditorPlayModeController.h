#pragma once
#include "Delegate.h"
// Editor의 play-mode 정책 소유자 (E3-2).
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

namespace Editor
{
    class PlayModeController
    {
    public:
        // SceneManager::PlayModeEvent를 구독한다. 씬이 만들어지기 전에 불러도 된다.
        void Initialize();
        void Shutdown();

    private:
        Core::DelegateHandle m_handle{};
    };
}
