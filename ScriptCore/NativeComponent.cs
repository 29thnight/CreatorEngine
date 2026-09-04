namespace CreatorEngine;

/// <summary>
/// 네이티브 컴포넌트를 가리키는 얇은 래퍼의 기반.
///
/// 상태를 들고 있지 않다 — 오브젝트 핸들 하나로 매번 네이티브에서 찾는다.
/// 컴포넌트 자체에 핸들을 부여하지 않는 이유는, 오브젝트당 하나뿐인 컴포넌트가
/// 대부분이라 슬롯 테이블을 하나 더 두는 값어치가 없기 때문이다.
/// 오브젝트가 파괴되면 세대 검사에서 자동으로 걸러진다.
///
/// ── 왜 <see cref="Component"/> 아래에 있나 ──
///
/// 네이티브와 같은 모양이기 때문이다. 그쪽도 Transform·MeshRenderer가 전부
/// Component를 상속하고, 스크립트를 담는 ScriptComponent도 같은 층에 선다
/// (Component.h). 예전에는 관리 측만 "공통 얼굴 Component + Behaviour + 네이티브
/// 래퍼" 3층이었는데, 그 Behaviour 층은 옛 게임 코드의 관례였지 엔진 구조가 아니었다.
/// </summary>
public abstract class NativeComponent : Component
{
    private ObjectHandle _handle;

    /// <summary>
    /// 대상 오브젝트의 핸들. 조회(<see cref="ComponentKind{T}"/>)가 채운다.
    ///
    /// 핸들을 넣을 때 <see cref="Component.Entity"/>도 함께 세운다 — 스크립트는
    /// 팩토리가 그 둘을 따로 채우지만, 네이티브 래퍼는 조회가 핸들 하나만 알기 때문이다.
    /// </summary>
    internal ObjectHandle OwnerHandle
    {
        get => _handle;
        set
        {
            _handle = value;
            Entity = new Entity(value);
        }
    }
}

/// <summary>
/// 네이티브 컴포넌트 래퍼 목록. 새 래퍼를 추가할 때 손대는 곳은 여기 한 군데다.
///
/// 조회는 <see cref="ComponentKind{T}"/>의 정적 생성자에서 타입당 딱 한 번 일어나므로,
/// Dictionary를 쓰더라도 실제 GetComponent 호출 비용에는 영향이 없다.
/// </summary>
internal static class NativeComponentTable
{
    internal readonly record struct Entry(Func<ObjectHandle, Component> Wrap, Func<ObjectHandle, bool> Exists);

    private static readonly Dictionary<Type, Entry> _entries = new()
    {
        // Transform이 여기 있는 것이 S1-b 승격의 관리 측 귀결이다 — 네이티브에서
        // Component가 된 것을 여기서도 컴포넌트로 다룬다. Exists가 있어야 UI처럼
        // Transform 없는 오브젝트에서 null이 나온다.
        [typeof(Transform)]      = new(handle => new Transform      { OwnerHandle = handle }, Native.HasTransform),

        [typeof(SoundComponent)] = new(handle => new SoundComponent { OwnerHandle = handle }, Native.HasSoundComponent),
        [typeof(Light)]          = new(handle => new Light           { OwnerHandle = handle }, Native.LightExists),
        [typeof(Animator)]       = new(handle => new Animator       { OwnerHandle = handle }, Native.HasAnimator),

        [typeof(CharacterControllerComponent)] =
            new(handle => new CharacterControllerComponent { OwnerHandle = handle }, Native.HasCct),

        [typeof(RectTransformComponent)] =
            new(handle => new RectTransformComponent { OwnerHandle = handle }, Native.HasRect),
        [typeof(ImageComponent)] = new(handle => new ImageComponent { OwnerHandle = handle }, Native.HasImage),
        [typeof(TextComponent)]  = new(handle => new TextComponent  { OwnerHandle = handle }, Native.HasText),
        [typeof(Canvas)]         = new(handle => new Canvas         { OwnerHandle = handle }, Native.HasCanvas),
        [typeof(UIButton)]       = new(handle => new UIButton       { OwnerHandle = handle }, Native.HasButton),
        [typeof(MeshRenderer)]   = new(handle => new MeshRenderer   { OwnerHandle = handle }, Native.HasMesh),

        [typeof(RigidBodyComponent)] =
            new(handle => new RigidBodyComponent { OwnerHandle = handle }, Native.HasRigid),

        [typeof(SphereColliderComponent)] = new(
            handle => new SphereColliderComponent { OwnerHandle = handle },
            handle => Native.HasCollider(handle, (int)ColliderKind.Sphere)),
        [typeof(BoxColliderComponent)] = new(
            handle => new BoxColliderComponent { OwnerHandle = handle },
            handle => Native.HasCollider(handle, (int)ColliderKind.Box)),
        [typeof(CapsuleColliderComponent)] = new(
            handle => new CapsuleColliderComponent { OwnerHandle = handle },
            handle => Native.HasCollider(handle, (int)ColliderKind.Capsule)),
    };

    public static Entry? Find(Type type) => _entries.TryGetValue(type, out var entry) ? entry : null;
}

/// <summary>
/// 타입별 컴포넌트 정보. 정적 생성자에서 한 번만 판정하고 이후는 필드 읽기다.
/// GetComponent 안에서 typeof 비교를 반복하지 않기 위한 장치다.
/// </summary>
internal static class ComponentKind<T> where T : Component
{
    /// <summary>C#에 실체가 있는 스크립트인가.</summary>
    public static readonly bool IsManaged;

    /// <summary>네이티브 래퍼라면 핸들로 감싸는 함수.</summary>
    public static readonly Func<ObjectHandle, T>? Wrap;

    /// <summary>네이티브 래퍼라면 존재 여부를 묻는 함수.</summary>
    public static readonly Func<ObjectHandle, bool>? Exists;

    static ComponentKind()
    {
        // 판정 방향에 주의 — "스크립트인가"가 아니라 "네이티브 래퍼가 아닌가"로 묻는다.
        //
        // 예전에는 Behaviour 파생인지로 갈랐지만, 그 층이 사라지고 스크립트가 Component를
        // 직접 상속하게 되면서 "스크립트인가"를 물을 기준 타입이 없어졌다. 네이티브 래퍼
        // 쪽은 여전히 NativeComponent라는 분명한 표지를 갖고 있으므로 그것을 뺀 나머지가
        // 스크립트다. 새 래퍼를 만들 때 NativeComponent를 빠뜨리면 스크립트로 오분류되어
        // 조회가 조용히 빈 결과를 내므로, 래퍼는 반드시 NativeComponent를 상속해야 한다.
        if (!typeof(NativeComponent).IsAssignableFrom(typeof(T)))
        {
            IsManaged = true;
            return;
        }

        if (NativeComponentTable.Find(typeof(T)) is { } entry)
        {
            Wrap = handle => (T)entry.Wrap(handle);
            Exists = entry.Exists;
        }
    }
}

