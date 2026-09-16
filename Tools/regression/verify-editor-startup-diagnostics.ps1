# 에디터가 한 바퀴 도는 동안 **아무도 안 읽던 두 채널**을 판정한다.
#
# ── 왜 필요한가 ─────────────────────────────────────────────────────────────
#
# 2026-09-16 에 결함 둘이 같은 날 들어왔고 **둘 다 빌드 exit 0** 이었다.
#
#   ① 치환 사고가 문자열 리터럴 46 자리에 ` << ` 를 박았다. 그중 둘이
#      `spReflection*_GetTypeLayout` 심볼 이름이라 Slang reflection 적재가
#      실패했고, 셰이더 파이프라인이 통째로 안 섰다.
#   ② 네이티브가 `CreatorScriptApiVersion` 을 24→25 로 올렸는데 관리 측
#      `Native.cs` 의 `ExpectedVersion` 이 24 로 남아 CLR 초기화가 실패했다.
#
# 둘 다 컴파일러가 잡을 것이 없다(①은 전부 문자열, ②는 런타임 비교). 그리고
# 두 실패 메시지는 **인메모리·HTML 싱크로만** 가고 stdout 에는 안 나온다 —
# 회귀 하네스가 읽을 수단이 아예 없었다. 사람이 로그 창을 열어 보고 찾았다.
#
# 여기서 그 눈을 기계로 바꾼다. 재는 것은 둘이다.
#
#   · HTML 로그의 error/critical/crash 행 수     → 0 이어야 한다
#   · stdout 의 `[imgui-error]` 줄 수            → 0 이어야 한다
#
# 뒤엣것은 ImGui 가 자기 규약 위반을 신고하는 채널이다. 2026-09-16 에 로그 창의
# 행 그리기가 `SetCursorPos` 로 창 경계를 늘려 **매 프레임** 오류를 냈는데,
# 그림은 맞았고 게이트 넷은 전부 초록이었다.
#
# ── 빈 집합을 성공으로 읽지 않는다 ──────────────────────────────────────────
#
# "오류 0" 은 로그를 못 읽어도 0 이다. 그래서 자극이 실제로 일어났음을 따로
# 단정한다 — 로그 파일이 **새로** 생겼고, 그 안에 이 회차의 `scene.save` 가 남긴
# 줄이 있고, 세션 종료 줄까지 있어 로그가 잘리지 않았고, 로그 창이 실제로 열렸다.
#
# 행 **개수**로 재지 않는다. 실측 16 을 기준으로 삼으면 그것이 곧 눈대중이고,
# 자극이 바뀌면 조용히 낡는다. 내가 일으킨 사건이 보이는지로 판정한다.
#
# ── 심각도 집합은 제품에서 뽑는다 ───────────────────────────────────────────
#
# 어느 수준이 "심각" 인지는 `HtmlFileSink.h` 안의 뷰어 스크립트가 이미 정하고
# 있다. 그 목록을 여기 손으로 베끼면 싱크가 바뀔 때 게이트만 낡는다. 파일에서
# 뽑아 쓰고, 뽑지 못하면 통과시키지 않고 던진다.
[CmdletBinding()]
param(
    [string]$Exe,
    # 판정에서 뺄 오류 메시지 조각. 비워 두는 것이 정상이다 — 채울 때는 왜
    # 그것이 기대되는 오류인지 한 줄 적어라.
    [string[]]$AllowedErrorSubstrings = @(),
    [switch]$KeepArtifacts
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if ([string]::IsNullOrWhiteSpace($Exe)) {
    # 기본값은 **출하 구성**이다. 이 저장소는 기본 -Exe 가 Debug 를 가리켜
    # 게이트가 낡은 바이너리를 재고도 초록이던 일을 겪었다.
    $Exe = Join-Path $repo 'Bin\x64-Release\Editor\CreatorEditor.exe'
}
if (-not (Test-Path -LiteralPath $Exe)) { throw "에디터를 찾지 못했다: $Exe" }

$script:checks = 0
$script:failures = @()
function Assert([bool]$condition, [string]$message) {
    $script:checks++
    if (-not $condition) { $script:failures += $message; Write-Host "  [실패] $message" }
}

# ── 심각도 집합을 제품 파일에서 뽑는다 ──────────────────────────────────────
$sinkPath = Join-Path $repo 'Engine\Utility_Framework\HtmlFileSink.h'
if (-not (Test-Path -LiteralPath $sinkPath)) { throw "HtmlFileSink.h 를 찾지 못했다: $sinkPath" }
$sinkText = Get-Content -LiteralPath $sinkPath -Raw
$severeMatch = [regex]::Match($sinkText, "return\s+lv\s*===\s*'(?<a>\w+)'\s*\|\|\s*lv\s*===\s*'(?<b>\w+)'\s*\|\|\s*lv\s*===\s*'(?<c>\w+)'")
if (-not $severeMatch.Success) {
    throw "HtmlFileSink.h 에서 심각도 집합을 뽑지 못했다 — 뷰어 스크립트가 바뀌었으면 이 정규식을 함께 고쳐라"
}
$severe = @($severeMatch.Groups['a'].Value, $severeMatch.Groups['b'].Value, $severeMatch.Groups['c'].Value)
Write-Host ("  심각도 집합(HtmlFileSink.h 에서 유도): " + ($severe -join ', '))
Assert ($severe -contains 'error' -and $severe -contains 'critical') `
    ("심각도 집합에 error·critical 이 없다: " + ($severe -join ', '))

# ── C++ 로그 래퍼가 호출자의 자리를 넘기는가 (소스 축) ─────────────────────
#
# `Debug::PrintLog` 의 `where` 는 기본 인자라, 메시지를 받아 넘기는 래퍼가 그것을
# 생략하면 기본값이 **래퍼 자리**에서 채워진다. 호출자 서른둘이 세 줄로 몰린다 —
# C# 쪽 `Component.Log` 와 같은 함정이다.
#
# 이 축은 런타임으로 못 잡는다. 셋 다 오류 경로거나(설정 파싱 실패·pak 정리 실패)
# pak 런타임 정리 경로라 기동 한 바퀴에서 한 번도 안 돈다. 자극할 수 없는 것을
# 런타임 단정으로 적으면 미자극이 초록으로 읽힌다 — 그래서 소스에서 본다.
$wrappers = @(
    @{ File = 'Engine\Utility_Framework\PakHelper.h';          Name = 'RuntimeCleanupError' }
    @{ File = 'Engine\Utility_Framework\PakHelper.h';          Name = 'RuntimeCleanupInfo' }
    @{ File = 'Editor\EngineEntry\EditorSettingsStore.cpp';    Name = 'ReportSettingsError' }
)
foreach ($wrapper in $wrappers) {
    $wrapperPath = Join-Path $repo $wrapper.File
    if (-not (Test-Path -LiteralPath $wrapperPath)) { throw ("래퍼 파일이 없다: " + $wrapper.File) }
    $wrapperText = Get-Content -LiteralPath $wrapperPath -Raw
    # ★ 정의만 잡아야 한다. 이름 뒤의 괄호를 그냥 찾으면 **호출 자리**가 먼저
    # 걸린다(`return ReportSettingsError("...")`). 실제로 그렇게 적었다가 W2
    # 변이에서 드러났다 — 이름을 바꿨는데 "정의를 못 찾았다" 가 아니라 호출의
    # 인수를 매개변수로 읽고 엉뚱한 단정이 울렸다. 지금까지 초록이던 것은 두 파일
    # 모두 정의가 호출보다 앞에 있었던 우연이다.
    #
    # 가르는 기준: 매개변수에 **타입 선언**이 있는가. 호출은 값이나 식을 넘긴다.
    $definitions = @([regex]::Matches($wrapperText,
        ('(?s)\b' + [regex]::Escape($wrapper.Name) + '\s*\((?<params>[^)]*)\)')) |
        Where-Object { $_.Groups['params'].Value -match 'const\s+std::string\s*&' })
    Assert ($definitions.Count -eq 1) `
        ("래퍼 정의를 " + $definitions.Count + " 건 찾았다 (기대 1): " + $wrapper.Name +
         " (" + $wrapper.File + ") — 이름이 바뀌었거나 오버로드가 생겼으면 이 표를 함께 고쳐라")
    if ($definitions.Count -ne 1) { continue }
    Assert ($definitions[0].Groups['params'].Value -match 'source_location') `
        ($wrapper.Name + " 가 std::source_location 을 받지 않는다 (" + $wrapper.File +
         ") — 호출자의 자리가 아니라 이 래퍼 자신이 출처로 찍힌다")
}

$work = Join-Path ([IO.Path]::GetTempPath()) ("editor-startup-diagnostics-" + [Guid]::NewGuid().ToString('N').Substring(0, 8))
New-Item -ItemType Directory -Force -Path $work | Out-Null

# ── 자극 ────────────────────────────────────────────────────────────────────
#
# 로그 창을 **연다.** 닫혀 있으면 그 창의 그리기 경로가 한 번도 안 돌아
# `[imgui-error]` 축이 통째로 눈먼다. 그리고 창 안을 한 번 찌른다 — 선택과
# 상세 영역은 눌러야만 그려진다.
$logDir = Join-Path (Split-Path $Exe) 'Saved\Log'
$before = @()
if (Test-Path -LiteralPath $logDir) {
    $before = @(Get-ChildItem -LiteralPath $logDir -Filter '*.html' -File | ForEach-Object { $_.FullName })
}

$mark = Join-Path $work 'mark.scene'
$lines = @(
    'window.resize 1600 1000'
    'wait 240'
    'editor.window ###Editor.OutputLog open'
    'editor.window ###Editor.OutputLog focus'
    'wait 120'
    'editor.nav pointer 400 400'
    'wait 6'
    'editor.nav press'
    'wait 6'
    'editor.nav release'
    'wait 120'
    # 프로파일러도 한 번 연다. 그리는 코드가 늘수록 ImGui 규약 위반이 드러날
    # 자리가 는다.
    'editor.window ###Editor.FrameProfiler open'
    'wait 120'
    # 관리 로그를 **래퍼를 통해** 낸다. 기동 로그(`Bootstrap.cs`)는 `Native.Log` 를
    # 직접 부르므로 `Component.Log` 계열의 [Caller*] 전달을 한 번도 안 지난다 —
    # 그 축을 자극하지 않고 아래 단정을 걸면 미자극을 초록으로 읽는다.
    'script.invoke ManagedLogProbe EmitLogLines'
    'wait 30'
    ('scene.save ' + $mark)
    'wait 60'
    'quit'
)
$scriptPath = Join-Path $work 'script.txt'
$lines | Set-Content -LiteralPath $scriptPath -Encoding UTF8
$resultPath = Join-Path $work 'results.jsonl'
$stdoutPath = Join-Path $work 'stdout.txt'

$env:CREATOR_EDITOR_WORKSPACE_DIR = Join-Path $work 'workspace'
$env:CREATOR_EDITOR_LEGACY_INI = Join-Path $work 'no-legacy.ini'
New-Item -ItemType Directory -Force -Path $env:CREATOR_EDITOR_WORKSPACE_DIR | Out-Null

$exitCode = -1
try {
    $proc = Start-Process -FilePath $Exe -WorkingDirectory (Split-Path $Exe) -PassThru `
        -ArgumentList @('--script', ('"' + $scriptPath + '"'), '--result-format', 'jsonl',
                        '--result-file', ('"' + $resultPath + '"')) `
        -RedirectStandardOutput $stdoutPath `
        -RedirectStandardError (Join-Path $work 'stderr.txt')
    if (-not $proc.WaitForExit(600000)) { $proc.Kill(); throw '에디터가 끝나지 않았다' }
    $exitCode = $proc.ExitCode
}
finally {
    Remove-Item Env:CREATOR_EDITOR_WORKSPACE_DIR -ErrorAction SilentlyContinue
    Remove-Item Env:CREATOR_EDITOR_LEGACY_INI -ErrorAction SilentlyContinue
}
Assert (0 -eq $exitCode) "에디터 종료 코드가 0 이 아니다: $exitCode"

