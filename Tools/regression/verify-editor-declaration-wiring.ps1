[CmdletBinding()]
param(
    [string]$Exe = (Join-Path $PSScriptRoot '../../Bin/x64-Debug/Editor/CreatorEditor.exe'),
    [string]$Work = (Join-Path $env:TEMP 'creator-editor-declaration')
)
# PHASE 21 M4 4단계 — `editor::` 선언 배선 게이트.
#
# 두 가지를 한 번에 태운다.
#
# ① `editor.selftest` — 창 표와 메뉴 표의 선언 자가 검사. 둘 다 자기 표를 옆으로
#    치우고 합성 선언 위에서 돌므로 살아 있는 에디터에서 부를 수 있다. 처음에는
#    "표가 비어 있을 때만"이라 부팅 전 한순간에만 돌 수 있었고, 그 말은 도는
#    세트에 넣을 수 없다는 뜻이었다. 이 저장소는 "게이트가 도는 세트에 없으면
#    없는 것"으로 두 번 데었다.
#
# ② `editor.windows` — 선언 표와 본문 보관소를 맞대 본다. 어느 한쪽에만 있는
#    이름이 곧 조용히 죽은 창이고, 그것을 양방향으로 본다. 오타 하나가 영영 비어
#    있는 창을 만들던 옛 `GetContext` 결함의 반대편이 새로 생겼기 때문이다 —
#    새 창구는 없는 이름에 아무 일도 하지 않는데, 그러면 이번에는 아무 일도
#    일어나지 않아서 오타가 안 보인다.
#
# ③ `editor.menu` — 선언된 메뉴 표와 충돌 판정(M2). 같은 표면 안 경로 충돌,
#    이름 없는 항목, 항목을 0개 낸 선언자를 본다.
#
# ④ 표면 대조 — `popup_host`·`top_menu_root` 열거자마다 **그리는 자리가 있는가.**
#    이것만 런타임이 볼 수 없다. 그리는 자리는 C++ 호출 지점이고 팝업이 실제로
#    열려야 한 번 도는 코드여서, 표에는 흔적이 남지 않는다. 그래서 소스에서
#    양쪽을 뽑아 맞댄다 — 한쪽은 `EditorMenuSurface.h` 의 열거자, 다른 한쪽은
#    트리 전체의 `draw_popup_menu_items<...>` 호출이다. **양쪽이 다 소스에서
#    유도되므로 손으로 적은 목록이 없다**(두 벌이 되면 한쪽이 낡아도 모른다).
#
# ★ 숫자를 못 박지 않는다. 창·메뉴 개수를 여기 적으면 하나 더할 때마다 이
#   게이트가 이유 없이 붉어지고, 그러면 사람이 숫자만 고치고 지나간다. 이
#   게이트가 지키는 것은 개수가 아니라 **배선이 성립하는가**다.
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$Exe = [IO.Path]::GetFullPath($Exe)
$Work = [IO.Path]::GetFullPath($Work)
$repoRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
New-Item -ItemType Directory -Force -Path $Work | Out-Null

if (-not (Test-Path -LiteralPath $Exe)) { throw "Editor executable not found: $Exe" }
# 프로젝트에 endpoint 파일은 하나다. 개발자가 띄워 둔 에디터를 죽이지 않는다.
if (Get-Process CreatorEditor -ErrorAction SilentlyContinue) { throw 'Close the existing editor before running this isolated gate.' }

