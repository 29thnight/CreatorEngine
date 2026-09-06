using System.Globalization;
using System.Reflection;
using System.Runtime.CompilerServices;

namespace CreatorEngine;

/// <summary>결과 갈래. 네이티브 <c>ClrHost::InvokeOutcome</c> 과 값이 같아야 한다.</summary>
internal enum InvokeOutcome
{
    Ok = 0,

    /// <summary>게임 스크립트 어셈블리가 올라와 있지 않다.</summary>
    NoAssembly = -1,

    TypeNotFound = -2,
    TypeAmbiguous = -3,
    MethodNotFound = -4,
    MethodAmbiguous = -5,

    /// <summary>있지만 <see cref="EngineCallableAttribute"/> 가 없다. **이것이 경계다.**</summary>
    NotMarked = -6,

    ArgumentMismatch = -7,

    /// <summary>사용자 코드가 던졌다. 에디터는 죽지 않는다.</summary>
    Threw = -8,

    Internal = -9,
}

/// <summary>
/// 이름으로 지목된 <b>표식된</b> static 메서드를 호출한다(L3-B · §10.2).
///
/// ── 왜 캐시가 하나도 없는가 ─────────────────────────────────────────────
///
/// <see cref="Type"/> 이나 <see cref="MethodInfo"/> 를 정적 필드에 담으면 그것이
/// 게임 스크립트 어셈블리를 붙들고, 붙들린 <c>ScriptLoadContext</c> 는 언로드되지
/// 않는다. 그러면 다음 리로드부터 같은 타입이 두 벌 생기고 캐스팅이 조용히 깨진다
/// — 크래시가 아니라 "저 오브젝트만 스크립트가 안 돈다" 로 나타나 원인을 짚기
/// 어려운 종류다. 그래서 조회는 매번 새로 하고, 이 클래스는 상태를 갖지 않는다.
///
/// 호출 비용은 리플렉션 조회 한 번이다. 이 통로는 에이전트가 왕복하는 자리이지
/// 프레임 루프가 아니므로, 캐시로 벌 것보다 캐시가 만들 결함이 크다.
///
/// ── 무엇을 거부하는가 ───────────────────────────────────────────────────
///
/// 표식이 없으면 거부한다. 그 거부는 <see cref="InvokeOutcome.MethodNotFound"/> 가
/// 아니라 <see cref="InvokeOutcome.NotMarked"/> 다 — "없다" 와 "있지만 안 연다" 를
/// 같은 코드로 내면 오타와 권한 거부를 구분해 셀 수 없다.
/// </summary>
internal static class EngineCallableInvoker
{
    /// <summary>인자와 반환값이 왕복할 수 있는 파라미터 타입.</summary>
    private static bool TryConvert(string text, Type target, out object? value)
    {
        value = null;

        if (target == typeof(string)) { value = text; return true; }

        if (target == typeof(bool))
        {
            if (bool.TryParse(text, out bool parsed)) { value = parsed; return true; }
            // CLI 습관을 하나만 더 받는다. "yes"/"on" 까지 받기 시작하면 어디까지가
            // 참인지 문서 없이는 알 수 없게 된다.
            if (text == "1") { value = true; return true; }
            if (text == "0") { value = false; return true; }
            return false;
        }

        // 숫자는 전부 InvariantCulture 다. 기계마다 소수점이 달라지면 같은 명령이
        // 기계마다 다른 값을 넘긴다.
        if (target == typeof(int))
        {
            if (!int.TryParse(text, NumberStyles.Integer, CultureInfo.InvariantCulture, out int parsed)) return false;
            value = parsed; return true;
        }
        if (target == typeof(long))
        {
            if (!long.TryParse(text, NumberStyles.Integer, CultureInfo.InvariantCulture, out long parsed)) return false;
            value = parsed; return true;
        }
        if (target == typeof(float))
        {
            if (!float.TryParse(text, NumberStyles.Float, CultureInfo.InvariantCulture, out float parsed)) return false;
            value = parsed; return true;
        }
        if (target == typeof(double))
        {
            if (!double.TryParse(text, NumberStyles.Float, CultureInfo.InvariantCulture, out double parsed)) return false;
            value = parsed; return true;
        }

        return false;
    }

    private static string Format(object? value) => value switch
    {
        null => "null",
        bool b => b ? "true" : "false",
        float f => f.ToString("R", CultureInfo.InvariantCulture),
        double d => d.ToString("R", CultureInfo.InvariantCulture),
        IFormattable f => f.ToString(null, CultureInfo.InvariantCulture),
        _ => value.ToString() ?? string.Empty,
    };

