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

$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)

# 검사 대상: 열거 이름 → (네이티브 파일, 관리 파일)
$targets = @(
    @{ Name = 'NodeStatus';        Native = 'ScriptBinder\BTEnum.h';         Managed = 'ScriptCore\BehaviorTree.cs' },
    @{ Name = 'BehaviorNodeType';  Native = 'ScriptBinder\BTEnum.h';         Managed = 'ScriptCore\BehaviorTree.cs' },
    @{ Name = 'ParallelPolicy';    Native = 'ScriptBinder\BTEnum.h';         Managed = 'ScriptCore\BehaviorTree.cs' },
    @{ Name = 'BlackBoardType';    Native = 'ScriptBinder\BlackBoardEnum.h'; Managed = 'ScriptCore\BlackBoard.cs' },
    # 갈래는 ClrHost 안의 중첩 열거라 'enum class BTNodeKind' 형태로 그대로 잡힌다(B6).
    @{ Name = 'BTNodeKind';        Native = 'ScriptBinder\ClrHost.h';       Managed = 'ScriptCore\BTNodeFactory.cs' }
)

# 열거 본문에서 (이름, 값) 목록을 뽑는다.
#
# 네이티브는 값을 생략하는 항목이 많고(BlackBoardType이 그렇다) C#은 전부 명시한다.
# 그래서 파싱이 아니라 **누적 계산**을 해야 한다 — 생략된 항목은 직전 값 + 1이다.
# 이 규칙을 지키지 않으면 "값을 안 적었으니 비교할 게 없다"고 넘어가 버린다.
function Get-EnumMembers {
    param([string]$Path, [string]$EnumName, [string]$Language)

    if (-not (Test-Path $Path)) { throw "파일이 없다: $Path" }
    $lines = Get-Content -LiteralPath $Path

    $open = if ($Language -eq 'cpp') {
        "^\s*enum\s+class\s+$EnumName\b"
    } else {
        "^\s*public\s+enum\s+$EnumName\b"
    }

    $inEnum = $false
    $sawBrace = $false
    $next = 0
    $members = New-Object System.Collections.Generic.List[object]

    foreach ($line in $lines) {
        if (-not $inEnum) {
            if ($line -match $open) { $inEnum = $true }
            else { continue }
        }

        # 한 줄 선언(enum class X { A, B };)도 받는다.
        $body = $line
        if (-not $sawBrace) {
            $braceAt = $body.IndexOf('{')
            if ($braceAt -lt 0) { continue }
            $sawBrace = $true
            $body = $body.Substring($braceAt + 1)
        }

        $closeAt = $body.IndexOf('}')
        $last = $false
        if ($closeAt -ge 0) { $body = $body.Substring(0, $closeAt); $last = $true }

        # 주석 제거 후 쉼표로 자른다.
        $body = ($body -replace '//.*$', '') -replace '/\*.*?\*/', ''
        foreach ($piece in ($body -split ',')) {
            $t = $piece.Trim()
            if ($t -eq '') { continue }

            if ($t -match '^([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(-?\d+)$') {
                $next = [int]$Matches[2]
                $members.Add([pscustomobject]@{ Name = $Matches[1]; Value = $next })
                $next++
            }
            elseif ($t -match '^([A-Za-z_][A-Za-z0-9_]*)$') {
                $members.Add([pscustomobject]@{ Name = $Matches[1]; Value = $next })
                $next++
            }
        }

        if ($last) { break }
    }

    if ($members.Count -eq 0) { throw "$EnumName 을(를) $Path 에서 찾지 못했다" }
    return $members
}

$failed = @()

foreach ($t in $targets) {
    $nativePath  = Join-Path $root $t.Native
    $managedPath = Join-Path $root $t.Managed

    $native  = Get-EnumMembers -Path $nativePath  -EnumName $t.Name -Language 'cpp'
    $managed = Get-EnumMembers -Path $managedPath -EnumName $t.Name -Language 'cs'

    Write-Output ("{0,-18} 네이티브 {1}개 · 관리 {2}개" -f $t.Name, $native.Count, $managed.Count)

    $limit = [Math]::Max($native.Count, $managed.Count)
    for ($i = 0; $i -lt $limit; $i++) {
        $n = if ($i -lt $native.Count)  { $native[$i]  } else { $null }
        $m = if ($i -lt $managed.Count) { $managed[$i] } else { $null }

        $nText = if ($n) { "$($n.Name)=$($n.Value)" } else { '(없음)' }
        $mText = if ($m) { "$($m.Name)=$($m.Value)" } else { '(없음)' }

        if ($nText -ne $mText) {
            Write-Output "  [$i] 불일치 — 네이티브 $nText / 관리 $mText"
            $failed += $t.Name
            break
        }
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
