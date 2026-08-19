using System.Runtime.InteropServices;

namespace CreatorEngine;

/// <summary>
/// 네이티브 호스트가 함수 포인터로 직접 호출하는 진입점 모음.
///
/// 여기 있는 것이 경계의 전부다. 스크립트가 몇 개든 프레임당 통과 횟수는 고정이다
/// (설계 문서 02절 "틱당 한 번 진입 → C# 내부에서 순회").
///
/// 모든 진입점은 예외를 밖으로 내보내지 않는다 — 관리 예외가 네이티브 프레임을 통과하면
/// 프로세스가 그대로 죽는다.
/// </summary>
public static class Bootstrap
{
    /// <summary>엔진 API 표를 받아 초기화한다. 0이면 성공.</summary>
    [UnmanagedCallersOnly]
    public static unsafe int Initialize(nint apiTable)
    {
        try
        {
            if (!Native.Bind((ScriptApiTable*)apiTable))
                return -1;  // 버전/크기 불일치 — 네이티브와 C# 표가 어긋났다

            ScriptFactory.Initialize();
            Native.Log(1, "[ScriptCore] 초기화 완료");
            return 0;
        }
        catch
        {
            return -2;
        }
    }

    [UnmanagedCallersOnly]
    public static int Shutdown()
    {
        try
        {
            BehaviourRegistry.Clear();
            return 0;
        }
        catch { return -1; }
    }

    // ── 틱 진입점 ──
    // 반환값은 현재 활성 스크립트 수 (호스트가 경계 로그에 남긴다 — 설계 문서 10.1)

    [UnmanagedCallersOnly]
    public static int Awake()
    {
        try { BehaviourRegistry.Awake(); return BehaviourRegistry.ActiveCount; }
        catch (Exception ex) { Report(ex, nameof(Awake)); return -1; }
    }

    [UnmanagedCallersOnly]
    public static int FixedUpdate(float dt)
    {
        try { BehaviourRegistry.FixedUpdate(dt); return BehaviourRegistry.ActiveCount; }
        catch (Exception ex) { Report(ex, nameof(FixedUpdate)); return -1; }
    }

    [UnmanagedCallersOnly]
    public static int Update(float dt)
    {
        try { BehaviourRegistry.Update(dt); return BehaviourRegistry.ActiveCount; }
        catch (Exception ex) { Report(ex, nameof(Update)); return -1; }
    }

    [UnmanagedCallersOnly]
    public static int LateUpdate(float dt)
    {
        try { BehaviourRegistry.LateUpdate(dt); return BehaviourRegistry.ActiveCount; }
        catch (Exception ex) { Report(ex, nameof(LateUpdate)); return -1; }
    }

    /// <summary>
    /// 한 프레임에 모인 물리 이벤트를 일괄 전달한다.
    ///
    /// 배열 포인터 하나만 넘기므로 충돌이 몇 건이든 경계 통과는 한 번이다.
    /// 버퍼는 네이티브 소유이고 이 호출이 끝나면 무효가 되므로, 관리 측에서 붙들지 않는다.
    /// </summary>
    // PhysicsEvent는 내부 표현이라 이 진입점도 internal로 둔다.
    // UnmanagedCallersOnly는 접근성과 무관하게 함수 포인터로 노출된다.
    [UnmanagedCallersOnly]
    internal static unsafe int FlushPhysicsEvents(PhysicsEvent* events, int count)
    {
        try
        {
            if (events == null || count <= 0) return 0;

            for (int i = 0; i < count; ++i)
            {
                ref readonly PhysicsEvent e = ref events[i];

                Behaviour? target = ScriptFactory.Find(e.InstanceId);
                if (target is null || !target.IsAlive || !target.Enabled) continue;

                BehaviourRegistry.DispatchPhysics(target, (PhysicsEventKind)e.Kind, in e.Collision);
            }
            return count;
        }
        catch (Exception ex) { Report(ex, nameof(FlushPhysicsEvents)); return -1; }
    }

