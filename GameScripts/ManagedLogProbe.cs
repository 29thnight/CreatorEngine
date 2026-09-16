namespace CreatorEngine.Scripts;

/// <summary>
/// 관리 로그가 <b>호출자</b>의 자리를 싣는지 재는 fixture.
///
/// 게임 스크립트 저작자가 실제로 쓰는 것은 <c>Component.Log</c> 계열이다. 그
/// 래퍼가 자기 <c>[Caller*]</c> 값을 전달하지 않으면 모든 로그가 <c>Component.cs</c>
/// 한 줄을 가리키는데, 출처는 **있으므로** "출처가 있는가" 로는 안 걸린다.
/// 그래서 이 프로브는 래퍼를 통해 내고, 게이트는 이 파일 이름이 로그에 뜨는지와
/// 래퍼 파일 이름이 뜨지 <b>않는지</b>를 함께 본다.
///
/// <c>Bootstrap.cs</c> 의 기동 로그는 <c>Native.Log</c> 를 직접 부르므로 이 경로를
/// 지나지 않는다 — 그것만으로는 래퍼 축이 미자극이다.
/// </summary>
public sealed partial class ManagedLogProbe : Component
{
    /// <summary>
    /// 서로 다른 줄에서 둘을 낸다 — 줄 번호가 갈려야 "호출자를 싣는다" 가 보인다.
    ///
    /// <c>LogError</c> 는 쓰지 않는다. 오류 수준은 기동 진단 게이트의 "심각 행 0"
    /// 단정에 걸려 자기 fixture 가 게이트를 붉게 만든다.
    /// </summary>
    [EngineCallable]
    public static string EmitLogLines()
    {
        Log("[ManagedLogProbe] info 한 줄");
        LogWarning("[ManagedLogProbe] warn 한 줄");
        return "{\"emitted\":2}";
    }
}
