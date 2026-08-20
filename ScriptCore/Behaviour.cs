namespace CreatorEngine;

/// <summary>
/// 네이티브 <c>ModuleBehavior</c>에 대응하는 스크립트 기반 클래스.
///
/// 이름은 Unity 관례를 그대로 따른다 — 기존 348개 스크립트를 옮길 때 구조를 바꾸지 않아도 되고,
/// 팀의 학습 비용도 없다. (설계 문서 02절)
///
/// 라이프사이클은 네이티브가 스크립트마다 부르지 않는다. 틱마다 한 번만 경계를 넘고,
/// 순회는 <see cref="BehaviourRegistry"/>가 관리 영역에서 수행한다.
/// </summary>
public abstract class Behaviour : Component
{
    /// <summary>이 스크립트가 붙어 있는 오브젝트.</summary>
    public new GameObject GameObject { get; internal set; }

    /// <summary>
    /// 자기 오브젝트의 Transform. 프로퍼티가 아니라 필드인 이유가 있다 —
    /// Transform은 핸들만 담은 struct라, 프로퍼티로 두면 <c>Transform.LocalPosition = ...</c>가
    /// 임시 복사본을 고치는 꼴이 되어 컴파일이 막힌다. 필드면 Unity와 같은 표기를 쓸 수 있고
    /// 매 접근마다 객체를 만들지도 않는다.
    /// </summary>
    public Transform Transform;

    private bool _enabled = true;

    /// <summary>
    /// 꺼진 스크립트는 순회에서 건너뛴다. 네이티브를 거치지 않으므로 호출 비용이 없다
    /// (실측에서 SetEnabled 호출이 148회였는데, 대상이 스크립트면 경계를 넘지 않는다).
    /// </summary>
    public bool Enabled
    {
        get => _enabled;
        set
        {
            if (_enabled == value) return;
            _enabled = value;

            if (value) OnEnable();
            else OnDisable();
        }
    }

    /// <summary>파괴 표시가 되었거나 대상 오브젝트가 사라졌으면 false.</summary>
    public bool IsAlive => !_destroyed && GameObject.IsAlive;

    private bool _destroyed;
    internal void MarkDestroyed() => _destroyed = true;

    /// <summary>
    /// 파괴 표시만 따로 본다. <see cref="IsAlive"/>는 소유자 생존까지 함께 보므로
    /// '표시는 됐는가'와 '소유자가 사라졌는가'를 가를 수 없다.
    ///
    /// 고아 청소(<see cref="BehaviourRegistry.SweepOrphans"/>)가 그 구분을 필요로 한다 —
    /// 정상 경로로 제거된 것은 Flush가 OnUninitializing(옛 OnDestroy)을 부를 예정이라
    /// 건드리면 두 번 불린다.
    /// </summary>
    internal bool IsMarkedDestroyed => _destroyed;

    /// <summary>
    /// OnInitialized가 실제로 불렸는지. 만들어지자마자 초기화 전에 파괴되는 경우가 있어서
    /// 둔다 — 재생을 시작하면 엔진이 에디터 씬 사본으로 갈아타면서 원본 쪽 인스턴스를
    /// 접는데, 그때 아직 한 번도 초기화되지 않은 인스턴스가 생긴다.
    ///
    /// "Initialized 없이 Uninitializing 없음"이 이 값이 지키는 계약이다(설계 문서
    /// SceneGraphRedesignPlan §4 트랙 L) — OnInitialized를 받은 적 없는 인스턴스는
    /// 나머지 다섯 훅도 받지 않는다. 짝이 맞지 않으면 스크립트가 초기화하지 않은 것을
    /// 정리하려 든다.
    /// </summary>
    internal bool IsInitialized { get; private set; }
    internal void MarkInitialized() => IsInitialized = true;

