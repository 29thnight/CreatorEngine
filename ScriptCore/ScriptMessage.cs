using System.Runtime.InteropServices;

namespace CreatorEngine;

/// <summary>
/// 이름으로 부르는 콜백 한 건. 애니메이션 키프레임 이벤트와 입력 액션이 같은 통로를 쓴다.
///
/// 물리·애니메이션 이벤트와 마찬가지로 발생 시점에 바로 넘기지 않고 큐에 모았다가
/// 틱 경계에서 한 번에 전달한다(설계 문서 02절). 키 입력이나 애니메이션 이벤트는
/// 프레임마다 여러 건이 몰릴 수 있어 특히 그렇다.
///
/// 이름을 고정 길이 배열로 실어 보내는 이유는 배치 전체를 POD 배열 하나로 넘기기
/// 위해서다 — 포인터를 담으면 네이티브 쪽 문자열 수명을 플러시까지 붙들어야 한다.
/// 네이티브의 ClrHost::ScriptMessage와 배치가 같아야 한다.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
internal unsafe struct ScriptMessage
{
    public const int NameCapacity = 64;

    public int InstanceId;              // 받을 스크립트
    public fixed byte Name[NameCapacity];   // UTF-8, 널 종단

    /// <summary>고정 배열은 포인터로만 안전하게 읽을 수 있어 정적 헬퍼로 둔다.</summary>
    public static string ReadName(ScriptMessage* message)
        => Marshal.PtrToStringUTF8((nint)message->Name) ?? string.Empty;
}