    /// <summary>
    /// 이름으로 부르는 콜백 배치를 전달한다(애니메이션 키프레임 이벤트·입력 액션).
    /// 물리 이벤트와 같은 규약 — 발생 시점이 아니라 틱 경계에서 한 번에 넘어온다.
    /// </summary>
    [UnmanagedCallersOnly]
    internal static unsafe int FlushScriptMessages(ScriptMessage* messages, int count)
    {
        try
        {
            if (messages == null || count <= 0) return 0;

            for (int i = 0; i < count; ++i)
            {
                ScriptMessage* message = &messages[i];

                Behaviour? target = ScriptFactory.Find(message->InstanceId);
                if (target is null || !target.IsAlive || !target.Enabled) continue;

                BehaviourRegistry.DispatchMessage(target, ScriptMessage.ReadName(message));
            }
            return count;
        }
        catch (Exception ex) { Report(ex, nameof(FlushScriptMessages)); return -1; }
    }

    // ── 애니메이션 상태 스크립트 ──
    //
    // 상태 머신이 수명을 쥐고 있으므로 생성·파괴를 네이티브가 직접 요청한다.
    // 콜백은 물리와 같이 틱 경계에서 한 번에 전달된다.

    [UnmanagedCallersOnly]
    public static unsafe int CreateAniBehaviour(byte* typeNameUtf8)
    {
        try
        {
            string typeName = Marshal.PtrToStringUTF8((nint)typeNameUtf8) ?? string.Empty;
            return AniBehaviourFactory.Create(typeName);
        }
        catch (Exception ex) { Report(ex, nameof(CreateAniBehaviour)); return -1; }
    }

    [UnmanagedCallersOnly]
    public static int DestroyAniBehaviour(int instanceId)
    {
        try { return AniBehaviourFactory.Destroy(instanceId) ? 0 : -1; }
        catch (Exception ex) { Report(ex, nameof(DestroyAniBehaviour)); return -1; }
    }

    /// <summary>이 이름의 애니메이션 스크립트가 C#에 있는지. 네이티브 팩토리와 이름이 겹치지 않게 확인용.</summary>
    [UnmanagedCallersOnly]
    public static unsafe int HasAniBehaviour(byte* typeNameUtf8)
    {
        try
        {
            string typeName = Marshal.PtrToStringUTF8((nint)typeNameUtf8) ?? string.Empty;
            return AniBehaviourFactory.Exists(typeName) ? 1 : 0;
        }
        catch (Exception ex) { Report(ex, nameof(HasAniBehaviour)); return 0; }
    }

    [UnmanagedCallersOnly]
    internal static unsafe int FlushAniEvents(AniEvent* events, int count)
    {
        try
        {
            if (events == null || count <= 0) return 0;

            for (int i = 0; i < count; ++i)
            {
                AniBehaviourFactory.Dispatch(in events[i]);
            }
            return count;
        }
        catch (Exception ex) { Report(ex, nameof(FlushAniEvents)); return -1; }
    }

    [UnmanagedCallersOnly]
    public static int OnSceneUnload()
    {
        // ⚠ 여기서 Clear를 부르면 안 된다 — DontDestroyOnLoad 오브젝트의 스크립트와
        // 트리까지 죽는다. Scene::AllDestroyMark가 DDOL을 건너뛰므로 그것들은 파괴
        // 표시조차 되지 않은 채 목록에 남아 있고, Clear는 목록을 통째로 돈다.
        // 오브젝트는 살아서 다음 씬으로 넘어가는데 스크립트만 죽는 셈이다.
        //
        // 정상 정리는 컴포넌트의 OnDestroy가 이미 한다(실측: 씬 왕복 2회에 트리·스크립트
        // 모두 기준선 복귀). 그래서 이 진입점은 청소가 아니라 <b>그물</b>이다 —
        // 그 경로를 타지 못한 것이 있으면 거두고, 있었다는 사실을 남긴다.
        // 오늘 기준으로는 0건이어야 하고, 0이 아니면 수명 배선에 구멍이 생긴 것이다.
        try
        {
            int behaviours = BehaviourRegistry.SweepOrphans();
            int trees = BehaviorTreeRegistry.SweepOrphans();

            // 0건이면 조용히 넘어간다. 매 씬 전환마다 '0건'을 찍으면 로그가 흐려지고,
            // 그러면 정작 0이 아닌 날에 눈에 띄지 않는다.
            if (behaviours > 0 || trees > 0)
            {
                Native.Log(2, $"[씬 언로드] 정상 경로를 타지 못한 인스턴스를 거뒀다 — " +
                              $"스크립트 {behaviours}개 · 트리 {trees}개. " +
                              "컴포넌트 OnDestroy가 돌지 않는 파괴 경로가 생겼다는 뜻이다.");
            }
            return 0;
        }
        catch (Exception ex) { Report(ex, nameof(OnSceneUnload)); return -1; }
    }

