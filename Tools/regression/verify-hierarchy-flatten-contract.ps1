[CmdletBinding()]
param()
# PHASE 21 W7-4 — Hierarchy 평탄 표시 목록의 **정본 계약** 소스 대조.
#
# W7-2·W7-3 은 두 가지를 걸고 섰다. ① 파생 목록은 `HierarchyStore` 의 정본을
# 베끼지 않고 **revision 이 달라졌을 때만** 다시 만든다. ② 목록을 인덱스로 끊어
# 보이는 줄만 그린다(`ImGuiListClipper`). 둘 다 **런타임이 못 보는 불변식**이다.
#
# **왜 런타임 게이트로 못 서는가.**
#
#   ①이 깨지는 모습은 "가끔 낡은 트리가 보인다" 다. `HierarchyStore` 의 어떤
#   변경 자리 하나가 revision 을 안 올리면, 그 한 종류의 편집 뒤에만 목록이
#   멈춘다. CLI 로 몰려면 그 편집을 정확히 재현해야 하는데 — 어느 자리가 빠졌는지
#   모르니 무엇을 재현할지도 모른다. 반대로 소스에서는 "정본을 바꾸는 함수" 와
#   "revision 을 올리는 함수" 를 **둘 다 유도해** 맞댈 수 있다.
#
#   ②가 깨지는 모습은 **스크롤했을 때만** 보인다. clipper 에게 준 줄 높이가 띠를
#   칠하는 높이와 다르면 화면 맨 위(스크롤 0)는 멀쩡하고 내려갈수록 어긋난다.
#   실제로 W7-3 착지 때 before/after 창을 찍어 392,888 픽셀 중 0 픽셀 차이를 받았지만
#   그것은 스크롤 0 의 그림이라 이 축을 통째로 비켜 간다. 스크롤을 CLI 로 몰 창구가
#   없다 — 그래서 두 높이 식을 소스에서 맞댄다.
#
# ★ 손으로 적은 식별자 목록을 두지 않는다. 정본 컨테이너도, 무효화 근거의 필드도,
#   행 종류도 전부 소스에서 뽑는다. 숫자도 못 박지 않는다 — 항목이 하나 늘 때마다
#   이유 없이 붉어지면 사람이 숫자만 고치고 지나간다.
#
# ★ C++ 어휘(`push_back` · `clear` · `reserve` …)만은 목록으로 적는다. 그것은 이
#   저장소의 식별자가 아니라 표준 라이브러리의 이름이라 두 벌이 될 수 없다. 대신
#   `reserve` 는 **쓰기가 아니다** — 용량만 바꾸고 내용을 안 바꾸므로 표시 목록이
#   낡지 않는다. 그 하나를 빼는 이유를 여기 적어 둔다.
#
# **세우면서 한 번 눈이 멀었다(2026-09-15).** 처음 판은 클래스 본문을 훑을 때
# `std::vector` 의 `::` 를 접근 지정자 `:` 로 세어 경계를 잘못 잡았고, 그 타입을
# 인자로 받는 `SetChildren` 이 **함수 목록에서 통째로 빠졌다.** 그런데 그 함수는
# 양쪽(정본을 바꾸는 쪽·revision 을 올리는 쪽)에서 **같이** 빠졌으므로 집합 대조는
# 초록이었다. 대조가 맞았다고 대상이 온전한 것은 아니다 — 그래서 유도한 집합의
# **이름을 전부 찍는다.** 열 개여야 할 목록이 아홉 줄로 찍히는 것은 눈에 보인다.
#
# **못 잡는 것.** 소스 대조라 실제 화면을 보지 않는다. 높이 식이 같아도 ImGui 가
# 그 값을 다르게 쓰는 경우(스타일 스택이 중간에 바뀌는 등)는 못 잡는다 — 그것은
# W8 visual golden 의 몫이고, 거기에는 **스크롤한 상태**가 들어가야 한다.
#
# **이빨(2026-09-15).** 변이 12 종이 각각 **의도한 단정에서** 붉어졌다. ① SetParent
# 가 revision 을 안 올림 · ② revision 초기값 0 · ③ revision 되감기 대입 · ④ 근거
# 하나를 비교에서 제거 · ⑤ 근거를 선언만 하고 안 채움 · ⑥ 행에 컨테이너 필드 추가 ·
# ⑦ 행 종류 하나의 case 제거 · ⑧ switch 에 default 추가 · ⑨ clipper 보폭을 다른 식
# 으로 · ⑩ ItemSpacing.y 를 4.f 로 · ⑪ 자리 메우는 Dummy 제거 · ⑫ 캐시가 정본의
# 저장소 이름을 씀. 종료 코드만이 아니라 **실패 문구**까지 맞는지 함께 봤다.
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent

