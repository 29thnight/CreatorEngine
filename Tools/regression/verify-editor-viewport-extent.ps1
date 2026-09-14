# 뷰포트 extent 기반 resize · 렌더 배율 (PHASE 21 W4 후속)
#
# 왜 필요한가
# ───────────
# 라이브 뷰의 렌더 해상도가 **창 클라이언트 크기**였다. 가운데 ViewportHost 의
# 캔버스는 창보다 늘 작으므로(좌우 패널·아래 패널이 자리를 가져간다) 보이는 것보다
# 큰 그림을 그려서 잘라 버리고 있었고, 창을 DPI 배수로 키운 뒤에는 그 낭비가
# 2 배를 넘었다. Release 실측으로 2880x1665 창에서 522fps 였다.
#
# Godot 은 에디터 3D 뷰를 `SubViewportContainer` 의 `SubViewport` 로 두고 stretch 가
# "the sub-viewport will be automatically resized to the control's size" 를 한다.
# Unreal 은 Slate 뷰포트 위젯 크기로 렌더 타깃을 잡고, 그 위에 secondary screen
# percentage 를 `100 / OS's DPI Scale` 로 둔다. 둘 다 창 크기가 아니다.
#
# 이 게이트가 지키는 것은 그 계약이다.
#
# ── 판정 항목 ──
#
#   1  측정이 나왔다        — 캔버스 extent 가 0 이 아니고 Host 가 돌고 있다
#   2  ★ 캔버스는 창보다 작다 — 창 크기로 그리면 그만큼이 통째로 낭비다
#   3  ★ 렌더 해상도 = 캔버스 × 배율 — 배율 셋(off·auto·fixed) 모두에서 참이다.
#      창 크기로 되돌아가면 셋 중 둘이 어긋난다(배율이 안 먹으므로)
#   4  배율 계약        — off=1.0 · auto=1/DPI · fixed=준 값
#   5  파이프라인이 따라온다 — `dx12.live` 의 width/height 가 렌더 해상도와 같다
#   6  범위 밖 배율 거부  — 1.0 을 넘기면 이 작업이 없애려던 낭비가 되살아난다
#   7  ★ 리사이즈 건강   — 창 크기를 연달아 바꾼 뒤에도 표시 텍스처가 살아 있고
#      실패가 0 이다. 계획서가 extent 기반 resize 를 "두 backend 의 generation/
#      retire 검증 뒤" 로 미뤄 둔 바로 그 축이다 — 미루는 대신 여기서 잰다
#   8  엔진이 정상 종료했다
param(
    [string]$Exe = (Join-Path $PSScriptRoot "..\..\Bin\x64-Debug\Editor\CreatorEditor.exe"),
    [string]$Work = ""
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path -LiteralPath $Exe)) { throw "Editor executable is missing: $Exe" }
if ([string]::IsNullOrWhiteSpace($Work)) {
    $Work = Join-Path ([IO.Path]::GetTempPath()) ("CE_ViewportExtent_" + [guid]::NewGuid().ToString("N"))
}
New-Item -ItemType Directory -Force -Path $Work | Out-Null

# workspace 를 격리한다. 개발자의 배치를 빌리면 캔버스 크기가 그 파일에 달려
# 있어 단정이 기계마다 다른 수를 보게 된다.
$savedWorkspaceDir = $env:CREATOR_EDITOR_WORKSPACE_DIR
$savedLegacyIni = $env:CREATOR_EDITOR_LEGACY_INI
$env:CREATOR_EDITOR_WORKSPACE_DIR = $Work
$env:CREATOR_EDITOR_LEGACY_INI = Join-Path $Work "legacy.ini"

$failures = New-Object System.Collections.Generic.List[string]
$checks = 0
function Assert([bool]$ok, [string]$what) {
    $script:checks++
    if (-not $ok) { $script:failures.Add($what) }
}

