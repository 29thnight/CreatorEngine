namespace CreatorEngine.Scripts;

/// <summary>
/// LC0 픽스처 — <b>틱 순회 한복판에서 자기를 제거한다.</b>
///
/// <see cref="SelfDestroyInBeginProbe"/>의 짝이다. 자리만 다르다 — 이쪽은
/// <c>ScriptRegistry.PostPhysicsTick</c>이 <c>_active</c>를 인덱스로 돌고 있는
/// 도중이다.
///
/// ── 무엇이 깨질 수 있나 ──
///
/// 순회는 <c>for (int i = 0; i &lt; _active.Count; ++i)</c>다. 제거가 그 자리에서
/// 목록을 줄이면 다음 인스턴스를 건너뛰거나 범위를 벗어난다. 지금 구조는 그것을
/// 보류 큐로 막고 있고(<c>Remove</c>는 <c>_pendingRemove</c>에 넣기만 한다),
/// 반영은 순회가 끝난 뒤 <c>Flush</c>가 한다.
///
/// 그 보호가 실제로 도는지를 이 픽스처가 잰다. 대조군이 같은 프레임에 훅을 잃지
/// 않는 것이 그 증거다 — 건너뛰기가 일어나면 이웃 하나가 그 프레임의 틱을 통째로
/// 못 받는다.
///
/// ── 두 번째 틱을 기다리는 이유 ──
///
/// 첫 틱에 바로 지우면 <c>_active</c>에 갓 편입된 프레임과 겹쳐 무엇이 원인인지
/// 흐려진다. 몇 프레임 정상으로 돈 뒤 지우면 "정상 순회 중의 제거"만 남는다.
/// </summary>
public sealed partial class SelfDestroyInTickProbe : Component
{
    private const string Kind = "tick";

    private void Mark(string point)
        => Log($"[LC0] kind={Kind} point={point} frame={FrameCount} tid={Environment.CurrentManagedThreadId}");

    public override void OnInitialized()       => Mark("init");
    public override void OnAddedToScene()      => Mark("added");
    public override void OnEnable()            => Mark("enable");
    public override void OnBeginSimulation()   => Mark("beginhook");
    public override void OnDisable()           => Mark("disable");
    public override void OnEndSimulation()     => Mark("end");
    public override void OnRemovingFromScene() => Mark("removing");
    public override void OnUninitializing()    => Mark("uninit");

    private int _ticks;

    public override void PostPhysics(float tick)
    {
        ++_ticks;

        if (_ticks == 3)
        {
            Mark("destroying");
            Entity.Destroy();
            Mark("destroyed");
            return;
        }

        // 제거 요청 뒤에도 틱을 받는지. 몇 개만 남긴다.
        if (_ticks > 3 && _ticks <= 6) Mark("tick");
    }
}