# ── 자극이 실제로 일어났는가 ────────────────────────────────────────────────
$rows = @()
if (Test-Path -LiteralPath $resultPath) {
    $rows = @(Get-Content -LiteralPath $resultPath | Where-Object { $_.Trim() } |
        ForEach-Object { $_ | ConvertFrom-Json })
}
$opened = @($rows | Where-Object {
    $_.command -eq 'editor.window' -and $_.status -eq 'succeeded' -and
    $null -ne $_.data -and $_.data.stableId -eq '###Editor.OutputLog' -and $_.data.request -eq 'open' })
Assert ($opened.Count -ge 1) `
    "로그 창을 열지 못했다 — 이 회차는 그 창의 그리기 경로를 한 번도 안 돌았고, imgui 축이 눈멀었다"
Assert (Test-Path -LiteralPath $mark) `
    "표지 씬이 저장되지 않았다 — 스크립트가 끝까지 돌지 않았다"

# ── ① HTML 로그의 심각 행 ───────────────────────────────────────────────────
$after = @()
if (Test-Path -LiteralPath $logDir) {
    $after = @(Get-ChildItem -LiteralPath $logDir -Filter '*.html' -File | ForEach-Object { $_.FullName })
}
$fresh = @($after | Where-Object { $before -notcontains $_ })
Assert ($fresh.Count -ge 1) `
    "이 회차가 만든 HTML 로그를 찾지 못했다 ($logDir) — '오류 0' 은 읽지 못해도 0 이다"

$severeRows = @()
$totalRows = 0
$sawStimulus = $false
$sawSessionEnd = $false
$managedSources = @()
foreach ($file in $fresh) {
    $html = Get-Content -LiteralPath $file -Raw
    $totalRows += ([regex]::Matches($html, '<tr\s+data-lv="')).Count
    # 관리 로그가 **자기** 호출 지점을 싣는가.
    #
    # `Native.Log` 는 예전에 표의 `Log` 슬롯으로 가서 위치를 안 실었고, 모든 C#
    # 로그가 `ClrHost.cpp` 한 줄을 자기 출처로 보고했다. 래퍼(`Component.Log`
    # 등)가 자기 `[Caller*]` 를 전달하지 않아도 같은 증상이 나는데, 그때는
    # `Component.cs` 한 줄로 몰린다. 둘 다 "출처가 있다" 로는 안 걸린다 —
    # `.cs` 파일이 **여럿** 나오는지를 봐야 한다.
    foreach ($match in [regex]::Matches($html, '<tr\s+data-lv="[^"]*"[^>]*?\sdata-src="(?<src>[^"]+)"')) {
        $value = $match.Groups['src'].Value
        if ($value -like '*.cs:*') { $managedSources += $value }
    }
    # 이 회차의 `scene.save` 가 남긴 줄. 로그 통로가 살아 있다는 증거를 개수가
    # 아니라 **내가 일으킨 사건**에서 얻는다 — 개수는 눈대중이 된다.
    if ($html.Contains('[CLI] 씬 저장')) { $sawStimulus = $true }
    # 세션이 정상으로 닫혔다는 마지막 줄. 이것이 없으면 로그가 잘린 것이고,
    # 잘린 로그의 '심각 0' 은 앞부분만 본 것이다.
    if ($html.Contains('로그 세션 정상 종료')) { $sawSessionEnd = $true }
    foreach ($match in [regex]::Matches($html, '(?s)<tr\s+data-lv="(?<lv>\w+)"[^>]*>(?<body>.*?)</tr>')) {
        if ($severe -notcontains $match.Groups['lv'].Value) { continue }
        $text = ($match.Groups['body'].Value -replace '<[^>]+>', ' ') -replace '\s+', ' '
        $text = $text.Trim()
        $skip = $false
        foreach ($allowed in $AllowedErrorSubstrings) {
            if ($allowed -and $text.Contains($allowed)) { $skip = $true; break }
        }
        if (-not $skip) { $severeRows += ("[" + $match.Groups['lv'].Value + "] " + $text) }
    }
}
Assert $sawStimulus `
    'scene.save 가 남긴 줄이 HTML 로그에 없다 — 로그 통로가 끊겼다면 심각 행 0 은 눈먼 초록이다'
