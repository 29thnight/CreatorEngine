# 행동 트리 열거 5종이 네이티브와 C#에서 같은 값을 갖는지 검사한다 (PHASE 9-8).
#
# 트리가 관리 측으로 옮겨 가면서 NodeStatus·BehaviorNodeType·ParallelPolicy·
# BlackBoardType·BTNodeKind가 양쪽에 존재하게 됐다. 값은 경계를 int로 건너가므로, 순서가
# 어긋나면 컴파일도 링크도 통과하고 실행 중에 **다른 노드가 다른 판정을 내린다** —
# 크래시가 아니라 "AI가 좀 이상하다"로 나타나는 종류라 원인을 짚기 어렵다.
#
# API 표에서 같은 사고가 있었다(필드 하나가 끝으로 밀렸는데 개수·크기가 그대로라
# 검사를 통과했고, 관리 코드가 엉뚱한 함수 포인터를 불러 접근 위반). 그때 만든
# check-api-table.ps1과 같은 방식으로 여기도 못을 박는다.
#
# 사용법: pwsh ScriptCore\check-bt-enums.ps1
# 어긋나면 종료 코드 1과 함께 첫 불일치 지점을 보여 준다.

$ErrorActionPreference = 'Stop'

# 열거 파서는 check-lifecycle-enums.ps1과 공유한다 — 같은 종류의 결함을 두 게이트가
# 서로 다른 규칙으로 재지 않게 하기 위해서다.
. (Join-Path $PSScriptRoot 'EnumMirror.ps1')

$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)

# 검사 대상: 열거 이름 → (네이티브 파일, 관리 파일)
$targets = @(
    @{ Name = 'NodeStatus';        Native = 'Engine\SceneRuntime\BTEnum.h';         Managed = 'ScriptCore\BehaviorTree.cs' },
    @{ Name = 'BehaviorNodeType';  Native = 'Engine\SceneRuntime\BTEnum.h';         Managed = 'ScriptCore\BehaviorTree.cs' },
    @{ Name = 'ParallelPolicy';    Native = 'Engine\SceneRuntime\BTEnum.h';         Managed = 'ScriptCore\BehaviorTree.cs' },
    @{ Name = 'BlackBoardType';    Native = 'Engine\SceneRuntime\BlackBoardEnum.h'; Managed = 'ScriptCore\BlackBoard.cs' },
    # 갈래는 ClrHost 안의 중첩 열거라 'enum class BTNodeKind' 형태로 그대로 잡힌다(B6).
    @{ Name = 'BTNodeKind';        Native = 'Engine\SceneRuntime\ClrHost.h';       Managed = 'ScriptCore\BTNodeFactory.cs' }
)

$failed = @()

foreach ($t in $targets) {
    $nativePath  = Join-Path $root $t.Native
    $managedPath = Join-Path $root $t.Managed

    $native  = Get-EnumMembers -Path $nativePath  -EnumName $t.Name -Language 'cpp'
    $managed = Get-EnumMembers -Path $managedPath -EnumName $t.Name -Language 'cs'

    Write-Output ("{0,-18} 네이티브 {1}개 · 관리 {2}개" -f $t.Name, $native.Count, $managed.Count)

    $diff = Compare-EnumMirror -Native $native -Managed $managed
    if ($null -ne $diff) {
        Write-Output $diff
        $failed += $t.Name
    }
}

if ($failed.Count -gt 0) {
    Write-Output ''
    Write-Output "어긋난 열거: $($failed -join ', ')"
    Write-Output '값이 경계를 int로 건너가므로, 순서가 다르면 컴파일도 링크도 통과하고'
    Write-Output '실행 중에 다른 노드가 다른 판정을 내린다 — 크래시가 아니라'
    Write-Output '"AI가 좀 이상하다"로 나타나 원인을 짚기 어렵다.'
    exit 1
}

Write-Output ''
Write-Output "BT 열거 $($targets.Count)종 값 일치"
exit 0
