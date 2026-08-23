# UI 레이아웃 골든 대조 (PHASE 7 승계 — verify-authored-rects의 후계)
#
# 시나리오와 "왜 후계인가"는 ui_layout_golden.txt 상단에 있다.
#
# ── 판정 항목 ──
#
#   1  측정이 실제로 나왔다        — ui.rect 줄이 최소치 이상(빈 결과를 통과시키지 않는다)
#   2  엔진이 정상 종료했다        — 크래시하고도 통과하는 게이트가 이 저장소에 실재했다
#   3  클라이언트 크기가 기대대로  — 창이 잘리면(요청 > 모니터) 모든 rect가 달라진다
#   4  ★ 골든과 diff 0            — 레이아웃 형상이 통째로 회귀하지 않았는가
#
# ── 골든을 뜨는 법 ──
#
#   pwsh Tools\regression\verify-ui-layout-golden.ps1 -Baseline
#
# 골든 파일이 없으면 이 검사는 **건너뛴다**(run-all이 그렇게 부른다) — reflect_golden과
# 같은 관례다. 레이아웃 규약을 의도적으로 바꿨을 때만 다시 뜬다.
#
# ★ 이 게이트는 "값이 옳은가"가 아니라 "값이 바뀌지 않았는가"를 잰다. 규약 자체의
#   정합성(원점 -size/2 · uGUI 로그 보간 배율 · 히트박스 일치)은 verify-resolution-sweep이
#   수식으로 검증한다. 둘은 상보적이다 — 한쪽만으로는 "규약대로 계산하지만 형상이
#   바뀐 경우"나 "형상은 그대로인데 규약이 틀린 경우"를 못 잡는다.
#
# ⚠ **골든은 "지금 값"을 정답으로 굳힌다.** 그래서 뜨기 전에 값을 사람이 검산해야
#   한다 — 틀린 값을 고정하면 결함이 영구화된다. 실제로 이 게이트를 세울 때 첫 골든이
#   그랬다: 앵커 프리셋 9종을 만들어 놓고 위치를 안 줘서 **전부 캔버스 중앙에 겹친**
#   상태였고, 자동 통과만 봤다면 "앵커별 배치를 검증한다"고 적힌 채 실제로는 같은
#   위치만 재는 게이트가 남았을 것이다(ui.pos 신설로 해소).
param(
    [string]$Exe = "C:\Users\lance\source\CreatorEngine\x64\Debug\CreatorEditor.exe",
    [string]$Work = $env:TEMP,
    [int]$TimeoutSeconds = 300,
    # 시나리오가 만드는 rect 수(캔버스 1 + 앵커 8 + screenPosition 1 + 중첩 3 + 버튼 1). 이보다 적으면
    # 저작이 도중에 실패한 것이다 — 빈 결과나 부분 결과를 통과시키지 않는다.
    [int]$MinRects = 14,
    [int]$ExpectedWidth = 1920,
    [int]$ExpectedHeight = 1080,
    [switch]$Baseline
)

$exeDir = [System.IO.Path]::GetDirectoryName($Exe)
if (-not (Test-Path $Exe)) { "실행 파일이 없다: $Exe"; exit 1 }

$script = Join-Path $PSScriptRoot "ui_layout_golden.txt"
if (-not (Test-Path $script)) { "시나리오가 없다: $script"; exit 1 }

$goldenPath = Join-Path $PSScriptRoot "ui_layout_golden.expected"

$outFile = Join-Path $Work "ui_layout_golden.out"
$errFile = Join-Path $Work "ui_layout_golden.err"

$proc = Start-Process -FilePath $Exe -ArgumentList @("--script", $script) `
    -WorkingDirectory $exeDir `
    -RedirectStandardOutput $outFile -RedirectStandardError $errFile -PassThru
$proc.WaitForExit($TimeoutSeconds * 1000) | Out-Null
if (-not $proc.HasExited) { $proc.Kill(); "타임아웃 ($TimeoutSeconds 초)"; exit 1 }