    // ── 인스턴스 생성/파괴 ──

    /// <summary>
    /// 타입 이름으로 스크립트를 만들어 오브젝트에 붙인다.
    /// 실패하면 음수를 돌려준다(0 이상은 생성된 인스턴스 id).
    /// </summary>
    [UnmanagedCallersOnly]
    public static unsafe int CreateBehaviour(ObjectHandle owner, byte* typeNameUtf8)
    {
        try
        {
            string typeName = Marshal.PtrToStringUTF8((nint)typeNameUtf8) ?? string.Empty;
            return ScriptFactory.Create(owner, typeName);
        }
        catch (Exception ex) { Report(ex, nameof(CreateBehaviour)); return -1; }
    }

    /// <summary>
    /// 방금 만들어진 스크립트를 즉시 깨운다.
    ///
    /// 프리팹을 스폰한 직후 네이티브가 부른다. 이것이 없으면 Awake가 다음 틱으로 밀려,
    /// 스폰 직후 대상을 초기화하는 흔한 패턴이 한 프레임 늦게 동작한다.
    /// </summary>
    [UnmanagedCallersOnly]
    public static int FlushPendingAwake()
    {
        try { BehaviourRegistry.AwakeNewlyCreated(); return 0; }
        catch (Exception ex) { Report(ex, nameof(FlushPendingAwake)); return -1; }
    }

    // ── 행동 트리 (PHASE 9-8) ──
    //
    // 그래프가 배열 하나로 건너온다. 노드가 몇 개든 경계 통과는 한 번이다 —
    // BT의 틱은 동기 재귀라 노드 단위로 넘기면 그 규약이 무너진다(설계 문서 §1).

    /// <summary>저작 그래프로 트리를 세운다. 0 이상이면 인스턴스 id, 음수면 실패.</summary>
    [UnmanagedCallersOnly]
    internal static unsafe int CreateBehaviorTree(ObjectHandle owner, BTNodeDesc* nodes, int count,
        BBEntry* entries, int entryCount)
    {
        try { return BehaviorTreeRegistry.Create(owner, nodes, count, entries, entryCount); }
        catch (Exception ex) { Report(ex, nameof(CreateBehaviorTree)); return -1; }
    }

    /// <summary>
    /// 한 프레임에 모인 트리 틱을 일괄 실행한다.
    ///
    /// 트리가 몇 개든 경계 통과는 한 번이고, 트리 안의 노드 순회는 전부 여기서 끝난다 —
    /// 그것이 트리째 관리 측으로 옮긴 이유다(설계 문서 §1).
    /// </summary>
    [UnmanagedCallersOnly]
    internal static unsafe int FlushAITicks(AITick* ticks, int count)
    {
        try
        {
            if (ticks == null || count <= 0) return 0;

            for (int i = 0; i < count; ++i)
            {
                BehaviorTreeRegistry.Tick(ticks[i].InstanceId, ticks[i].DeltaTime);
            }
            return count;
        }
        catch (Exception ex) { Report(ex, nameof(FlushAITicks)); return -1; }
    }

    [UnmanagedCallersOnly]
    public static int DestroyBehaviorTree(int instanceId)
    {
        try { return BehaviorTreeRegistry.Destroy(instanceId) ? 0 : -1; }
        catch (Exception ex) { Report(ex, nameof(DestroyBehaviorTree)); return -1; }
    }

    /// <summary>BT 진단 지표를 채운다. 0이면 성공. 진단 경로라 틱 규약과 무관하다.</summary>
    [UnmanagedCallersOnly]
    public static unsafe int GetBTStats(BTStats* outStats)
    {
        try
        {
            if (outStats == null) return -1;
            *outStats = BehaviorTreeRegistry.GetStats();
            return 0;
        }
        catch (Exception ex) { Report(ex, nameof(GetBTStats)); return -1; }
    }