$script:checks = 0
function Assert([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
    $script:checks++
}

# 주석과 문자열·문자 리터럴을 공백으로 덮는다. 오프셋을 보존한다 — 뒤에서 중괄호를
# 세어 블록을 잘라내기 때문이다.
function Remove-CodeNoise([string]$text) {
    $sb = [Text.StringBuilder]::new($text.Length)
    $i = 0; $n = $text.Length
    while ($i -lt $n) {
        $c = $text[$i]
        $d = if ($i + 1 -lt $n) { $text[$i + 1] } else { [char]0 }
        if ($c -eq '/' -and $d -eq '/') {
            while ($i -lt $n -and $text[$i] -ne "`n") { [void]$sb.Append(' '); $i++ }
        }
        elseif ($c -eq '/' -and $d -eq '*') {
            $end = $text.IndexOf('*/', $i + 2)
            if ($end -lt 0) { $end = $n - 2 }
            while ($i -lt $end + 2 -and $i -lt $n) {
                [void]$sb.Append($(if ($text[$i] -eq "`n") { "`n" } else { ' ' })); $i++
            }
        }
        elseif ($c -eq '"' -or $c -eq "'") {
            $quote = $c
            [void]$sb.Append(' '); $i++
            while ($i -lt $n -and $text[$i] -ne $quote) {
                if ($text[$i] -eq '\') { [void]$sb.Append(' '); $i++ }
                if ($i -lt $n) { [void]$sb.Append(' '); $i++ }
            }
            if ($i -lt $n) { [void]$sb.Append(' '); $i++ }
        }
        else { [void]$sb.Append($c); $i++ }
    }
    $sb.ToString()
}

function Read-Denoised([string]$relative) {
    $full = Join-Path $repoRoot $relative
    if (-not (Test-Path -LiteralPath $full)) { throw "파일이 없다: $relative" }
    Remove-CodeNoise ([IO.File]::ReadAllText($full))
}

# `$open` 이 가리키는 여는 괄호의 짝을 찾는다. 없으면 -1.
function Find-Match([string]$code, [int]$open, [char]$openChar, [char]$closeChar) {
    $depth = 0
    for ($i = $open; $i -lt $code.Length; $i++) {
        if ($code[$i] -eq $openChar) { $depth++ }
        elseif ($code[$i] -eq $closeChar) { $depth--; if (0 -eq $depth) { return $i } }
    }
    -1
}

# `<keyword> <name>` 뒤의 첫 `{` 부터 짝까지. 본문만 돌려준다.
function Get-BlockAfter([string]$code, [string]$pattern) {
    $m = [regex]::Match($code, $pattern)
    if (-not $m.Success) { return $null }
    $open = $code.IndexOf('{', $m.Index + $m.Length - 1)
    if ($open -lt 0) { return $null }
    $close = Find-Match $code $open ([char]'{') ([char]'}')
    if ($close -lt 0) { return $null }
    $code.Substring($open + 1, $close - $open - 1)
}

# 구조체/클래스 본문에서 `타입 이름;` 꼴의 자료 멤버를 뽑는다. 함수는 걸러 낸다.
function Get-DataMembers([string]$body) {
    $result = @()
    foreach ($raw in ($body -split ';')) {
        $line = ($raw -replace '\s+', ' ').Trim()
        if ('' -eq $line) { continue }
        if ($line.Contains('(')) { continue }          # 함수 선언
        if ($line -match '^(public|private|protected)') { $line = ($line -replace '^\w+\s*:\s*', '') }
        if ($line -match '^(using|static constexpr|friend|enum|struct|class)\b') { continue }
        # 기본값 초기자를 떼어 낸다.
        $line = ($line -replace '\{[^{}]*\}\s*$', '').Trim()
        $line = ($line -replace '=\s*[^=]*$', '').Trim()
        $m = [regex]::Match($line, '^(?<type>.+?)\s+(?<name>[A-Za-z_]\w*)$')
        if (-not $m.Success) { continue }
        $result += [pscustomobject]@{ Type = $m.Groups['type'].Value.Trim(); Name = $m.Groups['name'].Value }
    }
    $result
}

# 클래스 본문의 **최상위** 멤버 함수만 뽑는다(중첩 블록의 if/for 를 함수로 오인하지
# 않도록 중괄호 깊이를 직접 센다).
function Get-MemberFunctions([string]$classBody) {
    $result = @()
    $depth = 0
    $boundary = -1
    for ($i = 0; $i -lt $classBody.Length; $i++) {
        $c = $classBody[$i]
        if ($c -eq '{') {
            if (0 -eq $depth) {
                $signature = ($classBody.Substring($boundary + 1, $i - $boundary - 1) -replace '\s+', ' ').Trim()
                $close = Find-Match $classBody $i ([char]'{') ([char]'}')
                if ($close -lt 0) { break }
                $m = [regex]::Match($signature, '(?<name>[A-Za-z_]\w*)\s*\((?<args>[^)]*)\)\s*(?<quals>[^()]*)$')
                if ($m.Success) {
                    $result += [pscustomobject]@{
                        Name  = $m.Groups['name'].Value
                        Quals = $m.Groups['quals'].Value
                        Body  = $classBody.Substring($i + 1, $close - $i - 1)
                        IsConst = ($m.Groups['quals'].Value -match '\bconst\b')
                    }
                }
                $i = $close
                $boundary = $close
                continue
            }
            $depth++
        }
        elseif ($c -eq '}') { if ($depth -gt 0) { $depth-- } }
        elseif ($depth -eq 0 -and $c -eq ';') { $boundary = $i }
        elseif ($depth -eq 0 -and $c -eq ':') {
            # `public:` 은 경계고 `std::vector` 의 `::` 는 아니다. 이것을 안 가르면
            # 그 타입을 받는 함수가 통째로 빠지고, **양쪽에서 같이 빠져** 집합
            # 대조가 조용히 통과한다(실제로 SetChildren 이 그렇게 빠져 있었다).
            $prev = if ($i -gt 0) { $classBody[$i - 1] } else { [char]0 }
            $next = if ($i + 1 -lt $classBody.Length) { $classBody[$i + 1] } else { [char]0 }
            if ($prev -ne ':' -and $next -ne ':') { $boundary = $i }
        }
    }
    $result
}

Write-Host ''
Write-Host 'Hierarchy 평탄 목록 정본 계약 (PHASE 21 W7-4)'
Write-Host ''

# ── A. HierarchyStore 의 revision 계약 ──────────────────────────────────────
$storeText = Read-Denoised 'Engine/SceneRuntime/HierarchyStore.h'
$storeBody = Get-BlockAfter $storeText 'class\s+HierarchyStore\s*'
Assert ($null -ne $storeBody) 'HierarchyStore 클래스 본문을 못 찾았다'

$privateAt = $storeBody.LastIndexOf('private:')
Assert ($privateAt -ge 0) 'HierarchyStore 에 private 절이 없다'
$privateMembers = @(Get-DataMembers $storeBody.Substring($privateAt + 'private:'.Length))
Assert ($privateMembers.Count -gt 0) 'HierarchyStore 의 private 자료 멤버를 하나도 못 뽑았다'

# 정본 컨테이너 = private 자료 멤버 중 컨테이너 타입인 것. 유도다, 목록이 아니다.
$authority = @($privateMembers | Where-Object { $_.Type -match '\bstd::vector\b' } | ForEach-Object { $_.Name })
Assert ($authority.Count -ge 2) "정본 컨테이너를 유도하지 못했다(찾은 수 $($authority.Count))"

# revision 멤버 = 클래스 안에서 `++X` 로 오르는 private 정수 멤버. 이것도 유도다.
$bumped = @([regex]::Matches($storeBody, '\+\+\s*(?<name>m_\w+)') | ForEach-Object { $_.Groups['name'].Value } |
    Sort-Object -Unique)
Assert (1 -eq $bumped.Count) "++ 로 오르는 멤버가 하나여야 한다 — 찾은 것: $($bumped -join ', ')"
$revision = $bumped[0]
Assert ($privateMembers.Name -contains $revision) "$revision 이 private 자료 멤버가 아니다"

Write-Host ("  정본 컨테이너 {0} 개: {1}" -f $authority.Count, (($authority | Sort-Object) -join ', '))
Write-Host ("  무효화 근거가 되는 수: {0}" -f $revision)

$functions = @(Get-MemberFunctions $storeBody)
Assert ($functions.Count -ge 5) "HierarchyStore 멤버 함수를 못 뽑았다(찾은 수 $($functions.Count))"

# 내용을 바꾸는 연산의 어휘. `reserve` 는 용량만 바꾸므로 여기 없다(머리말 참고).
$writeOps = 'clear|push_back|emplace_back|pop_back|erase|insert|assign|resize|emplace|swap'

function Test-WritesAuthority([string]$body, [string[]]$members) {
    foreach ($member in $members) {
        $names = @($member)
        # `auto& children = m_childrenIndices[index];` 처럼 참조로 받아 쓰는 자리.
        foreach ($alias in [regex]::Matches($body, ('auto\s*&\s*(?<a>\w+)\s*=\s*' + [regex]::Escape($member) + '\b'))) {
            $names += $alias.Groups['a'].Value
        }
        foreach ($name in $names) {
            $q = [regex]::Escape($name)
            if ($body -match ($q + '\s*\[[^\]]*\]\s*=')) { return $true }              # 색인 대입
            if ($body -match ($q + '\s*\[[^\]]*\]\s*\.\s*(' + $writeOps + ')\s*\(')) { return $true }
            if ($body -match ($q + '\s*\.\s*(' + $writeOps + ')\s*\(')) { return $true }
            if ($body -match ('std::(' + $writeOps + ')\s*\(\s*' + $q + '\b')) { return $true }  # std::erase(v, x)
            if ($body -match ('(?<![\w.>])' + $q + '\s*=(?!=)')) { return $true }      # 통째 대입
        }
    }
    $false
}

$mutators = @($functions | Where-Object { -not $_.IsConst -and (Test-WritesAuthority $_.Body $authority) } |
    ForEach-Object { $_.Name } | Sort-Object -Unique)
$bumpers = @($functions | Where-Object { $_.Body -match ('\+\+\s*' + [regex]::Escape($revision)) } |
    ForEach-Object { $_.Name } | Sort-Object -Unique)

Assert ($mutators.Count -gt 0) '정본을 바꾸는 함수를 하나도 유도하지 못했다'
Write-Host ("  정본을 바꾸는 함수 {0} 개: {1}" -f $mutators.Count, ($mutators -join ', '))
Write-Host ("  {0} 을 올리는 함수 {1} 개: {2}" -f $revision, $bumpers.Count, ($bumpers -join ', '))

$missing = @($mutators | Where-Object { $bumpers -notcontains $_ })
Assert (0 -eq $missing.Count) ("정본을 바꾸면서 $revision 을 안 올리는 함수: " + ($missing -join ', ') +
    ' — 그 종류의 편집 뒤에만 Hierarchy 가 낡은 트리를 보인다')
$extra = @($bumpers | Where-Object { $mutators -notcontains $_ })
Assert (0 -eq $extra.Count) ("정본을 안 바꾸면서 $revision 만 올리는 함수: " + ($extra -join ', ') +
    ' — 근거 없이 매 편집마다 전량 재구축하게 된다')

# 오르기만 한다. 되감기면 캐시가 "그대로다" 로 읽어 조용히 낡는다.
$assigned = @([regex]::Matches($storeBody, ('(?<![\w>])' + [regex]::Escape($revision) + '\s*=(?!=)')))
Assert (0 -eq $assigned.Count) "$revision 에 대입하는 자리가 있다 — 되감기면 캐시가 낡은 것을 새것으로 읽는다"

# 초기값이 0 이면 안 된다. 0 은 캐시가 "아직 아무것도 못 봤다" 로 쓰는 값이다.
$initMatch = [regex]::Match($storeBody, ([regex]::Escape($revision) + '\s*\{\s*(?<v>\d+)\s*\}'))
Assert $initMatch.Success "$revision 의 초기값을 못 읽었다"
Assert ('0' -ne $initMatch.Groups['v'].Value) "$revision 의 초기값이 0 이다 — 캐시가 0 을 '못 봤다' 로 쓰므로 첫 프레임이 영원히 재구축된다"

# 값을 내주는 자리는 const 여야 한다.
$reader = @($functions | Where-Object { $_.Body -match ('return\s+' + [regex]::Escape($revision) + '\s*;') })
Assert (1 -eq $reader.Count) "$revision 을 돌려주는 함수가 하나여야 한다(찾은 수 $($reader.Count))"
Assert $reader[0].IsConst "$($reader[0].Name)() 이 const 가 아니다"

# ── B. 캐시가 정본을 베끼지 않는다 ──────────────────────────────────────────
$flatHeader = Read-Denoised 'Editor/EngineGUIWindow/HierarchyFlatten.h'
$flatSource = Read-Denoised 'Editor/EngineGUIWindow/HierarchyFlatten.cpp'
$windowSource = Read-Denoised 'Editor/EngineGUIWindow/HierarchyWindow.cpp'
$windowHeader = Read-Denoised 'Editor/EngineGUIWindow/HierarchyWindow.h'

$cacheSide = @{
    'HierarchyFlatten.h'   = $flatHeader
    'HierarchyFlatten.cpp' = $flatSource
    'HierarchyWindow.h'    = $windowHeader
    'HierarchyWindow.cpp'  = $windowSource
}
foreach ($member in ($privateMembers | ForEach-Object { $_.Name })) {
    foreach ($entry in $cacheSide.GetEnumerator()) {
        Assert (-not ($entry.Value -match ('\b' + [regex]::Escape($member) + '\b'))) `
            ("$($entry.Key) 이 정본의 저장소 이름 $member 을 쓴다 — 캐시는 접근자로만 닿아야 한다")
    }
}

$rowBody = Get-BlockAfter $flatHeader 'struct\s+hierarchy_flat_row\s*'
Assert ($null -ne $rowBody) 'hierarchy_flat_row 본문을 못 찾았다'
$rowFields = @(Get-DataMembers $rowBody)
Assert ($rowFields.Count -gt 0) 'hierarchy_flat_row 의 필드를 못 뽑았다'
foreach ($field in $rowFields) {
    Assert (-not ($field.Type -match '\b(vector|map|set|string|deque|list|array|span)\b')) `
        ("hierarchy_flat_row.$($field.Name) 이 컨테이너다($($field.Type)) — 한 줄은 파생 스칼라만 담는다")
}
Write-Host ("  행이 담는 파생값 {0} 개: {1}" -f $rowFields.Count, (($rowFields | ForEach-Object { $_.Name }) -join ', '))

# ── C. 무효화 근거가 하나도 안 빠지고 비교된다 ──────────────────────────────
$keyBody = Get-BlockAfter $flatHeader 'struct\s+hierarchy_flat_key\s*'
Assert ($null -ne $keyBody) 'hierarchy_flat_key 본문을 못 찾았다'
$keyFields = @(Get-DataMembers $keyBody | ForEach-Object { $_.Name })
Assert ($keyFields.Count -gt 0) 'hierarchy_flat_key 의 필드를 못 뽑았다'

$groundMatch = [regex]::Match($flatSource, 'const\s+bool\s+(?<var>\w+)\s*=(?<expr>[^;]*);')
Assert $groundMatch.Success '근거 비교식(const bool ... = ...;)을 못 찾았다'
$groundVar = $groundMatch.Groups['var'].Value
$compared = @([regex]::Matches($groundMatch.Groups['expr'].Value, 'key\s*\.\s*(?<f>\w+)') |
    ForEach-Object { $_.Groups['f'].Value } | Sort-Object -Unique)

Write-Host ("  선언된 무효화 근거 {0} 개: {1}" -f $keyFields.Count, (($keyFields | Sort-Object) -join ', '))
Write-Host ("  {0} 가 실제로 비교하는 근거 {1} 개: {2}" -f $groundVar, $compared.Count, ($compared -join ', '))

$notCompared = @($keyFields | Where-Object { $compared -notcontains $_ })
Assert (0 -eq $notCompared.Count) ("선언만 되고 비교되지 않는 무효화 근거: " + ($notCompared -join ', ') +
    ' — 그 축이 움직여도 목록이 그대로다(조용히 낡는다)')
$comparedGhost = @($compared | Where-Object { $keyFields -notcontains $_ })
Assert (0 -eq $comparedGhost.Count) ("선언에 없는 근거를 비교한다: " + ($comparedGhost -join ', '))

# 근거가 같을 때만 재사용한다. 이 관문이 없으면 비교식이 있어도 뜻이 없다.
$reuse = [regex]::Match($flatSource, ('if\s*\(\s*' + [regex]::Escape($groundVar) + '\b[^)]*\)'))
Assert $reuse.Success "목록 재사용이 $groundVar 로 막혀 있지 않다"
$rebuildAt = $flatSource.IndexOf('Rebuild(')
$returnAt = $flatSource.IndexOf('return m_rows;')
Assert ($returnAt -ge 0 -and $reuse.Index -lt $returnAt -and $returnAt -lt $rebuildAt) `
    '이른 반환이 근거 관문 뒤·Rebuild 앞에 있지 않다'

# 창이 근거를 빠짐없이 채운다. 자리 초기화라 개수로 센다.
$keyInit = [regex]::Match($windowSource, 'hierarchy_flat_key\s+\w+\s*\{')
Assert $keyInit.Success '창에서 hierarchy_flat_key 를 세우는 자리를 못 찾았다'
$initOpen = $windowSource.IndexOf('{', $keyInit.Index)
$initClose = Find-Match $windowSource $initOpen ([char]'{') ([char]'}')
Assert ($initClose -gt $initOpen) 'hierarchy_flat_key 초기자의 짝을 못 찾았다'
$initText = $windowSource.Substring($initOpen + 1, $initClose - $initOpen - 1)
$initCount = 1
$depthParen = 0; $depthAngle = 0
foreach ($ch in $initText.ToCharArray()) {
    if ($ch -eq '(' ) { $depthParen++ }
    elseif ($ch -eq ')') { $depthParen-- }
    elseif ($ch -eq '<') { $depthAngle++ }
    elseif ($ch -eq '>') { if ($depthAngle -gt 0) { $depthAngle-- } }
    elseif ($ch -eq ',' -and 0 -eq $depthParen -and 0 -eq $depthAngle) { $initCount++ }
}
Assert ($initCount -eq $keyFields.Count) `
    "창이 채우는 근거가 $initCount 개인데 선언은 $($keyFields.Count) 개다 — 새 근거를 세우고 안 채웠다"

# 근거 중 하나는 계층의 revision 이어야 한다. 그 값을 실제로 읽는지 본다.
Assert ($windowSource -match ('GetHierarchyStore\(\)\s*\.\s*' + [regex]::Escape($reader[0].Name) + '\s*\(\s*\)')) `
    "창이 $($reader[0].Name)() 을 읽지 않는다 — 계층 변경이 근거에 닿지 않는다"

# ── D. 행 종류가 하나도 안 빠지고 그려진다 ──────────────────────────────────
$kindBody = Get-BlockAfter $flatHeader 'enum\s+class\s+hierarchy_row_kind\s*:[^{]*'
if ($null -eq $kindBody) { $kindBody = Get-BlockAfter $flatHeader 'enum\s+class\s+hierarchy_row_kind\s*' }
Assert ($null -ne $kindBody) 'hierarchy_row_kind 본문을 못 찾았다'
$kinds = @($kindBody -split ',' | ForEach-Object { ($_ -replace '=.*$', '').Trim() } |
    Where-Object { $_ -match '^[A-Za-z_]\w*$' })
Assert ($kinds.Count -ge 2) "행 종류를 못 뽑았다(찾은 수 $($kinds.Count))"

$switchMatch = [regex]::Match($windowSource, 'switch\s*\(\s*\w+\s*\.\s*kind\s*\)')
Assert $switchMatch.Success '행 종류를 가르는 switch 를 못 찾았다'
$switchOpen = $windowSource.IndexOf('{', $switchMatch.Index)
$switchClose = Find-Match $windowSource $switchOpen ([char]'{') ([char]'}')
Assert ($switchClose -gt $switchOpen) 'switch 본문의 짝을 못 찾았다'
$switchBody = $windowSource.Substring($switchOpen + 1, $switchClose - $switchOpen - 1)
$cases = @([regex]::Matches($switchBody, 'case\s+[\w:]*hierarchy_row_kind\s*::\s*(?<k>\w+)') |
    ForEach-Object { $_.Groups['k'].Value } | Sort-Object -Unique)

Write-Host ("  선언된 행 종류 {0} 개: {1}" -f $kinds.Count, (($kinds | Sort-Object) -join ', '))
Write-Host ("  그리는 자리가 다루는 종류 {0} 개: {1}" -f $cases.Count, ($cases -join ', '))
$unhandled = @($kinds | Where-Object { $cases -notcontains $_ })
Assert (0 -eq $unhandled.Count) ("그려지지 않는 행 종류: " + ($unhandled -join ', ') +
    ' — clipper 는 한 줄을 건너뛴 것을 모르므로 아래 줄이 통째로 밀린다')
Assert (-not ($switchBody -match '(^|\s)default\s*:')) `
    'switch 에 default 가 있다 — 새 종류가 조용히 묻히고, 그 줄은 자리만 차지한 채 안 그려진다'

# ── E. clipper 의 보폭 (스크롤해야만 드러나는 축) ──────────────────────────
$rowRectBody = Get-BlockAfter $windowSource 'bool\s+row_rect\s*\([^)]*\)\s*(noexcept)?\s*'
Assert ($null -ne $rowRectBody) '줄 사각형을 세우는 row_rect 를 못 찾았다'
$bandCalls = @([regex]::Matches($rowRectBody, 'ImVec2\s*\([^)]*\+\s*ImGui::(?<f>\w+)\s*\(\s*\)') |
    ForEach-Object { $_.Groups['f'].Value } | Sort-Object -Unique)
Assert (1 -eq $bandCalls.Count) ("row_rect 의 아래 모서리를 세우는 줄 높이 식이 하나여야 한다 — 찾은 것: " +
    ($bandCalls -join ', '))
$rowHeightCall = $bandCalls[0]

$heightDecl = [regex]::Match($windowSource, 'const\s+float\s+(?<var>\w+)\s*=\s*ImGui::(?<f>\w+)\s*\(\s*\)\s*;[^;]{0,400}?ImGuiListClipper')
Assert $heightDecl.Success 'clipper 에 준 줄 높이 선언을 못 찾았다'
$heightVar = $heightDecl.Groups['var'].Value
$clipperCall = $heightDecl.Groups['f'].Value

Write-Host ("  띠를 칠하는 줄 높이: ImGui::{0}()" -f $rowHeightCall)
Write-Host ("  clipper 에 준 줄 높이: ImGui::{0}()  (변수 {1})" -f $clipperCall, $heightVar)
Assert ($rowHeightCall -eq $clipperCall) `
    ("줄 높이가 갈렸다 — 띠는 ImGui::$rowHeightCall(), clipper 는 ImGui::$clipperCall(). " +
     '스크롤 0 에서는 똑같아 보이고 내려갈수록 어긋난다')

# clipper 의 보폭은 "항목 높이 + ItemSpacing.y" 다. y 를 0 으로 누른 뒤라야 위 식이 맞다.
$spacingPush = [regex]::Match($windowSource,
    'PushStyleVar\s*\(\s*ImGuiStyleVar_ItemSpacing\s*,\s*ImVec2\s*\(')
Assert $spacingPush.Success 'ItemSpacing 을 누르는 자리를 못 찾았다'
# 인자 안에 또 호출이 있다(ThemePixels(...)). 괄호를 세어 잘라야 y 를 제대로 읽는다.
$vecOpen = $windowSource.LastIndexOf('(', $spacingPush.Index + $spacingPush.Length - 1)
$vecClose = Find-Match $windowSource $vecOpen ([char]'(') ([char]')')
Assert ($vecClose -gt $vecOpen) 'ItemSpacing 의 ImVec2 인자 짝을 못 찾았다'
$vecArgs = $windowSource.Substring($vecOpen + 1, $vecClose - $vecOpen - 1)
$spacingY = ''
$argDepth = 0; $argStart = 0
for ($i = 0; $i -lt $vecArgs.Length; $i++) {
    $ch = $vecArgs[$i]
    if ($ch -eq '(') { $argDepth++ }
    elseif ($ch -eq ')') { $argDepth-- }
    elseif ($ch -eq ',' -and 0 -eq $argDepth) { $argStart = $i + 1 }
}
$spacingY = $vecArgs.Substring($argStart).Trim()
Assert ($spacingY -match '^0(\.0*)?f?$') `
    "ItemSpacing.y 가 0 이 아니다($spacingY) — clipper 보폭이 항목 높이와 어긋난다"
$clipperBegin = $windowSource.IndexOf('.Begin(')
Assert ($clipperBegin -gt $spacingPush.Index) 'ItemSpacing 을 누르기 전에 clipper 가 시작한다'

# clipper 가 건너뛴 자리를 메우는 폭도 같은 값이어야 한다.
$filler = [regex]::Match($windowSource, 'ImGui::Dummy\s*\(\s*ImVec2\s*\([^,]*,\s*(?<h>\w+)\s*\)')
Assert $filler.Success '그리지 못한 줄의 자리를 메우는 Dummy 가 없다 — 한 줄이 빠지면 아래가 통째로 밀린다'
Assert ($heightVar -eq $filler.Groups['h'].Value) `
    "자리를 메우는 높이가 $($filler.Groups['h'].Value) 로 clipper 의 $heightVar 와 다르다"

# 평탄 목록이 실제로 **인덱스로** 끊기는지. 이것이 W7-2 가 W7-3 의 선행조건인 이유다.
$loop = [regex]::Match($windowSource, 'for\s*\(\s*int\s+(?<i>\w+)\s*=\s*(?<c>\w+)\.DisplayStart\s*;\s*\k<i>\s*<\s*\k<c>\.DisplayEnd')
Assert $loop.Success 'clipper 의 DisplayStart~DisplayEnd 루프를 못 찾았다'
$indexVar = $loop.Groups['i'].Value
Assert ($windowSource -match ('\w+\s*\[\s*static_cast<[^>]*>\s*\(\s*' + [regex]::Escape($indexVar) + '\s*\)\s*\]')) `
    "루프 변수 $indexVar 로 목록을 색인하지 않는다 — clipper 가 고른 구간을 안 쓰고 있다"

Write-Host ''
Write-Host ("PASS verify-hierarchy-flatten-contract — {0} checks, 정본 컨테이너 {1} · 근거 {2} · 행 종류 {3}" -f `
    $script:checks, $authority.Count, $keyFields.Count, $kinds.Count)
exit 0
