namespace CreatorEngine;

/// <summary>
/// 컴포넌트의 공통 얼굴.
///
/// 두 갈래가 이 아래에 있다.
///  · <see cref="Behaviour"/> 파생 — C#에 실체가 있는 스크립트. 조회가 경계를 넘지 않는다.
///  · 네이티브 래퍼(<see cref="SoundComponent"/> 등) — 실체는 C++에 있고 핸들만 들고 있다.
///
/// GetComponent가 둘을 같은 문법으로 다루기 위한 최소 공통분모다(설계 문서 11.2).
/// </summary>
public abstract class Component
{
    /// <summary>이 컴포넌트가 붙어 있는 오브젝트의 핸들.</summary>
    internal ObjectHandle OwnerHandle;

    public GameObject GameObject => new(OwnerHandle);
}

/// <summary>
/// 네이티브 컴포넌트를 가리키는 얇은 래퍼의 기반.
///
/// 상태를 들고 있지 않다 — 오브젝트 핸들 하나로 매번 네이티브에서 찾는다.
/// 컴포넌트 자체에 핸들을 부여하지 않는 이유는, 오브젝트당 하나뿐인 컴포넌트가
/// 대부분이라 슬롯 테이블을 하나 더 두는 값어치가 없기 때문이다.
/// 오브젝트가 파괴되면 세대 검사에서 자동으로 걸러진다.
/// </summary>
public abstract class NativeComponent : Component
{
    /// <summary>대상 오브젝트가 살아 있는가. 컴포넌트 제거까지는 잡지 못한다.</summary>
    public bool IsAlive => Native.IsAlive(OwnerHandle);
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
        [typeof(SoundComponent)] = new(handle => new SoundComponent { OwnerHandle = handle }, Native.HasSoundComponent),
        [typeof(Animator)]       = new(handle => new Animator       { OwnerHandle = handle }, Native.HasAnimator),

        [typeof(CharacterControllerComponent)] =
            new(handle => new CharacterControllerComponent { OwnerHandle = handle }, Native.HasCct),

        [typeof(EffectComponent)] = new(handle => new EffectComponent { OwnerHandle = handle }, Native.HasEffect),

        [typeof(RectTransformComponent)] =
            new(handle => new RectTransformComponent { OwnerHandle = handle }, Native.HasRect),
        [typeof(ImageComponent)] = new(handle => new ImageComponent { OwnerHandle = handle }, Native.HasImage),
        [typeof(TextComponent)]  = new(handle => new TextComponent  { OwnerHandle = handle }, Native.HasText),
        [typeof(Canvas)]         = new(handle => new Canvas         { OwnerHandle = handle }, Native.HasCanvas),
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
        if (typeof(Behaviour).IsAssignableFrom(typeof(T)))
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
