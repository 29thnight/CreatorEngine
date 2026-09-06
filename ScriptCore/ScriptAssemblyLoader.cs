using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.Loader;

namespace CreatorEngine;

/// <summary>
/// 게임 스크립트 어셈블리를 언로드 가능한 컨텍스트에 올린다.
///
/// ScriptCore 자체는 기본 컨텍스트에 상주한다 — 네이티브가 붙잡고 있는 진입점
/// 함수 포인터가 여기 있기 때문에, 이쪽이 언로드되면 엔진이 죽는다.
/// 교체되는 것은 GameScripts 쪽뿐이다.
///
/// 언로드는 참조가 하나라도 남으면 실패한다. 그래서 리로드 전에 인스턴스를 전부
/// 버리고(ScriptRegistry.Clear) 팩토리 표도 비운다. 표에 담긴 델리게이트가
/// 스크립트 타입을 가리키고 있어, 이것 하나만 남아도 컨텍스트가 살아남는다.
/// </summary>
internal sealed class ScriptLoadContext(string name) : AssemblyLoadContext(name, isCollectible: true)
{
    // 지금 실행 중인 ScriptCore. 호스트가 hostfxr로 직접 올린 것이라
    // 일반적인 probing 경로로는 찾을 수 없어(FileNotFoundException) 여기서 직접 물려준다.
    private static readonly Assembly CoreAssembly = typeof(Component).Assembly;

    protected override Assembly? Load(AssemblyName assemblyName)
    {
        // 공용 어셈블리는 반드시 이미 로드된 것을 그대로 쓴다.
        // 새로 로드하면 같은 타입이 두 벌 생겨 Component 캐스팅이 조용히 깨진다.
        if (assemblyName.Name == CoreAssembly.GetName().Name)
        {
            return CoreAssembly;
        }

        // 나머지(System.* 등)는 기본 컨텍스트가 해석하게 둔다.
        return null;
    }
}

internal static class ScriptAssemblyLoader
{
    private static ScriptLoadContext? _context;
    private static WeakReference? _unloadProbe;
    private static string? _assemblyPath;
    private static int _generation;

    /// <summary>몇 번째 로드인지. 리로드가 실제로 일어났는지 확인하는 데 쓴다.</summary>
    public static int Generation => _generation;

    public static bool IsLoaded => _context is not null;

    /// <summary>
    /// 지금 올라와 있는 게임 스크립트 어셈블리.
    ///
    /// ★ **결과를 붙들지 말 것.** 정적 필드에 <see cref="Assembly"/> 를 담으면 그것이
    ///   컨텍스트를 붙들어 언로드가 영영 실패하고, 다음 리로드부터 같은 타입이 두 벌이
    ///   된다. 컨텍스트에서 매번 새로 물어 오는 이유가 그것이다 — 여기에도 캐시가 없다.
    ///
    /// 호출자(<see cref="EngineCallableInvoker"/>)는 이것을 지역에 두었다 곧 버린다.
    /// </summary>
    public static Assembly[] LoadedAssemblies()
        => _context is null ? [] : [.. _context.Assemblies];

    /// <summary>
    /// 스크립트 어셈블리를 로드하고 등록을 수행한다.
    /// 파일을 잠그지 않도록 바이트로 읽어 올린다 — 잠그면 다음 빌드가 실패한다.
    /// </summary>
    // 로드에 쓰는 지역변수(Assembly·Type·MethodInfo)가 호출자 프레임에 남지 않도록 분리한다.
    // 인라인되면 그 참조들이 리로드 시점까지 스택에 살아 있어 언로드가 막힌다.
    [MethodImpl(MethodImplOptions.NoInlining)]
    public static bool Load(string assemblyPath)
    {
        if (_context is not null)
        {
            Native.Log(2, "[ScriptCore] 이미 로드되어 있습니다. 먼저 Unload 하세요.");
            return false;
        }

        if (!File.Exists(assemblyPath))
        {
            Native.Log(2, $"[ScriptCore] 스크립트 어셈블리가 없습니다: {assemblyPath}");
            return false;
        }

        _assemblyPath = assemblyPath;
        _context = new ScriptLoadContext($"GameScripts#{_generation}");

        try
        {
            Assembly assembly;
            using (var stream = new FileStream(assemblyPath, FileMode.Open, FileAccess.Read, FileShare.ReadWrite))
            {
                // 심볼도 함께 올려야 예외 스택에 줄 번호가 남는다.
                string pdbPath = Path.ChangeExtension(assemblyPath, ".pdb");
                if (File.Exists(pdbPath))
                {
                    using var pdb = new FileStream(pdbPath, FileMode.Open, FileAccess.Read, FileShare.ReadWrite);
                    assembly = _context.LoadFromStream(stream, pdb);
                }
                else
                {
                    assembly = _context.LoadFromStream(stream);
                }
            }

            if (!InvokeRegistry(assembly))
            {
                Native.Log(2, "[ScriptCore] ScriptRegistry를 찾지 못했습니다 — 등록된 스크립트가 없습니다.");
            }

            ++_generation;
            return true;
        }
        catch (Exception ex)
        {
            Native.Log(3, $"[ScriptCore] 스크립트 어셈블리 로드 실패\n{ex}");
            Unload();
            return false;
        }
    }

