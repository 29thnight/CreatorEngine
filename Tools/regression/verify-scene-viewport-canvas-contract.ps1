# 뷰포트 캔버스 규약 — 정본 하나 (PHASE 21 W2-V0)
#
# 왜 필요한가
# ───────────
# W2-V0 의 완료 기준은 두 줄이다: *"같은 크기에서 창 원점이 달라도 상대 배치 동일,
# W4 정본 외 좌표식 없음"*. 구현은 2026-09-16 에 닫혔지만 **둘 다 읽을 자가 없었다.**
#
# 그 사이 정본은 조용히 둘이 되어 있었다. `LayoutViewportCanvas` 가 소스 픽셀이
# 없을 때 `content` 까지 0 으로 돌려주었고 — content 는 모드가 받은 자리라 소스와
# 무관하게 늘 아는 값인데도 — 그래서 씬뷰가 `imageMin` 이라는 이름의 **지역 content
# 사각형**을 따로 만들어 배경·안내문·드롭 영역·오버레이에 넘기고 있었다. 오버레이는
# 그 사각형과 캔버스를 **둘 다** 받으면서 배치는 인자 쪽으로 했다. 값이 같아 보이는
# 동안에는 드러나지 않는다 — Scene 이 `fill` 이라 content 와 image 가 같기 때문이고,
# 정책이 갈리는 순간(crop 은 image 가 content 를 넘는다) 툴바가 화면 밖으로 나간다.
#
# ── 판정 항목 ──
#
#   1  ★ 정본 외 좌표식 0   — 캔버스를 만든 **뒤** 뷰포트 좌표를 다시 만드는 자리가
#      없다. 입력을 만드는 자리는 각 모드에 하나씩만 허용하되 **호출 횟수까지**
#      적는다 — 면제 단위가 강제 단위(호출 지점)와 같아야 한다(소스 축)
#   2  ★ 제목표시줄 보정 0  — 규약 ①이 "원점은 content" 라고 적은 그 금지다.
#      보정을 쓰는 줄이 아니라 보정을 **만드는 호출**을 금지한다
#   3  규약 ② — 사각형은 min/max 쌍이고 크기는 `max - min` 이다
#   4  규약 — clip = image ∩ content
#   5  uv 는 image 기준이다 — clip 의 uv 가 [0,1] 안의 부분 구간
#   6  ★ 창 원점이 달라도 상대 배치가 같다 — 창을 **실제로 옮겨** 두 번 잰다.
#      `window.resize` 로는 못 재는 축이다(도크 노드가 절대 폭을 지켜 원점이
#      움직이지 않는다). 좌표계의 원점은 멀티뷰포트 설정이 정하므로 그것도
#      함께 단정한다 — 꺼지면 클라이언트 기준이라 좌표가 그대로고, 켜지면
#      창 이동량 × DPI 배율만큼 따라 움직인다. 계약은 둘 다에서 하나다
#   7  엔진이 정상 종료했다
param(
    [string]$Exe = (Join-Path $PSScriptRoot "..\..\Bin\x64-Release\Editor\CreatorEditor.exe"),
    [string]$Work = "",
    [int]$TimeoutSeconds = 180
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path -LiteralPath $Exe)) { throw "Editor executable is missing: $Exe" }
if ([string]::IsNullOrWhiteSpace($Work)) {
    $Work = Join-Path ([IO.Path]::GetTempPath()) ("CE_Canvas_" + [guid]::NewGuid().ToString("N"))
}
New-Item -ItemType Directory -Force -Path $Work | Out-Null

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$failures = New-Object System.Collections.Generic.List[string]
$checks = 0
function Assert([bool]$ok, [string]$what) {
    $script:checks++
    if (-not $ok) { $script:failures.Add($what) }
}

# ─────────────────────────────────────────────────────────────────────────
# 소스 축 — 정본 외 좌표식
# ─────────────────────────────────────────────────────────────────────────
#
# 강제 단위는 **호출 지점**이다. 그래서 허용 목록도 호출 지점으로 적는다 —
# 파일과 그 호출을 감싼 함수 이름. 파일 단위로 면제하면 그 파일 안에 새로 생긴
# 좌표식이 조용히 통과한다.
$kViewportSources = @(
    'Editor\EngineGUIWindow\SceneViewWindow.cpp',
    'Editor\EngineGUIWindow\GameViewWindow.cpp',
    'Editor\EngineGUIWindow\SceneViewportOverlay.cpp'
)
# 뷰포트 좌표를 **만드는** 호출. 캔버스가 있는데 이것을 다시 부르면 출처가 둘이 된다.
$kOriginCalls = @('GetCursorScreenPos', 'GetContentRegionAvail', 'GetWindowPos', 'GetWindowSize')

# 창 프레임 높이를 내는 호출. 규약 ①은 "원점은 content 이고 제목표시줄 높이를
# 되더하는 호출자는 없어야 한다" 고 적는다. 그 보정을 **만들 재료**가 이것들이라
# 면제 없이 금지한다(2026-09-18 실측: 이 세 파일에 호출 0 건).
#
# ★ 예전에는 `GetFrameHeight()` 와 좌표 이름이 **한 줄 안에** 같이 있는지를
#   봤다. 변이 `titlebar-correction` 이 그 줄을 둘로 쪼개 통과했다 —
#   `const float titleBar = ImGui::GetFrameHeight();` 다음 줄에서 빼면
#   어느 줄도 두 조건을 함께 만족하지 않는다. 줄을 보는 대신 호출을 금지한다.
$kForbiddenHeightCalls = @('GetFrameHeight', 'GetFrameHeightWithSpacing', 'GetTextLineHeightWithSpacing')

