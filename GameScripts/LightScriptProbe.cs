namespace CreatorEngine.Scripts;

/// <summary>
/// <c>LightComponent</c> 래퍼의 경계 왕복 검증(W2).
///
/// 이 프로브는 <b>스크립트에서 보이는 것</b>만 판정한다. 값을 쓰고 되읽으면
/// 언제나 새 값이 나오므로, 이것만으로는 그 값이 렌더 쪽으로 가는지 알 수 없다 —
/// dirty가 실제로 섰는지는 회귀 스크립트가 <c>light.proxy</c>의 누계로 따로
/// 본다(verify-light-script.ps1).
///
/// 대상은 <b>에디터 기본 씬</b>의 "Directional Light" 오브젝트다(EditorMain.cpp의
/// newSceneCreatedEvent가 만든다 — status만 StaticShadows이고 나머지는 기본값).
/// 저작 씬을 열지 않는 이유: <c>--script</c> 헤드리스는 렌더 프레임이 거의 돌지
/// 않아 프록시 커맨드가 소비되지 않는다. 씬을 전환하면 새 광원의 등록도 옛 광원의
/// 해제도 그 큐로 가므로 어느 쪽도 반영되지 않는다(2026-09-04 실측 — queued는
/// 늘고 applied는 멈춘다). 렌더가 실제로 도는 하네스가 생기면 저작 씬으로
/// 옮기는 편이 낫다 — 기본 씬은 자산이 아니라 코드가 만드는 것이라, 그 정책이
/// 바뀌면 이 프로브의 기준값도 함께 바뀐다.
/// </summary>
public sealed partial class LightScriptProbe : Component
{
    private const float Epsilon = 1e-4f;

    private static int _completed;
    [EngineCallable] public static string Results() => FormattableString.Invariant($"{{\"completed\":{_completed},\"passed\":{_passed},\"failed\":{_failed}}}");

    private static int _passed;
    private static int _failed;

    public override void OnInitialized()
    {
        LightComponent? light = Entity.GetComponent<LightComponent>();
        if (light is null)
        {
            LogError($"[LightScriptProbe] '{Entity.Name}'에 LightComponent가 없다 — 대상 오브젝트가 맞는지 확인할 것");
            return;
        }

        Log($"[LightScriptProbe] 시작 — color={light.Color} intensity={light.Intensity} " +
            $"range={light.Range} spot={light.SpotAngle} type={light.Type} status={light.Status}");

        CheckAuthoredValues(light);
        CheckRoundTrip(light);
        CheckEnumGuards(light);
        CheckAbsence();

        ++_completed;
        if (_failed == 0) Log($"[LightScriptProbe] 전체 통과 ({_passed}건)");
        else LogError($"[LightScriptProbe] {_failed}건 실패 / {_passed}건 통과");
    }

    /// <summary>저작값을 그대로 읽어 오는가 — 경계가 값을 뭉개지 않는지.</summary>
    private void CheckAuthoredValues(LightComponent light)
    {
        Assert("저작 intensity=1", Near(light.Intensity, 1f), $"{light.Intensity}");
        Assert("저작 range=10", Near(light.Range, 10f), $"{light.Range}");
        Assert("저작 spotAngle=30", Near(light.SpotAngle, 30f), $"{light.SpotAngle}");
        Assert("저작 type=Directional", light.Type == LightType.Directional, $"{light.Type}");
        Assert("저작 status=StaticShadows", light.Status == LightStatus.StaticShadows, $"{light.Status}");

        Color4 c = light.Color;
        Assert("저작 color=(1,1,1,1)",
            Near(c.R, 1f) && Near(c.G, 1f) && Near(c.B, 1f) && Near(c.A, 1f), $"{c}");
    }

    /// <summary>쓴 값이 그대로 돌아오는가. 마지막에 저작값으로 되돌린다.</summary>
    private void CheckRoundTrip(LightComponent light)
    {
        light.Intensity = 4.25f;
        Assert("intensity 왕복", Near(light.Intensity, 4.25f), $"{light.Intensity}");

        light.Range = 33.5f;
        Assert("range 왕복", Near(light.Range, 33.5f), $"{light.Range}");

        light.SpotAngle = 47.5f;
        Assert("spotAngle 왕복", Near(light.SpotAngle, 47.5f), $"{light.SpotAngle}");

        light.Color = new Color4(0.25f, 0.5f, 0.75f, 1f);
        Color4 c = light.Color;
        Assert("color 왕복",
            Near(c.R, 0.25f) && Near(c.G, 0.5f) && Near(c.B, 0.75f), $"{c}");

        light.Type = LightType.Spot;
        Assert("type 왕복", light.Type == LightType.Spot, $"{light.Type}");

        light.Status = LightStatus.Enabled;
        Assert("status 왕복", light.Status == LightStatus.Enabled, $"{light.Status}");

        // 회귀 스크립트가 프록시에서 이 값을 찾는다. 되돌리지 않는다 —
        // 되돌리면 프록시 축을 재는 쪽이 볼 것이 없어진다.
        light.Intensity = 4.25f;
        light.Type = LightType.Spot;
    }

    /// <summary>범위를 벗어난 열거는 무시되어야 한다 — 정의되지 않은 광원을 만들지 않는다.</summary>
    private void CheckEnumGuards(LightComponent light)
    {
        LightType before = light.Type;
        light.Type = (LightType)99;
        Assert("범위 밖 type은 무시", light.Type == before, $"{light.Type}");

        LightStatus statusBefore = light.Status;
        light.Status = (LightStatus)(-1);
        Assert("범위 밖 status는 무시", light.Status == statusBefore, $"{light.Status}");
    }

    /// <summary>
    /// 라이트가 없는 오브젝트에서는 null이 나와야 한다.
    ///
    /// 이 단정이 없으면 <c>Light_Exists</c>가 늘 1을 돌려주도록 망가져도
    /// 위 검사들이 전부 통과한다 — 대상 오브젝트에는 실제로 라이트가 있으니까.
    ///
    /// 대조군은 기본 씬의 "Main Camera"다. 같은 씬에 있고 라이트가 없다.
    /// </summary>
    private void CheckAbsence()
    {
        Entity camera = Entity.Find("Main Camera");
        if (!camera.IsAlive)
        {
            LogError("[LightScriptProbe] 'Main Camera'를 찾지 못했다 — 씬 구성이 바뀌었다");
            ++_failed;
            return;
        }

        Assert($"'{camera.Name}'에는 LightComponent가 없다", camera.GetComponent<LightComponent>() is null, "찾아졌습니다");
    }

    private static bool Near(float a, float b) => System.MathF.Abs(a - b) < Epsilon;

    private void Assert(string name, bool ok, string detail)
    {
        if (ok) { ++_passed; return; }

        ++_failed;
        LogError($"[LightScriptProbe] 실패: {name} — {detail}");
    }
}
