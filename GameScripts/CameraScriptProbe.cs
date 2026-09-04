namespace CreatorEngine.Scripts;

/// <summary>
/// <c>CameraComponent</c> 래퍼의 경계 왕복 검증(W2).
///
/// 대상은 에디터 기본 씬의 "Main Camera"다(EditorMain.cpp의 newSceneCreatedEvent가
/// 만들고 SetPrimary(true)를 준다 — 나머지는 Camera 클래스의 기본값 그대로).
///
/// ── LightComponent 프로브와 다른 점 ──
///
/// 카메라에는 dirty 축이 없다. 렌더 프록시를 쓰지 않고 매 프레임
/// <c>CaptureFrameSnapshot</c>으로 읽히므로, 값을 넣으면 다음 프레임에 반영된다.
/// 그래서 이 프로브의 판정이 곧 게이트의 전부다.
///
/// 대신 <see cref="Camera.Main"/>과의 일치를 함께 본다. 그것이 컴포넌트 축과
/// 전역 접근점이 같은 카메라를 가리키는지 재는 유일한 자리다.
/// </summary>
public sealed partial class CameraScriptProbe : Component
{
    private const float Epsilon = 1e-4f;

    private int _passed;
    private int _failed;

    public override void OnInitialized()
    {
        CameraComponent? camera = Entity.GetComponent<CameraComponent>();
        if (camera is null)
        {
            LogError($"[CameraScriptProbe] '{Entity.Name}'에 CameraComponent가 없다 — 대상 오브젝트가 맞는지 확인할 것");
            return;
        }

        Log($"[CameraScriptProbe] 시작 — fov={camera.FieldOfView} near={camera.NearPlane} " +
            $"far={camera.FarPlane} primary={camera.IsPrimary}");

        CheckDefaults(camera);
        CheckRoundTrip(camera);
        CheckPrimary(camera);
        CheckGlobalAccess();
        CheckAbsence();

        if (_failed == 0) Log($"[CameraScriptProbe] 전체 통과 ({_passed}건)");
        else LogError($"[CameraScriptProbe] {_failed}건 실패 / {_passed}건 통과");
    }

    /// <summary>기본값을 그대로 읽어 오는가 — 경계가 값을 뭉개지 않는지.</summary>
    private void CheckDefaults(CameraComponent camera)
    {
        Assert("기본 fov=60", Near(camera.FieldOfView, 60f), $"{camera.FieldOfView}");
        Assert("기본 near=0.1", Near(camera.NearPlane, 0.1f), $"{camera.NearPlane}");
        Assert("기본 far=500", Near(camera.FarPlane, 500f), $"{camera.FarPlane}");
        Assert("기본 씬 카메라는 primary", camera.IsPrimary, "false");
    }

    /// <summary>
    /// 쓴 값이 그대로 돌아오는가.
    ///
    /// 되돌리지 않는다 — 이 프로브가 끝난 뒤 씬을 계속 쓰지 않고, 오히려
    /// 값이 남아 있어야 나중에 다른 관측 수단이 생겼을 때 대조할 것이 있다.
    /// </summary>
    private void CheckRoundTrip(CameraComponent camera)
    {
        camera.FieldOfView = 42.5f;
        Assert("fov 왕복", Near(camera.FieldOfView, 42.5f), $"{camera.FieldOfView}");

        camera.NearPlane = 0.25f;
        Assert("near 왕복", Near(camera.NearPlane, 0.25f), $"{camera.NearPlane}");

        camera.FarPlane = 1234.5f;
        Assert("far 왕복", Near(camera.FarPlane, 1234.5f), $"{camera.FarPlane}");
    }

    /// <summary>
    /// primary 표시가 왕복하는가.
    ///
    /// 끈 상태에서도 Camera.Main은 이 카메라를 돌려준다 — 씬에 카메라가 하나뿐이면
    /// 엔진이 "켜져 있는 것 중 가장 작은 인스턴스 ID"로 물러서기 때문이다
    /// (CameraSystem::GetPrimaryCamera). 그 폴백까지 함께 단정한다.
    /// </summary>
    private void CheckPrimary(CameraComponent camera)
    {
        camera.IsPrimary = false;
        Assert("primary 끄기 왕복", !camera.IsPrimary, "true");
        Assert("꺼도 Camera.Main은 폴백으로 이 카메라",
            Camera.Main is not null && Camera.Main.Entity == Entity, $"{Camera.Main?.Entity.Name}");

        camera.IsPrimary = true;
        Assert("primary 켜기 왕복", camera.IsPrimary, "false");
    }

    /// <summary>전역 접근점과 컴포넌트 축이 같은 카메라를 가리키는가.</summary>
    private void CheckGlobalAccess()
    {
        Assert("Camera.Exists", Camera.Exists, "false");

        CameraComponent? main = Camera.Main;
        Assert("Camera.Main이 있다", main is not null, "null");
        if (main is null) return;

        Assert("Camera.Main이 이 오브젝트", main.Entity == Entity, $"{main.Entity.Name}");

        Float2 size = Camera.ScreenSize;
        Assert("ScreenSize가 양수", size.X > 0f && size.Y > 0f, $"{size}");
    }

    /// <summary>
    /// 카메라가 없는 오브젝트에서는 null이 나와야 한다.
    ///
    /// 이 단정이 없으면 <c>Camera_Exists</c>가 늘 1을 돌려주도록 망가져도
    /// 위 검사들이 전부 통과한다 — 대상에는 실제로 카메라가 있으니까.
    /// </summary>
    private void CheckAbsence()
    {
        Entity light = Entity.Find("Directional Light");
        if (!light.IsAlive)
        {
            LogError("[CameraScriptProbe] 'Directional Light'를 찾지 못했다 — 씬 구성이 바뀌었다");
            ++_failed;
            return;
        }

        Assert($"'{light.Name}'에는 CameraComponent가 없다",
            light.GetComponent<CameraComponent>() is null, "찾아졌습니다");
    }

    private static bool Near(float a, float b) => System.MathF.Abs(a - b) < Epsilon;

    private void Assert(string name, bool ok, string detail)
    {
        if (ok) { ++_passed; return; }

        ++_failed;
        LogError($"[CameraScriptProbe] 실패: {name} — {detail}");
    }
}
