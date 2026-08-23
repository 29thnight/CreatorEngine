#include "RuntimeFrame.h"
#include "SceneManager.h"
#include "TimeSystem.h"
#include "ClrHost.h"

namespace
{
    // 물리 스텝 **앞**의 관리 틱 (설계 문서 §4 트랙 L5).
    //
    // 예전에는 관리 측 틱이 전부 물리 뒤에 있었다 — 프레임 루프가
    // Physics → GameLogic → 관리 틱 순이고 그 안에서 Update/LateUpdate를
    // 연속으로 넘겼다. 그래서 "이번 프레임의 물리에 영향을 주는" 자리가 아예 없었다.
    //
    // 크로싱이 프레임당 하나 는다. 규약은 "스크립트가 몇 개든 프레임당 통과 횟수는
    // 고정"이지 1회가 아니므로 이 분할은 규약 안이다.
    void TickManagedPrePhysics(float deltaSeconds)
    {
        auto& clr = ClrHost::Get();
        if (!clr.IsReady()) return;

        clr.TickPrePhysics(deltaSeconds);
    }

    // 물리 뒤의 관리 틱. 경계는 여기가 전부다 — 스크립트가 몇 개든 프레임당 통과
    // 횟수는 고정이고 순회는 관리 영역에서 끝난다(설계 문서 02절).
    // 게임 스레드에서만 부른다 — CoreCLR GC가 스레드를 정지시키기 때문이다.
    void TickManagedPostPhysics(float deltaSeconds)
    {
        auto& clr = ClrHost::Get();
        if (!clr.IsReady()) return;

        // 물리·GameLogic이 만들거나 없앤 스크립트를 관리 측 활성 목록에 반영한다.
        // 아래 플러시들이 그 최신 목록으로 배달되어야 하므로 여기가 자리다.
        clr.FlushRegistrations();

        // 물리에서 모인 충돌 이벤트를 Update 전에 흘려보낸다.
        // 발생 시점에 바로 부르지 않는 이유는 설계 문서 02절 참고.
        clr.FlushPhysicsEvents();

        // 애니메이션 상태 전이도 같은 규약이다. 상태 머신이 이번 프레임에 쌓아 둔
        // Enter/Update/Exit를 발생 순서 그대로 넘긴다.
        clr.FlushAniEvents();

        // 이름으로 부르는 콜백(애니메이션 키프레임 이벤트·입력 액션).
        // Update 전에 흘려보내는 이유는 물리와 같다 — 스크립트가 이번 프레임
        // Update에서 그 결과를 보고 판단할 수 있어야 한다.
        clr.FlushScriptMessages();

        // AI 잡 스레드가 이번 프레임에 담아 둔 트리 틱을 흘려보낸다(PHASE 9-8).
        //
        // 트리가 몇 개든 경계 통과는 한 번이고, 트리 안의 노드 순회는 전부 관리
        // 측에서 끝난다 — BT의 틱이 동기 재귀라 노드 단위로 넘기면 그 규약이 무너진다.
        clr.FlushAITicks();

        clr.TickPostPhysics(deltaSeconds);
    }
}

namespace Runtime
{
    double ResolveFrameDelta()
    {
        if (SceneManagers->IsGamePaused()) return 0.0;
        return Time->GetElapsedSeconds();
    }

    void TickSimulationFrame(float deltaSeconds)
    {
        SceneManagers->Initialization();
        SceneManagers->InputEvents(deltaSeconds);

        if (SceneManagers->IsGamePaused())
        {
            SceneManagers->Pausing();
            return;
        }

        // 앞뒤 관리 틱은 같은 가드를 쓴다 — 헤더 주석 참고.
        if (!SceneManagers->HasPendingSceneStructureChange())
        {
            TickManagedPrePhysics(deltaSeconds);
        }

        SceneManagers->Physics(deltaSeconds);
        SceneManagers->GameLogic(deltaSeconds);

        if (!SceneManagers->HasPendingSceneStructureChange())
        {
            TickManagedPostPhysics(deltaSeconds);
        }
    }
}