$script:checks = 0
function Assert([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
    $script:checks++
}

$scriptPath = Join-Path $Work 'declaration.txt'
$resultPath = Join-Path $Work 'declaration.jsonl'
$stdoutPath = Join-Path $Work 'declaration.out'
$stderrPath = Join-Path $Work 'declaration.err'
foreach ($file in @($resultPath, $stdoutPath, $stderrPath)) {
    if (Test-Path -LiteralPath $file) { Remove-Item -LiteralPath $file }
}
Set-Content -LiteralPath $scriptPath -Encoding UTF8 -Value @('editor.selftest', 'editor.windows', 'editor.menu', 'quit')

$proc = Start-Process -FilePath $Exe -WorkingDirectory (Split-Path $Exe) -WindowStyle Hidden -PassThru `
    -ArgumentList @('--script', ('"' + $scriptPath + '"'), '--result-format', 'jsonl',
                    '--result-file', ('"' + $resultPath + '"')) `
    -RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath
if (-not $proc.WaitForExit(300000)) {
    $proc.Kill()
    throw 'Editor did not exit within 300s while running the declaration gate.'
}
# ★ 종료 코드 단정은 맨 뒤다. 배치 러너는 명령 하나가 실패하면 4로 나가는데, 그
#   숫자만으로는 **어느 창이 어떻게 끊겼는지**를 알 수 없다. 변이 증명에서 실제로
#   그것이 드러났다 — 본문 거는 이름에 오타를 하나 넣었을 때 게이트는 붉어졌지만
#   사람이 읽은 것은 "exited 4" 한 줄이었고, 아래 열다섯 단정은 한 번도 돌지 않았다.
#   그래서 결과 줄을 먼저 읽고 내용을 단정한 뒤에 종료 코드를 본다. 이 순서라야
#   붉은 줄이 "orphan_bodies=1 [Hierarchy_typo]"처럼 고칠 곳을 가리킨다.
#
#   종료 코드 단정을 **없애지는 않는다.** 내용이 전부 초록인데 프로세스가 0이 아닌
#   것은 그 자체로 결함이고(크래시·종료 경로 실패), 이 저장소는 종료 코드를 보지
#   않는 게이트에 이미 한 번 데었다.
Assert (Test-Path -LiteralPath $resultPath) "No result file produced at $resultPath (batch exited $($proc.ExitCode); see $stdoutPath)"
$lines = @(Get-Content -LiteralPath $resultPath | Where-Object { $_.Trim().Length -gt 0 })
$results = @{}
foreach ($line in $lines) {
    $parsed = $line | ConvertFrom-Json
    $results[$parsed.command] = $parsed
}

# ── ① 선언 자가 검사 ────────────────────────────────────────────────────
Assert ($results.ContainsKey('editor.selftest')) 'editor.selftest produced no result line'
$selftest = $results['editor.selftest']
Assert ($selftest.status -eq 'succeeded') "editor.selftest failed: $($selftest.message)"
Assert ($selftest.data.windows -eq $true) "Window declaration selftest failed: $($selftest.data.windowReport)"
Assert ($selftest.data.menus -eq $true) "Menu declaration selftest failed: $($selftest.data.menuReport)"

# ── ② 창 배선 감사 ──────────────────────────────────────────────────────
Assert ($results.ContainsKey('editor.windows')) 'editor.windows produced no result line'
$windows = $results['editor.windows']
Assert ($windows.status -eq 'succeeded') "editor.windows failed: $($windows.message)"
Assert ($windows.data.clean -eq $true) "Window wiring audit is dirty: $($windows.message)"
Assert ($windows.data.orphanBodies -eq 0) "Bodies bound to undeclared window ids: $($windows.data.orphanBodies)"
Assert ($windows.data.bodylessWindows -eq 0) "Declared windows with no body bound: $($windows.data.bodylessWindows)"
Assert ($windows.data.duplicateIds -eq 0) "Duplicate stable ids: $($windows.data.duplicateIds)"
Assert ($windows.data.emptyDockSlots -eq 0) "Dock slots no window targets: $($windows.data.emptyDockSlots)"

# 표가 비어 있는데 초록으로 지나가는 것을 막는다. 빈 집합을 성공으로 읽는 것이
# 이 저장소에서 두 번 나온 실패 양식이다.
Assert ($windows.data.declared -gt 0) 'Window declaration table is empty; the gate would be vacuous'
Assert ($windows.data.boundBodies -gt 0) 'No window bodies are bound; the gate would be vacuous'

# 표에 도크 자리를 가진 창이 실제로 있어야 한다 — 전부 floating 이면 위의 빈 자리
# 단정이 자동으로 통과한다.
$table = Get-Content -LiteralPath $stdoutPath
Assert (($table | Where-Object { $_ -match "`tcenter`t" }).Count -gt 0) 'No window declares the center dock slot'

# ── ③ 메뉴 배선 감사 (M2) ───────────────────────────────────────────────
Assert ($results.ContainsKey('editor.menu')) 'editor.menu produced no result line'
$menus = $results['editor.menu']
Assert ($menus.status -eq 'succeeded') "editor.menu failed: $($menus.message)"
Assert ($menus.data.clean -eq $true) "Menu wiring audit is dirty: $($menus.message)"
Assert ($menus.data.pathConflicts -eq 0) "Same path declared twice on one surface: $($menus.data.pathConflicts)"
Assert ($menus.data.unnamedItems -eq 0) "Declared menu items with an empty sub path: $($menus.data.unnamedItems)"
Assert ($menus.data.silentDeclarers -eq 0) "Declarers in the central list that contributed nothing: $($menus.data.silentDeclarers)"
Assert ($menus.data.declarersSeen -eq $menus.data.declarersExpected) `
    "Declarer count mismatch: seen $($menus.data.declarersSeen) expected $($menus.data.declarersExpected)"

# 표가 비어 있는데 초록으로 지나가는 것을 막는다 — 중앙 목록에서 줄을 지우는 것은
# 표와 기대치를 **함께** 줄이므로 위의 동수 단정으로는 잡히지 않는다. 하한이 막는다.
Assert ($menus.data.declarersSeen -gt 0) 'No menu declarer contributed anything; the gate would be vacuous'
Assert (($menus.data.topItems + $menus.data.popupItems) -gt 0) 'Menu declaration table is empty; the gate would be vacuous'

# ── ④ 표면 대조: 열거자마다 그리는 자리가 있는가 ────────────────────────
$surfaceHeader = Join-Path $repoRoot 'Editor/EditorMenu/EditorMenuSurface.h'
Assert (Test-Path -LiteralPath $surfaceHeader) "Menu surface header not found: $surfaceHeader"
$surfaceText = [IO.File]::ReadAllText($surfaceHeader)

function Get-Enumerators([string]$Text, [string]$EnumName) {
    $m = [regex]::Match($Text, "enum class $EnumName\s*\{(?<body>[^}]*)\}")
    if (-not $m.Success) { throw "Could not find 'enum class $EnumName' in the surface header." }
    $names = @()
    foreach ($line in $m.Groups['body'].Value -split "`n") {
        $bare = ($line -replace '//.*$', '').Trim().TrimEnd(',').Trim()
        if ($bare.Length -eq 0) { continue }
        if ($bare -eq 'count') { continue }
        if ($bare -notmatch '^[a-z_][a-z0-9_]*$') { continue }
        $names += $bare
    }
    return $names
}