    /// <summary>
    /// 생성기가 만든 등록 진입점을 찾아 호출한다.
    ///
    /// 리플렉션은 여기 한 곳뿐이다. AOT 배포에서는 스크립트를 정적 링크하고
    /// 이 경로 대신 직접 호출로 바꾸면 된다(설계 문서 06절 모드 분리).
    /// </summary>
    [MethodImpl(MethodImplOptions.NoInlining)]
    private static bool InvokeRegistry(Assembly assembly)
        => InvokeRegistry(assembly, ScriptFactory.Register,
                          AniBehaviorFactory.Register, BTNodeFactory.Register);

    /// <summary>
    /// 생성기가 만든 등록 진입점을 찾아, 주어진 sink 로 등록을 흘린다.
    ///
    /// sink 를 인자로 받는 이유는 <see cref="Validate"/> 때문이다. 검증은 **실제
    /// 등록 경로를 그대로 태우되** 살아 있는 표에는 아무것도 넣지 않아야 한다.
    /// 경로가 다르면 검증이 통과한 것이 진짜 통과가 아니게 된다.
    /// </summary>
    [MethodImpl(MethodImplOptions.NoInlining)]
    private static bool InvokeRegistry(Assembly assembly,
                                       Action<string, Func<Component>> register,
                                       Action<string, Func<AniBehavior>> registerAni,
                                       Action<string, int, Func<BTNode>> registerBT)
    {
        Type? registry = assembly.GetType("CreatorEngine.Generated.ScriptRegistry", throwOnError: false);
        MethodInfo? method = registry?.GetMethod("RegisterAll", BindingFlags.Public | BindingFlags.Static);
        if (method is null) return false;

        method.Invoke(null, [register]);

        // 애니메이션 상태 스크립트는 별도 목록이다. 없는 프로젝트도 있으므로 없으면 건너뛴다.
        MethodInfo? aniMethod = registry?.GetMethod("RegisterAllAni", BindingFlags.Public | BindingFlags.Static);
        if (aniMethod is not null)
        {
            aniMethod.Invoke(null, [registerAni]);
        }

        // 행동 트리의 사용자 노드도 마찬가지 — 구 생성기로 만든 어셈블리에는 없다.
        MethodInfo? btMethod = registry?.GetMethod("RegisterAllBTNodes", BindingFlags.Public | BindingFlags.Static);
        if (btMethod is not null)
        {
            btMethod.Invoke(null, [registerBT]);
        }

        return true;
    }

    /// <summary>
    /// 갈아 끼우기 전에 새 어셈블리가 실제로 올라가는지 **버리는 컨텍스트에서** 확인한다.
    ///
    /// ★ 왜 필요한가 — 실측된 결함이다(LC7).
    ///
    ///   <see cref="Reload"/> 는 `Unload(); Load();` 였다. 그래서 새 어셈블리가
    ///   깨져 있으면 이전 것은 이미 사라진 뒤였고, 에디터에는 **스크립트가 하나도
    ///   남지 않았다.** 실측으로 확인했다 — 리로드 전에는 붙던 `script.add Bobber`
    ///   가 리로드 실패 뒤에는 "부착 실패(타입=Bobber)" 가 됐다. 복구 방법은
    ///   성공적 리로드나 프로세스 재시작뿐이었다.
    ///
    ///   빌드가 깨진 채로 리로드를 부르는 것은 드문 일이 아니라 **가장 흔한 일**이다.
    ///   그때마다 에디터가 못 쓰는 상태가 되면 라이브 코드 교체라고 부를 수 없다.
    ///
    /// ★ 무엇까지 잡는가 — 정직하게.
    ///
    ///   파일 부재·손상·아키텍처 불일치·등록 진입점 부재, 그리고 `RegisterAll` 이
    ///   던지는 예외까지 잡는다. 검증이 실제 등록 경로를 그대로 태우기 때문이다.
    ///   남는 창은 "검증에서는 성공했는데 본 적재에서 실패" 하는 비결정적 경우뿐이고,
    ///   그때는 예전과 같은 상태가 된다 — 그 창을 0 으로 만들려면 세 팩토리에
    ///   staging 을 넣어야 하고, 그것은 이 슬라이스의 크기가 아니다.
    /// </summary>
    [MethodImpl(MethodImplOptions.NoInlining)]
    private static bool Validate(string assemblyPath)
    {
        if (!File.Exists(assemblyPath))
        {
            Native.Log(2, $"[ScriptCore] 리로드 취소 — 어셈블리가 없습니다: {assemblyPath}");
            return false;
        }

        var probe = new ScriptLoadContext("validate");
        try
        {
            Assembly assembly;
            using (var stream = new FileStream(assemblyPath, FileMode.Open, FileAccess.Read, FileShare.ReadWrite))
            {
                assembly = probe.LoadFromStream(stream);
            }

            // 등록을 **버린다.** 살아 있는 표는 이 호출로 한 항목도 바뀌지 않는다.
            if (!InvokeRegistry(assembly, static (_, _) => { }, static (_, _) => { }, static (_, _, _) => { }))
            {
                Native.Log(2, "[ScriptCore] 리로드 취소 — ScriptRegistry 진입점이 없습니다.");
                return false;
            }
            return true;
        }
        catch (Exception ex)
        {
            Native.Log(3, $"[ScriptCore] 리로드 취소 — 새 어셈블리를 올릴 수 없습니다. 이전 것을 유지합니다.\n{ex}");
            return false;
        }
        finally
        {
            probe.Unload();
        }
    }