# 캔버스 **입력**을 만드는 자리. 각 모드에 하나씩이고, 호출 **횟수**까지 적는다.
#
# ★ 횟수를 적는 이유. 강제 단위는 호출 지점인데 면제 단위가 (파일, 함수, 이름)
#   이면 그 함수 안에 같은 호출을 **하나 더** 넣어도 통과한다. 변이
#   `private-rect-again` 이 정확히 그것으로 빠져나갔다 — `RenderSceneView` 안에
#   `GetCursorScreenPos` 를 하나 더 두어 사설 사각형을 되살렸는데 검사는
#   조용했다. 면제 단위가 강제 단위와 같아야 한다.
$kInputSites = @(
    @{ File = 'SceneViewWindow.cpp'; Function = 'RenderSceneView'; Calls = @{ GetCursorScreenPos = 1; GetContentRegionAvail = 1 } },
    @{ File = 'GameViewWindow.cpp';  Function = 'draw_game_view';  Calls = @{ GetCursorScreenPos = 1; GetContentRegionAvail = 1 } },
    # 오버레이의 위젯 지역 좌표는 뷰포트 좌표가 아니다 — ImGui 가 쌓아 주는
    # 커서를 읽어 그 자리에 그리고, 끝나면 되돌린다. 캔버스와 무관하다.
    @{ File = 'SceneViewportOverlay.cpp'; Function = 'Button';     Calls = @{ GetCursorScreenPos = 1 } },
    @{ File = 'SceneViewportOverlay.cpp'; Function = 'Draw';       Calls = @{ GetCursorScreenPos = 1 } }
)

function Get-EnclosingFunction([string[]]$lines, [int]$index) {
    # 위로 올라가며 열 0 에서 시작하는 정의 줄을 찾는다. 이 저장소의 본문은
    # 전부 들여쓰기가 있으므로 열 0 의 `이름(` 이 함수 머리다.
    for ($i = $index; $i -ge 0; $i--) {
        $l = $lines[$i]
        if ($l -match '^\S.*?([A-Za-z_][A-Za-z0-9_]*)\s*\(' -and $l -notmatch '^\s*//') {
            return $Matches[1]
        }
        # 익명 네임스페이스 안의 정적 헬퍼는 들여쓰기가 있다. 4칸 들여쓴 정의도 받는다.
        if ($l -match '^    [A-Za-z_][A-Za-z0-9_:<>,\s\*&]*\s([A-Za-z_][A-Za-z0-9_]*)\s*\([^;]*$' -and $l -notmatch '^\s*//') {
            return $Matches[1]
        }
    }
    return '<unknown>'
}

$sourceHits = New-Object System.Collections.Generic.List[string]
$inputSiteSeen = @{}   # 호출 지점 → 실제로 본 횟수
foreach ($rel in $kViewportSources) {
    $path = Join-Path $repoRoot $rel
    if (-not (Test-Path -LiteralPath $path)) { $failures.Add("소스가 없다: $rel"); continue }
    $leaf = Split-Path $rel -Leaf
    $lines = Get-Content -LiteralPath $path
    for ($i = 0; $i -lt $lines.Count; $i++) {
        # 주석은 센다면 거짓 양성이 된다. 이 저장소는 주석에 API 이름을 자주 적는다.
        $code = $lines[$i] -replace '//.*$', ''
        if ([string]::IsNullOrWhiteSpace($code)) { continue }

        foreach ($call in $kOriginCalls) {
            if ($code -notmatch ("ImGui::" + $call + "\s*\(")) { continue }
            $fn = Get-EnclosingFunction $lines $i
            $allowed = @($kInputSites | Where-Object { $_.File -eq $leaf -and $_.Function -eq $fn -and $_.Calls.ContainsKey($call) })
            if ($allowed.Count -gt 0) {
                # 면제 여부가 아니라 **몇 번째 호출인지**를 센다. 정해진 횟수를
                # 넘는 순간 아래의 대조에서 붉는다.
                $key = "${leaf}::$fn::$call"
                if ($inputSiteSeen.ContainsKey($key)) { $inputSiteSeen[$key] = $inputSiteSeen[$key] + 1 }
                else { $inputSiteSeen[$key] = 1 }
                continue
            }
            $sourceHits.Add("${leaf}:$($i+1) [$fn] ImGui::$call — 캔버스 밖에서 뷰포트 좌표를 만든다")
        }
        # 2 — 제목표시줄 보정. 규약 ①이 금지한 그 식이다. 보정을 쓰는 줄이
        #     아니라 보정을 **만드는 호출**을 본다 — 변수에 담아 다음 줄에서
        #     빼는 형태를 줄 단위 검사로는 잡을 수 없다.
        foreach ($call in $kForbiddenHeightCalls) {
            if ($code -notmatch ("ImGui::" + $call + "\s*\(")) { continue }
            $fn = Get-EnclosingFunction $lines $i
            $sourceHits.Add("${leaf}:$($i+1) [$fn] ImGui::$call — 창 프레임 높이를 좌표에 쓴다(규약 ①)")
        }
    }
}
Assert ($sourceHits.Count -eq 0) ("정본 외 좌표식 $($sourceHits.Count) 건: " + ($sourceHits -join ' · '))