    /// <summary>BT 누계를 0으로 되돌린다. 트리는 건드리지 않는다.</summary>
    [UnmanagedCallersOnly]
    public static int ResetBTStats()
    {
        try { BehaviorTreeRegistry.ResetStats(); return 0; }
        catch (Exception ex) { Report(ex, nameof(ResetBTStats)); return -1; }
    }

    [UnmanagedCallersOnly]
    public static int DestroyBehaviour(int instanceId)
    {
        try { return ScriptFactory.Destroy(instanceId) ? 0 : -1; }
        catch (Exception ex) { Report(ex, nameof(DestroyBehaviour)); return -1; }
    }

    /// <summary>
    /// 네이티브가 생명주기 단계 하나를 인스턴스 하나에 직접 전달한다
    /// (설계 문서 §4 트랙 L · L3 잔여). phase는 BehaviourRegistry.LifecyclePhase 값이고
    /// 네이티브 ScriptBinder/ScriptLifecyclePhase.h와 값이 같아야 한다.
    /// </summary>
    [UnmanagedCallersOnly]
    public static int DispatchLifecycle(int instanceId, int phase)
    {
        try { return BehaviourRegistry.DispatchLifecycle(instanceId, phase) ? 0 : -1; }
        catch (Exception ex) { Report(ex, nameof(DispatchLifecycle)); return -1; }
    }

    // ── 스크립트 어셈블리 로드·핫리로드 ──
    //
    // ScriptCore(이 어셈블리)는 기본 컨텍스트에 상주한다. 네이티브가 붙잡은 진입점이
    // 여기 있으므로 언로드 대상이 아니다. 교체되는 것은 게임 스크립트 어셈블리뿐이다.

    /// <summary>스크립트 어셈블리를 올린다. 0이면 성공.</summary>
    [UnmanagedCallersOnly]
    public static unsafe int LoadScripts(byte* assemblyPathUtf8)
    {
        try
        {
            string path = Marshal.PtrToStringUTF8((nint)assemblyPathUtf8) ?? string.Empty;
            if (!ScriptAssemblyLoader.Load(path)) return -1;

            Native.Log(1, $"[ScriptCore] 스크립트 {ScriptFactory.RegisteredCount}종 등록 (세대 {ScriptAssemblyLoader.Generation})");
            return 0;
        }
        catch (Exception ex) { Report(ex, nameof(LoadScripts)); return -2; }
    }

    /// <summary>
    /// 스크립트 어셈블리를 내렸다 다시 올린다.
    /// 살아 있던 인스턴스는 전부 사라지므로, 네이티브가 ScriptComponent들을 다시 깨워
    /// 인스턴스를 만들고 저장해 둔 필드 값을 되돌려야 한다.
    /// </summary>
    [UnmanagedCallersOnly]
    public static int ReloadScripts()
    {
        try
        {
            if (!ScriptAssemblyLoader.Reload()) return -1;

            Native.Log(1, $"[ScriptCore] 리로드 완료 — 스크립트 {ScriptFactory.RegisteredCount}종 (세대 {ScriptAssemblyLoader.Generation})");
            return 0;
        }
        catch (Exception ex) { Report(ex, nameof(ReloadScripts)); return -2; }
    }

    /// <summary>진단용 — 이전 컨텍스트가 아직 살아 있으면 1(참조 누수 신호).</summary>
    [UnmanagedCallersOnly]
    public static int IsPreviousContextAlive()
    {
        try { return ScriptAssemblyLoader.IsPreviousContextAlive() ? 1 : 0; }
        catch { return 0; }
    }

    // ── 노출 필드 (인스펙터·직렬화) ──
    //
    // 소스 제너레이터가 만든 접근자를 그대로 경계에 내보낸다.
    // 인스펙터가 그리든 직렬화가 저장하든 같은 통로를 쓴다.

    [UnmanagedCallersOnly]
    public static int GetFieldCount(int instanceId)
    {
        try { return ScriptFactory.Find(instanceId)?.FieldCount ?? 0; }
        catch { return 0; }
    }