$scriptPath = Join-Path $Work "extent.txt"
$resultPath = Join-Path $Work "extent.jsonl"
# ★ 첫 표본 앞의 대기가 길다. 부팅 직후의 파이프라인은 **부팅 당시 창 크기**로
#   서 있고, 캔버스 extent 가 그것을 밀어내려면 (진정 프레임 → 버스 통지 →
#   렌더 스레드의 파이프라인 재생성) 셋을 다 지나야 한다. 처음에 400 프레임만
#   두었더니 첫 표본이 부팅 크기를 읽어 5번이 붉었다 — 게이트의 결함이었다.
# 배율을 바꾼 뒤 **수렴할 때까지 표집한다**. 한 번만 재면 "얼마나 기다리면
# 되는가" 를 게이트가 상수로 박게 되고, 그 상수는 기계 속도에 달린 거짓 단정이
# 된다. 대신 표본 여럿을 찍고 그 중 하나라도 맞으면 통과 · 몇 번째에 맞았는지를
# 함께 낸다 — 수렴이 느려지면 그 수가 먼저 커진다.
$kPollSamples = 6
$lines = @('wait 900', 'window.resize 2560 1440', 'wait 900')
foreach ($mode in @('off', 'auto', '0.5')) {
    $lines += "editor.renderscale $mode"
    $lines += 'wait 400'
    $lines += 'editor.viewport'
    for ($p = 0; $p -lt $kPollSamples; $p++) {
        $lines += 'dx12.live status'
        $lines += 'wait 300'
    }
}
# 리사이즈 연타 — generation/retire 가 이것을 견디는지가 7번이다.
foreach ($size in @('1600 900', '2200 1200', '1280 800', '2560 1440')) {
    $lines += "window.resize $size"
    $lines += 'wait 250'
}
$lines += @('editor.renderscale auto', 'wait 400', 'editor.viewport')
for ($p = 0; $p -lt $kPollSamples; $p++) { $lines += 'dx12.live status'; $lines += 'wait 300' }
$lines += 'quit'
Set-Content -LiteralPath $scriptPath -Encoding UTF8 -Value $lines