# 허용 목록이 **낡지도 헐겁지도** 않은지 본다. 적어 둔 자리가 없어졌다면 그
# 항목은 아무것도 면제하지 않으면서 다음 사람에게 거짓 지도를 주고, 적어 둔
# 횟수보다 많이 나왔다면 면제가 그만큼 넓어진 것이다. 양쪽을 한 단정으로 묶으면
# 어느 쪽인지 모르게 되므로 사유를 나눠 적는다.
foreach ($site in $kInputSites) {
    foreach ($call in $site.Calls.Keys) {
        $key = "$($site.File)::$($site.Function)::$call"
        $want = [int]$site.Calls[$call]
        $got = if ($inputSiteSeen.ContainsKey($key)) { [int]$inputSiteSeen[$key] } else { 0 }
        if ($got -eq 0) { Assert $false "허용 목록이 낡았다: $key 가 소스에 없다" }
        elseif ($got -gt $want) { Assert $false "허용한 자리에 호출이 늘었다: $key 는 $want 회인데 $got 회다 — 캔버스 밖에서 좌표를 하나 더 만든다" }
        elseif ($got -lt $want) { Assert $false "허용 목록이 낡았다: $key 는 $want 회로 적혀 있는데 $got 회뿐이다" }
        else { Assert $true "$key = $want 회" }
    }
}

# ─────────────────────────────────────────────────────────────────────────
# 런타임 축
# ─────────────────────────────────────────────────────────────────────────
Add-Type -Namespace CanvasGate -Name Win -MemberDefinition @'
[DllImport("user32.dll", SetLastError=true)]
public static extern bool SetWindowPos(IntPtr hWnd, IntPtr after, int x, int y, int cx, int cy, uint flags);
[DllImport("user32.dll")]
public static extern bool GetWindowRect(IntPtr hWnd, out RECT r);
public struct RECT { public int Left, Top, Right, Bottom; }
'@

$savedWorkspaceDir = $env:CREATOR_EDITOR_WORKSPACE_DIR
$savedLegacyIni = $env:CREATOR_EDITOR_LEGACY_INI
$env:CREATOR_EDITOR_WORKSPACE_DIR = $Work
$env:CREATOR_EDITOR_LEGACY_INI = Join-Path $Work "legacy.ini"

$endpointPath = Join-Path $repoRoot "Dynamic_CPP\Library\CommandService\endpoint.json"
if (Test-Path -LiteralPath $endpointPath) { Remove-Item -LiteralPath $endpointPath -Force }

# ★ 배치(`--script`)를 쓰지 않는다.
#
#   `wait N` 은 `Pump()` 를 조기 반환시켜 **서비스 큐를 통째로 세운다**. 배치로
#   에디터를 살려 두면 그 동안 `POST /command` 가 전부 `command.timeout` 이 된다
#   (처음에 그렇게 짜서 표본이 0 이었다). `/warmup` 은 큐를 타지 않으므로 멀쩡해
#   보였고, 그 차이가 이 게이트의 설계 근거이기도 하다.
#
#   서비스만 켜고 띄운 뒤 필요한 명령을 HTTP 로 보낸다. 종료는 이 스크립트가 한다.