    /// <summary>필드 이름을 UTF-8로 채우고 바이트 수를 돌려준다.</summary>
    [UnmanagedCallersOnly]
    public static unsafe int GetFieldName(int instanceId, int index, byte* buffer, int capacity)
    {
        try
        {
            if (buffer == null || capacity <= 0) return 0;

            string name = ScriptFactory.Find(instanceId)?.GetFieldName(index) ?? string.Empty;
            if (name.Length == 0) return 0;

            return System.Text.Encoding.UTF8.GetBytes(name, new Span<byte>(buffer, capacity));
        }
        catch { return 0; }
    }

    [UnmanagedCallersOnly]
    public static int GetFieldType(int instanceId, int index)
    {
        try { return (int)(ScriptFactory.Find(instanceId)?.GetFieldType(index) ?? FieldType.Unknown); }
        catch { return 0; }
    }

    /// <summary>
    /// 등록된 스크립트 타입 이름을 '\n'으로 이어 buffer에 담는다. 반환값은 기록한 바이트 수.
    /// 에디터의 컴포넌트 추가 메뉴가 C# 스크립트 목록을 그릴 때 쓴다.
    /// </summary>
    [UnmanagedCallersOnly]
    public static unsafe int GetBehaviourTypeNames(byte* buffer, int capacity)
    {
        try
        {
            if (buffer == null || capacity <= 0) return 0;

            string joined = string.Join('\n', ScriptFactory.RegisteredTypeNames);
            if (joined.Length == 0) return 0;

            return System.Text.Encoding.UTF8.GetBytes(joined, new Span<byte>(buffer, capacity));
        }
        catch { return 0; }
    }

    /// <summary>등록된 애니메이션 상태 스크립트 이름 목록. 애니메이터 편집기가 쓴다.</summary>
    [UnmanagedCallersOnly]
    public static unsafe int GetAniBehaviourTypeNames(byte* buffer, int capacity)
    {
        try
        {
            if (buffer == null || capacity <= 0) return 0;

            string joined = string.Join('\n', AniBehaviourFactory.RegisteredTypeNames);
            if (joined.Length == 0) return 0;

            return System.Text.Encoding.UTF8.GetBytes(joined, new Span<byte>(buffer, capacity));
        }
        catch { return 0; }
    }

    /// <summary>
    /// 갈래별 BT 노드 타입 이름을 '\n'으로 이어 담는다. BT 편집기의 선택 목록용.
    ///
    /// 갈래를 인자로 받는 이유는 편집기가 Action·Condition·ConditionDecorator를
    /// 각각 다른 메뉴로 보여 주기 때문이다. 세 목록을 따로 부르면 경계를 세 번
    /// 넘지만, 이건 편집기가 메뉴를 열 때만 도는 경로라 틱 규약과 무관하다.
    /// </summary>
    /// <summary>
    /// 이 이름의 BT 노드가 등록돼 있는가. 있으면 1.
    ///
    /// 편집기가 노드를 그릴 때 노드마다 부른다 — 목록을 받아 훑으면 프레임마다
    /// 문자열 수십 개를 만들게 되므로, 판정만 넘긴다.
    /// </summary>
    [UnmanagedCallersOnly]
    public static unsafe int HasBTNodeType(byte* typeNameUtf8)
    {
        try
        {
            string name = Marshal.PtrToStringUTF8((nint)typeNameUtf8) ?? string.Empty;
            return BTNodeFactory.Exists(name) ? 1 : 0;
        }
        catch { return 0; }
    }

    [UnmanagedCallersOnly]
    public static unsafe int GetBTNodeTypeNames(int kind, byte* buffer, int capacity)
    {
        try
        {
            if (buffer == null || capacity <= 0) return 0;

            string joined = string.Join('\n', BTNodeFactory.TypeNames((BTNodeKind)kind));
            if (joined.Length == 0) return 0;

            return System.Text.Encoding.UTF8.GetBytes(joined, new Span<byte>(buffer, capacity));
        }
        catch { return 0; }
    }

    [UnmanagedCallersOnly]
    public static float GetFieldFloat(int instanceId, int index)
    {
        try { return ScriptFactory.Find(instanceId)?.GetFloat(index) ?? 0f; }
        catch { return 0f; }
    }