if (-not (Test-Path $outFile)) { "출력이 없다: $outFile"; exit 1 }
$lines = Get-Content -LiteralPath $outFile

# ── 관측 수집 ──
#
# ui.rect는 캔버스부터 자식까지 **들여쓰기로 계층을 표현해** 찍는다. 그 들여쓰기를
# 골든에 그대로 담으면 값뿐 아니라 **구조**도 함께 고정된다(부모가 바뀌면 diff가 난다).
# "[CLI] " 접두만 떼고 나머지는 원문 그대로 쓴다.
$rects = @()
$clientLine = $null
$hitboxes = @()
foreach ($line in $lines) {
    if ($line -match '^\[CLI\] 클라이언트 영역: (\d+)x(\d+)') {
        $clientLine = "client $($matches[1])x$($matches[2])"
        continue
    }
    if ($line -match '^\[CLI\] (\s*)(\S+) world\(') {
        $rects += ($line -replace '^\[CLI\] ', '')
        continue
    }
    if ($line -match '^\[CLI\] (\S+) rect\(.*\) hitbox\(') {
        $hitboxes += ($line -replace '^\[CLI\] ', '')
        continue
    }
}

if ($null -eq $clientLine) {
    "실패: window.info 출력을 못 찾았다 — 시나리오가 시작하지 못했다"
    "  출력: $outFile"
    exit 1
}

"관측: $clientLine · rect $($rects.Count) 줄 · 히트박스 $($hitboxes.Count) 줄"

# ── 판정 1·2·3 (골든 대조 전에 전제부터) ──
$hard = @()

if ($proc.ExitCode -ne 0) {
    $hard += ("엔진이 비정상 종료했다 (0x{0:X8}) — 아래 관측은 부분 결과다" -f $proc.ExitCode)
}
if ($rects.Count -lt $MinRects) {
    $hard += "rect가 $($rects.Count) 줄뿐이다(기대 $MinRects 이상) — CLI 저작이 도중에 실패했다"
}
if ($clientLine -ne "client ${ExpectedWidth}x${ExpectedHeight}") {
    $hard += "클라이언트 크기가 '$clientLine'다(기대 client ${ExpectedWidth}x${ExpectedHeight}) — 창이 잘렸다면 모든 rect가 달라지므로 골든 대조가 무의미하다"
}

if ($hard.Count -gt 0) {
    ""
    "실패 $($hard.Count)건:"
    $hard | ForEach-Object { "  $_" }
    exit 1
}

$observed = @($clientLine) + $rects + $hitboxes

# ── 골든 뜨기 ──
if ($Baseline) {
    $observed | Set-Content -LiteralPath $goldenPath -Encoding UTF8
    "골든을 새로 떴다: $goldenPath ($($observed.Count) 줄)"
    exit 0
}

if (-not (Test-Path $goldenPath)) {
    "골든이 없다 — 건너뜀 (뜨려면: verify-ui-layout-golden.ps1 -Baseline)"
    exit 0
}

# ── 판정 4 — diff 0 ──
$expected = @(Get-Content -LiteralPath $goldenPath)
$diff = 0
$limit = [Math]::Max($expected.Count, $observed.Count)
for ($i = 0; $i -lt $limit; $i++) {
    $e = if ($i -lt $expected.Count) { $expected[$i] } else { '(없음)' }
    $o = if ($i -lt $observed.Count) { $observed[$i] } else { '(없음)' }
    if ($e -ne $o) {
        $diff++
        if ($diff -le 10) {
            "  줄 $($i + 1): 골든 '$e'"
            "           관측 '$o'"
        }
    }
}

if ($diff -gt 0) {
    ""
    "실패: 레이아웃이 골든과 다르다 ($diff 줄 / 골든 $($expected.Count) 줄)"
    if ($diff -gt 10) { "  (앞 10건만 표시)" }
    "  의도한 변경이라면 -Baseline으로 다시 뜬다"
    exit 1
}

"전체 통과 — UI 레이아웃 골든 diff 0 (rect $($rects.Count) · 히트박스 $($hitboxes.Count))"
exit 0
