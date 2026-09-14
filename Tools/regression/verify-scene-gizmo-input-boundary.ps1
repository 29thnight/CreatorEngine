[CmdletBinding()]
param()
# PHASE 21 W4 후속 — 씬 뷰 기즈모의 **그림과 입력 경계** 소스 대조.
#
# 지키는 불변식 하나. **기즈모를 그리는 조건은 포인터가 어디에 있는지와 무관하다.**
# 포인터가 오버레이에 막혔다는 사실은 `ImGuizmo::Enable` 로만 흘러야 하고 그림
# 조건으로 새어 들어오면 안 된다.
#
# 이것이 실제로 깨져 있었다(2026-09-14). `ImGuizmo::Manipulate` 는 한 호출로 그림과
# 입력을 함께 하는데 호출 블록 전체가 `!m_overlay.blocksPointer` 로 감싸여 있었고,
# 그 값은 순수 hover 로 서므로 상단 툴바에 마우스를 올리는 것만으로 기즈모가 통째로
# 사라졌다. run-all 은 초록이었다 — 기즈모 픽셀을 세는 단정이 어디에도 없다.
#
# **왜 런타임이 못 보는가.** 기즈모는 ImGui 드로 리스트에 직접 그려지고 CLI 가 읽는
# 표에 흔적을 남기지 않는다. 표시 여부를 물어볼 창구가 없고, 만들더라도 그 창구
# 자체가 같은 조건을 다시 적는 두 벌이 된다. 그래서 소스에서 **양쪽을 다 유도해**
# 맞댄다 — 손으로 적은 식별자 목록을 두지 않는다(두 벌이 되면 한쪽이 낡아도 모른다).
#
#   한쪽: 오버레이가 **포인터 관문이라고 선언한 것**. `SceneViewportOverlay.cpp` 의
#         `blocksPointer` 대입식에서 출발해, 그 식에 등장하는 이름 중 오버레이가
#         `bool` 로 선언한 것만 남기고 고정점까지 닫는다(`pointerOverToolbar` ·
#         `anyPopup` · `viewUsing` · `leftOwned` · `rightOwned` · `canUse` …).
#         `layout` 이나 `h` 같은 비-bool 은 여기서 떨어져 나간다.
#   다른쪽: `SceneViewWindow.cpp` 의 `ImGuizmo::Manipulate` 호출을 **감싸는 블록
#         머리들**. 중괄호를 실제로 세어 올라가며 뽑는다.
#
# 씬 뷰 쪽에서 한 번 더 닫는다. 관문 이름을 초기식에 쓴 지역 `bool` 은 관문이다
# (`canvasInput`). 그리고 ImGui 의 **포인터 위치 술어**로 선 지역 `bool` 도 관문으로
# 본다(`pointerInCanvas` · `isWindowHovered`) — 이쪽은 우리 식별자가 아니라 외부 API
# 어휘라 목록을 적을 수밖에 없고, 대신 그 어휘가 하나도 안 맞으면 게이트를 붉힌다
# (API 이름이 바뀌었는데 집합만 조용히 비는 것이 이 저장소가 여러 번 데인 양식이다).
#
# 입력 관문이 **사라지는** 쪽도 함께 막는다. 그림 조건을 푸는 것만으로 끝내고
# `Enable` 을 안 두면 툴바 빈틈(ImGui 항목이 아닌 자리)을 눌러 기즈모가 잡힌다.
# 그래서 호출을 감싸는 가장 바깥 `if` 의 본문에서 **호출 앞까지**에 `ImGuizmo::Enable`
# 과 관문 이름이 함께 있어야 하고, 전역 상태이므로 `ImGuizmo::Enable(true)` 복구도
# 같은 블록에 있어야 한다.
#
# ★ 숫자를 못 박지 않는다. 관문 개수도 호출 개수도 여기 적지 않는다. 적으면 하나
#   늘 때마다 이유 없이 붉어지고 사람이 숫자만 고치고 지나간다.
#
# **못 잡는 것.** 이것은 소스 대조라 실제 화면을 보지 않는다. 조건을 통과시켜 놓고
# 다른 이유로 기즈모가 안 그려지는 경우(카메라·선택 경로)는 W8 visual golden 몫이다.
# 원시 문자열 리터럴(R"(...)")은 노이즈 제거가 다루지 않는다 — 이 파일들에는 없다.
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent

