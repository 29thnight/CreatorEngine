# 뷰포트 좌표는 적힌 화면 구성에서만 산다 (PHASE 21 W6 후속 · 멀티뷰포트)
#
# 왜 필요한가
# ───────────
# 2026-09-18 에 `ImGuiConfigFlags_ViewportsEnable` 을 켰다. 패널을 메인 창 밖으로
# 꺼낼 수 있게 되었고, 그 순간 워크스페이스 파일의 성질이 하나 달라졌다 — ImGui 는
# 창이 메인 뷰포트 밖에 있을 때만 `ViewportPos`/`ViewportId` 를 적는데(imgui.cpp
# `ViewportId != IMGUI_VIEWPORT_DEFAULT_ID`), `ViewportPos` 는 **절대 데스크톱
# 좌표**다. 이 파일의 다른 값들과 성질이 다르다: `geometry` 는 창 크기라 어디서
# 열어도 뜻이 같지만, 뷰포트 좌표는 모니터를 하나 빼기만 해도 **없는 화면**을
# 가리킨다. 그리고 보이지 않는 패널은 닫을 창구가 없다.
#
# 워크스페이스는 기계 사이를 옮겨 다니는 파일이다(`Tools/regression/fixtures/`
# 에 커밋되어 있고, 사람 사이에도 오간다). 그래서 **어느 화면 구성에서 적힌
# 좌표인지**를 같이 적고, 다른 구성에서 열리면 좌표만 버린다.
#
# ── 판정 항목 ──
#
#   1  지문이 적힌다 — 비어 있지 않고 `WxH+X+Y` 를 `;` 로 이은 형태다. 스키마는 3
#   2  ★ **지문이 같으면 좌표가 산다** — 이 절이 없으면 "늘 버린다" 와 구분되지
#      않는다. 3 번만 두면 아무것도 안 하는 구현이 통과한다
#   3  ★ 지문이 다르면 좌표가 사라진다 — 가짜 지문을 심어 실제로 버리는지 본다
#   4  ★ v2 파일(지문 줄이 없는 옛 파일)도 버리는 쪽으로 간다 — "모른다" 는
#      "같다" 가 아니다. 그리고 다시 쓸 때는 v3 로 올라가며 지금 지문을 얻는다
#   5  ★ 둘 다 모르면 "같다" 가 아니다 — 빈 지문끼리는 같아 보이지만, 빈 것은
#      "모니터가 없다" 가 아니라 **모른다** 이다. 실제 기계에서는 열거가 실패한
#      환경을 만들 수 없으므로 `CREATOR_EDITOR_MONITOR_FINGERPRINT=none` 으로
#      자를 연다 — 이 절이 없던 동안 `empty()` 검사를 걷는 변이가 나머지 넷을
#      모두 지나갔다
#   6  어느 경우에도 **도크 배치는 그대로다** — 걷는 것은 그 두 줄뿐이므로
#      `DockId` 줄 수가 변하지 않는다
param(
    [string]$Exe = (Join-Path $PSScriptRoot '../../Bin/x64-Release/Editor/CreatorEditor.exe'),
    [string]$Work = (Join-Path $env:TEMP 'creator-workspace-fingerprint')
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$Exe = [IO.Path]::GetFullPath($Exe)
$Work = [IO.Path]::GetFullPath($Work)

if (-not (Test-Path -LiteralPath $Exe)) { throw "Editor executable not found: $Exe" }
if (Get-Process CreatorEditor -ErrorAction SilentlyContinue) {
    throw '이미 떠 있는 에디터를 닫아라 — 이 검사는 남의 에디터를 멈추지 않는다.'
}
if (Test-Path -LiteralPath $Work) { Remove-Item -LiteralPath $Work -Recurse -Force }
New-Item -ItemType Directory -Force -Path $Work | Out-Null

$failures = New-Object System.Collections.Generic.List[string]
$checks = 0
function Assert([bool]$Condition, [string]$Message) {
    $script:checks++
    if (-not $Condition) { $script:failures.Add($Message) }
}

# 주입할 창. 지금 배치에서 **도킹되지 않은** 이름이어야 뷰포트 좌표가 뜻을 갖는다.
$kProbeWindow = '###Editor.OutputLog'
$kProbePos = 'ViewportPos=1000,500'
$kProbeId = 'ViewportId=0x12345678'

function New-Scenario([string]$Name) {
    $dir = Join-Path $Work "scenario/$Name"
    if (Test-Path -LiteralPath $dir) { Remove-Item -LiteralPath $dir -Recurse -Force }
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
    return [IO.Path]::GetFullPath($dir)
}

function Invoke-Editor([string]$Tag, [string]$Scenario, [string]$ForcedFingerprint = '') {
    $scriptPath = Join-Path $Work "$Tag.txt"
    $resultPath = Join-Path $Work "$Tag.jsonl"
    Set-Content -LiteralPath $scriptPath -Encoding UTF8 -Value @('wait 45', 'editor.workspace', 'quit')

    $priorWorkspace = $env:CREATOR_EDITOR_WORKSPACE_DIR
    $priorLegacy = $env:CREATOR_EDITOR_LEGACY_INI
    $priorFingerprint = $env:CREATOR_EDITOR_MONITOR_FINGERPRINT
    $env:CREATOR_EDITOR_WORKSPACE_DIR = $Scenario
    $env:CREATOR_EDITOR_LEGACY_INI = (Join-Path $Scenario 'legacy.ini')
    # 빈 문자열은 환경변수로 전할 수 없다(Windows 가 삭제로 읽는다). `none` 이 그 뜻이다.
    if ($ForcedFingerprint) { $env:CREATOR_EDITOR_MONITOR_FINGERPRINT = $ForcedFingerprint }
    else { $env:CREATOR_EDITOR_MONITOR_FINGERPRINT = $null }
    try {
        $proc = Start-Process -FilePath $Exe -WorkingDirectory (Split-Path $Exe) -WindowStyle Hidden -PassThru `
            -ArgumentList @('--script', ('"' + $scriptPath + '"'), '--result-format', 'jsonl',
                            '--result-file', ('"' + $resultPath + '"')) `
            -RedirectStandardOutput (Join-Path $Work "$Tag.out") `
            -RedirectStandardError (Join-Path $Work "$Tag.err")
        if (-not $proc.WaitForExit(300000)) {
            $proc.Kill()
            throw "Editor did not exit within 300s ($Tag)."
        }
    }
    finally {
        $env:CREATOR_EDITOR_WORKSPACE_DIR = $priorWorkspace
        $env:CREATOR_EDITOR_LEGACY_INI = $priorLegacy
        $env:CREATOR_EDITOR_MONITOR_FINGERPRINT = $priorFingerprint
    }
    $path = Join-Path $Scenario 'active.workspace'
    if (-not (Test-Path -LiteralPath $path)) { throw "Editor wrote no workspace ($Tag)" }
    return [IO.File]::ReadAllText($path)
}

function Get-Fingerprint([string]$Text) {
    $match = [regex]::Match($Text, '(?m)^monitors "([^"]*)"$')
    if (-not $match.Success) { return $null }
    return $match.Groups[1].Value
}

function Count-DockIds([string]$Text) {
    return @([regex]::Matches($Text, '(?m)^DockId=')).Count
}

# 도킹되지 않은 창 하나를 뷰포트 밖에 놓인 것처럼 심는다. ImGui 는 그리지 않은
# 창의 설정도 보존하므로, 걷지 않는 한 저장본에 그대로 돌아온다.
function Add-ProbeViewport([string]$Text) {
    $section = @(
        ''
        "[Window][$kProbeWindow]"
        'Pos=60,60'
        'Size=400,300'
        $kProbePos
        $kProbeId
        'Collapsed=0'
        ''
    ) -join "`n"
    return $Text.TrimEnd() + "`n" + $section
}

# ─────────────────────────────────────────────────────────────────────────
# 1 — 지문이 적힌다
# ─────────────────────────────────────────────────────────────────────────
$fresh = New-Scenario 'records'
$recorded = Invoke-Editor 'records' $fresh

Assert ($recorded -match '(?m)^CreatorWorkspace 3$') `
    'Workspace schema is not 3 — the fingerprint field did not ship'

$fingerprint = Get-Fingerprint $recorded
Assert ($null -ne $fingerprint) 'Workspace has no monitors line'
Assert (-not [string]::IsNullOrWhiteSpace($fingerprint)) `
    'Monitor fingerprint is empty — EnumDisplayMonitors found nothing, so every restore would drop viewport positions'
Assert ($fingerprint -match '^\d+x\d+\+-?\d+\+-?\d+(;\d+x\d+\+-?\d+\+-?\d+)*$') `
    "Monitor fingerprint is malformed: '$fingerprint'"

$baselineDocks = Count-DockIds $recorded
Assert ($baselineDocks -ge 1) `
    "Baseline workspace has no DockId lines ($baselineDocks) — the dock comparison below would be vacuous"

# ─────────────────────────────────────────────────────────────────────────
# 2 ★ 지문이 같으면 좌표가 산다
# ─────────────────────────────────────────────────────────────────────────
#
# 이 절이 이 게이트의 이빨이다. 3 번만 있으면 `strip` 을 무조건 부르는 구현도,
# 아예 뷰포트를 못 쓰게 막은 구현도 통과한다.
$matched = New-Scenario 'match'
[IO.File]::WriteAllText((Join-Path $matched 'active.workspace'), (Add-ProbeViewport $recorded))
$afterMatch = Invoke-Editor 'match' $matched

Assert ($afterMatch.Contains($kProbePos)) `
    'Viewport position was dropped even though the monitor fingerprint matched — a workspace can never keep an undocked panel'
Assert ($afterMatch.Contains($kProbeId)) `
    'Viewport id was dropped even though the monitor fingerprint matched'
Assert ((Count-DockIds $afterMatch) -eq $baselineDocks) `
    "Dock layout changed on a matching restore: $(Count-DockIds $afterMatch) vs $baselineDocks"

# ─────────────────────────────────────────────────────────────────────────
# 3 ★ 지문이 다르면 좌표가 사라진다
# ─────────────────────────────────────────────────────────────────────────
$mismatch = New-Scenario 'mismatch'
$foreign = (Add-ProbeViewport $recorded) -replace '(?m)^monitors "[^"]*"$', 'monitors "640x480+0+0"'
Assert ($foreign.Contains($kProbePos)) 'Fixture build failed: the probe viewport line is missing before the run'
Assert ($foreign -match '(?m)^monitors "640x480\+0\+0"$') 'Fixture build failed: the foreign fingerprint did not take'
[IO.File]::WriteAllText((Join-Path $mismatch 'active.workspace'), $foreign)
$afterMismatch = Invoke-Editor 'mismatch' $mismatch

Assert (-not $afterMismatch.Contains($kProbePos)) `
    'Viewport position survived a foreign monitor fingerprint — the panel would open on a screen that is not there'
Assert (-not $afterMismatch.Contains($kProbeId)) `
    'Viewport id survived a foreign monitor fingerprint'
Assert ((Get-Fingerprint $afterMismatch) -eq $fingerprint) `
    'A restore under a foreign fingerprint did not rewrite the monitors line with the current configuration'
Assert ((Count-DockIds $afterMismatch) -eq $baselineDocks) `
    "Dropping viewport positions also changed the dock layout: $(Count-DockIds $afterMismatch) vs $baselineDocks"

# ─────────────────────────────────────────────────────────────────────────
# 4 ★ v2 파일 — 모른다는 것은 같다는 뜻이 아니다
# ─────────────────────────────────────────────────────────────────────────
$legacy = New-Scenario 'legacy-v2'
$v2 = (Add-ProbeViewport $recorded) -replace '(?m)^monitors "[^"]*"\r?\n', '' -replace '^CreatorWorkspace 3', 'CreatorWorkspace 2'
Assert ($v2 -match '^CreatorWorkspace 2') 'Fixture build failed: the schema was not lowered to 2'
Assert ($null -eq (Get-Fingerprint $v2)) 'Fixture build failed: the monitors line is still present in the v2 fixture'
[IO.File]::WriteAllText((Join-Path $legacy 'active.workspace'), $v2)
$afterLegacy = Invoke-Editor 'legacy-v2' $legacy

Assert (-not $afterLegacy.Contains($kProbePos)) `
    'A v2 workspace kept its viewport position — a file that never recorded a screen configuration cannot vouch for one'
Assert ($afterLegacy -match '(?m)^CreatorWorkspace 3$') `
    'A v2 workspace was not upgraded to 3 on save'
Assert ((Get-Fingerprint $afterLegacy) -eq $fingerprint) `
    'An upgraded workspace did not gain the current monitor fingerprint'
Assert ((Count-DockIds $afterLegacy) -eq $baselineDocks) `
    "The v2 upgrade changed the dock layout: $(Count-DockIds $afterLegacy) vs $baselineDocks"

# ─────────────────────────────────────────────────────────────────────────
# 5 ★ 둘 다 모르면 "같다" 가 아니다
# ─────────────────────────────────────────────────────────────────────────
#
# 열거가 실패한 환경은 빈 지문을 적는다. 그 파일을 **역시 열거가 실패하는 환경**에서
# 열면 빈 문자열끼리 같아져, 좌표를 믿어도 된다는 결론이 나온다. 그것이 틀린 이유는
# 빈 지문이 "모니터가 없다" 가 아니라 **모른다** 이기 때문이다.
#
# 이 절이 없던 동안 `monitors.empty()` 검사를 걷어 내는 변이가 앞의 네 절을 **모두
# 지나갔다** — v2 파일의 빈 지문은 실제 지문과 어차피 다르라서, 그 조건이 없어도
# 버리는 쪽으로 갔기 때문이다. 자극이 닿지 않는 절은 변이를 잡지 못한다.
$unknown = New-Scenario 'unknown'
$blank = (Add-ProbeViewport $recorded) -replace '(?m)^monitors "[^"]*"$', 'monitors ""'
Assert ($blank -match '(?m)^monitors ""$') 'Fixture build failed: the blank fingerprint did not take'
[IO.File]::WriteAllText((Join-Path $unknown 'active.workspace'), $blank)
$afterUnknown = Invoke-Editor 'unknown' $unknown 'none'

Assert (-not $afterUnknown.Contains($kProbePos)) `
    'A blank saved fingerprint matched a blank current one and kept the viewport position — "unknown" is not "the same"'
Assert ((Get-Fingerprint $afterUnknown) -eq '') `
    'The forced-unknown run did not write a blank fingerprint, so the stimulus never reached the code under test'
Assert ((Count-DockIds $afterUnknown) -eq $baselineDocks) `
    "The unknown-fingerprint restore changed the dock layout: $(Count-DockIds $afterUnknown) vs $baselineDocks"

# ─────────────────────────────────────────────────────────────────────────
# 판정
# ─────────────────────────────────────────────────────────────────────────
Write-Host ''
Write-Host "  fingerprint  : $fingerprint"
Write-Host "  dock ids     : $baselineDocks (unchanged across all four restores)"
Write-Host "  kept / dropped: match=kept · foreign=dropped · v2=dropped · unknown=dropped"
Write-Host ''

if ($failures.Count -gt 0) {
    Write-Host "FAIL  workspace viewport fingerprint ($($failures.Count) of $checks)" -ForegroundColor Red
    foreach ($item in $failures) { Write-Host "  - $item" -ForegroundColor Red }
    exit 1
}
Write-Host "PASS  workspace viewport fingerprint ($checks checks)" -ForegroundColor Green
exit 0
