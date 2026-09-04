using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace CreatorEngine;

/// <summary>
/// 네이티브가 넘겨주는 엔진 API 함수 포인터 표.
///
/// DllImport를 쓰지 않는 이유: 호스트가 실행 파일(Academy_4Q.exe)이라 이름으로 찾기가
/// 번거롭고, export 테이블을 유지해야 한다. 초기화 때 표를 한 번 받아 두면 이후 호출은
/// 함수 포인터 직행이라 마샬링도 스텁도 없다. (설계 문서 01절 "경계 함수는 평면 C ABI")
///
/// 네이티브 <c>ScriptApiTable</c>과 필드 순서가 정확히 같아야 한다. 하나라도 어긋나면
/// 엉뚱한 함수를 호출하게 되므로, 양쪽에 버전 필드를 두어 초기화 때 검사한다.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
internal unsafe struct ScriptApiTable
{
    public int Version;
    public int StructSize;

    // 로그 — level: 0=Debug 1=Info 2=Warning 3=Error, message: null 종료 UTF-8
    public delegate* unmanaged<int, byte*, void> Log;

    // Entity
    //
    // 이름 조회는 11.3절에서 "컴파일 타임 해시로 치환"을 제안했지만, 엔진의 HashingString이
    // std::hash<string_view>(구현 정의)를 쓰고 있어 C# 쪽에서 같은 값을 재현하면 컴파일러
    // 버전에 묶인다. 엔진이 자체 해시로 바꾸기 전까지는 UTF-8 문자열을 그대로 넘긴다.
    public delegate* unmanaged<byte*, ObjectHandle> Entity_FindByName;
    public delegate* unmanaged<ObjectHandle, int> Entity_IsAlive;
    public delegate* unmanaged<ObjectHandle, byte*, int, int> Entity_GetName;
    public delegate* unmanaged<ObjectHandle, int, void> Entity_SetEnabled;

    // 계층 접근. 실측 208곳(자식 76 · FindIndex 100 · 부모 32)으로 표면이 가장 넓다.
    public delegate* unmanaged<ObjectHandle, int> Entity_GetChildCount;
    public delegate* unmanaged<ObjectHandle, int, ObjectHandle> Entity_GetChild;
    public delegate* unmanaged<ObjectHandle, ObjectHandle> Entity_GetParent;
    public delegate* unmanaged<int, ObjectHandle> Entity_FindByIndex;
    public delegate* unmanaged<ObjectHandle, int> Entity_GetIndex;

    // Transform. 네이티브와 같은 컴포넌트다(S1-b 승격).
    //
    // Exists가 맨 앞에 있는 이유: S3부터 UI/Canvas는 Transform을 갖지 않는다. 부재를
    // 묻지 못하면 없는 Transform에 값을 쓰고도 성공한 것처럼 보인다 — 네이티브가
    // 공유 더미로 받아 로그 한 줄만 남기고 값을 버리기 때문이다.
    public delegate* unmanaged<ObjectHandle, int> Transform_Exists;
    public delegate* unmanaged<ObjectHandle, Float3> Transform_GetLocalPosition;
    public delegate* unmanaged<ObjectHandle, Float3, void> Transform_SetLocalPosition;
    public delegate* unmanaged<ObjectHandle, Float3> Transform_GetWorldPosition;

    // 회전·스케일·방향축. Transform 획득이 실측 154회로 전체 1위인데 위치만 열려 있었다.
    public delegate* unmanaged<ObjectHandle, Quaternion> Transform_GetLocalRotation;
    public delegate* unmanaged<ObjectHandle, Quaternion, void> Transform_SetLocalRotation;
    public delegate* unmanaged<ObjectHandle, Float3> Transform_GetLocalScale;
    public delegate* unmanaged<ObjectHandle, Float3, void> Transform_SetLocalScale;

    public delegate* unmanaged<ObjectHandle, Float3, void> Transform_AddLocalPosition;
    public delegate* unmanaged<ObjectHandle, Quaternion, void> Transform_AddLocalRotation;

    public delegate* unmanaged<ObjectHandle, Float3, void> Transform_SetWorldPosition;
    public delegate* unmanaged<ObjectHandle, Quaternion> Transform_GetWorldRotation;
    public delegate* unmanaged<ObjectHandle, Quaternion, void> Transform_SetWorldRotation;
    public delegate* unmanaged<ObjectHandle, Float3> Transform_GetWorldScale;
    public delegate* unmanaged<ObjectHandle, Float3, void> Transform_SetWorldScale;

    public delegate* unmanaged<ObjectHandle, Float3> Transform_GetForward;
    public delegate* unmanaged<ObjectHandle, Float3> Transform_GetRight;
    public delegate* unmanaged<ObjectHandle, Float3> Transform_GetUp;

    // 프리팹·수명 (실측 172회 — T2에서 가장 먼저 필요한 표면)
    public delegate* unmanaged<byte*, int> Prefab_Exists;
    public delegate* unmanaged<byte*, byte*, ObjectHandle> Prefab_Instantiate;
    public delegate* unmanaged<ObjectHandle, void> Entity_Destroy;

    // 엔진 프레임 번호. 라이프사이클이 어느 프레임에 불렸는지 판정할 때 쓴다.
    public delegate* unmanaged<ulong> Engine_GetFrameCount;

    // SoundComponent (실측 435회 — T2에서 프리팹 다음가는 비중)
    //
    // 컴포넌트에 별도 핸들을 두지 않고 오브젝트 핸들만 넘긴다. 네이티브가 매번
    // GetComponent로 찾는데, 사운드 호출은 프레임당 수십 회 수준이라 이 비용이 문제되지 않고
    // 컴포넌트용 슬롯 테이블을 하나 더 유지하는 쪽이 오히려 비싸다.
    public delegate* unmanaged<ObjectHandle, int> Sound_Exists;
    public delegate* unmanaged<ObjectHandle, void> Sound_Play;
    public delegate* unmanaged<ObjectHandle, void> Sound_Stop;
    public delegate* unmanaged<ObjectHandle, int, void> Sound_Pause;
    public delegate* unmanaged<ObjectHandle, int> Sound_IsPlaying;
    public delegate* unmanaged<ObjectHandle, void> Sound_PlayOneShot;
    public delegate* unmanaged<ObjectHandle, byte*, int, int> Sound_GetClipKey;
    public delegate* unmanaged<ObjectHandle, byte*, void> Sound_SetClipKey;
    public delegate* unmanaged<ObjectHandle, float> Sound_GetVolume;
    public delegate* unmanaged<ObjectHandle, float, void> Sound_SetVolume;
    public delegate* unmanaged<ObjectHandle, float> Sound_GetPitch;
    public delegate* unmanaged<ObjectHandle, float, void> Sound_SetPitch;

    // Animator (실측 122회 — 그 중 SetParameter만 94회)
    public delegate* unmanaged<ObjectHandle, int> Animator_Exists;
    public delegate* unmanaged<ObjectHandle, byte*, int> Animator_HasParameter;
    public delegate* unmanaged<ObjectHandle, byte*, int, void> Animator_SetBool;
    public delegate* unmanaged<ObjectHandle, byte*, float, void> Animator_SetFloat;
    public delegate* unmanaged<ObjectHandle, byte*, int, void> Animator_SetInt;
    public delegate* unmanaged<ObjectHandle, byte*, void> Animator_SetTrigger;
    public delegate* unmanaged<ObjectHandle, byte*, void> Animator_ResetTrigger;
    public delegate* unmanaged<ObjectHandle, byte*, int> Animator_GetBool;
    public delegate* unmanaged<ObjectHandle, byte*, float> Animator_GetFloat;
    public delegate* unmanaged<ObjectHandle, byte*, int> Animator_GetInt;
    public delegate* unmanaged<ObjectHandle, int, int, void> Animator_SetUseLayer;
    public delegate* unmanaged<ObjectHandle, float, void> Animator_StopAnimation;