    [UnmanagedCallersOnly]
    public static void SetFieldFloat(int instanceId, int index, float value)
    {
        try { ScriptFactory.Find(instanceId)?.SetFloat(index, value); }
        catch { /* 인스펙터 입력이 스크립트를 죽이지 않게 한다 */ }
    }

    [UnmanagedCallersOnly]
    public static int GetFieldInt32(int instanceId, int index)
    {
        try { return ScriptFactory.Find(instanceId)?.GetInt32(index) ?? 0; }
        catch { return 0; }
    }

    [UnmanagedCallersOnly]
    public static void SetFieldInt32(int instanceId, int index, int value)
    {
        try { ScriptFactory.Find(instanceId)?.SetInt32(index, value); }
        catch { }
    }

    [UnmanagedCallersOnly]
    public static int GetFieldBool(int instanceId, int index)
    {
        try { return (ScriptFactory.Find(instanceId)?.GetBool(index) ?? false) ? 1 : 0; }
        catch { return 0; }
    }

    [UnmanagedCallersOnly]
    public static void SetFieldBool(int instanceId, int index, int value)
    {
        try { ScriptFactory.Find(instanceId)?.SetBool(index, value != 0); }
        catch { }
    }

    [UnmanagedCallersOnly]
    public static unsafe int GetFieldString(int instanceId, int index, byte* buffer, int capacity)
    {
        try
        {
            if (buffer == null || capacity <= 0) return 0;

            string value = ScriptFactory.Find(instanceId)?.GetString(index) ?? string.Empty;
            if (value.Length == 0) return 0;

            return System.Text.Encoding.UTF8.GetBytes(value, new Span<byte>(buffer, capacity));
        }
        catch { return 0; }
    }

    [UnmanagedCallersOnly]
    public static unsafe void SetFieldString(int instanceId, int index, byte* valueUtf8)
    {
        try { ScriptFactory.Find(instanceId)?.SetString(index, Marshal.PtrToStringUTF8((nint)valueUtf8) ?? string.Empty); }
        catch { }
    }

    [UnmanagedCallersOnly]
    public static Float2 GetFieldFloat2(int instanceId, int index)
    {
        try { return ScriptFactory.Find(instanceId)?.GetFloat2(index) ?? default; }
        catch { return default; }
    }

    [UnmanagedCallersOnly]
    public static void SetFieldFloat2(int instanceId, int index, Float2 value)
    {
        try { ScriptFactory.Find(instanceId)?.SetFloat2(index, value); }
        catch { }
    }

    [UnmanagedCallersOnly]
    public static Float3 GetFieldFloat3(int instanceId, int index)
    {
        try { return ScriptFactory.Find(instanceId)?.GetFloat3(index) ?? default; }
        catch { return default; }
    }

    [UnmanagedCallersOnly]
    public static void SetFieldFloat3(int instanceId, int index, Float3 value)
    {
        try { ScriptFactory.Find(instanceId)?.SetFloat3(index, value); }
        catch { }
    }

    /// <summary>오브젝트 참조를 핸들로 돌려준다. 파일에 적을 때는 네이티브가 instanceID로 바꾼다.</summary>
    [UnmanagedCallersOnly]
    public static ObjectHandle GetFieldObject(int instanceId, int index)
    {
        try { return ScriptFactory.Find(instanceId)?.GetObject(index).Handle ?? ObjectHandle.Invalid; }
        catch { return ObjectHandle.Invalid; }
    }

    [UnmanagedCallersOnly]
    public static void SetFieldObject(int instanceId, int index, ObjectHandle value)
    {
        try { ScriptFactory.Find(instanceId)?.SetObject(index, new GameObject(value)); }
        catch { }
    }

    private static void Report(Exception ex, string phase)
    {
        // Native.Log 자체가 실패할 수 있으므로(초기화 전 등) 여기서도 예외를 삼킨다.
        try { Native.Log(3, $"[ScriptCore] {phase} 처리 중 예외\n{ex}"); }
        catch { /* 로그조차 불가능한 상황 — 조용히 넘긴다 */ }
    }
}