Assert $sawSessionEnd `
    "HTML 로그에 세션 종료 줄이 없다 — 로그가 잘렸고, 잘린 로그의 심각 0 은 앞부분만 본 것이다"

# ── ③ 관리 로그가 자기 호출 지점을 싣는가 ──────────────────────────────────
$distinctManaged = @($managedSources | Sort-Object -Unique)
Write-Host ("  관리 로그 출처 " + $managedSources.Count + " 줄 · 고유 " + $distinctManaged.Count + " 자리")
foreach ($src in ($distinctManaged | Select-Object -First 5)) { Write-Host ("    " + $src) }
Assert ($managedSources.Count -ge 1) `
    "C# 이 낸 로그가 한 줄도 없다 — CLR 이 기동했는데 관리 로그가 안 보이면 경로가 끊긴 것이고, 아래 '고유 자리' 단정은 빈 집합을 통과한다"
# 자리가 **하나뿐**이면 통과시키지 않는다. 브리지나 래퍼가 자기 위치를 싣는
# 고장이 정확히 그 모양이다 — 출처는 있는데 전부 같은 한 줄이다.
Assert ($distinctManaged.Count -ge 2) `
    ("관리 로그 출처가 " + $distinctManaged.Count + " 자리뿐이다 (" + ($distinctManaged -join ', ') +
     ") — 래퍼나 브리지가 호출자 대신 자기 위치를 싣고 있다. [Caller*] 를 전달하는지 봐라")

# 자극이 실제로 래퍼를 지났는가. 이 줄이 없으면 아래 '래퍼 파일이 없다' 단정이
# 빈 집합을 통과한다 — 래퍼를 안 지났으니 당연히 래퍼 이름도 안 나온다.
Assert (@($managedSources | Where-Object { $_ -like 'ManagedLogProbe.cs:*' }).Count -ge 1) `
    ("ManagedLogProbe 가 낸 줄이 로그에 없다 — script.invoke 가 돌지 않았거나 래퍼 경로가 끊겼다. 관측: " +
     ($distinctManaged -join ', '))