    // CharacterControllerComponent (획득 47회 — 이동의 뼈대)
    public delegate* unmanaged<ObjectHandle, int> Cct_Exists;
    public delegate* unmanaged<ObjectHandle, float, float, void> Cct_Move;
    public delegate* unmanaged<ObjectHandle, Float3, float, void> Cct_TriggerForcedMove;
    public delegate* unmanaged<ObjectHandle, void> Cct_StopForcedMove;
    public delegate* unmanaged<ObjectHandle, int> Cct_IsInForcedMove;
    public delegate* unmanaged<ObjectHandle, int, void> Cct_SetAutomaticRotation;
    public delegate* unmanaged<ObjectHandle, Float3, void> Cct_SetLookDirection;
    public delegate* unmanaged<ObjectHandle, void> Cct_ClearLookDirection;
    public delegate* unmanaged<ObjectHandle, Float3, void> Cct_ForcedSetPosition;
    public delegate* unmanaged<ObjectHandle, float> Cct_GetBaseSpeed;
    public delegate* unmanaged<ObjectHandle, float, void> Cct_SetBaseSpeed;
    public delegate* unmanaged<ObjectHandle, int> Cct_IsOnMove;
    public delegate* unmanaged<ObjectHandle, int, void> Cct_SetOnMove;
    public delegate* unmanaged<ObjectHandle, int> Cct_IsFalling;
    public delegate* unmanaged<ObjectHandle, float> Cct_GetRadius;
    public delegate* unmanaged<ObjectHandle, float> Cct_GetHeight;
    public delegate* unmanaged<ObjectHandle, uint> Cct_GetId;

    // RectTransformComponent (획득 28회 · SetAnchoredPosition만 39회)
    public delegate* unmanaged<ObjectHandle, int> Rect_Exists;
    public delegate* unmanaged<ObjectHandle, Float2> Rect_GetAnchoredPosition;
    public delegate* unmanaged<ObjectHandle, Float2, void> Rect_SetAnchoredPosition;
    public delegate* unmanaged<ObjectHandle, Float2> Rect_GetSizeDelta;
    public delegate* unmanaged<ObjectHandle, Float2, void> Rect_SetSizeDelta;
    public delegate* unmanaged<ObjectHandle, Float2> Rect_GetPivot;
    public delegate* unmanaged<ObjectHandle, Float2, void> Rect_SetPivot;

    // ImageComponent (획득 48회 · SetTexture 39 · color 46)
    public delegate* unmanaged<ObjectHandle, int> Image_Exists;
    public delegate* unmanaged<ObjectHandle, int, void> Image_SetTexture;
    public delegate* unmanaged<ObjectHandle, int> Image_GetTextureCount;
    public delegate* unmanaged<ObjectHandle, Color4> Image_GetColor;
    public delegate* unmanaged<ObjectHandle, Color4, void> Image_SetColor;
    public delegate* unmanaged<ObjectHandle, float> Image_GetClipPercent;
    public delegate* unmanaged<ObjectHandle, float, void> Image_SetClipPercent;
    public delegate* unmanaged<ObjectHandle, void> Image_SetNativeSize;

    // 카메라. 월드→스크린 변환이 게임 스크립트 11개 파일에 복제돼 있어 하나로 접었다.
    public delegate* unmanaged<int> Camera_Exists;
    public delegate* unmanaged<Float2> Camera_GetScreenSize;
    public delegate* unmanaged<Float3, Float3> Camera_WorldToScreenPoint;

    // MeshRenderer + Material (m_Material 42회 — 대부분 셰이더 상수 넣기)
    public delegate* unmanaged<ObjectHandle, int> Mesh_Exists;
    public delegate* unmanaged<ObjectHandle, byte*, void> Mesh_InstantiateMaterial;
    public delegate* unmanaged<ObjectHandle, byte*, int, int> Mesh_GetMaterialName;
    public delegate* unmanaged<ObjectHandle, byte*, float, int> Mesh_SetMaterialFloat;
    public delegate* unmanaged<ObjectHandle, byte*, int, int> Mesh_SetMaterialInt;
    public delegate* unmanaged<ObjectHandle, Color4> Mesh_GetBaseColor;
    public delegate* unmanaged<ObjectHandle, Color4, void> Mesh_SetBaseColor;

    // 입력 (실측 43회 · 지금까지 스크립트가 아무것도 받을 수 없던 표면)
    public delegate* unmanaged<int, int> Input_GetKeyState;
    public delegate* unmanaged<int, int> Input_GetMouseButtonState;
    public delegate* unmanaged<int, int, int> Input_GetControllerButtonState;
    public delegate* unmanaged<int> Input_IsAnyKeyPressed;

    public delegate* unmanaged<Float2> Input_GetMousePosition;
    public delegate* unmanaged<Float2> Input_GetMouseDelta;
    public delegate* unmanaged<int> Input_GetWheelDelta;
    public delegate* unmanaged<int, void> Input_SetCursorVisible;

    public delegate* unmanaged<int, int> Input_IsControllerConnected;
    public delegate* unmanaged<int, int> Input_IsControllerTriggerL;
    public delegate* unmanaged<int, int> Input_IsControllerTriggerR;
    public delegate* unmanaged<int, Float2> Input_GetControllerThumbL;
    public delegate* unmanaged<int, Float2> Input_GetControllerThumbR;

    // 물리 질의 (Raycast 19 · SphereOverlap 16)
    public delegate* unmanaged<Float3, Float3, float, uint, RaycastHit*, int> Physics_Raycast;
    public delegate* unmanaged<Float3, Float3, float, uint, RaycastHit*, int, int> Physics_RaycastAll;
    public delegate* unmanaged<Float3, float, uint, RaycastHit*, int, int> Physics_OverlapSphere;

    // RigidBodyComponent (실측 34회)
    public delegate* unmanaged<ObjectHandle, int> Rigid_Exists;
    public delegate* unmanaged<ObjectHandle, Float3> Rigid_GetLinearVelocity;
    public delegate* unmanaged<ObjectHandle, Float3, void> Rigid_SetLinearVelocity;
    public delegate* unmanaged<ObjectHandle, Float3, void> Rigid_AddLinearVelocity;
    public delegate* unmanaged<ObjectHandle, Float3> Rigid_GetAngularVelocity;
    public delegate* unmanaged<ObjectHandle, Float3, void> Rigid_SetAngularVelocity;
    public delegate* unmanaged<ObjectHandle, Float3, int, void> Rigid_AddForce;
    public delegate* unmanaged<ObjectHandle, int, void> Rigid_SetBodyType;
    public delegate* unmanaged<ObjectHandle, int> Rigid_IsKinematic;
    public delegate* unmanaged<ObjectHandle, int, void> Rigid_SetKinematic;
    public delegate* unmanaged<ObjectHandle, int> Rigid_IsTrigger;
    public delegate* unmanaged<ObjectHandle, int, void> Rigid_SetIsTrigger;
    public delegate* unmanaged<ObjectHandle, int> Rigid_IsColliderEnabled;
    public delegate* unmanaged<ObjectHandle, int, void> Rigid_SetColliderEnabled;
    public delegate* unmanaged<ObjectHandle, int> Rigid_IsUsingGravity;
    public delegate* unmanaged<ObjectHandle, int, void> Rigid_UseGravity;
    public delegate* unmanaged<ObjectHandle, float> Rigid_GetMass;
    public delegate* unmanaged<ObjectHandle, float, void> Rigid_SetMass;
    public delegate* unmanaged<ObjectHandle, float, void> Rigid_SetLinearDamping;
    public delegate* unmanaged<ObjectHandle, float, void> Rigid_SetAngularDamping;
    public delegate* unmanaged<ObjectHandle, Float3, void> Rigid_SetScale;
    public delegate* unmanaged<ObjectHandle, int, int, int, void> Rigid_SetLockLinear;
    public delegate* unmanaged<ObjectHandle, int, int, int, void> Rigid_SetLockAngular;

