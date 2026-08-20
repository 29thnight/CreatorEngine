# 해상도 독립 회귀 검사(PHASE 7-7).
#
# resolution_sweep.txt를 돌린 뒤 로그를 읽어 다음을 단정한다.
#
#   1) 최상위 캔버스 rect가 화면 크기를 따라온다 (7-1)
#   2) 원점 규약 -size/2가 모든 해상도에서 유지된다 (7-2)
#   3) 캔버스 배율이 uGUI와 같은 로그 보간 값과 일치한다 (7-3)
#   4) 자식 크기가 배율만큼 따라간다 (7-3)
#   5) 버튼의 클릭 판정 상자가 rect와 같다 — 보이는 곳과 눌리는 곳의 일치 (7-6)
#   6) 원래 해상도로 돌아오면 값이 무손실로 복원된다
#
# 기대값은 요청 해상도가 아니라 window.info가 알려 준 실제 클라이언트 크기에서
# 계산한다. 요청 크기가 모니터보다 크면 창이 잘리기 때문이다(실제로 3840 요청이
# 2564로 잘리는 것을 이 검증에서 발견했다).
param(
    [string]$Exe = "C:\Users\lance\source\CreatorEngine\x64\Debug\Academy_4Q.exe",
    [string]$Script = (Join-Path $PSScriptRoot "resolution_sweep.txt"),
    [string]$Work = $env:TEMP,
    # 캔버스 스케일러 기본값. Canvas.h의 ReferenceResolution/MatchWidthOrHeight와 같아야 한다.
    [double]$RefWidth = 1920,
    [double]$RefHeight = 1080,
    [double]$Match = 0.5,
    # 시나리오가 저작하는 캔버스 이름(resolution_sweep.txt와 같아야 한다).
    # 예전에는 저작 프리팹 이름 "Canvas"가 파서에 박혀 있었다 — CLI 이전과 함께
    # 파라미터로 뺐다(2026-08-20).
    [string]$CanvasName = "SweepCanvas",
    # 시나리오가 순회하려는 해상도 수(시작 1 + window.resize 6).
    [int]$ExpectedResolutions = 7,
    # ★ 실제로 도달하는 수의 **하한**(래칫). 아래 "알려진 결함" 주석 참고 —
    # swapchain resize 크래시로 순회가 2개에서 끊긴다. 그 현상을 고정해 두고
    # **더 나빠지는 것만** 잡는다. 결함이 고쳐지면 이 값을 7로 올린다.
    [int]$MinResolutions = 2
)

$exeDir = [System.IO.Path]::GetDirectoryName($Exe)
$started = Get-Date

$proc = Start-Process -FilePath $Exe -ArgumentList "--script", $Script `
    -WorkingDirectory $exeDir `
    -RedirectStandardOutput (Join-Path $Work "resolution_sweep.out") `
    -RedirectStandardError (Join-Path $Work "resolution_sweep.err") -PassThru
$proc.WaitForExit(300000) | Out-Null
if (-not $proc.HasExited) { $proc.Kill(); "TIMEOUT"; exit 1 }

$log = Get-ChildItem (Join-Path $exeDir "Log\Editor_*.html") |
       Where-Object { $_.LastWriteTime -ge $started } |
       Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (-not $log) { "로그를 찾지 못했다"; exit 1 }

$lines = (Get-Content $log.FullName -Raw) -split "`n" |
         ForEach-Object { ($_ -replace '<[^>]+>', '') -replace '^\d\d:\d\d:\d\d\.\d+\w+\d+', '' }

$failures = 0
$checks = 0
$hitboxChecks = 0
function Assert([string]$name, [bool]$ok, [string]$detail) {
    $script:checks++
    if (-not $ok) { $script:failures++; "  실패: $name — $detail" }
}

# 기준선(첫 해상도에서 본 캔버스·자식 값)을 잡아 두고 마지막에 복원됐는지 본다.
$screenW = 0.0; $screenH = 0.0; $expectedScale = 1.0
$baseline = $null
$observed = @()

