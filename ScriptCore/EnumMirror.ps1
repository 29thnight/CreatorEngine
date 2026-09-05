# 네이티브 ↔ 관리 열거 미러 검사의 공용 파서.
#
# 값이 경계를 int 하나로 건너가는 열거는 순서가 어긋나도 컴파일도 링크도 통과한다.
# 그래서 이 저장소에는 그런 미러가 늘어날 때마다 게이트가 하나씩 붙는다
# (check-bt-enums.ps1 · check-lifecycle-enums.ps1). 파서를 각자 베끼면 한쪽만
# 고쳐지는 순간 두 게이트가 서로 다른 규칙으로 같은 종류의 결함을 재게 된다.
#
# 사용법: 게이트 스크립트에서 점 소싱한다.
#   . (Join-Path $PSScriptRoot 'EnumMirror.ps1')
#
# $root 계산은 여기서 하지 않는다 — 점 소싱하면 $MyInvocation이 소스 쪽을 가리켜
# 호출한 게이트의 위치와 어긋난다. 경로는 부르는 쪽이 넘긴다.

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

# 두 열거 목록을 자리별로 대조한다. 불일치가 있으면 첫 지점을 문자열로 돌려주고,
# 일치하면 $null을 돌려준다. 개수 차이는 (없음) 표기로 드러난다.
function Compare-EnumMirror {
    param($Native, $Managed)

    $limit = [Math]::Max($Native.Count, $Managed.Count)
    for ($i = 0; $i -lt $limit; $i++) {
        $n = if ($i -lt $Native.Count)  { $Native[$i]  } else { $null }
        $m = if ($i -lt $Managed.Count) { $Managed[$i] } else { $null }

        $nText = if ($n) { "$($n.Name)=$($n.Value)" } else { '(없음)' }
        $mText = if ($m) { "$($m.Name)=$($m.Value)" } else { '(없음)' }

        if ($nText -ne $mText) { return "  [$i] 불일치 — 네이티브 $nText / 관리 $mText" }
    }
    return $null
}