    /// <summary>
    /// 표식된 static 메서드 하나를 부른다.
    ///
    /// 성공하면 <paramref name="payload"/> 는 <c>반환타입 \x1f 반환값</c> 이고,
    /// 실패하면 사람이 읽는 사유다. 네이티브가 <see cref="InvokeOutcome"/> 을 보고
    /// 둘을 가른다.
    /// </summary>
    // 조회에 쓰는 지역(Assembly·Type·MethodInfo)이 호출자 프레임에 남지 않도록
    // 분리한다 — ScriptAssemblyLoader.Load 가 같은 이유로 같은 표식을 달고 있다.
    [MethodImpl(MethodImplOptions.NoInlining)]
    public static InvokeOutcome Invoke(string typeName, string methodName, string[] args,
                                       out string payload)
    {
        Assembly[] assemblies = ScriptAssemblyLoader.LoadedAssemblies();
        if (assemblies.Length == 0)
        {
            payload = "게임 스크립트 어셈블리가 올라와 있지 않다";
            return InvokeOutcome.NoAssembly;
        }

        InvokeOutcome resolved = ResolveType(assemblies, typeName, out Type? type, out payload);
        if (resolved != InvokeOutcome.Ok) return resolved;

        // ★ 순서가 판정의 뜻을 정한다.
        //
        //   이름으로 **먼저** 찾고 표식은 **나중에** 본다. 표식으로 걸러서 찾으면
        //   표식 없는 메서드가 "그런 메서드 없음" 으로 보고되고, 오타와 거부가
        //   같은 코드로 합쳐진다 — 그러면 "표식 없는 호출 0" 을 셀 수 없다.
        MethodInfo[] named = type!
            .GetMethods(BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Static)
            .Where(m => m.Name == methodName)
            .ToArray();

        if (named.Length == 0)
        {
            payload = $"static 메서드가 없다: {type.FullName}.{methodName}";
            return InvokeOutcome.MethodNotFound;
        }

        MethodInfo[] byArity = named.Where(m => m.GetParameters().Length == args.Length).ToArray();
        if (byArity.Length == 0)
        {
            string arities = string.Join(", ", named.Select(m => m.GetParameters().Length).Distinct().Order());
            payload = $"인자 개수가 맞지 않는다: {args.Length} 개를 줬고 {methodName} 은 {arities} 개를 받는다";
            return InvokeOutcome.ArgumentMismatch;
        }
        if (byArity.Length > 1)
        {
            payload = $"같은 인자 개수의 오버로드가 여럿이다: {type.FullName}.{methodName}";
            return InvokeOutcome.MethodAmbiguous;
        }

        MethodInfo method = byArity[0];
        if (!method.IsDefined(typeof(EngineCallableAttribute), inherit: false))
        {
            payload = $"{type.FullName}.{methodName} 은 [EngineCallable] 표식이 없다 — "
                    + "이름을 알아도 호출되지 않는다";
            return InvokeOutcome.NotMarked;
        }

        ParameterInfo[] parameters = method.GetParameters();
        object?[] converted = new object?[parameters.Length];
        for (int i = 0; i < parameters.Length; ++i)
        {
            if (TryConvert(args[i], parameters[i].ParameterType, out converted[i])) continue;

            payload = $"인자 {i}('{args[i]}') 를 {parameters[i].ParameterType.Name} 로 바꿀 수 없다"
                    + " — 받는 타입은 string·bool·int·long·float·double 이다";
            return InvokeOutcome.ArgumentMismatch;
        }

        try
        {
            object? returned = method.Invoke(null, converted);
            string valueText = (method.ReturnType == typeof(void)) ? string.Empty : Format(returned);
            payload = method.ReturnType.Name + '\x1f' + valueText;
            return InvokeOutcome.Ok;
        }
        catch (TargetInvocationException ex)
        {
            // ★ 사용자 코드의 예외가 에디터를 죽이지 않는다(§10.3 의 전제 2).
            //   던진 것은 사용자 코드이므로 **내부 결함이 아니라 결과**로 낸다.
            Exception inner = ex.InnerException ?? ex;
            payload = $"{inner.GetType().Name}: {inner.Message}";
            return InvokeOutcome.Threw;
        }
        catch (Exception ex)
        {
            // 리플렉션 자체가 던진 경우(접근 위반·잘못된 인자 등). 사용자 코드는
            // 아직 돌지 않았으므로 위와 갈래를 나눠 두지만, 호출자에게는 같은
            // "부르지 못했다" 이므로 코드를 나누지 않는다.
            payload = $"호출하지 못했다 — {ex.GetType().Name}: {ex.Message}";
            return InvokeOutcome.Threw;
        }
    }

    /// <summary>전체 이름을 먼저, 없으면 짧은 이름으로 찾는다. 모호하면 거부한다.</summary>
    [MethodImpl(MethodImplOptions.NoInlining)]
    private static InvokeOutcome ResolveType(Assembly[] assemblies, string typeName,
                                             out Type? type, out string payload)
    {
        type = null;
        payload = string.Empty;

        List<Type> exact = [];
        List<Type> shortName = [];

        foreach (Assembly assembly in assemblies)
        {
            Type?[] types;
            try
            {
                types = assembly.GetTypes();
            }
            catch (ReflectionTypeLoadException ex)
            {
                // 일부 타입이 못 올라온 어셈블리에서도 **올라온 것으로는** 답한다.
                // 여기서 통째로 포기하면 깨진 타입 하나가 나머지 전부를 못 부르게 한다.
                types = ex.Types;
            }

            foreach (Type? candidate in types)
            {
                if (candidate is null) continue;
                if (candidate.FullName == typeName) exact.Add(candidate);
                else if (candidate.Name == typeName) shortName.Add(candidate);
            }
        }

        List<Type> hits = exact.Count > 0 ? exact : shortName;
        if (hits.Count == 0)
        {
            payload = $"타입이 없다: {typeName}";
            return InvokeOutcome.TypeNotFound;
        }
        if (hits.Count > 1)
        {
            payload = $"이름이 겹친다({hits.Count}) — 전체 이름으로 부를 것: "
                    + string.Join(", ", hits.Select(t => t.FullName));
            return InvokeOutcome.TypeAmbiguous;
        }

        type = hits[0];
        return InvokeOutcome.Ok;
    }
}