foreach ($line in $lines) {
    if ($line -match '\[CLI\] 클라이언트 영역: (\d+)x(\d+)') {
        $screenW = [double]$matches[1]; $screenH = [double]$matches[2]
        # uGUI와 같은 로그 공간 보간
        $logW = [Math]::Log($screenW / $RefWidth, 2)
        $logH = [Math]::Log($screenH / $RefHeight, 2)
        $expectedScale = [Math]::Pow(2, $logW + ($logH - $logW) * $Match)
        $observed += "${screenW}x${screenH}"
        continue
    }

    if ($screenW -le 0) { continue }

    if ($line -match ('\[ui\.rect\]\s+' + [regex]::Escape($CanvasName) + ' world\((-?\d+), (-?\d+), (-?\d+), (-?\d+)\).*scale\(([\d.]+)\)')) {
        $x = [double]$matches[1]; $y = [double]$matches[2]
        $w = [double]$matches[3]; $h = [double]$matches[4]
        $scale = [double]$matches[5]
        $tag = "${screenW}x${screenH}"

        Assert "[$tag] 캔버스가 화면 크기를 따라감" `
            (([Math]::Abs($w - $screenW) -le 1) -and ([Math]::Abs($h - $screenH) -le 1)) `
            "캔버스 ${w}x${h}"

        Assert "[$tag] 원점 = -size/2" `
            (([Math]::Abs($x + $screenW / 2) -le 1) -and ([Math]::Abs($y + $screenH / 2) -le 1)) `
            "원점 ($x, $y)"

        # ui.rect는 배율을 소수 3자리로 자른다.
        Assert "[$tag] 배율이 로그 보간 값과 일치" `
            ([Math]::Abs($scale - $expectedScale) -le 0.002) `
            ("관측 {0} vs 기대 {1:N3}" -f $scale, $expectedScale)
        continue
    }

    # 캔버스 바로 다음 줄에 오는 첫 자식으로 자식 크기 추종을 본다.
    if ($line -match '\[ui\.rect\]\s+(\S+) world\((-?\d+), (-?\d+), (-?\d+), (-?\d+)\) sizeDelta\((-?\d+), (-?\d+)\)') {
        $name = $matches[1]
        $w = [double]$matches[4]; $h = [double]$matches[5]
        $sizeX = [double]$matches[6]; $sizeY = [double]$matches[7]
        if ($sizeX -le 0 -or $sizeY -le 0) { continue }

        $tag = "${screenW}x${screenH}"
        Assert "[$tag] $name 크기가 배율을 따름" `
            (([Math]::Abs($w - $sizeX * $expectedScale) -le 2) -and ([Math]::Abs($h - $sizeY * $expectedScale) -le 2)) `
            ("{0}x{1} vs 기대 {2:N1}x{3:N1}" -f $w, $h, ($sizeX * $expectedScale), ($sizeY * $expectedScale))

        if ($null -eq $baseline) { $baseline = @{ Name = $name; W = $w; H = $h } }
        elseif ($baseline.Name -eq $name -and $observed[-1] -eq "$RefWidth" + "x" + "$RefHeight") {
            Assert "복귀 후 값 복원" `
                (([Math]::Abs($w - $baseline.W) -le 1) -and ([Math]::Abs($h - $baseline.H) -le 1)) `
                ("{0}x{1} vs 기준 {2}x{3}" -f $w, $h, $baseline.W, $baseline.H)
        }
        continue
    }

    if ($line -match '\[ui\.hitbox\]\s+(\S+) rect\((-?\d+), (-?\d+), (-?\d+), (-?\d+)\) hitbox\((-?\d+), (-?\d+), (-?\d+), (-?\d+)\)') {
        $name = $matches[1]
        $tag = "${screenW}x${screenH}"
        $same = $true
        for ($i = 2; $i -le 5; $i++) {
            if ([Math]::Abs([double]$matches[$i] - [double]$matches[$i + 4]) -gt 1) { $same = $false }
        }
        $hitboxChecks++
        Assert "[$tag] $name 히트박스 = rect" $same `
            ("rect({0},{1},{2},{3}) hitbox({4},{5},{6},{7})" -f `
                $matches[2], $matches[3], $matches[4], $matches[5], `
                $matches[6], $matches[7], $matches[8], $matches[9])
    }
}

"검사한 해상도: " + ($observed -join ' -> ')

# ── 거짓 통과 봉합 + 알려진 결함 래칫 (2026-08-20) ──
#
# 이 게이트는 **엔진이 크래시해도, 검사 범위가 무너져도 통과했다.** 종료 코드를
# 보지 않았고 단정 수 하한이 0뿐이었다.
#
# ★ 실측으로 드러난 것: 이 스위프는 **오래전부터 해상도 7개 중 2개만** 재고
#   있었다. 두 번째 window.resize에서 엔진이 죽기 때문이다:
#
#     ID3D12Resource ... is referenced by GPU operations in-flight on Command Queue.
#     It is not safe to final-release objects that may have GPU operations pending.
#     -> DX12DeviceResources::ResizeSwapChain <- ImGuiDx12Shell::Resize
#
#   ResizeSwapChain은 DrainForLifecycle로 GPU 완주 대기를 **하는데도** 난다.
#   RHI 계층의 리소스 수명 문제이고 SceneGraph 트랙 밖이라 여기서 고치지 않는다.
#
#   이 사실이 오래 숨어 있던 이유: 저작 프리팹은 UI 자식을 많이 담아 **2개
#   해상도만으로도 단정이 84건** 나왔다. 숫자가 커서 정상처럼 보였다. CLI 저작본
#   (자식 둘)으로 옮기며 12건으로 떨어지자 비로소 눈에 띄었다.
#
# 그래서 **래칫**으로 만든다: 크래시는 보고하되 그 자체로 실패시키지 않고,
# 도달한 해상도 수의 하한으로 **악화만** 잡는다. 결함이 고쳐지면 MinResolutions를
# ExpectedResolutions까지 올리는 것이 그 증명이 된다.
if ($proc.ExitCode -ne 0) {
    "⚠ 엔진 비정상 종료 (0x{0:X8}) — 알려진 결함: swapchain resize 중 D3D12 CORRUPTION" -f $proc.ExitCode
    "  그래서 해상도 순회가 도중에 끊긴다. 아래 판정은 끊기기 전까지의 것이다."
    "  출력: $(Join-Path $Work 'resolution_sweep.out')"
}

if ($checks -eq 0) { "단정이 하나도 실행되지 않았다 — 로그 형식이 바뀌었는지 확인할 것"; exit 1 }

if ($observed.Count -lt $MinResolutions) {
    "실패: 해상도를 $($observed.Count)개만 쟀다(하한 $MinResolutions · 시나리오 목표 $ExpectedResolutions) — 이전보다 나빠졌다"
    exit 1
}
if ($observed.Count -lt $ExpectedResolutions) {
    "  (참고: 목표 $ExpectedResolutions 개 중 $($observed.Count) 개까지만 도달 — 위 알려진 결함)"
}

# 조용히 건너뛴 검사는 통과가 아니다. 실제로 버튼 없는 프리팹을 쓰는 바람에
# 히트박스 단정이 하나도 돌지 않고 '전체 통과'가 나온 적이 있다.
if ($hitboxChecks -eq 0) {
    "히트박스 단정이 하나도 실행되지 않았다 — 스위프 스크립트가 버튼 있는 프리팹을 띄우는지 확인할 것"
    exit 1
}

if ($failures -gt 0) { "실패 $failures / 단정 $checks"; exit 1 }
"전체 통과 (단정 $checks 건 · 그중 히트박스 $hitboxChecks 건)"
exit 0