    // 콜라이더 3종 (kind: 0=Sphere 1=Box 2=Capsule)
    public delegate* unmanaged<ObjectHandle, int, int> Collider_Exists;
    public delegate* unmanaged<ObjectHandle, int, float> Collider_GetRadius;
    public delegate* unmanaged<ObjectHandle, int, float, void> Collider_SetRadius;
    public delegate* unmanaged<ObjectHandle, int, float> Collider_GetHeight;
    public delegate* unmanaged<ObjectHandle, int, float, void> Collider_SetHeight;
    public delegate* unmanaged<ObjectHandle, int, Float3> Collider_GetExtents;
    public delegate* unmanaged<ObjectHandle, int, Float3, void> Collider_SetExtents;
    public delegate* unmanaged<ObjectHandle, int, Float3> Collider_GetPositionOffset;
    public delegate* unmanaged<ObjectHandle, int, Float3, void> Collider_SetPositionOffset;
    public delegate* unmanaged<ObjectHandle, int, float> Collider_GetRestitution;
    public delegate* unmanaged<ObjectHandle, int, float, void> Collider_SetRestitution;
    public delegate* unmanaged<ObjectHandle, int, float> Collider_GetStaticFriction;
    public delegate* unmanaged<ObjectHandle, int, float, void> Collider_SetStaticFriction;
    public delegate* unmanaged<ObjectHandle, int, float> Collider_GetDynamicFriction;
    public delegate* unmanaged<ObjectHandle, int, float, void> Collider_SetDynamicFriction;

    // TextComponent (SetMessage 16 · SetAlpha 6)
    public delegate* unmanaged<ObjectHandle, int> Text_Exists;
    public delegate* unmanaged<ObjectHandle, byte*, int, int> Text_GetMessage;
    public delegate* unmanaged<ObjectHandle, byte*, void> Text_SetMessage;
    public delegate* unmanaged<ObjectHandle, Color4> Text_GetColor;
    public delegate* unmanaged<ObjectHandle, Color4, void> Text_SetColor;
    public delegate* unmanaged<ObjectHandle, float> Text_GetAlpha;
    public delegate* unmanaged<ObjectHandle, float, void> Text_SetAlpha;
    public delegate* unmanaged<ObjectHandle, float> Text_GetFontSize;
    public delegate* unmanaged<ObjectHandle, float, void> Text_SetFontSize;
    public delegate* unmanaged<ObjectHandle, Float2> Text_GetRelativePosition;
    public delegate* unmanaged<ObjectHandle, Float2, void> Text_SetRelativePosition;

    // UIComponent 공통 (Image·Text가 함께 쓴다)
    public delegate* unmanaged<ObjectHandle, int> Ui_GetOrder;
    public delegate* unmanaged<ObjectHandle, int, void> Ui_SetOrder;

    // Canvas
    public delegate* unmanaged<ObjectHandle, int> Canvas_Exists;
    public delegate* unmanaged<ObjectHandle, int> Canvas_GetOrder;
    public delegate* unmanaged<ObjectHandle, int, void> Canvas_SetOrder;
    public delegate* unmanaged<ObjectHandle, byte*, int, int> Canvas_GetName;
    public delegate* unmanaged<ObjectHandle, byte*, void> Canvas_SetName;

    // UI 내비게이션·버튼·Image 잔여
    public delegate* unmanaged<ObjectHandle, int> Ui_IsSelected;
    public delegate* unmanaged<ObjectHandle, int> Ui_IsNavLocked;
    public delegate* unmanaged<ObjectHandle, int, void> Ui_SetNavLock;
    public delegate* unmanaged<ObjectHandle> UiNav_GetSelected;
    public delegate* unmanaged<ObjectHandle, void> UiNav_SetSelected;

    public delegate* unmanaged<ObjectHandle, int> Button_Exists;
    public delegate* unmanaged<ObjectHandle, int> Button_ConsumeClicked;

    public delegate* unmanaged<ObjectHandle, int> Image_GetTextureIndex;
    public delegate* unmanaged<ObjectHandle, float> Image_GetRotation;
    public delegate* unmanaged<ObjectHandle, float, void> Image_SetRotation;

    // 레이아웃 검증용 최종 사각형
    public delegate* unmanaged<ObjectHandle, Color4> Rect_GetWorldRect;
    public delegate* unmanaged<ObjectHandle, Float2> Rect_GetScreenPosition;
    public delegate* unmanaged<ObjectHandle, Float2, void> Rect_SetScreenPosition;
}

/// <summary>엔진 API 접근점. 표를 정적으로 들고 있어 호출 비용을 최소화한다.</summary>
internal static unsafe class Native
{
    /// <summary>네이티브와 맞춰야 하는 표 버전. 필드를 추가하면 반드시 올린다.</summary>
    public const int ExpectedVersion = 21;

    private static ScriptApiTable _api;
    private static bool _ready;

    public static bool IsReady => _ready;

    public static bool Bind(ScriptApiTable* table)
    {
        if (table == null) return false;
        if (table->Version != ExpectedVersion) return false;
        if (table->StructSize != sizeof(ScriptApiTable)) return false;

        _api = *table;
        _ready = true;
        return true;
    }

    // ── 로그 ──
    // 문자열은 호출 시점에만 필요하므로 스택 버퍼로 UTF-8 변환한다(할당 없음).
    public static void Log(int level, string message)
    {
        if (!_ready || _api.Log == null) return;

        const int stackLimit = 512;
        int maxBytes = System.Text.Encoding.UTF8.GetMaxByteCount(message.Length) + 1;

        if (maxBytes <= stackLimit)
        {
            byte* buffer = stackalloc byte[stackLimit];
            int written = System.Text.Encoding.UTF8.GetBytes(message, new Span<byte>(buffer, stackLimit - 1));
            buffer[written] = 0;
            _api.Log(level, buffer);
        }
        else
        {
            byte[] heap = new byte[maxBytes];
            fixed (byte* p = heap)
            {
                int written = System.Text.Encoding.UTF8.GetBytes(message, new Span<byte>(p, maxBytes - 1));
                p[written] = 0;
                _api.Log(level, p);
            }
        }
    }