    /// <summary>
    /// 최초 씬 진입(OnAddedToScene)이 한 번 전달됐는지. 활성 축(OnEnable)을 그 뒤에
    /// 딱 한 번만 이어 붙이기 위해 둔다 — DontDestroyOnLoad 이송의 재부착도 같은
    /// 단계를 보내오므로, '최초인가'를 IsInitialized로는 가를 수 없다.
    /// </summary>
    internal bool EnterDelivered { get; private set; }
    internal void MarkEnterDelivered() => EnterDelivered = true;

    /// <summary>
    /// 축소 삼단(OnEndSimulation → OnRemovingFromScene → OnUninitializing)이 이미
    /// 네이티브 구동으로 전달됐는지 (설계 문서 §4 트랙 L · L3 완결).
    ///
    /// 단계별이 아니라 **묶음 하나**로 두는 이유: OnRemovingFromScene은 DontDestroyOnLoad
    /// 이송에서 **정상적으로 여러 번** 온다. 단계별 플래그로 막으면 두 번째 이송의
    /// 통지가 사라진다. 축소는 인스턴스당 한 번뿐이므로 그 시작(OnEndSimulation)에서
    /// 한 번 세우면 충분하다.
    ///
    /// 이 값이 서면 관리 측 TearDown은 훅을 건너뛴다 — 고아 청소·어셈블리 리로드는
    /// 구동할 네이티브 컴포넌트가 없어 그때만 TearDown이 직접 발화한다.
    /// </summary>
    internal bool TeardownDelivered { get; private set; }
    internal void MarkTeardownDelivered() => TeardownDelivered = true;

    // ── 씬 그래프 6단계 생명주기 (SceneGraphRedesignPlan §4 트랙 L) ──
    //
    // 기준점이 오브젝트가 아니라 컴포넌트다 — 옛 Awake는 "오브젝트가 태어남"이었지만
    // OnInitialized는 "이 컴포넌트가 초기화됨"이다. 네이티브 Component.h와 같은 매핑을
    // 쓴다. 디스패치는 BehaviourRegistry가 한다.
    //
    // ★ L3 — 옛 Awake/Start/OnDestroy와 그 브리지를 걷어냈다.
    //   전환기에는 기본 구현이 옛 훅을 불러 주었지만(`OnInitialized() => Awake()`),
    //   살아있는 소비자를 전부 새 이름으로 옮겼으므로 다리를 치운다.
    //   네이티브와 같은 회차에 같은 근거로 정리했다 — 자세한 것은 Component.h 주석.
    public virtual void OnInitialized() { }
    public virtual void OnAddedToScene() { }
    public virtual void OnBeginSimulation() { }
    public virtual void OnEndSimulation() { }
    public virtual void OnRemovingFromScene() { }
    public virtual void OnUninitializing() { }

    // ── 활성/비활성 축 (6단계와 직교) ──
    // 씬 페이즈와 무관하게 "지금 켜져 있는가"를 다룬다 — 대응물이 없어 남는다.
    public virtual void OnEnable() { }
    public virtual void OnDisable() { }

    // ── 틱 축 ──
    // ── 틱 축 (설계 문서 §4 트랙 L5) ──
    //
    // 옛 FixedUpdate/Update/LateUpdate 셋을 **물리 기준 두 지점**으로 대체했다.
    // 프레임에서 무엇이 언제 도는지가 이름에 드러나야 한다는 것이 요지다 —
    // "Update가 물리 앞인가 뒤인가"는 옛 이름으로는 알 수 없었고, 실제로는
    // 셋 다 물리 뒤였다(EditorMain의 프레임 루프).
    //
    // 은퇴 시점 실측: FixedUpdate 오버라이드 0곳 · Update 17곳 · LateUpdate 1곳,
    // 그리고 **Update와 LateUpdate를 함께 쓰는 파일 0개** — 둘을 PostPhysics 하나로
    // 합쳐도 잃는 순서가 없다.

