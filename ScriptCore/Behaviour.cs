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
    /// 정상 경로로 제거된 것은 Flush가 OnDestroy를 부를 예정이라 건드리면 두 번 불린다.
    /// </summary>
    internal bool IsMarkedDestroyed => _destroyed;

    /// <summary>
    /// Awake가 실제로 불렸는지. 만들어지자마자 Awake 전에 파괴되는 경우가 있어서 둔다 —
    /// 재생을 시작하면 엔진이 에디터 씬 사본으로 갈아타면서 원본 쪽 인스턴스를 접는데,
    /// 그때 아직 한 번도 깨어나지 않은 인스턴스가 생긴다.
    /// Awake 없이 OnDestroy만 불리면 스크립트가 초기화하지 않은 것을 정리하려 든다.
    /// </summary>
    internal bool IsAwakened { get; private set; }
    internal void MarkAwakened() => IsAwakened = true;

    // ── 라이프사이클 ──
    // ModuleBehavior의 가상 함수 이름을 그대로 유지한다.
    public virtual void Awake() { }
    public virtual void OnEnable() { }
    public virtual void Start() { }
    public virtual void FixedUpdate(float fixedTick) { }
    public virtual void Update(float tick) { }
    public virtual void LateUpdate(float tick) { }
    public virtual void OnDisable() { }
    public virtual void OnDestroy() { }

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



