namespace CreatorEngine.Scripts;

/// <summary>
/// Packaged Player의 관리 인스턴스 생성과 simulation 편입을 검증하는 고정 fixture.
/// stdout marker는 Tools/build.ps1이 정확히 한 번씩 요구한다.
/// </summary>
public sealed partial class PackageSmokeProbe : Component
{
    public override void OnInitialized()
    {
        Console.WriteLine("[SMOKE] managed OnInitialized: PackageSmokeProbe");
    }

    public override void OnBeginSimulation()
    {
        Console.WriteLine("[SMOKE] managed OnBeginSimulation: PackageSmokeProbe");
    }
}
