namespace CreatorEngine.Scripts;

/// <summary>
/// LC5-c 픽스처 — <b>워커 몸통에서 엔진 API를 부르면.</b>
///
/// LC5-b는 <c>await</c> <b>뒤</b>를 닫았다. 재개가 프레임 경계로 돌아오므로 그
/// 뒤의 본문은 게임 스레드에 있다. 이 픽스처가 재는 것은 그 마셜링이 원리적으로
/// 닿지 못하는 자리다 — <c>Task.Run</c>의 <b>몸통 안</b>. 거기는 재개가 아니라
/// 워커 그 자체이고, 컨텍스트는 아무것도 해 주지 않는다.
///
/// <see cref="ExternalAwaitProbe"/>와 나란히 두면 두 자리가 갈린다:
///
///   await 뒤     — LC5-b가 게임 스레드로 되돌린다
///   워커 몸통    — 아무도 막지 않는다  ← 여기
///
/// ── 무엇을 재는가 ──
///
/// 같은 두 값을 게임 스레드에서 한 번, 워커에서 한 번 읽는다. 게임 스레드 쪽이
/// 정답이고 워커 쪽이 시험 대상이다. 값을 두 번 읽어 <b>맞대는</b> 이유는, 워커
/// 값만 보면 "빈 이름"이 거부인지 원래 이름이 없는 것인지 가릴 수 없기 때문이다.
///
/// ── 오늘의 값 ──
///
/// 워커가 게임 스레드와 <b>같은 값</b>을 받는다. 검사가 없어 호출이 그대로
/// 통과한다는 뜻이다 — <c>Entity.IsAlive</c>도 <c>Entity.Name</c>도 함수 포인터로
/// C++에 곧장 들어가 씬 그래프를 읽는다. 게임 스레드가 같은 순간에 그것을 고치고
/// 있어도 아무도 막지 않는다.
///
/// ── 기대 ──
///
/// 고침(LC5-c)이 착지하면 워커 쪽 호출이 <b>거부</b>되어 표가 없을 때와 같은 값
/// (<c>alive=False</c>, 빈 이름)을 받고, 그 사실이 로그에 남아야 한다. 거부만
/// 있고 기록이 없으면 조용히 틀린 값을 돌려주는 것이라 더 나쁘다.
/// </summary>
public sealed partial class WorkerCallProbe : Component
{
    private void Mark(string where, bool alive, string name)
        => Log($"[LC5c] where={where} tid={Environment.CurrentManagedThreadId} alive={alive} name='{name}'");

    public override async Task OnSimulate()
    {
        // 게임 스레드에서의 정답.
        Mark("game", Entity.IsAlive, Entity.Name);

        // 핸들만 복사해 워커로 넘긴다. Entity는 readonly struct라 사본이 같은
        // 네이티브 핸들을 가리킨다 — 워커가 부르는 것이 정확히 같은 호출이다.
        Entity self = Entity;

        await Task.Run(() => Mark("worker", self.IsAlive, self.Name));
    }
}