try {
    $proc = Start-Process -FilePath $Exe -WorkingDirectory (Split-Path $Exe) -PassThru `
        -ArgumentList @('--script', ('"' + $scriptPath + '"'), '--result-format', 'jsonl',
                        '--result-file', ('"' + $resultPath + '"'))
    if (-not $proc.WaitForExit(600000)) { $proc.Kill(); throw "editor did not exit in time" }
    $exitCode = $proc.ExitCode
}
finally {
    if ($null -ne $savedWorkspaceDir) { $env:CREATOR_EDITOR_WORKSPACE_DIR = $savedWorkspaceDir }
    else { Remove-Item Env:CREATOR_EDITOR_WORKSPACE_DIR -ErrorAction SilentlyContinue }
    if ($null -ne $savedLegacyIni) { $env:CREATOR_EDITOR_LEGACY_INI = $savedLegacyIni }
    else { Remove-Item Env:CREATOR_EDITOR_LEGACY_INI -ErrorAction SilentlyContinue }
}

if (-not (Test-Path -LiteralPath $resultPath)) { throw "result file missing: $resultPath" }
$rows = @(Get-Content -LiteralPath $resultPath | Where-Object { $_.Trim() } | ForEach-Object { $_ | ConvertFrom-Json })

$viewports = @($rows | Where-Object { $_.command -eq 'editor.viewport' })
$lives     = @($rows | Where-Object { $_.command -eq 'dx12.live' })
$scales    = @($rows | Where-Object { $_.command -eq 'editor.renderscale' })
$resizes   = @($rows | Where-Object { $_.command -eq 'window.resize' })

Assert ($viewports.Count -ge 4) "editor.viewport 표본이 4개 미만이다 ($($viewports.Count))"
Assert ($lives.Count -ge (4 * $kPollSamples)) "dx12.live 표본이 모자라다 ($($lives.Count))"
if ($failures.Count -gt 0) {
    $failures | ForEach-Object { "  FAIL $_" }
    throw "뷰포트 extent 게이트: 표본이 모자라 판정할 수 없다"
}

# 창 크기는 `window.resize` 가 알려 준 실제 값에서 받는다 — 요청값은 클램프될 수 있다.
$firstResize = $resizes | Select-Object -First 1
$windowWidth = 0; $windowHeight = 0
if ($firstResize.message -match '실제\s+(\d+)x(\d+)') { $windowWidth = [int]$Matches[1]; $windowHeight = [int]$Matches[2] }
Assert ($windowWidth -gt 0 -and $windowHeight -gt 0) "창 크기를 window.resize 결과에서 읽지 못했다"

$expectedModes = @('off', 'auto', 'fixed', 'auto')
$convergedAt = @()
for ($i = 0; $i -lt 4; $i++) {
    $v = $viewports[$i].data
    $tag = "[$i $($expectedModes[$i])]"

    # 1 — 측정이 나왔다
    Assert ($viewports[$i].status -eq 'succeeded') "$tag editor.viewport 가 실패했다: $($viewports[$i].code)"
    Assert ([int]$v.canvasWidth -gt 0 -and [int]$v.canvasHeight -gt 0) "$tag 캔버스 extent 가 0 이다"

    # 2 — 캔버스는 창보다 작다
    if ($windowWidth -gt 0) {
        Assert ([int]$v.canvasWidth -lt $windowWidth) `
            "$tag 캔버스 폭 $($v.canvasWidth) 이 창 폭 $windowWidth 보다 작지 않다"
        Assert ([int]$v.canvasHeight -lt $windowHeight) `
            "$tag 캔버스 높이 $($v.canvasHeight) 이 창 높이 $windowHeight 보다 작지 않다"
    }

    # 4 — 배율 계약
    $scale = [double]$v.renderScale
    $dpi = [double]$v.dpiScale
    Assert ($v.renderScaleMode -eq $expectedModes[$i]) "$tag 배율 모드가 $($v.renderScaleMode) 다"
    switch ($expectedModes[$i]) {
        'off'   { Assert ([math]::Abs($scale - 1.0) -lt 0.001) "$tag off 인데 배율이 $scale 다" }
        'auto'  { Assert ([math]::Abs($scale - (1.0 / $dpi)) -lt 0.01) "$tag auto 인데 배율 $scale 이 1/$dpi 와 다르다" }
        'fixed' { Assert ([math]::Abs($scale - 0.5) -lt 0.001) "$tag fixed 인데 배율이 $scale 다" }
    }

    # 3 — 렌더 해상도 = 캔버스 × 배율 (격자 2 이므로 허용 오차도 2)
    $expectedW = [math]::Round([int]$v.canvasWidth * $scale)
    $expectedH = [math]::Round([int]$v.canvasHeight * $scale)
    Assert ([math]::Abs([int]$v.renderWidth - $expectedW) -le 2) `
        "$tag 렌더 폭 $($v.renderWidth) 이 캔버스×배율 $expectedW 과 다르다 (캔버스 $($v.canvasWidth), 배율 $scale)"
    Assert ([math]::Abs([int]$v.renderHeight - $expectedH) -le 2) `
        "$tag 렌더 높이 $($v.renderHeight) 이 캔버스×배율 $expectedH 과 다르다 (캔버스 $($v.canvasHeight), 배율 $scale)"

    # 5 — 파이프라인이 따라온다. 이 arm 의 표본 묶음 안에서 수렴하면 된다.
    $armSamples = @($lives[($i * $kPollSamples)..(($i + 1) * $kPollSamples - 1)])
    $hit = -1
    for ($k = 0; $k -lt $armSamples.Count; $k++) {
        if ([int]$armSamples[$k].data.display.width -eq [int]$v.renderWidth -and
            [int]$armSamples[$k].data.display.height -eq [int]$v.renderHeight) { $hit = $k; break }
    }
    $convergedAt += $hit
    $lastSeen = $armSamples[$armSamples.Count - 1].data.display
    Assert ($hit -ge 0) `
        "$tag 파이프라인이 $kPollSamples 표본 안에 렌더 해상도 $($v.renderWidth)x$($v.renderHeight) 로 수렴하지 않았다 (마지막 $($lastSeen.width)x$($lastSeen.height))"
}

# 6 — 범위 밖 배율 거부. **따로 띄운다** — 거부는 session 이 종료 코드에 적으므로
#     본 실행에 섞으면 8번(정상 종료)이 그 거부 때문에 붉는다.
$refusePath = Join-Path $Work "refuse.txt"
$refuseResult = Join-Path $Work "refuse.jsonl"
Set-Content -LiteralPath $refusePath -Encoding UTF8 -Value @(
    'wait 600', 'editor.renderscale 2.0', 'editor.renderscale 0.1', 'quit')