    // ── Entity ──
    public static ObjectHandle FindByName(string name)
    {
        if (!_ready || _api.Entity_FindByName == null) return ObjectHandle.Invalid;

        const int cap = 256;
        byte* buffer = stackalloc byte[cap];
        int written = System.Text.Encoding.UTF8.GetBytes(name, new Span<byte>(buffer, cap - 1));
        buffer[written] = 0;
        return _api.Entity_FindByName(buffer);
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static bool IsAlive(ObjectHandle h)
        => _ready && _api.Entity_IsAlive != null && _api.Entity_IsAlive(h) != 0;

    public static string GetName(ObjectHandle h)
    {
        if (!_ready || _api.Entity_GetName == null) return string.Empty;

        const int cap = 256;
        byte* buffer = stackalloc byte[cap];
        int len = _api.Entity_GetName(h, buffer, cap);
        return len > 0 ? System.Text.Encoding.UTF8.GetString(buffer, len) : string.Empty;
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static void SetEnabled(ObjectHandle h, bool enabled)
    {
        if (_ready && _api.Entity_SetEnabled != null) _api.Entity_SetEnabled(h, enabled ? 1 : 0);
    }

    // ── 계층 ──

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static int GetChildCount(ObjectHandle h)
        => _ready && _api.Entity_GetChildCount != null ? _api.Entity_GetChildCount(h) : 0;

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static ObjectHandle GetChild(ObjectHandle h, int index)
        => _ready && _api.Entity_GetChild != null ? _api.Entity_GetChild(h, index) : ObjectHandle.Invalid;

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static ObjectHandle GetParent(ObjectHandle h)
        => _ready && _api.Entity_GetParent != null ? _api.Entity_GetParent(h) : ObjectHandle.Invalid;

    public static ObjectHandle FindByIndex(int index)
        => _ready && _api.Entity_FindByIndex != null ? _api.Entity_FindByIndex(index) : ObjectHandle.Invalid;

    public static int GetIndex(ObjectHandle h)
        => _ready && _api.Entity_GetIndex != null ? _api.Entity_GetIndex(h) : -1;

    // ── Transform ──

    /// <summary>
    /// 이 오브젝트가 Transform을 갖는가. UI/Canvas는 갖지 않는다(S3).
    ///
    /// 바인딩이 없는 구 호스트에서는 false가 아니라 <c>true</c>로 답한다 — 없다고
    /// 답하면 Transform이 멀쩡한 오브젝트까지 전부 없는 것으로 보여 더 나쁘다.
    /// </summary>
    public static bool HasTransform(ObjectHandle h)
        => !_ready || _api.Transform_Exists == null || _api.Transform_Exists(h) != 0;

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static Float3 GetLocalPosition(ObjectHandle h)
        => _ready && _api.Transform_GetLocalPosition != null ? _api.Transform_GetLocalPosition(h) : default;

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static void SetLocalPosition(ObjectHandle h, Float3 p)
    {
        if (_ready && _api.Transform_SetLocalPosition != null) _api.Transform_SetLocalPosition(h, p);
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static Float3 GetWorldPosition(ObjectHandle h)
        => _ready && _api.Transform_GetWorldPosition != null ? _api.Transform_GetWorldPosition(h) : default;

    // 아래는 전부 같은 모양이다 — 준비 전이거나 표에 없으면 무해한 기본값을 돌려준다.
    // 회전의 기본값은 default(=0,0,0,0)가 아니라 단위 쿼터니언이어야 한다.

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static Quaternion GetLocalRotation(ObjectHandle h)
        => _ready && _api.Transform_GetLocalRotation != null ? _api.Transform_GetLocalRotation(h) : Quaternion.Identity;

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static void SetLocalRotation(ObjectHandle h, Quaternion q)
    {
        if (_ready && _api.Transform_SetLocalRotation != null) _api.Transform_SetLocalRotation(h, q);
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static Float3 GetLocalScale(ObjectHandle h)
        => _ready && _api.Transform_GetLocalScale != null ? _api.Transform_GetLocalScale(h) : Float3.One;

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static void SetLocalScale(ObjectHandle h, Float3 s)
    {
        if (_ready && _api.Transform_SetLocalScale != null) _api.Transform_SetLocalScale(h, s);
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static void AddLocalPosition(ObjectHandle h, Float3 delta)
    {
        if (_ready && _api.Transform_AddLocalPosition != null) _api.Transform_AddLocalPosition(h, delta);
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static void AddLocalRotation(ObjectHandle h, Quaternion delta)
    {
        if (_ready && _api.Transform_AddLocalRotation != null) _api.Transform_AddLocalRotation(h, delta);
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static void SetWorldPosition(ObjectHandle h, Float3 p)
    {
        if (_ready && _api.Transform_SetWorldPosition != null) _api.Transform_SetWorldPosition(h, p);
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static Quaternion GetWorldRotation(ObjectHandle h)
        => _ready && _api.Transform_GetWorldRotation != null ? _api.Transform_GetWorldRotation(h) : Quaternion.Identity;

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static void SetWorldRotation(ObjectHandle h, Quaternion q)
    {
        if (_ready && _api.Transform_SetWorldRotation != null) _api.Transform_SetWorldRotation(h, q);
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static Float3 GetWorldScale(ObjectHandle h)
        => _ready && _api.Transform_GetWorldScale != null ? _api.Transform_GetWorldScale(h) : Float3.One;

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static void SetWorldScale(ObjectHandle h, Float3 s)
    {
        if (_ready && _api.Transform_SetWorldScale != null) _api.Transform_SetWorldScale(h, s);
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static Float3 GetForward(ObjectHandle h)
        => _ready && _api.Transform_GetForward != null ? _api.Transform_GetForward(h) : Float3.Forward;

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static Float3 GetRight(ObjectHandle h)
        => _ready && _api.Transform_GetRight != null ? _api.Transform_GetRight(h) : Float3.Right;

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static Float3 GetUp(ObjectHandle h)
        => _ready && _api.Transform_GetUp != null ? _api.Transform_GetUp(h) : Float3.Up;

    // ── 프리팹·수명 ──

    public static bool PrefabExists(string name)
    {
        if (!_ready || _api.Prefab_Exists == null) return false;

        const int cap = 512;
        byte* buffer = stackalloc byte[cap];
        int written = System.Text.Encoding.UTF8.GetBytes(name, new Span<byte>(buffer, cap - 1));
        buffer[written] = 0;
        return _api.Prefab_Exists(buffer) != 0;
    }

    public static ObjectHandle InstantiatePrefab(string prefabName, string instanceName)
    {
        if (!_ready || _api.Prefab_Instantiate == null) return ObjectHandle.Invalid;

        const int cap = 512;
        byte* nameBuffer = stackalloc byte[cap];
        int written = System.Text.Encoding.UTF8.GetBytes(prefabName, new Span<byte>(nameBuffer, cap - 1));
        nameBuffer[written] = 0;

        byte* instanceBuffer = stackalloc byte[cap];
        written = System.Text.Encoding.UTF8.GetBytes(instanceName, new Span<byte>(instanceBuffer, cap - 1));
        instanceBuffer[written] = 0;

        return _api.Prefab_Instantiate(nameBuffer, instanceBuffer);
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static void DestroyObject(ObjectHandle h)
    {
        if (_ready && _api.Entity_Destroy != null) _api.Entity_Destroy(h);
    }

    public static ulong FrameCount
        => _ready && _api.Engine_GetFrameCount != null ? _api.Engine_GetFrameCount() : 0;

    // ── SoundComponent ──

    public static bool HasSoundComponent(ObjectHandle h)
        => _ready && _api.Sound_Exists != null && _api.Sound_Exists(h) != 0;

    public static void SoundPlay(ObjectHandle h)
    {
        if (_ready && _api.Sound_Play != null) _api.Sound_Play(h);
    }

    public static void SoundStop(ObjectHandle h)
    {
        if (_ready && _api.Sound_Stop != null) _api.Sound_Stop(h);
    }

    public static void SoundPause(ObjectHandle h, bool pause)
    {
        if (_ready && _api.Sound_Pause != null) _api.Sound_Pause(h, pause ? 1 : 0);
    }

    public static bool SoundIsPlaying(ObjectHandle h)
        => _ready && _api.Sound_IsPlaying != null && _api.Sound_IsPlaying(h) != 0;

    public static void SoundPlayOneShot(ObjectHandle h)
    {
        if (_ready && _api.Sound_PlayOneShot != null) _api.Sound_PlayOneShot(h);
    }

    public static string SoundGetClipKey(ObjectHandle h)
    {
        if (!_ready || _api.Sound_GetClipKey == null) return string.Empty;

        const int cap = 256;
        byte* buffer = stackalloc byte[cap];
        int len = _api.Sound_GetClipKey(h, buffer, cap);
        return len > 0 ? System.Text.Encoding.UTF8.GetString(buffer, len) : string.Empty;
    }

    public static void SoundSetClipKey(ObjectHandle h, string value)
    {
        if (!_ready || _api.Sound_SetClipKey == null) return;

        const int cap = 256;
        byte* buffer = stackalloc byte[cap];
        int written = System.Text.Encoding.UTF8.GetBytes(value, new Span<byte>(buffer, cap - 1));
        buffer[written] = 0;
        _api.Sound_SetClipKey(h, buffer);
    }

    public static float SoundGetVolume(ObjectHandle h)
        => _ready && _api.Sound_GetVolume != null ? _api.Sound_GetVolume(h) : 0f;

    public static void SoundSetVolume(ObjectHandle h, float value)
    {
        if (_ready && _api.Sound_SetVolume != null) _api.Sound_SetVolume(h, value);
    }

    public static float SoundGetPitch(ObjectHandle h)
        => _ready && _api.Sound_GetPitch != null ? _api.Sound_GetPitch(h) : 0f;

    public static void SoundSetPitch(ObjectHandle h, float value)
    {
        if (_ready && _api.Sound_SetPitch != null) _api.Sound_SetPitch(h, value);
    }

    // ── Animator ──
    //
    // 파라미터 이름은 매 호출마다 UTF-8로 바꿔 넘긴다. 스택 버퍼라 할당은 없다.
    // 이름 대신 인덱스를 캐시하는 방안도 있지만, 컨트롤러 교체와 핫리로드마다
    // 무효화를 관리해야 해서 지금 단계에서는 이득이 비용을 넘지 않는다.

    private const int NameBufferSize = 128;

    public static bool HasAnimator(ObjectHandle h)
        => _ready && _api.Animator_Exists != null && _api.Animator_Exists(h) != 0;

    /// <summary>이름을 스택 버퍼에 UTF-8로 담는다. 잘리면 그만큼만 넘어간다.</summary>
    private static int EncodeName(string name, byte* buffer)
    {
        int written = System.Text.Encoding.UTF8.GetBytes(name, new Span<byte>(buffer, NameBufferSize - 1));
        buffer[written] = 0;
        return written;
    }

    public static bool AnimatorHasParameter(ObjectHandle h, string name)
    {
        if (!_ready || _api.Animator_HasParameter == null) return false;

        byte* buffer = stackalloc byte[NameBufferSize];
        EncodeName(name, buffer);
        return _api.Animator_HasParameter(h, buffer) != 0;
    }

    public static void AnimatorSetBool(ObjectHandle h, string name, bool value)
    {
        if (!_ready || _api.Animator_SetBool == null) return;

        byte* buffer = stackalloc byte[NameBufferSize];
        EncodeName(name, buffer);
        _api.Animator_SetBool(h, buffer, value ? 1 : 0);
    }

    public static void AnimatorSetFloat(ObjectHandle h, string name, float value)
    {
        if (!_ready || _api.Animator_SetFloat == null) return;

        byte* buffer = stackalloc byte[NameBufferSize];
        EncodeName(name, buffer);
        _api.Animator_SetFloat(h, buffer, value);
    }

    public static void AnimatorSetInt(ObjectHandle h, string name, int value)
    {
        if (!_ready || _api.Animator_SetInt == null) return;

        byte* buffer = stackalloc byte[NameBufferSize];
        EncodeName(name, buffer);
        _api.Animator_SetInt(h, buffer, value);
    }

    public static void AnimatorSetTrigger(ObjectHandle h, string name)
    {
        if (!_ready || _api.Animator_SetTrigger == null) return;

        byte* buffer = stackalloc byte[NameBufferSize];
        EncodeName(name, buffer);
        _api.Animator_SetTrigger(h, buffer);
    }

    public static void AnimatorResetTrigger(ObjectHandle h, string name)
    {
        if (!_ready || _api.Animator_ResetTrigger == null) return;

        byte* buffer = stackalloc byte[NameBufferSize];
        EncodeName(name, buffer);
        _api.Animator_ResetTrigger(h, buffer);
    }

    public static bool AnimatorGetBool(ObjectHandle h, string name)
    {
        if (!_ready || _api.Animator_GetBool == null) return false;

        byte* buffer = stackalloc byte[NameBufferSize];
        EncodeName(name, buffer);
        return _api.Animator_GetBool(h, buffer) != 0;
    }

    public static float AnimatorGetFloat(ObjectHandle h, string name)
    {
        if (!_ready || _api.Animator_GetFloat == null) return 0f;

        byte* buffer = stackalloc byte[NameBufferSize];
        EncodeName(name, buffer);
        return _api.Animator_GetFloat(h, buffer);
    }

    public static int AnimatorGetInt(ObjectHandle h, string name)
    {
        if (!_ready || _api.Animator_GetInt == null) return 0;

        byte* buffer = stackalloc byte[NameBufferSize];
        EncodeName(name, buffer);
        return _api.Animator_GetInt(h, buffer);
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static void AnimatorSetUseLayer(ObjectHandle h, int layerIndex, bool useLayer)
    {
        if (_ready && _api.Animator_SetUseLayer != null) _api.Animator_SetUseLayer(h, layerIndex, useLayer ? 1 : 0);
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static void AnimatorStopAnimation(ObjectHandle h, float duration)
    {
        if (_ready && _api.Animator_StopAnimation != null) _api.Animator_StopAnimation(h, duration);
    }

    // ── CharacterControllerComponent ──

    public static bool HasCct(ObjectHandle h)
        => _ready && _api.Cct_Exists != null && _api.Cct_Exists(h) != 0;

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static void CctMove(ObjectHandle h, float inputX, float inputY)
    {
        if (_ready && _api.Cct_Move != null) _api.Cct_Move(h, inputX, inputY);
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static void CctTriggerForcedMove(ObjectHandle h, Float3 velocity, float duration)
    {
        if (_ready && _api.Cct_TriggerForcedMove != null) _api.Cct_TriggerForcedMove(h, velocity, duration);
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static void CctStopForcedMove(ObjectHandle h)
    {
        if (_ready && _api.Cct_StopForcedMove != null) _api.Cct_StopForcedMove(h);
    }

    public static bool CctIsInForcedMove(ObjectHandle h)
        => _ready && _api.Cct_IsInForcedMove != null && _api.Cct_IsInForcedMove(h) != 0;

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static void CctSetAutomaticRotation(ObjectHandle h, bool useAuto)
    {
        if (_ready && _api.Cct_SetAutomaticRotation != null) _api.Cct_SetAutomaticRotation(h, useAuto ? 1 : 0);
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static void CctSetLookDirection(ObjectHandle h, Float3 direction)
    {
        if (_ready && _api.Cct_SetLookDirection != null) _api.Cct_SetLookDirection(h, direction);
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static void CctClearLookDirection(ObjectHandle h)
    {
        if (_ready && _api.Cct_ClearLookDirection != null) _api.Cct_ClearLookDirection(h);
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static void CctForcedSetPosition(ObjectHandle h, Float3 position)
    {
        if (_ready && _api.Cct_ForcedSetPosition != null) _api.Cct_ForcedSetPosition(h, position);
    }

    public static float CctGetBaseSpeed(ObjectHandle h)
        => _ready && _api.Cct_GetBaseSpeed != null ? _api.Cct_GetBaseSpeed(h) : 0f;

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static void CctSetBaseSpeed(ObjectHandle h, float speed)
    {
        if (_ready && _api.Cct_SetBaseSpeed != null) _api.Cct_SetBaseSpeed(h, speed);
    }

    public static bool CctIsOnMove(ObjectHandle h)
        => _ready && _api.Cct_IsOnMove != null && _api.Cct_IsOnMove(h) != 0;

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static void CctSetOnMove(ObjectHandle h, bool isMove)
    {
        if (_ready && _api.Cct_SetOnMove != null) _api.Cct_SetOnMove(h, isMove ? 1 : 0);
    }

    public static bool CctIsFalling(ObjectHandle h)
        => _ready && _api.Cct_IsFalling != null && _api.Cct_IsFalling(h) != 0;

    public static float CctGetRadius(ObjectHandle h)
        => _ready && _api.Cct_GetRadius != null ? _api.Cct_GetRadius(h) : 0f;

    public static float CctGetHeight(ObjectHandle h)
        => _ready && _api.Cct_GetHeight != null ? _api.Cct_GetHeight(h) : 0f;

    public static uint CctGetId(ObjectHandle h)
        => _ready && _api.Cct_GetId != null ? _api.Cct_GetId(h) : 0u;

    // ── RectTransformComponent ──

    public static bool HasRect(ObjectHandle h)
        => _ready && _api.Rect_Exists != null && _api.Rect_Exists(h) != 0;

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static Float2 RectGetAnchoredPosition(ObjectHandle h)
        => _ready && _api.Rect_GetAnchoredPosition != null ? _api.Rect_GetAnchoredPosition(h) : default;

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static void RectSetAnchoredPosition(ObjectHandle h, Float2 p)
    {
        if (_ready && _api.Rect_SetAnchoredPosition != null) _api.Rect_SetAnchoredPosition(h, p);
    }

    public static Float2 RectGetScreenPosition(ObjectHandle h)
        => _ready && _api.Rect_GetScreenPosition != null ? _api.Rect_GetScreenPosition(h) : default;

    public static void RectSetScreenPosition(ObjectHandle h, Float2 p)
    {
        if (_ready && _api.Rect_SetScreenPosition != null) _api.Rect_SetScreenPosition(h, p);
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static Float2 RectGetSizeDelta(ObjectHandle h)
        => _ready && _api.Rect_GetSizeDelta != null ? _api.Rect_GetSizeDelta(h) : default;

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static void RectSetSizeDelta(ObjectHandle h, Float2 s)
    {
        if (_ready && _api.Rect_SetSizeDelta != null) _api.Rect_SetSizeDelta(h, s);
    }

    public static Float2 RectGetPivot(ObjectHandle h)
        => _ready && _api.Rect_GetPivot != null ? _api.Rect_GetPivot(h) : default;

    public static void RectSetPivot(ObjectHandle h, Float2 p)
    {
        if (_ready && _api.Rect_SetPivot != null) _api.Rect_SetPivot(h, p);
    }

    // ── ImageComponent ──

    public static bool HasImage(ObjectHandle h)
        => _ready && _api.Image_Exists != null && _api.Image_Exists(h) != 0;

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static void ImageSetTexture(ObjectHandle h, int index)
    {
        if (_ready && _api.Image_SetTexture != null) _api.Image_SetTexture(h, index);
    }

    public static int ImageGetTextureCount(ObjectHandle h)
        => _ready && _api.Image_GetTextureCount != null ? _api.Image_GetTextureCount(h) : 0;

    public static Color4 ImageGetColor(ObjectHandle h)
        => _ready && _api.Image_GetColor != null ? _api.Image_GetColor(h) : Color4.White;

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static void ImageSetColor(ObjectHandle h, Color4 c)
    {
        if (_ready && _api.Image_SetColor != null) _api.Image_SetColor(h, c);
    }

    public static float ImageGetClipPercent(ObjectHandle h)
        => _ready && _api.Image_GetClipPercent != null ? _api.Image_GetClipPercent(h) : 0f;

    public static void ImageSetClipPercent(ObjectHandle h, float percent)
    {
        if (_ready && _api.Image_SetClipPercent != null) _api.Image_SetClipPercent(h, percent);
    }

    public static void ImageSetNativeSize(ObjectHandle h)
    {
        if (_ready && _api.Image_SetNativeSize != null) _api.Image_SetNativeSize(h);
    }

    // ── 카메라 ──

    public static bool HasCamera()
        => _ready && _api.Camera_Exists != null && _api.Camera_Exists() != 0;

    public static Float2 CameraGetScreenSize()
        => _ready && _api.Camera_GetScreenSize != null ? _api.Camera_GetScreenSize() : default;

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static Float3 CameraWorldToScreenPoint(Float3 world)
        => _ready && _api.Camera_WorldToScreenPoint != null ? _api.Camera_WorldToScreenPoint(world) : default;

    // ── MeshRenderer · Material ──

    public static bool HasMesh(ObjectHandle h)
        => _ready && _api.Mesh_Exists != null && _api.Mesh_Exists(h) != 0;

    public static void MeshInstantiateMaterial(ObjectHandle h, string newName)
    {
        if (!_ready || _api.Mesh_InstantiateMaterial == null) return;

        byte* buffer = stackalloc byte[NameBufferSize];
        EncodeName(newName, buffer);
        _api.Mesh_InstantiateMaterial(h, buffer);
    }

    public static string MeshGetMaterialName(ObjectHandle h)
    {
        if (!_ready || _api.Mesh_GetMaterialName == null) return string.Empty;

        byte* buffer = stackalloc byte[NameBufferSize];
        int len = _api.Mesh_GetMaterialName(h, buffer, NameBufferSize);
        return len > 0 ? System.Text.Encoding.UTF8.GetString(buffer, len) : string.Empty;
    }

    public static bool MeshSetMaterialFloat(ObjectHandle h, string name, float value)
    {
        if (!_ready || _api.Mesh_SetMaterialFloat == null) return false;

        byte* valueName = stackalloc byte[NameBufferSize];
        EncodeName(name, valueName);

        return _api.Mesh_SetMaterialFloat(h, valueName, value) != 0;
    }

    public static bool MeshSetMaterialInt(ObjectHandle h, string name, int value)
    {
        if (!_ready || _api.Mesh_SetMaterialInt == null) return false;

        byte* valueName = stackalloc byte[NameBufferSize];
        EncodeName(name, valueName);

        return _api.Mesh_SetMaterialInt(h, valueName, value) != 0;
    }

    public static Color4 MeshGetBaseColor(ObjectHandle h)
        => _ready && _api.Mesh_GetBaseColor != null ? _api.Mesh_GetBaseColor(h) : Color4.White;

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static void MeshSetBaseColor(ObjectHandle h, Color4 c)
    {
        if (_ready && _api.Mesh_SetBaseColor != null) _api.Mesh_SetBaseColor(h, c);
    }

    // ── 입력 ──
    // 상태값을 그대로 받는다. 술어 조합은 Input이 한다.

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static KeyState GetKeyState(int key)
        => _ready && _api.Input_GetKeyState != null ? (KeyState)_api.Input_GetKeyState(key) : KeyState.Idle;

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static KeyState GetMouseButtonState(int button)
        => _ready && _api.Input_GetMouseButtonState != null
            ? (KeyState)_api.Input_GetMouseButtonState(button) : KeyState.Idle;

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static KeyState GetControllerButtonState(int index, int button)
        => _ready && _api.Input_GetControllerButtonState != null
            ? (KeyState)_api.Input_GetControllerButtonState(index, button) : KeyState.Idle;

    public static bool InputIsAnyKeyPressed()
        => _ready && _api.Input_IsAnyKeyPressed != null && _api.Input_IsAnyKeyPressed() != 0;

    public static Float2 InputGetMousePosition()
        => _ready && _api.Input_GetMousePosition != null ? _api.Input_GetMousePosition() : default;

    public static Float2 InputGetMouseDelta()
        => _ready && _api.Input_GetMouseDelta != null ? _api.Input_GetMouseDelta() : default;

    public static int InputGetWheelDelta()
        => _ready && _api.Input_GetWheelDelta != null ? _api.Input_GetWheelDelta() : 0;

    public static void InputSetCursorVisible(bool visible)
    {
        if (_ready && _api.Input_SetCursorVisible != null) _api.Input_SetCursorVisible(visible ? 1 : 0);
    }

    public static bool InputIsControllerConnected(int index)
        => _ready && _api.Input_IsControllerConnected != null && _api.Input_IsControllerConnected(index) != 0;

    public static bool InputIsControllerTriggerL(int index)
        => _ready && _api.Input_IsControllerTriggerL != null && _api.Input_IsControllerTriggerL(index) != 0;

    public static bool InputIsControllerTriggerR(int index)
        => _ready && _api.Input_IsControllerTriggerR != null && _api.Input_IsControllerTriggerR(index) != 0;

    public static Float2 InputGetControllerThumbL(int index)
        => _ready && _api.Input_GetControllerThumbL != null ? _api.Input_GetControllerThumbL(index) : default;

    public static Float2 InputGetControllerThumbR(int index)
        => _ready && _api.Input_GetControllerThumbR != null ? _api.Input_GetControllerThumbR(index) : default;

    // ── 물리 질의 ──
    //
    // 결과는 호출자가 준 버퍼에 채운다. Span을 고정해 그대로 넘기므로 복사도 할당도 없다.

    public static bool PhysicsRaycast(Float3 origin, Float3 direction, float distance,
        uint layerMask, out RaycastHit hit)
    {
        hit = default;
        if (!_ready || _api.Physics_Raycast == null) return false;

        fixed (RaycastHit* p = &hit)
        {
            return _api.Physics_Raycast(origin, direction, distance, layerMask, p) != 0;
        }
    }

    public static int PhysicsRaycastAll(Float3 origin, Float3 direction, float distance,
        uint layerMask, Span<RaycastHit> results)
    {
        if (!_ready || _api.Physics_RaycastAll == null || results.IsEmpty) return 0;

        fixed (RaycastHit* p = results)
        {
            return _api.Physics_RaycastAll(origin, direction, distance, layerMask, p, results.Length);
        }
    }

    public static int PhysicsOverlapSphere(Float3 position, float radius,
        uint layerMask, Span<RaycastHit> results)
    {
        if (!_ready || _api.Physics_OverlapSphere == null || results.IsEmpty) return 0;

        fixed (RaycastHit* p = results)
        {
            return _api.Physics_OverlapSphere(position, radius, layerMask, p, results.Length);
        }
    }

    // ── RigidBodyComponent ──

    public static bool HasRigid(ObjectHandle h)
        => _ready && _api.Rigid_Exists != null && _api.Rigid_Exists(h) != 0;

    public static Float3 RigidGetLinearVelocity(ObjectHandle h)
        => _ready && _api.Rigid_GetLinearVelocity != null ? _api.Rigid_GetLinearVelocity(h) : default;

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static void RigidSetLinearVelocity(ObjectHandle h, Float3 v)
    {
        if (_ready && _api.Rigid_SetLinearVelocity != null) _api.Rigid_SetLinearVelocity(h, v);
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static void RigidAddLinearVelocity(ObjectHandle h, Float3 v)
    {
        if (_ready && _api.Rigid_AddLinearVelocity != null) _api.Rigid_AddLinearVelocity(h, v);
    }

    public static Float3 RigidGetAngularVelocity(ObjectHandle h)
        => _ready && _api.Rigid_GetAngularVelocity != null ? _api.Rigid_GetAngularVelocity(h) : default;

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static void RigidSetAngularVelocity(ObjectHandle h, Float3 v)
    {
        if (_ready && _api.Rigid_SetAngularVelocity != null) _api.Rigid_SetAngularVelocity(h, v);
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static void RigidAddForce(ObjectHandle h, Float3 force, int mode)
    {
        if (_ready && _api.Rigid_AddForce != null) _api.Rigid_AddForce(h, force, mode);
    }

    public static void RigidSetBodyType(ObjectHandle h, int bodyType)
    {
        if (_ready && _api.Rigid_SetBodyType != null) _api.Rigid_SetBodyType(h, bodyType);
    }

    public static bool RigidIsKinematic(ObjectHandle h)
        => _ready && _api.Rigid_IsKinematic != null && _api.Rigid_IsKinematic(h) != 0;

    public static void RigidSetKinematic(ObjectHandle h, bool v)
    {
        if (_ready && _api.Rigid_SetKinematic != null) _api.Rigid_SetKinematic(h, v ? 1 : 0);
    }

    public static bool RigidIsTrigger(ObjectHandle h)
        => _ready && _api.Rigid_IsTrigger != null && _api.Rigid_IsTrigger(h) != 0;

    public static void RigidSetIsTrigger(ObjectHandle h, bool v)
    {
        if (_ready && _api.Rigid_SetIsTrigger != null) _api.Rigid_SetIsTrigger(h, v ? 1 : 0);
    }

    public static bool RigidIsColliderEnabled(ObjectHandle h)
        => _ready && _api.Rigid_IsColliderEnabled != null && _api.Rigid_IsColliderEnabled(h) != 0;

    public static void RigidSetColliderEnabled(ObjectHandle h, bool v)
    {
        if (_ready && _api.Rigid_SetColliderEnabled != null) _api.Rigid_SetColliderEnabled(h, v ? 1 : 0);
    }

    public static bool RigidIsUsingGravity(ObjectHandle h)
        => _ready && _api.Rigid_IsUsingGravity != null && _api.Rigid_IsUsingGravity(h) != 0;

    public static void RigidUseGravity(ObjectHandle h, bool v)
    {
        if (_ready && _api.Rigid_UseGravity != null) _api.Rigid_UseGravity(h, v ? 1 : 0);
    }

    public static float RigidGetMass(ObjectHandle h)
        => _ready && _api.Rigid_GetMass != null ? _api.Rigid_GetMass(h) : 0f;

    public static void RigidSetMass(ObjectHandle h, float mass)
    {
        if (_ready && _api.Rigid_SetMass != null) _api.Rigid_SetMass(h, mass);
    }

    public static void RigidSetLinearDamping(ObjectHandle h, float d)
    {
        if (_ready && _api.Rigid_SetLinearDamping != null) _api.Rigid_SetLinearDamping(h, d);
    }

    public static void RigidSetAngularDamping(ObjectHandle h, float d)
    {
        if (_ready && _api.Rigid_SetAngularDamping != null) _api.Rigid_SetAngularDamping(h, d);
    }

    public static void RigidSetScale(ObjectHandle h, Float3 s)
    {
        if (_ready && _api.Rigid_SetScale != null) _api.Rigid_SetScale(h, s);
    }

    public static void RigidSetLockLinear(ObjectHandle h, bool x, bool y, bool z)
    {
        if (_ready && _api.Rigid_SetLockLinear != null) _api.Rigid_SetLockLinear(h, x ? 1 : 0, y ? 1 : 0, z ? 1 : 0);
    }

    public static void RigidSetLockAngular(ObjectHandle h, bool x, bool y, bool z)
    {
        if (_ready && _api.Rigid_SetLockAngular != null) _api.Rigid_SetLockAngular(h, x ? 1 : 0, y ? 1 : 0, z ? 1 : 0);
    }

    // ── 콜라이더 3종 ──

    public static bool HasCollider(ObjectHandle h, int kind)
        => _ready && _api.Collider_Exists != null && _api.Collider_Exists(h, kind) != 0;

    public static float ColliderGetRadius(ObjectHandle h, int kind)
        => _ready && _api.Collider_GetRadius != null ? _api.Collider_GetRadius(h, kind) : 0f;

    public static void ColliderSetRadius(ObjectHandle h, int kind, float v)
    {
        if (_ready && _api.Collider_SetRadius != null) _api.Collider_SetRadius(h, kind, v);
    }

    public static float ColliderGetHeight(ObjectHandle h, int kind)
        => _ready && _api.Collider_GetHeight != null ? _api.Collider_GetHeight(h, kind) : 0f;

    public static void ColliderSetHeight(ObjectHandle h, int kind, float v)
    {
        if (_ready && _api.Collider_SetHeight != null) _api.Collider_SetHeight(h, kind, v);
    }

    public static Float3 ColliderGetExtents(ObjectHandle h, int kind)
        => _ready && _api.Collider_GetExtents != null ? _api.Collider_GetExtents(h, kind) : default;

    public static void ColliderSetExtents(ObjectHandle h, int kind, Float3 v)
    {
        if (_ready && _api.Collider_SetExtents != null) _api.Collider_SetExtents(h, kind, v);
    }

    public static Float3 ColliderGetPositionOffset(ObjectHandle h, int kind)
        => _ready && _api.Collider_GetPositionOffset != null ? _api.Collider_GetPositionOffset(h, kind) : default;

    public static void ColliderSetPositionOffset(ObjectHandle h, int kind, Float3 v)
    {
        if (_ready && _api.Collider_SetPositionOffset != null) _api.Collider_SetPositionOffset(h, kind, v);
    }

    public static float ColliderGetRestitution(ObjectHandle h, int kind)
        => _ready && _api.Collider_GetRestitution != null ? _api.Collider_GetRestitution(h, kind) : 0f;

    public static void ColliderSetRestitution(ObjectHandle h, int kind, float v)
    {
        if (_ready && _api.Collider_SetRestitution != null) _api.Collider_SetRestitution(h, kind, v);
    }

    public static float ColliderGetStaticFriction(ObjectHandle h, int kind)
        => _ready && _api.Collider_GetStaticFriction != null ? _api.Collider_GetStaticFriction(h, kind) : 0f;

    public static void ColliderSetStaticFriction(ObjectHandle h, int kind, float v)
    {
        if (_ready && _api.Collider_SetStaticFriction != null) _api.Collider_SetStaticFriction(h, kind, v);
    }

    public static float ColliderGetDynamicFriction(ObjectHandle h, int kind)
        => _ready && _api.Collider_GetDynamicFriction != null ? _api.Collider_GetDynamicFriction(h, kind) : 0f;

    public static void ColliderSetDynamicFriction(ObjectHandle h, int kind, float v)
    {
        if (_ready && _api.Collider_SetDynamicFriction != null) _api.Collider_SetDynamicFriction(h, kind, v);
    }

    // ── TextComponent ──

    public static bool HasText(ObjectHandle h)
        => _ready && _api.Text_Exists != null && _api.Text_Exists(h) != 0;

    public static string TextGetMessage(ObjectHandle h)
    {
        if (!_ready || _api.Text_GetMessage == null) return string.Empty;

        // 대사 한 줄이 들어갈 만큼은 되어야 한다. 넘치면 잘린다.
        const int cap = 1024;
        byte* buffer = stackalloc byte[cap];
        int len = _api.Text_GetMessage(h, buffer, cap);
        return len > 0 ? System.Text.Encoding.UTF8.GetString(buffer, len) : string.Empty;
    }

    public static void TextSetMessage(ObjectHandle h, string message)
    {
        if (!_ready || _api.Text_SetMessage == null) return;

        const int cap = 1024;
        byte* buffer = stackalloc byte[cap];
        int written = System.Text.Encoding.UTF8.GetBytes(message, new Span<byte>(buffer, cap - 1));
        buffer[written] = 0;
        _api.Text_SetMessage(h, buffer);
    }

    public static Color4 TextGetColor(ObjectHandle h)
        => _ready && _api.Text_GetColor != null ? _api.Text_GetColor(h) : Color4.White;

    public static void TextSetColor(ObjectHandle h, Color4 c)
    {
        if (_ready && _api.Text_SetColor != null) _api.Text_SetColor(h, c);
    }

    public static float TextGetAlpha(ObjectHandle h)
        => _ready && _api.Text_GetAlpha != null ? _api.Text_GetAlpha(h) : 0f;

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static void TextSetAlpha(ObjectHandle h, float alpha)
    {
        if (_ready && _api.Text_SetAlpha != null) _api.Text_SetAlpha(h, alpha);
    }

    public static float TextGetFontSize(ObjectHandle h)
        => _ready && _api.Text_GetFontSize != null ? _api.Text_GetFontSize(h) : 0f;

    public static void TextSetFontSize(ObjectHandle h, float size)
    {
        if (_ready && _api.Text_SetFontSize != null) _api.Text_SetFontSize(h, size);
    }

    public static Float2 TextGetRelativePosition(ObjectHandle h)
        => _ready && _api.Text_GetRelativePosition != null ? _api.Text_GetRelativePosition(h) : default;

    public static void TextSetRelativePosition(ObjectHandle h, Float2 p)
    {
        if (_ready && _api.Text_SetRelativePosition != null) _api.Text_SetRelativePosition(h, p);
    }

    // ── UIComponent 공통 ──

    public static int UiGetOrder(ObjectHandle h)
        => _ready && _api.Ui_GetOrder != null ? _api.Ui_GetOrder(h) : 0;

    public static void UiSetOrder(ObjectHandle h, int order)
    {
        if (_ready && _api.Ui_SetOrder != null) _api.Ui_SetOrder(h, order);
    }

    // ── Canvas ──

    public static bool HasCanvas(ObjectHandle h)
        => _ready && _api.Canvas_Exists != null && _api.Canvas_Exists(h) != 0;

    public static int CanvasGetOrder(ObjectHandle h)
        => _ready && _api.Canvas_GetOrder != null ? _api.Canvas_GetOrder(h) : 0;

    public static void CanvasSetOrder(ObjectHandle h, int order)
    {
        if (_ready && _api.Canvas_SetOrder != null) _api.Canvas_SetOrder(h, order);
    }

    public static string CanvasGetName(ObjectHandle h)
    {
        if (!_ready || _api.Canvas_GetName == null) return string.Empty;

        byte* buffer = stackalloc byte[NameBufferSize];
        int len = _api.Canvas_GetName(h, buffer, NameBufferSize);
        return len > 0 ? System.Text.Encoding.UTF8.GetString(buffer, len) : string.Empty;
    }

    public static void CanvasSetName(ObjectHandle h, string name)
    {
        if (!_ready || _api.Canvas_SetName == null) return;

        byte* buffer = stackalloc byte[NameBufferSize];
        EncodeName(name, buffer);
        _api.Canvas_SetName(h, buffer);
    }

    // ── UI 내비게이션·버튼·Image 잔여 ──

    public static bool UiIsSelected(ObjectHandle h)
        => _ready && _api.Ui_IsSelected != null && _api.Ui_IsSelected(h) != 0;

    public static bool UiIsNavLocked(ObjectHandle h)
        => _ready && _api.Ui_IsNavLocked != null && _api.Ui_IsNavLocked(h) != 0;

    public static void UiSetNavLock(ObjectHandle h, bool locked)
    {
        if (_ready && _api.Ui_SetNavLock != null) _api.Ui_SetNavLock(h, locked ? 1 : 0);
    }

    public static ObjectHandle UiNavGetSelected()
        => _ready && _api.UiNav_GetSelected != null ? _api.UiNav_GetSelected() : ObjectHandle.Invalid;

    public static void UiNavSetSelected(ObjectHandle h)
    {
        if (_ready && _api.UiNav_SetSelected != null) _api.UiNav_SetSelected(h);
    }

    public static bool HasButton(ObjectHandle h)
        => _ready && _api.Button_Exists != null && _api.Button_Exists(h) != 0;

    public static bool ButtonConsumeClicked(ObjectHandle h)
        => _ready && _api.Button_ConsumeClicked != null && _api.Button_ConsumeClicked(h) != 0;

    public static int ImageGetTextureIndex(ObjectHandle h)
        => _ready && _api.Image_GetTextureIndex != null ? _api.Image_GetTextureIndex(h) : 0;

    public static float ImageGetRotation(ObjectHandle h)
        => _ready && _api.Image_GetRotation != null ? _api.Image_GetRotation(h) : 0f;

    public static void ImageSetRotation(ObjectHandle h, float rotation)
    {
        if (_ready && _api.Image_SetRotation != null) _api.Image_SetRotation(h, rotation);
    }

    // Color4를 (x, y, width, height) 운반체로 재활용한다 — 4 float 블리터블이면 충분하다.
    public static Color4 RectGetWorldRect(ObjectHandle h)
        => _ready && _api.Rect_GetWorldRect != null ? _api.Rect_GetWorldRect(h) : default;
}