$script:checks = 0
function Assert([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
    $script:checks++
}

# 주석과 문자열·문자 리터럴을 공백으로 덮는다. **오프셋을 보존한다** — 뒤에서
# 중괄호를 세어 올라가며 줄 번호를 되짚기 때문이다.
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

function Get-Identifiers([string]$expr) {
    @([regex]::Matches($expr, '[A-Za-z_][A-Za-z0-9_]*') | ForEach-Object { $_.Value }) | Sort-Object -Unique
}

function Test-MentionsAny([string]$text, [string[]]$names) {
    foreach ($name in $names) {
        if ([regex]::IsMatch($text, "(?<!\w)$([regex]::Escape($name))(?!\w)")) { return $true }
    }
    $false
}

# `bool a{}, b{}, c = expr;` 에서 선언된 이름들. `=` 앞까지만 보고 깊이 0 의 쉼표로
# 가른다 — 그래야 호출 인자의 쉼표를 선언으로 오독하지 않는다.
function Get-BoolDeclarations([string]$code) {
    $names = [Collections.Generic.List[string]]::new()
    foreach ($match in [regex]::Matches($code, '(?<!\w)bool\s+([^;]{1,400});')) {
        $body = $match.Groups[1].Value
        $depth = 0
        $part = [Text.StringBuilder]::new()
        $parts = [Collections.Generic.List[string]]::new()
        foreach ($ch in $body.ToCharArray()) {
            if ($ch -eq '(' -or $ch -eq '[' -or $ch -eq '{') { $depth++ }
            elseif ($ch -eq ')' -or $ch -eq ']' -or $ch -eq '}') { $depth-- }
            if ($ch -eq ',' -and $depth -le 0) {
                [void]$parts.Add($part.ToString())
                $part = [Text.StringBuilder]::new()
            }
            else { [void]$part.Append($ch) }
        }
        [void]$parts.Add($part.ToString())
        foreach ($piece in $parts) {
            $head = ($piece -split '=', 2)[0]
            $id = [regex]::Match($head, '^\s*([A-Za-z_][A-Za-z0-9_]*)')
            if ($id.Success) { [void]$names.Add($id.Groups[1].Value) }
        }
    }
    @($names | Sort-Object -Unique)
}

function Get-LineNumber([string]$code, [int]$index) {
    ($code.Substring(0, $index) -split "`n").Count
}

# 여는 중괄호 다음의 **첫 문장** — 중첩 깊이 0 의 첫 `;` 까지. 구조체 정의를 낀
# 선언(RAII 스코프)도 한 문장으로 통째로 잡힌다.
function Get-FirstStatement([string]$code, [int]$openBrace) {
    $depth = 0
    for ($i = $openBrace + 1; $i -lt $code.Length; $i++) {
        $ch = $code[$i]
        if ($ch -eq '{') { $depth++ }
        elseif ($ch -eq '}') { if ($depth -eq 0) { return '' }; $depth-- }
        elseif ($ch -eq ';' -and $depth -eq 0) {
            return $code.Substring($openBrace + 1, $i - $openBrace)
        }
    }
    ''
}

$overlayHeaderPath = Join-Path $repoRoot 'Editor/EngineGUIWindow/SceneViewportOverlay.h'
$overlaySourcePath = Join-Path $repoRoot 'Editor/EngineGUIWindow/SceneViewportOverlay.cpp'
$sceneViewPath     = Join-Path $repoRoot 'Editor/EngineGUIWindow/SceneViewWindow.cpp'
foreach ($path in @($overlayHeaderPath, $overlaySourcePath, $sceneViewPath)) {
    Assert (Test-Path -LiteralPath $path) "Source not found: $path"
}
$overlayCode = Remove-CodeNoise ([IO.File]::ReadAllText($overlayHeaderPath) + "`n" + [IO.File]::ReadAllText($overlaySourcePath))
$sceneCode   = Remove-CodeNoise ([IO.File]::ReadAllText($sceneViewPath))

# ── ① 오버레이가 선언한 포인터 관문 ─────────────────────────────────────────
$overlayBools = Get-BoolDeclarations $overlayCode
Assert ($overlayBools.Count -gt 0) 'SceneViewportOverlay 에서 bool 선언을 하나도 찾지 못했다 — 파서가 죽었거나 파일 모양이 바뀌었다.'

$rootGate = 'blocksPointer'
Assert ($overlayBools -contains $rootGate) `
    "오버레이가 더 이상 '$rootGate' 를 bool 로 선언하지 않는다. 포인터 관문의 뿌리 이름이 바뀌었다면 이 게이트의 출발점도 함께 옮겨라."

$gates = [Collections.Generic.HashSet[string]]::new()
[void]$gates.Add($rootGate)
for ($round = 0; $round -lt 16; $round++) {
    $before = $gates.Count
    foreach ($name in @($gates)) {
        $pattern = "(?<!\w)$([regex]::Escape($name))\s*[|&]?=\s*([^;]{1,600});"
        foreach ($assign in [regex]::Matches($overlayCode, $pattern)) {
            foreach ($id in (Get-Identifiers $assign.Groups[1].Value)) {
                if ($overlayBools -contains $id) { [void]$gates.Add($id) }
            }
        }
    }
    if ($gates.Count -eq $before) { break }
}
Assert ($gates.Count -gt 1) `
    "'$rootGate' 대입식에서 아무 관문도 유도하지 못했다 — 대입이 사라졌거나 파서가 식을 못 읽는다(집합이 조용히 비는 것을 성공으로 읽지 않는다)."

# ── ② 씬 뷰 쪽 관문 확장 ────────────────────────────────────────────────────
# 우리 식별자가 아니라 외부 API 어휘라 여기만 목록을 적는다. 안 맞으면 붉힌다.
$pointerPredicates = @('IsMouseHoveringRect', 'IsWindowHovered', 'IsAnyItemHovered', 'IsItemHovered')
$sceneBools = Get-BoolDeclarations $sceneCode
Assert ($sceneBools.Count -gt 0) 'SceneViewWindow 에서 bool 선언을 하나도 찾지 못했다 — 파서가 죽었다.'

$localPattern = '(?<!\w)bool\s+([A-Za-z_][A-Za-z0-9_]*)\s*=\s*([^;]{1,600});'
$predicateHits = 0
foreach ($decl in [regex]::Matches($sceneCode, $localPattern)) {
    if (Test-MentionsAny $decl.Groups[2].Value $pointerPredicates) {
        [void]$gates.Add($decl.Groups[1].Value)
        $predicateHits++
    }
}
Assert ($predicateHits -gt 0) `
    ('ImGui 포인터 술어(' + ($pointerPredicates -join ', ') + ') 로 선 지역 bool 이 하나도 없다 — API 이름이 바뀌었다면 이 어휘를 함께 고쳐라.')

for ($round = 0; $round -lt 16; $round++) {
    $before = $gates.Count
    foreach ($decl in [regex]::Matches($sceneCode, $localPattern)) {
        if (Test-MentionsAny $decl.Groups[2].Value @($gates)) { [void]$gates.Add($decl.Groups[1].Value) }
    }
    if ($gates.Count -eq $before) { break }
}
$gateList = @($gates | Sort-Object)
Write-Host ('  유도된 포인터 관문 ' + $gateList.Count + ' 개: ' + ($gateList -join ', '))

# ── ③ Manipulate 호출을 감싸는 블록 머리 ────────────────────────────────────
# 중괄호를 실제로 세며 스택을 쌓는다. 각 블록의 '머리' 는 직전 문장 경계(; { })
# 부터 여는 중괄호까지의 원문이다 — if (...) 든 함수 시그니처든 그대로 잡힌다.
$calls = @([regex]::Matches($sceneCode, 'ImGuizmo::Manipulate\s*\('))
Assert ($calls.Count -gt 0) 'SceneViewWindow 에 ImGuizmo::Manipulate 호출이 없다 — 기즈모를 그리는 자리가 사라졌거나 다른 파일로 옮겨 갔다.'
$callIndices = [Collections.Generic.HashSet[int]]::new()
foreach ($call in $calls) { [void]$callIndices.Add($call.Index) }

$stack = [Collections.Generic.List[object]]::new()
$blocksAt = @{}
$lastBoundary = 0
for ($i = 0; $i -lt $sceneCode.Length; $i++) {
    if ($callIndices.Contains($i)) { $blocksAt[$i] = @($stack.ToArray()) }
    $ch = $sceneCode[$i]
    if ($ch -eq '{') {
        [void]$stack.Add([pscustomobject]@{
            Header = $sceneCode.Substring($lastBoundary, $i - $lastBoundary)
            Start  = $i
            End    = -1
        })
        $lastBoundary = $i + 1
    }
    elseif ($ch -eq '}') {
        if ($stack.Count -gt 0) {
            $stack[$stack.Count - 1].End = $i
            $stack.RemoveAt($stack.Count - 1)
        }
        $lastBoundary = $i + 1
    }
    elseif ($ch -eq ';') { $lastBoundary = $i + 1 }
}

foreach ($call in $calls) {
    $line = Get-LineNumber $sceneCode $call.Index
    Assert ($blocksAt.ContainsKey($call.Index)) "ImGuizmo::Manipulate(줄 $line) 를 감싸는 블록을 찾지 못했다 — 중괄호 스캐너가 어긋났다."
    $enclosing = @($blocksAt[$call.Index])
    Assert ($enclosing.Count -gt 0) "ImGuizmo::Manipulate(줄 $line) 가 어떤 블록에도 들어 있지 않다."

    # (a) 감싸는 어떤 조건도 포인터 관문을 언급하지 않는다.
    foreach ($block in $enclosing) {
        $header = $block.Header
        $hit = @($gateList | Where-Object { [regex]::IsMatch($header, "(?<!\w)$([regex]::Escape($_))(?!\w)") })
        Assert ($hit.Count -eq 0) `
            ("ImGuizmo::Manipulate(줄 $line) 를 감싸는 조건이 포인터 관문 [" + ($hit -join ', ') + '] 을 언급한다: ' +
             (($header -replace '\s+', ' ').Trim()) +
             ' — Manipulate 는 그림과 입력을 함께 하므로 이 조건은 기즈모를 지운다. 포인터 관문은 ImGuizmo::Enable 로만 흘려라.')
    }

    # (b) 입력 관문이 호출 앞에 실제로 서 있다.
    $guard = $null
    foreach ($block in $enclosing) {
        if ([regex]::IsMatch($block.Header, '(?<!\w)if\s*\(')) { $guard = $block; break }
    }
    Assert ($null -ne $guard) "ImGuizmo::Manipulate(줄 $line) 를 감싸는 if 블록이 없다 — 선택·편집 가능 판정이 사라졌다."
    Assert ($guard.End -gt $guard.Start) "ImGuizmo::Manipulate(줄 $line) 를 감싸는 if 블록의 끝을 찾지 못했다."
    # 관문은 블록의 **첫 문장**이어야 한다. 블록 전체나 호출 앞 전부를 보면 안 된다 —
    # 실제로 그렇게 짰다가 변이 하나를 놓쳤다. 그 사이에 `isWindowHovered` 같은 다른
    # 관문 이름이 우연히 놓여 있어서, 입력 관문을 상수로 굳혀도 초록이었다.
    $opening = Get-FirstStatement $sceneCode $guard.Start
    Assert ($opening.Length -gt 0) "ImGuizmo::Manipulate(줄 $line) 를 감싸는 if 블록이 비어 있다."
    Assert ($opening -match 'ImGuizmo::Enable\s*\(') `
        "ImGuizmo::Manipulate(줄 $line) 를 감싸는 블록의 첫 문장이 ImGuizmo::Enable 을 세우지 않는다 — 입력 관문이 빠지면 툴바 빈틈(ImGui 항목이 아닌 자리)을 눌러 기즈모가 잡힌다."
    Assert (Test-MentionsAny $opening $gateList) `
        "ImGuizmo::Manipulate(줄 $line) 의 입력 관문이 포인터 관문을 하나도 쓰지 않는다 — Enable 이 상수로 굳었다."
    Assert ($opening -match 'ImGuizmo::Enable\s*\(\s*true\s*\)') `
        "ImGuizmo::Enable 복구(Enable(true))가 입력 관문과 같은 문장에 없다 — Enable 은 전역 상태라 다른 기즈모 소비자로 샌다. RAII 로 되돌려라."
}

Write-Host ('PASS verify-scene-gizmo-input-boundary — ' + $script:checks + ' checks, Manipulate 호출 ' + $calls.Count + ' 자리')