    /// <summary>물리 스텝 **앞**. 이번 프레임의 물리에 영향을 주려면 여기서 한다.</summary>
    public virtual void PrePhysics(float tick) { }

    /// <summary>물리 스텝 **뒤**. 물리 결과를 보고 판단하는 대부분의 게임 로직 자리다.</summary>
    public virtual void PostPhysics(float tick) { }

    /// <summary>
    /// 시뮬레이션 본문 (설계 문서 §4 트랙 L5). <see cref="OnBeginSimulation"/> 직후
    /// 한 번 시작하고, 엔티티가 제거될 때 <see cref="Scope"/> 취소가 이것을 **먼저**
    /// 끊는다(취소 → OnEndSimulation → OnRemovingFromScene).
    ///
    /// AI 시퀀스·애니메이션 시퀀스·대기·비동기 상호작용처럼 **여러 프레임에 걸친
    /// 흐름**을 상태 머신 대신 직선으로 쓰는 자리다:
    /// <code>
    /// public override async Task OnSimulate()
    /// {
    ///     await Scope.Delay(1f);
    ///     Play("charge");
    ///     await Scope.Delay(0.5f);
    ///     Fire();
    /// }
    /// </code>
    ///
    /// ── 반드시 지킬 것 ──
    ///
    /// <b>await 대상은 <see cref="Scope"/>의 것이어야 한다.</b> Scope.Delay는
    /// 엔진 dt로 흐르고 완료 콜백이 게임 스레드에서 <b>동기로</b> 재개된다
    /// (SimulationScope.Tick이 TaskCompletionSource를 그 자리에서 완료시킨다).
    /// Task.Delay나 임의의 라이브러리 Task를 await하면 재개가 스레드풀로 넘어가
    /// 그 뒤 코드가 게임 스레드 밖에서 엔진 API를 만진다 — 관리 코드 호출 규약
    /// 위반이고, 증상은 산발적 크래시다.
    ///
    /// 취소는 협조적이다(.NET은 태스크를 강제 종료할 수 없다). Scope.Delay를
    /// await하고 있으면 그 대기가 취소되며 OperationCanceledException으로 풀린다.
    /// 긴 동기 루프를 도는 본문은 <see cref="SimulationScope.Token"/>을 스스로 봐야 한다.
    /// </summary>
    public virtual Task OnSimulate() => Task.CompletedTask;

    /// <summary>
    /// 이 컴포넌트의 시뮬레이션 스코프. OnBeginSimulation에서 시작한 태스크·이벤트
    /// 구독은 여기 걸어 두면 OnEndSimulation 직전에 <see cref="BehaviourRegistry"/>가
    /// 일괄 취소한다(Verse spawn/suspends의 C# 대응물, 설계 문서 §4 트랙 L2).
    /// </summary>
    public SimulationScope Scope { get; } = new();

    // 물리 콜백. 발생 시점에 바로 불리지 않고 틱 경계에서 일괄 전달된다 —
    // 충돌마다 경계를 넘으면 "틱당 1회" 원칙이 무너지기 때문이다(설계 문서 02절).
    // 따라서 이 안에서 본 상태는 물리 시뮬레이션 시점이 아니라 틱 경계의 상태다.
    /// <summary>
    /// 이름으로 부르는 콜백(애니메이션 키프레임 이벤트·입력 액션)의 진입점.
    ///
    /// 구현은 BehaviourGenerator가 만든다 — 스크립트의 public 무인자 void 메서드를
    /// 이름으로 찾아 직접 호출하는 switch다. 리플렉션을 쓰지 않으므로 AOT에서도 동작한다.
    /// 에셋에 저장된 메서드 이름 데이터가 그대로 유효한 것도 이 구조 덕분이다.
    /// </summary>
    /// <returns>해당 이름의 메서드를 찾아 불렀으면 true.</returns>
    public virtual bool InvokeMessage(string message) => false;