# 래퍼 자신이 출처로 찍히면 안 된다.
#
# `Component.Log` 계열이 자기 [Caller*] 를 전달하지 않으면 호출자 전부가 래퍼 한
# 줄로 몰린다. 그때도 출처는 **있고** 자리 수도 2 이상일 수 있어서(기동 로그가
# 따로 있다) 위 단정들로는 안 걸린다. 래퍼 파일 이름을 직접 금지한다.
$wrapperFiles = @('Component.cs', 'BehaviorTree.cs', 'AniBehavior.cs', 'Native.cs', 'Debug.cs')
$wrapperHits = @($distinctManaged | Where-Object {
    $name = ($_ -split ':')[0]
    $wrapperFiles -contains $name })
Assert ($wrapperHits.Count -eq 0) `
    ("관리 로그가 래퍼 자신을 출처로 싣는다: " + ($wrapperHits -join ', ') +
     " — 그 래퍼가 [Caller*] 를 받아 넘기지 않는다. 호출자의 자리가 아니면 출처는 없는 것만 못하다")

foreach ($row in $severeRows) { Write-Host ("    " + $row.Substring(0, [Math]::Min(220, $row.Length))) }
Assert ($severeRows.Count -eq 0) `
    ("기동 한 바퀴에서 심각 로그가 " + $severeRows.Count + " 건 나왔다 (위 목록). 고치거나, 기대되는 것이면 -AllowedErrorSubstrings 에 이유와 함께 넣어라")

