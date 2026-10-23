using System.Runtime;
using System.Runtime.InteropServices;

namespace CreatorEngine;

/// <summary>
/// 관리 힙 지표. 네이티브 ClrHost::ScriptGcStats와 배치가 같아야 한다.
///
/// 전부 blittable이라 마샬링 없이 포인터 하나로 넘어간다 — 물리 이벤트와 같은 규약이다.
/// long을 쓰는 이유는 힙 크기가 int로 넘칠 수 있어서다(2GB 넘는 힙은 드물지 않다).
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public struct GcStats
{
    public int  Gen0Collections;
    public int  Gen1Collections;
    public int  Gen2Collections;
    public long HeapSizeBytes;      // 마지막 수집 시점의 힙 크기
    public long TotalAllocatedBytes;
    public long FragmentedBytes;
    public long PauseTimePercentage; // 백분율 × 100 (정수로 넘기려고 스케일을 곱한다)
}

/// <summary>
/// .NET GC를 엔진 스케줄에 종속시킨다(PHASE 9-6·9-7).
///
/// 원칙: GC에 오브젝트 수명을 맡기지 않는다. 세대 핸들(ObjectHandle)이 수명의 진실이고,
/// GC가 관여하는 것은 "언제 도는가"뿐이다. 유니티가 30년 가까이 앓는 이중 수명 문제
/// (관리 셸은 살아 있는데 네이티브는 죽은)가 그 선을 넘은 데서 나왔다.
///
/// 여기 있는 것은 전부 게임 스레드에서만 불린다 — ClrHost의 규약 그대로다.
/// </summary>
public static class GcControl
{
    [UnmanagedCallersOnly]
    public static int CollectNow()
    {
        try { return ManagedRuntime.Current.CollectNow(); }
        catch { return -1; }
    }

    [UnmanagedCallersOnly]
    public static int SetLatencyMode(int lowLatency)
    {
        try { return ManagedRuntime.Current.SetLatencyMode(lowLatency); }
        catch { return -1; }
    }

    [UnmanagedCallersOnly]
    public static unsafe int GetStats(GcStats* output)
    {
        if (output == null) return -1;
        try { *output = ManagedRuntime.Current.ReadGcStats(); return 0; }
        catch { return -1; }
    }
}