    /// <summary>
    /// 컨텍스트를 내린다. 인스턴스와 팩토리 표를 먼저 비워야 실제로 언로드된다.
    /// </summary>
    [MethodImpl(MethodImplOptions.NoInlining)]
    public static void Unload()
    {
        if (_context is null) return;

        // 스크립트 타입을 가리키는 참조를 전부 끊는다.
        ScriptRegistry.Clear();
        ScriptFactory.ClearRegistrations();
        AniBehaviorFactory.ClearRegistrations();
        BTNodeFactory.ClearRegistrations();

        // 살아 있는 트리도 끊어야 한다. BTNodeFactory를 비우는 것은 '앞으로 만들지
        // 않겠다'일 뿐, 이미 만들어진 노드 인스턴스는 그대로 남아 스크립트 타입을
        // 붙든다 — 그러면 컨텍스트가 언로드되지 않고 다음 리로드부터 타입이 두 벌이 된다.
        //
        // 여기가 빠져 있었다. 팩토리 표 셋만 비우고 인스턴스 목록은 ScriptRegistry만
        // 비웠는데, 트리는 ScriptRegistry가 아니라 자기 목록에 있다(수명 주체가
        // 달라 따로 둔 것이다 — BehaviorTreeRegistry 주석 참고).
        BehaviorTreeRegistry.Clear();

        // 내리는 이 컨텍스트를 추적한다. Load에서 잡으면 안 된다 —
        // 그러면 항상 "현재 살아 있는" 컨텍스트를 보게 되어 판정이 늘 참이 된다.
        _unloadProbe = new WeakReference(_context, trackResurrection: false);

        _context.Unload();
        _context = null;

        // 여기서 언로드 완료를 확인하려 하면 안 된다.
        // 언로드는 비동기이고, 컨텍스트의 코드를 거쳐 온 스택 프레임이 남아 있는 동안에는
        // 절대 끝나지 않는다. 지금은 Reload → Unload 호출 스택 위라 항상 "살아 있음"으로 나온다.
        // 실제 판정은 스택이 풀린 뒤 IsPreviousContextAlive로 한다.
        GC.Collect();
        GC.WaitForPendingFinalizers();
    }

    /// <summary>내렸다가 다시 올린다. 경로는 마지막에 쓴 것을 재사용한다.</summary>
    public static bool Reload()
    {
        string? path = _assemblyPath;
        if (path is null)
        {
            Native.Log(2, "[ScriptCore] 로드한 적이 없어 리로드할 수 없습니다.");
            return false;
        }

        // ★ 갈아 끼우기 **전에** 새 것이 올라가는지 확인한다.
        //
        //   예전에는 `Unload(); Load();` 였다. 새 어셈블리가 깨져 있으면 이전
        //   것은 이미 사라진 뒤라 에디터에 스크립트가 하나도 남지 않았다.
        //   검증이 실패하면 여기서 멈추므로 이전 어셈블리는 그대로다.
        if (!Validate(path)) return false;

        Unload();
        return Load(path);
    }

    /// <summary>
    /// 이전 컨텍스트가 아직 메모리에 남아 있는가.
    ///
    /// 리로드를 호출한 스택이 완전히 풀린 뒤에 물어야 의미가 있다 —
    /// 리로드 직후에 부르면 항상 true다. 몇 프레임 지난 뒤 확인할 것.
    /// true가 계속 유지되면 스크립트 타입을 붙잡은 참조가 어딘가 남았다는 뜻이다.
    /// </summary>
    [MethodImpl(MethodImplOptions.NoInlining)]
    public static bool IsPreviousContextAlive()
    {
        if (_unloadProbe is null) return false;

        // 판정 직전에 수거를 유도한다. 언로드는 여러 사이클에 걸쳐 끝날 수 있다.
        GC.Collect();
        GC.WaitForPendingFinalizers();
        GC.Collect();

        if (_unloadProbe.IsAlive) return true;

        _unloadProbe = null;
        return false;
    }
}