# ── ② ImGui 가 신고한 규약 위반 ─────────────────────────────────────────────
$imguiErrors = @()
if (Test-Path -LiteralPath $stdoutPath) {
    $imguiErrors = @(Select-String -LiteralPath $stdoutPath -Pattern '[imgui-error]' -SimpleMatch |
        ForEach-Object { $_.Line.Trim() })
}
$distinct = @($imguiErrors | ForEach-Object { $_ -replace '^\[\d+\]\s*', '' } | Sort-Object -Unique)
foreach ($line in ($distinct | Select-Object -First 5)) {
    Write-Host ("    " + $line.Substring(0, [Math]::Min(220, $line.Length)))
}
Assert ($imguiErrors.Count -eq 0) `
    ("ImGui 가 규약 위반을 " + $imguiErrors.Count + " 줄 신고했다(고유 " + $distinct.Count + " 종, 위 목록). 그림이 맞아도 위반은 위반이다")

Write-Host ""
Write-Host ("  HTML 로그 행 " + $totalRows + " · 심각 " + $severeRows.Count +
            " · imgui-error " + $imguiErrors.Count + " 줄")

if (-not $KeepArtifacts) { Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue }
else { Write-Host ("  산출물: " + $work) }

Write-Host ""
if ($script:failures.Count -gt 0) {
    Write-Host ("EDITOR_STARTUP_DIAGNOSTICS_FAILED: " + $script:failures.Count + " / " + $script:checks)
    exit 1
}
Write-Host ("EDITOR_STARTUP_DIAGNOSTICS_OK (" + $script:checks + " checks)")
exit 0