$savedWorkspaceDir2 = $env:CREATOR_EDITOR_WORKSPACE_DIR
$env:CREATOR_EDITOR_WORKSPACE_DIR = $Work
$env:CREATOR_EDITOR_LEGACY_INI = Join-Path $Work "legacy.ini"
try {
    $refuseProc = Start-Process -FilePath $Exe -WorkingDirectory (Split-Path $Exe) -PassThru `
        -ArgumentList @('--script', ('"' + $refusePath + '"'), '--result-format', 'jsonl',
                        '--result-file', ('"' + $refuseResult + '"'))
    if (-not $refuseProc.WaitForExit(300000)) { $refuseProc.Kill(); throw "refusal run did not exit" }
}
finally {
    if ($null -ne $savedWorkspaceDir2) { $env:CREATOR_EDITOR_WORKSPACE_DIR = $savedWorkspaceDir2 }
    else { Remove-Item Env:CREATOR_EDITOR_WORKSPACE_DIR -ErrorAction SilentlyContinue }
    Remove-Item Env:CREATOR_EDITOR_LEGACY_INI -ErrorAction SilentlyContinue
}
$refuseRows = @(Get-Content -LiteralPath $refuseResult | Where-Object { $_.Trim() } |
    ForEach-Object { $_ | ConvertFrom-Json } | Where-Object { $_.command -eq 'editor.renderscale' })
Assert ($refuseRows.Count -eq 2) "거부 실행의 표본이 2개가 아니다 ($($refuseRows.Count))"
foreach ($r in $refuseRows) {
    Assert ($r.status -ne 'succeeded') "범위 밖 배율이 통과했다: $($r.message)"
}

# 7 — 리사이즈 건강. 마지막 표본이 연타 뒤의 상태다.
#
#     ★ 처음에는 `maxMissingTextureMs` 를 문턱으로 쟀는데 그것은 **부팅**까지
#       포함하는 누계 최댓값이라 18 초가 나왔다 — 첫 라이브 프레임이 그만큼
#       걸린다는 사실을 잰 것이지 리사이즈를 잰 것이 아니었다. 리사이즈가
#       재야 하는 것은 "세대가 수렴했는가" 다.
$last = $lives[$lives.Count - 1].data
Assert ([bool]$last.ready) "리사이즈 연타 뒤 파이프라인이 준비 상태가 아니다"
Assert ([bool]$last.display.scene.active) "리사이즈 연타 뒤 씬 표시 타깃이 꺼져 있다"
Assert ([bool]$last.display.scene.lastTextureAvailable) "리사이즈 연타 뒤 씬 표시 텍스처가 없다"
$generation = [int64]$last.display.resizeGeneration
$completed = [int64]$last.display.scene.completedResizeGeneration
$textureGeneration = [int64]$last.display.scene.lastTextureResizeGeneration
Assert ($generation -gt 1) "리사이즈 세대가 늘지 않았다 ($generation) — 연타가 아무것도 안 했다"
Assert ($completed -eq $generation) `
    "완료 세대 $completed 가 현재 세대 $generation 에 못 미친다 (retire 가 밀렸다)"
Assert ($textureGeneration -eq $generation) `
    "표시 텍스처가 옛 세대 $textureGeneration 것이다 (현재 $generation)"
$firstRendered = [int64]$lives[0].data.framesRendered
$lastRendered = [int64]$last.framesRendered
Assert ($lastRendered -gt $firstRendered) "리사이즈 연타 구간에서 렌더 프레임이 늘지 않았다 ($firstRendered → $lastRendered)"

# 8 — 정상 종료
Assert ($exitCode -eq 0) "에디터 종료 코드가 $exitCode 다"

""
"뷰포트 extent 판정 (창 ${windowWidth}x${windowHeight}):"
for ($i = 0; $i -lt 4; $i++) {
    $v = $viewports[$i].data
    "  [{0,-5}] 캔버스 {1,4}x{2,-4} × {3:N2} = 렌더 {4,4}x{5,-4} · 파이프라인 수렴 표본 {6}/{7}" -f `
        $v.renderScaleMode, $v.canvasWidth, $v.canvasHeight, $v.renderScale,
        $v.renderWidth, $v.renderHeight, ($convergedAt[$i] + 1), $kPollSamples
}
$wastedBefore = if ($windowWidth -gt 0) { [math]::Round(($windowWidth * $windowHeight) / ([double]$viewports[3].data.renderWidth * [int]$viewports[3].data.renderHeight), 2) } else { 0 }
"  창 크기로 그리던 때 대비 픽셀 $wastedBefore 배 절감 · 리사이즈 세대 $generation 수렴"

if ($failures.Count -gt 0) {
    ""
    $failures | ForEach-Object { "  FAIL $_" }
    ""
    throw "뷰포트 extent 게이트 실패 — 단정 $checks 개 중 $($failures.Count) 개"
}
"viewport extent OK — 단정 $checks 개 통과"
exit 0
