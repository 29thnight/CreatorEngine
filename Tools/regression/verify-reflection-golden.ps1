# 리플렉션 골든 대조 (PHASE 18 CT0).
#
# ── 이 검사가 메우는 구멍 ──
#
# PHASE 18은 리플렉션의 메타데이터 생성(헤더툴)과 접근 계약(any/function)을
# 통째로 갈아끼운다. 그동안 "직렬화 출력이 같다"를 증명할 자가 없으면, 씬이
# 열리고 화면이 멀쩡해 보이는 것만으로 전환을 통과시키게 된다 — 필드 하나가
# 조용히 빠져도 다음에 그 값을 쓸 때에야 드러난다. reflect.golden은 등록 전
# 타입을 기본 생성해 직렬화한 출력이므로, 여기의 diff 0이 곧 "타입 커버리지
# 전체에서 출력 동등"이다.
#
# ── 판정 항목 ──
#
#   1  reflect.golden 실행 기록이 있다        — 시나리오가 도중에 멈추지 않았다
#   2  직렬화 타입 수가 최소치 이상           — 검사가 조용히 건너뛰지 않았다(README 원칙)
#   3  실패 0                                 — 직렬화 도중 예외가 없다
#   4  골든 파일과 diff 0                     — ★ 출력 동등 (전환 구간의 존재 이유)
#   5  perf.reflect 수치 기록                 — 기준선 표에 옮겨 적는 원천(판정 아님)
#
# 기준선 뜨기(최초 1회, 이후 의도된 포맷 변경 시에만 갱신):
#   pwsh Tools/regression/verify-reflection-golden.ps1 -Baseline
#
# 실행: pwsh -NoProfile -File Tools\regression\verify-reflection-golden.ps1
param(
    [string]$Exe = "C:\Users\lance\source\CreatorEngine\x64\Debug\CreatorEditor.exe",
    [string]$Work = $env:TEMP,
    [switch]$Baseline,
    [int]$TimeoutSeconds = 300,
    [int]$MinSerialized = 50   # 등록 클래스 76 중 팩토리 보유분의 하한 — 0이면 검사가 눈을 감은 것
)

$exeDir = [System.IO.Path]::GetDirectoryName($Exe)
if (-not (Test-Path $Exe)) { "실행 파일이 없다: $Exe"; exit 1 }

$template = Join-Path $PSScriptRoot "reflect_golden.txt"

# 이 시나리오가 굽는 픽스처(2026-08-20 CLI 이전 — 그전에는 저작 자산 BTProbe를
# 소환했다). 이전 실행의 잔재를 미리 지운다(.gitignore 대상).
$repoRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$prefabDir = Join-Path $repoRoot "Dynamic_CPP\Assets\Prefabs"
foreach ($p in @((Join-Path $prefabDir "ReflectGoldenProbe.prefab"),
                 (Join-Path $prefabDir "ReflectGoldenProbe.prefab.meta"))) {
    if (Test-Path $p) { Remove-Item $p -Force }
}
if (-not (Test-Path $template)) { "시나리오가 없다: $template"; exit 1 }

$goldenPath = Join-Path $PSScriptRoot "reflect_golden.yaml"

# 출력 경로를 실행 직전에 치환한다 — 절대 경로를 시나리오에 박으면 다른 작업
# 폴더에서 조용히 실패한다 (prefab_roundtrip과 같은 이유).
$outYaml = (Join-Path $Work "reflect_golden_run.yaml") -replace '\\', '/'
if (Test-Path $outYaml) { Remove-Item $outYaml -Force }

$scenario = Join-Path $Work "reflect_golden_resolved.txt"
(Get-Content $template -Raw) -replace '\{\{OUT_PATH\}\}', $outYaml |
    Set-Content $scenario -Encoding UTF8

$outFile = Join-Path $Work "reflect_golden.out"
$errFile = Join-Path $Work "reflect_golden.err"

$proc = Start-Process -FilePath $Exe -ArgumentList @("--script", $scenario) `
    -WorkingDirectory $exeDir `
    -RedirectStandardOutput $outFile -RedirectStandardError $errFile -PassThru
$proc.WaitForExit($TimeoutSeconds * 1000) | Out-Null
if (-not $proc.HasExited) { $proc.Kill(); "TIMEOUT"; exit 1 }

if (-not (Test-Path $outFile)) { "출력이 없다: $outFile"; exit 1 }
$text = Get-Content -LiteralPath $outFile -Raw