    public virtual void OnTriggerEnter(in Collision collision) { }
    public virtual void OnTriggerStay(in Collision collision) { }
    public virtual void OnTriggerExit(in Collision collision) { }
    public virtual void OnCollisionEnter(in Collision collision) { }
    public virtual void OnCollisionStay(in Collision collision) { }
    public virtual void OnCollisionExit(in Collision collision) { }

    // ── 컴포넌트 조회 ──
    // 자기 오브젝트를 대상으로 하는 지름길. 실측에서 GetComponent와 GetOwner가
    // 나란히 상위권(734·497회)인데, 대부분 "내 오브젝트의 다른 스크립트"를 찾는 형태다.
    // 이 경로는 경계를 넘지 않는다.

    public T? GetComponent<T>() where T : Component => GameObject.GetComponent<T>();
    public List<T> GetComponents<T>() where T : Behaviour => GameObject.GetComponents<T>();
    public bool TryGetComponent<T>(out T component) where T : Component => GameObject.TryGetComponent(out component);
    public bool HasComponent<T>() where T : Component => GameObject.HasComponent<T>();

    public List<T> GetComponentsInChildren<T>(bool includeSelf = true) where T : Component
        => GameObject.GetComponentsInChildren<T>(includeSelf);

    public T? GetComponentInChildren<T>(bool includeSelf = true) where T : Component
        => GameObject.GetComponentInChildren<T>(includeSelf);

    public T? GetComponentInParent<T>(bool includeSelf = true) where T : Component
        => GameObject.GetComponentInParent<T>(includeSelf);

    // ── 편의 ──
    /// <summary>엔진 프레임 번호. 라이프사이클 시점을 비교할 때 쓴다.</summary>
    public static ulong FrameCount => Native.FrameCount;

    protected static void Log(string message)      => Native.Log(1, message);
    protected static void LogWarning(string message) => Native.Log(2, message);
    protected static void LogError(string message)   => Native.Log(3, message);

    // ── 노출 필드 접근자 ──
    //
    // 아래 메서드들은 소스 제너레이터가 [SerializeField] 필드를 보고 재정의한다.
    // 직접 구현하지 말 것.
    //
    // 필드 오프셋을 넘기는 방식(설계 문서 04절 초안)이 아니라 인덱스 + 접근자로 간 이유:
    // C#에서 관리 객체의 필드 오프셋은 런타임이 정하고 GC가 객체를 옮길 수도 있어,
    // 네이티브가 주소로 직접 읽고 쓰는 것이 안전하지 않다. 생성된 switch는 AOT에서도
    // 그대로 남고 비용도 인덱스 분기 한 번뿐이다.
    //
    // 인스펙터와 직렬화가 같은 접근자를 공유한다 — 별도의 __Serialize를 만들 필요가 없다.

    public virtual int FieldCount => 0;
    public virtual string GetFieldName(int index) => string.Empty;
    public virtual FieldType GetFieldType(int index) => FieldType.Unknown;

    public virtual float GetFloat(int index) => 0f;
    public virtual void  SetFloat(int index, float value) { }

    public virtual int  GetInt32(int index) => 0;
    public virtual void SetInt32(int index, int value) { }

    public virtual bool GetBool(int index) => false;
    public virtual void SetBool(int index, bool value) { }

    public virtual Float2 GetFloat2(int index) => default;
    public virtual void   SetFloat2(int index, Float2 value) { }

    public virtual Float3 GetFloat3(int index) => default;
    public virtual void   SetFloat3(int index, Float3 value) { }

    public virtual string GetString(int index) => string.Empty;
    public virtual void   SetString(int index, string value) { }

    // 오브젝트 참조는 핸들로 주고받는다. 파일에 적을 때만 네이티브가 instanceID로 바꾼다.
    public virtual GameObject GetObject(int index) => default;
    public virtual void       SetObject(int index, GameObject value) { }
}