$proc = $null
$exitCode = -1
$moved = @()
$laid = @()
try {
    $proc = Start-Process -FilePath $Exe -WorkingDirectory (Split-Path $Exe) -PassThru `
        -ArgumentList @('--command-service')

    $deadline = [datetime]::UtcNow.AddSeconds($TimeoutSeconds)
    while (-not (Test-Path -LiteralPath $endpointPath) -and [datetime]::UtcNow -lt $deadline -and -not $proc.HasExited) {
        Start-Sleep -Milliseconds 150
    }
    if (-not (Test-Path -LiteralPath $endpointPath)) { throw "서비스가 열리지 않았다 (종료=$($proc.HasExited))" }
    $info = Get-Content -LiteralPath $endpointPath -Raw | ConvertFrom-Json
    $base = "http://$($info.host):$($info.port)"
    $headers = @{ Authorization = "Bearer $($info.token)" }

    # ★ 예열을 **창구로** 기다린다. 프레임 수로 기다리면 구성마다 뜻이 달라지고,
    #   덜 기다리면 캔버스가 무효라 아래 단정이 전부 빈다(2026-09-18 에 그렇게
    #   읽어 제품 결함으로 오독할 뻔했다).
    while ([datetime]::UtcNow -lt $deadline -and -not $proc.HasExited) {
        try { $w = Invoke-RestMethod -Uri "$base/warmup" -Headers $headers -TimeoutSec 10 } catch { Start-Sleep -Milliseconds 300; continue }
        if ($w.complete) { break }
        Start-Sleep -Milliseconds 400
    }
    Assert ($null -ne $w -and $w.complete) "예열이 끝나지 않았다 — 마지막 단계 '$($w.last)'"

    function Read-SceneView {
        $body = @{ command = 'editor.sceneview' } | ConvertTo-Json -Compress
        try {
            return Invoke-RestMethod -Uri "$base/command" -Headers $headers -Method Post `
                -ContentType 'application/json' -Body $body -TimeoutSec 30
        }
        catch {
            $detail = $_.Exception.Message
            $resp = $_.Exception.Response
            if ($null -ne $resp) {
                try {
                    $reader = New-Object IO.StreamReader($resp.GetResponseStream())
                    $detail += " | " + $reader.ReadToEnd()
                } catch { }
            }
            $script:failures.Add("editor.sceneview 호출이 실패했다: $detail")
            return $null
        }
    }

    function Send-Cmd([string]$cmd, [string[]]$a) {
        $b = @{ command = $cmd; args = $a } | ConvertTo-Json -Compress
        try {
            return Invoke-RestMethod -Uri "$base/command" -Headers $headers -Method Post `
                -ContentType 'application/json' -Body $b -TimeoutSec 40
        }
        catch { $script:failures.Add("$cmd $($a -join ' ') 가 실패했다: $($_.Exception.Message)"); return $null }
    }

    # ★ 좌표계의 **원점이 어디인가**를 제품에게 묻는다. 멀티뷰포트가 켜지면 ImGui
    #   화면 좌표가 클라이언트 좌표계에서 가상 데스크톱 좌표계로 바뀐다. 이것을
    #   모르면 아래 축 A 가 무엇을 기대해야 하는지 정할 수 없다.
    $themeResult = Send-Cmd 'editor.theme' @()
    $script:osViewports = $false
    if ($null -ne $themeResult -and $null -ne $themeResult.data) {
        $script:osViewports = [bool]$themeResult.data.osViewportsEnabled
    }
    $viewportResult = Send-Cmd 'editor.viewport' @()
    $script:dpiScale = 1.0
    if ($null -ne $viewportResult -and $null -ne $viewportResult.data -and $viewportResult.data.dpiScale -gt 0) {
        $script:dpiScale = [double]$viewportResult.data.dpiScale
    }

    # 씬 모드로 둔다. 배치가 없으므로 이것도 HTTP 로 보낸다.
    $modeBody = @{ command = 'editor.viewport'; args = @('scene') } | ConvertTo-Json -Compress
    try {
        Invoke-RestMethod -Uri "$base/command" -Headers $headers -Method Post `
            -ContentType 'application/json' -Body $modeBody -TimeoutSec 30 | Out-Null
    } catch { $failures.Add("editor.viewport scene 전환이 실패했다: $($_.Exception.Message)") }
    Start-Sleep -Milliseconds 600

    $handle = $proc.MainWindowHandle
    if ($handle -eq [IntPtr]::Zero) {
        $proc.Refresh(); $handle = $proc.MainWindowHandle
    }
    Assert ($handle -ne [IntPtr]::Zero) "에디터 창 핸들을 찾지 못했다 — 원점 축을 잴 수 없다"

    $rect = New-Object CanvasGate.Win+RECT
    [void][CanvasGate.Win]::GetWindowRect($handle, [ref]$rect)
    $w0 = $rect.Right - $rect.Left
    $h0 = $rect.Bottom - $rect.Top

    # 창을 **옮긴다**. 크기는 그대로다 — 계약이 말하는 자극이 정확히 이것이다.
    # SWP_NOSIZE(0x0001) | SWP_NOZORDER(0x0004) | SWP_NOACTIVATE(0x0010)
    foreach ($origin in @(@(40, 40), @(260, 150), @(40, 40))) {
        $ok = [CanvasGate.Win]::SetWindowPos($handle, [IntPtr]::Zero, $origin[0], $origin[1], 0, 0, 0x0015)
        Assert $ok "SetWindowPos 가 실패했다 — 창을 ($($origin[0]),$($origin[1])) 로 옮기지 못했다"
        Start-Sleep -Milliseconds 700

        # ★ 옮겨 달라고 **부탁한** 좌표를 증거로 쓰지 않는다. 창이 실제로 어디에
        #   있는지 다시 묻는다. 요청을 증거로 세면 창이 한 칸도 안 움직여도
        #   "원점 두 곳" 이 되고, 이 축 전체가 조용히 눈먼다.
        $after = New-Object CanvasGate.Win+RECT
        $got = [CanvasGate.Win]::GetWindowRect($handle, [ref]$after)
        Assert $got "GetWindowRect 가 실패했다 — 창이 실제로 어디 있는지 확인할 수 없다"
        Assert (($after.Right - $after.Left) -eq $w0 -and ($after.Bottom - $after.Top) -eq $h0) `
            ("창을 옮기면서 크기가 바뀌었다: ${w0}x${h0} → " +
             "$($after.Right - $after.Left)x$($after.Bottom - $after.Top) — 자극이 원점 축만 건드리지 않았다")

        $r = Read-SceneView
        if ($null -eq $r) { continue }
        if ($null -eq $r.data -or $null -eq $r.data.contentMin) {
            $failures.Add("editor.sceneview 가 자리를 내지 않았다: status=$($r.status) code=$($r.code) message=$($r.message)")
            continue
        }
        $moved += [pscustomobject]@{ x = $after.Left; y = $after.Top; asked = "$($origin[0]),$($origin[1])"; d = $r.data }
    }

    # ─────────────────────────────────────────────────────────────────
    # 축 B — 뷰포트가 앉은 **자리**를 옮긴다
    # ─────────────────────────────────────────────────────────────────
    #
    # 축 A 는 절대 좌표 굳히기를 잡지 못한다. ImGui 의 화면 좌표는 OS 창의
    # 클라이언트 좌표계라서, 창을 220 px 옮겨도 content 원점은 한 칸도 움직이지
    # 않기 때문이다(2026-09-18 실측: 창 (40,40)↔(260,150) 에서 content 원점은
    # 양쪽 다 (0,99)). 자기 요청을 증거로 세면 "원점 두 곳" 으로 보이지만
    # 자극은 재는 값에 닿지 않는다 — 위의 `contentOriginsSeen` 단정이 그 자리다.
    #
    # content 사각형을 실제로 움직이는 것은 배치 preset 이다. 세 배치를 고른
    # 이유가 각각 있다:
    #
    #   sbox_compact   기준. content (0,99)-(2244,1177)
    #   legacy_unity   왼쪽에 패널이 붙어 **원점 x 가 519 오른쪽으로** 간다.
    #                  여기서 왼쪽 거리를 굳힌 좌표가 갈린다.
    #   ui_editing     오른쪽 끝이 115 안쪽으로 온다(contentMax.x 2129).
    #                  여기서 오른쪽 거리를 굳힌 좌표가 갈린다.
    #
    # 둘이 함께 있어야 이빨이 선다. legacy 만 있으면 절대 좌표가 오른쪽 거리를
    # 우연히 지켜(두 배치의 contentMax.x 가 같다) 통과한다.
    foreach ($presetId in @('sbox_compact', 'legacy_unity', 'ui_editing', 'sbox_compact')) {
        $pr = Send-Cmd 'editor.workspace' @('preset', $presetId)
        if ($null -eq $pr) { continue }
        Assert ($pr.status -eq 'succeeded') "배치 preset '$presetId' 적용이 실패했다: $($pr.code) $($pr.message)"
        Start-Sleep -Milliseconds 1200
        # preset 은 창 열림 상태를 갈아치우므로 모드를 다시 세운다.
        Send-Cmd 'editor.viewport' @('scene') | Out-Null
        Start-Sleep -Milliseconds 800

        $r = Read-SceneView
        if ($null -eq $r) { continue }
        if ($null -eq $r.data -or $null -eq $r.data.contentMin) {
            $failures.Add("preset '$presetId' 에서 editor.sceneview 가 자리를 내지 않았다: status=$($r.status) code=$($r.code)")
            continue
        }
        $laid += [pscustomobject]@{ preset = $presetId; d = $r.data }
    }
}
finally {
    if ($null -ne $proc -and -not $proc.HasExited) { $proc.Kill() | Out-Null; $proc.WaitForExit(15000) | Out-Null; $exitCode = 0 }
    elseif ($null -ne $proc) { $exitCode = $proc.ExitCode }
    if ($null -ne $savedWorkspaceDir) { $env:CREATOR_EDITOR_WORKSPACE_DIR = $savedWorkspaceDir }
    else { Remove-Item Env:CREATOR_EDITOR_WORKSPACE_DIR -ErrorAction SilentlyContinue }
    if ($null -ne $savedLegacyIni) { $env:CREATOR_EDITOR_LEGACY_INI = $savedLegacyIni }
    else { Remove-Item Env:CREATOR_EDITOR_LEGACY_INI -ErrorAction SilentlyContinue }
}

Assert ($moved.Count -ge 3) "창을 옮겨 잰 표본이 3 개 미만이다 ($($moved.Count))"
Assert ($laid.Count -ge 3) "배치를 갈아 잰 표본이 3 개 미만이다 ($($laid.Count))"
if ($moved.Count -eq 0 -or $laid.Count -eq 0) {
    foreach ($f in $failures) { Write-Host "FAIL $f" }
    throw "뷰포트 캔버스 규약 검사: 런타임 표본이 0 이다 / 단정 $checks 건"
}

$eps = 0.51   # 논리 픽셀. 창 원점은 정수라 반올림 차이만 허용한다.
# ★ 배열을 돌려주는 헬퍼를 두지 않는다. PowerShell 함수는 출력 스트림을 모으므로
#   `@(a,b)` 는 원소가 흩어져 나가고, `,@(a,b)` 로 막으면 이번엔 바깥 배열이 한 겹
#   더 생겨 `(f x)[0]` 이 내부 배열을 준다. 둘 다 `op_Subtraction` 으로 터졌다.
#   스칼라만 돌려주는 둘로 나누면 그 흔들림이 없다.
function Cx([object]$d, [string]$name) { return [double]$d.$name[0] }
function Cy([object]$d, [string]$name) { return [double]$d.$name[1] }

# 캔버스가 든 자리와 오버레이가 놓은 자리. 앞의 넷은 규약이 정의하는 사각형이고
# 뒤의 셋은 그 사각형을 읽어 배치되는 소비자다 — 소비자가 갈리는 것이 곧 결함이다.
$kPlaced = @('canvasImageMin', 'canvasImageMax', 'clipMin', 'clipMax', 'left', 'right', 'gizmoCenter')
$kScalars = @('leftWidth', 'rightWidth', 'toolbarHeight', 'gizmoRadius', 'sourceAspect')

function Spread([object]$values) {
    $mn = ($values | Measure-Object -Minimum).Minimum
    $mx = ($values | Measure-Object -Maximum).Maximum
    return [math]::Abs([double]$mx - [double]$mn)
}

# ─────────────────────────────────────────────────────────────────────────
# 규약 3·4·5 — 표본 하나하나가 스스로 성립해야 한다
# ─────────────────────────────────────────────────────────────────────────
foreach ($m in ($moved + $laid)) {
    $d = $m.d
    $tag = if ($null -ne $m.preset) { "preset $($m.preset)" } else { "창 $($m.x),$($m.y)" }

    # 3 — 크기는 max - min 이다. 게시가 크기를 따로 내지 않으므로 여기서 유도한다.
    $contentW = (Cx $d 'contentMax') - (Cx $d 'contentMin')
    $contentH = (Cy $d 'contentMax') - (Cy $d 'contentMin')
    Assert ($contentW -gt 0 -and $contentH -gt 0) "content 크기가 0 이다 ($contentW x $contentH) @ $tag"

    # 4 — clip = image ∩ content
    Assert ([math]::Abs((Cx $d 'clipMin') - [math]::Max((Cx $d 'canvasImageMin'), (Cx $d 'contentMin'))) -le $eps) "clipMin.x 가 image∩content 가 아니다 @ $tag"
    Assert ([math]::Abs((Cy $d 'clipMin') - [math]::Max((Cy $d 'canvasImageMin'), (Cy $d 'contentMin'))) -le $eps) "clipMin.y 가 image∩content 가 아니다 @ $tag"
    Assert ([math]::Abs((Cx $d 'clipMax') - [math]::Min((Cx $d 'canvasImageMax'), (Cx $d 'contentMax'))) -le $eps) "clipMax.x 가 image∩content 가 아니다 @ $tag"
    Assert ([math]::Abs((Cy $d 'clipMax') - [math]::Min((Cy $d 'canvasImageMax'), (Cy $d 'contentMax'))) -le $eps) "clipMax.y 가 image∩content 가 아니다 @ $tag"

    # 5 — uv 는 image 기준의 부분 구간이다.
    Assert ((Cx $d 'uvMin') -ge -0.001 -and (Cy $d 'uvMin') -ge -0.001) "uvMin 이 음수다 @ $tag"
    Assert ((Cx $d 'uvMax') -le 1.001 -and (Cy $d 'uvMax') -le 1.001) "uvMax 가 1 을 넘는다 @ $tag"
    Assert ((Cx $d 'uvMax') -gt (Cx $d 'uvMin') -and (Cy $d 'uvMax') -gt (Cy $d 'uvMin')) "uv 구간이 비었다 @ $tag"

    # 오버레이가 캔버스 밖에 놓이지 않는다. content 를 벗어난 툴바는 어느 배치에서도
    # 틀렸다 — 아래의 "규칙이 보존되는가" 와 달리 이것은 표본 하나로 판정된다.
    foreach ($name in @('left', 'right', 'gizmoCenter')) {
        $px = Cx $d $name; $py = Cy $d $name
        Assert ($px -ge (Cx $d 'contentMin') - $eps -and $px -le (Cx $d 'contentMax') + $eps -and
                $py -ge (Cy $d 'contentMin') - $eps -and $py -le (Cy $d 'contentMax') + $eps) `
            "$name ($px,$py) 이 content 밖에 있다 @ $tag"
    }
}