# 1·2·3 — 실행 기록과 커버리지.
$m = [regex]::Match($text,
    '\[reflect\.golden\] 타입 (\d+) · 직렬화 (\d+) · 팩토리없음 (\d+) · 실패 (\d+)')
if (-not $m.Success) {
    "reflect.golden 기록이 없다 — 시나리오가 도중에 멈췄다 (출력: $outFile)"
    exit 1
}
$types      = [int]$m.Groups[1].Value
$serialized = [int]$m.Groups[2].Value
$noFactory  = [int]$m.Groups[3].Value
$failCount  = [int]$m.Groups[4].Value

"타입 $types · 직렬화 $serialized · 팩토리없음 $noFactory · 실패 $failCount"

$hard = @()
if ($serialized -lt $MinSerialized) {
    $hard += "직렬화 타입이 $serialized 개 — 최소 $MinSerialized 미달. 등록이 무너졌거나 검사가 눈을 감았다"
}
if ($failCount -gt 0) {
    $hard += "직렬화 실패 $failCount 건 — 덤프의 __failed__ 절 참조: $outYaml"
}
if (-not (Test-Path $outYaml)) {
    $hard += "덤프 파일이 없다: $outYaml"
}

# 5 — perf 수치는 판정하지 않고 기록만 한다. 시간은 머신·부하에 흔들리므로
# 문턱을 걸면 검사가 거짓 실패로 신뢰를 잃는다. 기준선 표는 사람이 옮겨 적는다.
$perf = [regex]::Match($text, '\[perf\.reflect\] [^\r\n]+')
if ($perf.Success) { $perf.Value } else { "perf.reflect 기록 없음 (판정 아님)" }

if ($hard.Count -gt 0) {
    foreach ($h in $hard) { "실패: $h" }
    exit 1
}

# 가변 필드 정규화. m_instanceID(컴포넌트 인스턴스 GUID)와 BT 그래프의
# ID/ParentID(노드 발급 ID)는 생성 시마다 새로 발급되는 값이라 실행마다 다르다
# — 값이 아니라 "필드가 그 자리에 있다"가 계약이므로 자리만 대조한다.
# 실측: 정규화 없이 돌리면 이 세 키만 어긋나고 나머지 876줄은 동일했다.
function Normalize-Dump([string]$s) {
    $s = $s -replace "`r`n", "`n"
    return [regex]::Replace($s,
        '(?m)^(\s*(?:- )?(?:m_instanceID|ID|ParentID)):\s*\d+\s*$', '$1: <vol>')
}

# 기준선 모드 — 정규화본을 골든으로 저장한다. 원본 그대로 저장하면 재기준선을
# 뜰 때마다 가변 ID가 바뀌어 git diff가 잡음으로 가득해진다.
if ($Baseline) {
    Normalize-Dump (Get-Content $outYaml -Raw) | Set-Content $goldenPath -Encoding UTF8 -NoNewline
    "기준선 저장: $goldenPath (직렬화 $serialized 타입, 가변 ID 정규화됨)"
    exit 0
}

# 4 — 골든 대조.
if (-not (Test-Path $goldenPath)) {
    "골든이 없다 — 먼저 기준선을 뜬다: verify-reflection-golden.ps1 -Baseline"
    exit 1
}

$golden = Normalize-Dump (Get-Content $goldenPath -Raw)
$actual = Normalize-Dump (Get-Content $outYaml -Raw)

if ($golden -eq $actual) {
    "전체 통과 — 골든 diff 0 (직렬화 $serialized 타입)"
    exit 0
}

$goldenLines = $golden -split "`n"
$actualLines = $actual -split "`n"
"실패: 골든과 다르다 (골든 $($goldenLines.Count)줄 vs 실측 $($actualLines.Count)줄)"

# 처음 어긋난 지점을 보여준다 — 전체 diff는 도구로: git diff --no-index
$limit = [Math]::Min($goldenLines.Count, $actualLines.Count)
$shown = 0
for ($i = 0; $i -lt $limit -and $shown -lt 10; $i++) {
    if ($goldenLines[$i] -ne $actualLines[$i]) {
        "  줄 $($i + 1): 골든 '$($goldenLines[$i])' / 실측 '$($actualLines[$i])'"
        $shown++
    }
}
"전체 대조: git diff --no-index `"$goldenPath`" `"$outYaml`""
exit 1