# 양성 확인: 파서가 실제로 열거자를 뽑았다. 빈 집합 위에서 도는 부재 단정 방지.
$popupHosts = @(Get-Enumerators $surfaceText 'popup_host')
$topRoots = @(Get-Enumerators $surfaceText 'top_menu_root')
Assert ($popupHosts.Count -ge 3) "Parsed only $($popupHosts.Count) popup_host enumerators; the parser likely broke"
Assert ($topRoots.Count -ge 3) "Parsed only $($topRoots.Count) top_menu_root enumerators; the parser likely broke"

# 그리는 자리는 소스에서 뽑는다. Editor 트리 전체를 본다 — 배선이 어느 창에
# 들어갈지는 정해져 있지 않다.
$sources = @(Get-ChildItem -LiteralPath (Join-Path $repoRoot 'Editor') -Recurse -File -Include *.cpp `
    | Where-Object { $_.FullName -notmatch '[\\/](ThirdParty|Build|x64)[\\/]' })
Assert ($sources.Count -ge 50) "Scanned only $($sources.Count) editor sources; the scan scope likely broke"
$allSource = ($sources | ForEach-Object { [IO.File]::ReadAllText($_.FullName) }) -join "`n"

# 선언 계층 자체(감사·그리기)는 호출 지점이 아니다. 그쪽의 템플릿 정의가
# 대조를 통과시키면 "배선 안 된 표면 0"이 공허해진다.
$drawSites = @{}
foreach ($m in [regex]::Matches($allSource, 'draw_popup_menu_items<[^>]*popup_host::(?<host>[a-z_][a-z0-9_]*)\s*>')) {
    $drawSites[$m.Groups['host'].Value] = $true
}
$unwiredHosts = @($popupHosts | Where-Object { -not $drawSites.ContainsKey($_) })
Assert ($unwiredHosts.Count -eq 0) "popup_host enumerators with no draw site: $($unwiredHosts -join ', ')"

$unknownHosts = @($drawSites.Keys | Where-Object { $popupHosts -notcontains $_ })
Assert ($unknownHosts.Count -eq 0) "Draw sites for popup hosts that are not enumerated: $($unknownHosts -join ', ')"

# 상단 쪽은 **그리는 함수 이름과 함께** 찾는다. `top_menu_root::x` 가 어디에 나오든
# 세면 자가 검사나 감사의 언급이 draw site 로 잡혀 대조가 공허해진다.
$topSites = @{}
foreach ($m in [regex]::Matches($allSource,
        '(?:append_top_menu_items|draw_top_menu_items|draw_top_menu_root)\s*\(\s*(?:::)?editor::top_menu_root::(?<root>[a-z_][a-z0-9_]*)')) {
    $topSites[$m.Groups['root'].Value] = $true
}
$unwiredRoots = @($topRoots | Where-Object { -not $topSites.ContainsKey($_) })
Assert ($unwiredRoots.Count -eq 0) "top_menu_root enumerators with no draw site: $($unwiredRoots -join ', ')"

# 내용이 다 맞았으면 마지막으로 프로세스가 깨끗하게 나갔는지 본다.
Assert ($proc.ExitCode -eq 0) "Declaration gate batch exited $($proc.ExitCode) though every declaration check passed; see $stdoutPath"

"editor:: declaration wiring OK — windows declared=$($windows.data.declared) bound=$($windows.data.boundBodies), menu items=$($menus.data.topItems + $menus.data.popupItems) from $($menus.data.declarersSeen) declarer(s), surfaces wired=$($popupHosts.Count) popup / $($topRoots.Count) top, checks=$($script:checks)"
exit 0