# ─────────────────────────────────────────────────────────────────────────
# 축 A — 창을 옮긴다
# ─────────────────────────────────────────────────────────────────────────
#
# ImGui 화면 좌표의 **원점이 어디인가**는 멀티뷰포트 설정에 달렸다.
#
#   꺼짐 — 클라이언트 좌표계. 창을 옮겨도 좌표가 한 칸도 안 움직인다.
#          즉 이 자극은 재는 값에 **닿지 않는다**(2026-09-18 실측: 창
#          (40,40)↔(260,150) 에서 content 원점은 양쪽 다 (0,99)).
#   켜짐 — 가상 데스크톱 좌표계. 좌표가 창을 따라 움직이고, 그때 비로소 이
#          자극이 절대 좌표 굳히기를 잡는 자가 된다.
#
# 어느 쪽이든 **계약은 하나**다 — W2-V0 의 완료 기준이 적은 그대로 "창 원점이
# 달라도 상대 배치 동일". 이 자리를 한 번 "절대 좌표 그대로" 로 적었다가,
# 다른 트랙이 멀티뷰포트를 켜자 단정 아홉이 한꺼번에 붉었다. 꺼진 상태에서
# 관측한 것을 계약으로 굳힌 것이었고, 제품은 내내 옳았다.
#
# 그래서 좌표계가 어느 쪽인지도 **단정한다**. 어느 쪽이든 통과시키면 전환이
# 조용히 일어나고, 그때 이 축이 무엇을 재고 있었는지 아무도 모른다.
$originsSeen = @{}
foreach ($m in $moved) { $originsSeen["$($m.x),$($m.y)"] = $true }
Assert ($originsSeen.Keys.Count -ge 2) `
    ("창을 실제로 옮기지 못했다 — GetWindowRect 가 낸 원점이 하나뿐이다: " +
     ($originsSeen.Keys -join ' / '))

$base0 = $moved[0].d
$contentOriginsSeen = @{}
foreach ($m in $moved) { $contentOriginsSeen["$(Cx $m.d 'contentMin'),$(Cy $m.d 'contentMin')"] = $true }

if ($osViewports) {
    # 켜짐 — 좌표가 창을 따라야 한다. 그리고 **얼마나** 따르는지까지 본다:
    # 창 좌표는 물리 픽셀이고 ImGui 는 그것을 배율만큼 늘린 자리에 둔다
    # (실측: 창 +220 → content +330, dpiScale 1.5).
    Assert ($contentOriginsSeen.Keys.Count -ge 2) `
        ("멀티뷰포트가 켜졌는데 창을 옮겨도 content 원점이 하나뿐이다 " +
         "($($contentOriginsSeen.Keys -join ' / ')) — 좌표가 창 밖 좌표계를 따르지 않는다")
    foreach ($m in $moved) {
        $wantX = ($m.x - $moved[0].x) * $dpiScale
        $wantY = ($m.y - $moved[0].y) * $dpiScale
        $gotX = (Cx $m.d 'contentMin') - (Cx $base0 'contentMin')
        $gotY = (Cy $m.d 'contentMin') - (Cy $base0 'contentMin')
        Assert ([math]::Abs($wantX - $gotX) -le 1.0 -and [math]::Abs($wantY - $gotY) -le 1.0) `
            ("content 원점이 창 이동량을 따르지 않는다: 창 ($($m.x - $moved[0].x),$($m.y - $moved[0].y)) " +
             "× 배율 $dpiScale = ($wantX,$wantY) 인데 좌표는 ($gotX,$gotY) 움직였다")
    }
}
else {
    # 꺼짐 — 클라이언트 좌표계라 좌표가 그대로여야 한다. 움직였다면 어딘가가
    # 창 프레임을 읽고 있다는 뜻이고, 그것이 규약 ①이 금지한 출처다.
    Assert ($contentOriginsSeen.Keys.Count -eq 1) `
        ("멀티뷰포트가 꺼졌는데 content 원점이 창을 따라 움직였다 " +
         "($($contentOriginsSeen.Keys -join ' / ')) — 뷰포트 좌표가 창 프레임을 읽고 있다")
    foreach ($m in $moved) {
        foreach ($name in ($kPlaced + @('contentMin', 'contentMax'))) {
            Assert ([math]::Abs((Cx $base0 $name) - (Cx $m.d $name)) -le $eps -and
                    [math]::Abs((Cy $base0 $name) - (Cy $m.d $name)) -le $eps) `
                ("$name 이 OS 창 원점을 따라 움직였다: ($(Cx $base0 $name),$(Cy $base0 $name)) → " +
                 "($(Cx $m.d $name),$(Cy $m.d $name)) @ 창 $($m.x),$($m.y)")
        }
    }
}

# ★ 계약 본문. 좌표계가 어느 쪽이든 여기는 같다 — content 기준 상대 배치와
#   content 크기, 그리고 배치와 무관한 스칼라들이 창 원점을 타지 않는다.
foreach ($m in $moved) {
    $w1 = (Cx $m.d 'contentMax') - (Cx $m.d 'contentMin')
    $h1 = (Cy $m.d 'contentMax') - (Cy $m.d 'contentMin')
    $w0c = (Cx $base0 'contentMax') - (Cx $base0 'contentMin')
    $h0c = (Cy $base0 'contentMax') - (Cy $base0 'contentMin')
    Assert ([math]::Abs($w1 - $w0c) -le $eps -and [math]::Abs($h1 - $h0c) -le $eps) `
        "창을 옮겼는데 content 크기가 달라졌다: ${w0c}x${h0c} → ${w1}x${h1} @ 창 $($m.x),$($m.y)"

    foreach ($name in $kPlaced) {
        $ax = (Cx $base0 $name) - (Cx $base0 'contentMin')
        $ay = (Cy $base0 $name) - (Cy $base0 'contentMin')
        $bx = (Cx $m.d $name)   - (Cx $m.d 'contentMin')
        $by = (Cy $m.d $name)   - (Cy $m.d 'contentMin')
        Assert ([math]::Abs($ax - $bx) -le $eps -and [math]::Abs($ay - $by) -le $eps) `
            ("$name 의 content 기준 상대 위치가 창 원점을 따라 달라졌다: ($ax,$ay) → ($bx,$by) " +
             "@ 창 $($m.x),$($m.y) — 절대 좌표가 굳어 있다")
    }
    foreach ($name in $kScalars) {
        Assert ([math]::Abs([double]$base0.$name - [double]$m.d.$name) -le $eps) `
            "$name 이 창 원점을 따라 달라졌다: $($base0.$name) → $($m.d.$name) @ 창 $($m.x),$($m.y)"
    }
}

# ─────────────────────────────────────────────────────────────────────────
# 축 B — content 사각형이 달라져도 배치 규칙은 같다
# ─────────────────────────────────────────────────────────────────────────
#
# 요소마다 정렬 기준이 다르다. 왼쪽 툴바는 왼쪽에 붙고, 뷰 큐브는 오른쪽에
# 붙는다(2026-09-18 실측: contentMax.x - gizmoCenter.x 가 세 배치 모두 103.5).
# 검사는 어느 쪽인지 모르므로 셋을 다 재고 **하나라도 보존되면** 통과시킨다:
# 왼쪽 거리 · 오른쪽 거리 · 정규화 위치.
#
# 절대 좌표를 굳히면 셋이 전부 깨진다 — 그것이 이 단정의 이빨이다.
# 원점이 가는 배치(legacy_unity)가 왼쪽 거리를 깨고, 오른쪽 끝이 오는
# 배치(ui_editing)가 오른쪽 거리를 깬다. 정규화는 둘 중 하나로도 깨진다.
$layoutOriginsX = @{}; $layoutMaxX = @{}
foreach ($m in $laid) {
    $layoutOriginsX["$(Cx $m.d 'contentMin')"] = $true
    $layoutMaxX["$(Cx $m.d 'contentMax')"] = $true
}
Assert ($layoutOriginsX.Keys.Count -ge 2) `
    ("배치를 갈았는데 content 원점 x 가 하나뿐이다 ($($layoutOriginsX.Keys -join ' / ')) — " +
     "왼쪽 거리를 굳힌 좌표를 잡을 자극이 없다")
Assert ($layoutMaxX.Keys.Count -ge 2) `
    ("배치를 갈았는데 content 오른쪽 끝이 하나뿐이다 ($($layoutMaxX.Keys -join ' / ')) — " +
     "오른쪽 거리를 굳힌 좌표를 잡을 자극이 없다")

$ruleReport = @()
foreach ($name in $kPlaced) {
    foreach ($axis in @('x', 'y')) {
        $lo = @(); $hi = @(); $nm = @()
        foreach ($m in $laid) {
            $d = $m.d
            if ($axis -eq 'x') { $p = Cx $d $name; $a = Cx $d 'contentMin'; $b = Cx $d 'contentMax' }
            else               { $p = Cy $d $name; $a = Cy $d 'contentMin'; $b = Cy $d 'contentMax' }
            $lo += ($p - $a); $hi += ($b - $p); $nm += (($p - $a) / ($b - $a))
        }
        $rules = @()
        if ((Spread $lo) -le $eps)   { $rules += 'left' }
        if ((Spread $hi) -le $eps)   { $rules += 'right' }
        if ((Spread $nm) -le 0.002)  { $rules += 'norm' }
        Assert ($rules.Count -ge 1) `
            ("$name.$axis 이 배치가 바뀌자 어떤 규칙도 지키지 않았다 — 왼쪽 거리 " +
             "[$(($lo | ForEach-Object { [math]::Round($_,1) }) -join ' ')] · 오른쪽 거리 " +
             "[$(($hi | ForEach-Object { [math]::Round($_,1) }) -join ' ')] · 정규화 " +
             "[$(($nm | ForEach-Object { [math]::Round($_,4) }) -join ' ')] " +
             "(배치 $(($laid | ForEach-Object { $_.preset }) -join ' ')) — 절대 좌표가 굳어 있다")
        $ruleReport += [pscustomobject]@{ name = "$name.$axis"; rules = ($rules -join ',') }
    }
}

# 배치가 바뀌면 폭은 달라져도 좋지만 툴바 높이와 기즈모 반지름은 배치와 무관하다.
foreach ($m in $laid) {
    foreach ($name in @('toolbarHeight', 'gizmoRadius')) {
        Assert ([math]::Abs([double]$laid[0].d.$name - [double]$m.d.$name) -le $eps) `
            "$name 이 배치를 따라 달라졌다: $($laid[0].d.$name) → $($m.d.$name) @ preset $($m.preset)"
    }
}

# 7
Assert ($exitCode -eq 0) "엔진이 비정상 종료했다 (exit=$exitCode)"

Write-Host ""
$allowedCallCount = ($kInputSites | ForEach-Object { $_.Calls.Values } | Measure-Object -Sum).Sum
Write-Host ("  소스 축: 뷰포트 소스 {0} 개 · 허용한 호출 {1} 회(자리 {2}) · 금지한 높이 호출 {3} 종 · 정본 외 좌표식 {4}" -f `
    $kViewportSources.Count, $allowedCallCount, $kInputSites.Count, $kForbiddenHeightCalls.Count, $sourceHits.Count)
$frameName = if ($osViewports) { "가상 데스크톱(멀티뷰포트 켜짐 · 배율 $dpiScale)" } else { "클라이언트(멀티뷰포트 꺼짐)" }
Write-Host "  축 A — 창 이동 $($originsSeen.Keys.Count) 곳 · 좌표계 $frameName · content 원점 $($contentOriginsSeen.Keys.Count) 가지"
foreach ($m in $moved) {
    Write-Host ("    창 ({0,4},{1,4})  content ({2,6:N0},{3,6:N0})-({4,6:N0},{5,6:N0})  기즈모 ({6,7:N1},{7,7:N1})" -f `
        $m.x, $m.y, (Cx $m.d 'contentMin'), (Cy $m.d 'contentMin'), (Cx $m.d 'contentMax'), (Cy $m.d 'contentMax'),
        (Cx $m.d 'gizmoCenter'), (Cy $m.d 'gizmoCenter'))
}
Write-Host "  축 B — 배치 $($laid.Count) 회 · content 원점 x $($layoutOriginsX.Keys.Count) 가지 · 오른쪽 끝 $($layoutMaxX.Keys.Count) 가지"
foreach ($m in $laid) {
    Write-Host ("    {0,-14} content ({1,6:N0},{2,6:N0})-({3,6:N0},{4,6:N0})  기즈모 ({5,7:N1},{6,7:N1})" -f `
        $m.preset, (Cx $m.d 'contentMin'), (Cy $m.d 'contentMin'), (Cx $m.d 'contentMax'), (Cy $m.d 'contentMax'),
        (Cx $m.d 'gizmoCenter'), (Cy $m.d 'gizmoCenter'))
}
Write-Host "    지켜진 규칙: $(($ruleReport | ForEach-Object { "$($_.name)=$($_.rules)" }) -join '  ')"
Write-Host ""

if ($failures.Count -gt 0) {
    foreach ($f in $failures) { Write-Host "FAIL $f" }
    throw "뷰포트 캔버스 규약 검사 실패 $($failures.Count) 건 / 단정 $checks 건"
}
Write-Host "뷰포트 캔버스 규약 검사: 단정 $checks 건 · PASS"
