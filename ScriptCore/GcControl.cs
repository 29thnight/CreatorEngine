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
    /// <summary>
    /// 씬 경계에서 확정 수집한다. 파괴된 씬의 Behaviour와 그 필드가 참조하던
    /// 관리 객체를 이 시점에 회수해, 다음 씬이 깨끗한 힙에서 시작하게 한다.
    ///
    /// blocking·compacting으로 부르는 이유는 "언젠가 회수된다"로는 씬 전환 벤치의
    /// 평탄성을 판정할 수 없기 때문이다. 회수가 다음 씬 중간에 일어나면 그 프레임이
    /// 튀고, 그 튐이 재설계 효과인지 GC 효과인지 구분되지 않는다.
    ///
    /// 비싸다(수십 ms). 씬 전환은 이미 그만큼 걸리는 구간이라 여기서만 부른다.
    /// 프레임 루프에서는 절대 부르지 않는다.
    ///
    /// WaitForPendingFinalizers를 두 수집 사이에 끼우는 것은 표준 관용구다 —
    /// 첫 수집이 파이널라이저 큐에 넣은 객체는 파이널라이저가 돈 뒤에야 실제로
    /// 회수되므로, 한 번만 부르면 그것들이 남는다.
    /// </summary>
    [UnmanagedCallersOnly]
    public static int CollectNow()
    {
        try
        {
            GC.Collect(GC.MaxGeneration, GCCollectionMode.Forced, blocking: true, compacting: true);
            GC.WaitForPendingFinalizers();
            GC.Collect(GC.MaxGeneration, GCCollectionMode.Forced, blocking: true, compacting: true);
            return 0;
        }
        catch { return -1; }
    }

    /// <summary>
    /// 지연 모드를 바꾼다. lowLatency가 0이 아니면 SustainedLowLatency.
    ///
    /// 재생 중에는 gen2 블로킹 수집을 억제해 프레임 스파이크를 줄이고, 에디터로
    /// 돌아오면 Interactive로 되돌린다. 편집 중에는 프레임 예산보다 메모리 회수가
    /// 중요하고, 재생 중에는 그 반대다.
    ///
    /// SustainedLowLatency는 gen2를 "안 한다"가 아니라 "블로킹으로는 안 한다"이다.
    /// 메모리 압박이 크면 런타임이 무시하고 수집한다 — 보장이 아니라 요청이다.
    /// </summary>
    [UnmanagedCallersOnly]
    public static int SetLatencyMode(int lowLatency)
    {
        try
        {
            GCSettings.LatencyMode = (lowLatency != 0)
                ? GCLatencyMode.SustainedLowLatency
                : GCLatencyMode.Interactive;
            return 0;
        }
        catch
        {
            // NoGCRegion 중에는 설정이 거부된다. 진단 편의 기능이므로 조용히 넘긴다.
            return -1;
        }
    }

    /// <summary>
    /// 관리 힙 지표를 채운다. 0이면 성공.
    ///
    /// 틱당 한 번만 부른다(호스트가 그렇게 부른다). 여기서 하는 일은 카운터 읽기뿐이라
    /// 싸지만, 경계 통과 자체가 규약의 대상이라 횟수를 고정한다.
    /// </summary>
    [UnmanagedCallersOnly]
    public static unsafe int GetStats(GcStats* outStats)
    {
        try
        {
            if (outStats == null) return -1;

            GCMemoryInfo info = GC.GetGCMemoryInfo();

            outStats->Gen0Collections = GC.CollectionCount(0);
            outStats->Gen1Collections = GC.CollectionCount(1);
            outStats->Gen2Collections = GC.CollectionCount(2);
            outStats->HeapSizeBytes = info.HeapSizeBytes;
            outStats->TotalAllocatedBytes = GC.GetTotalAllocatedBytes(precise: false);
            outStats->FragmentedBytes = info.FragmentedBytes;
            outStats->PauseTimePercentage = (long)(info.PauseTimePercentage * 100.0);
            return 0;
        }
        catch { return -1; }
    }
}
