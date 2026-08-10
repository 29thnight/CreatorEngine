# 프리팹 왕복 검증 (트랙 P · P0-4).
#
# ── 이 검사가 메우는 구멍 ──
#
# 회귀 세트의 다른 검사는 한 번 띄운 상태만 본다. 저장했다 다시 여는 왕복이 없으므로
# "인스턴스가 프리팹과의 연결을 잃는다"는 종류의 회귀는 세트가 전부 통과해도 드러나지
# 않는다. 화면은 멀쩡하고, 다음에 프리팹을 수정해서 반영이 안 될 때에야 알게 된다.
#
# ── 판정 항목 ──
#
#   1  시작 시 등록 0            — 뒤의 증가가 이 시나리오의 결과임을 보장
#   2  소환 후 씬 인스턴스 2개   — 프리팹이 실제로 소환됐다
#   3  소환 후 등록 2개          — 런타임 연결이 맺어졌다
#   4  재로드 후 씬 인스턴스 2개 — m_prefabFileGuid가 직렬화를 건넜다
#   5  재로드 후 등록 2개        — ★ 연결이 왕복을 건넜다 (현재 실패한다)
#
# 5번이 이 검사의 존재 이유다. P0 시점에는 실패하는 것이 정상이고 — SceneManager의
# 재연결 경로가 통째로 주석 처리되어 있다 — P2에서 초록이 되는 것이 트랙 P의 완료
# 신호다. 그래서 이 스크립트는 5번 실패를 "예상된 실패"로 구분해 보고한다.
#
# 실행: pwsh -NoProfile -File Tools\regression\verify-prefab-roundtrip.ps1
param(
    [string]$Exe = "C:\Users\lance\source\CreatorEngine\x64\Debug\Academy_4Q.exe",
    [string]$Work = $env:TEMP,
    [int]$TimeoutSeconds = 300,
    # P2 완료 후 이 스위치를 빼고 기본 동작을 '엄격'으로 바꾼다.
    [switch]$Strict
)

$exeDir = [System.IO.Path]::GetDirectoryName($Exe)
if (-not (Test-Path $Exe)) { "실행 파일이 없다: $Exe"; exit 1 }

$template = Join-Path $PSScriptRoot "prefab_roundtrip.txt"
if (-not (Test-Path $template)) { "시나리오가 없다: $template"; exit 1 }

# 씬 경로를 실행 직전에 채운다. 절대 경로를 시나리오에 박으면 다른 작업 폴더에서
# 조용히 실패하고, 그 실패는 종료 코드가 아니라 '기록 0건'으로만 나타난다.
$scenePath = (Join-Path $Work "PrefabRoundTrip.creator") -replace '\\', '/'
if (Test-Path $scenePath) { Remove-Item $scenePath -Force }

$scenario = Join-Path $Work "prefab_roundtrip_resolved.txt"
(Get-Content $template -Raw) -replace '\{\{SCENE_PATH\}\}', $scenePath |
    Set-Content $scenario -Encoding UTF8

$outFile = Join-Path $Work "prefab_roundtrip.out"
$errFile = Join-Path $Work "prefab_roundtrip.err"

$proc = Start-Process -FilePath $Exe -ArgumentList @("--script", $scenario) `
    -WorkingDirectory $exeDir `
    -RedirectStandardOutput $outFile -RedirectStandardError $errFile -PassThru
$proc.WaitForExit($TimeoutSeconds * 1000) | Out-Null
if (-not $proc.HasExited) { $proc.Kill(); "TIMEOUT"; exit 1 }

if (-not (Test-Path $outFile)) { "출력이 없다: $outFile"; exit 1 }
$text = Get-Content -LiteralPath $outFile -Raw

# prefab.status 세 번의 출력을 순서대로 집는다.
$matches = [regex]::Matches($text,
    '\[prefab\.status\]\s*씬 인스턴스 (\d+)개 · 등록 (\d+)개 · 캐시 (\d+)개')

if ($matches.Count -lt 3) {
    "prefab.status 기록이 3회여야 하는데 $($matches.Count)회다 — 시나리오가 도중에 멈췄다"
    "출력: $outFile"
    exit 1
}

function Get-Stat([int]$i) {
    [pscustomobject]@{
        Scene      = [int]$matches[$i].Groups[1].Value
        Registered = [int]$matches[$i].Groups[2].Value
        Cached     = [int]$matches[$i].Groups[3].Value
    }
}

$before = Get-Stat 0
$after  = Get-Stat 1
$reload = Get-Stat 2

"시작     씬 $($before.Scene) · 등록 $($before.Registered) · 캐시 $($before.Cached)"
"소환 후  씬 $($after.Scene) · 등록 $($after.Registered) · 캐시 $($after.Cached)"
"재로드 후 씬 $($reload.Scene) · 등록 $($reload.Registered) · 캐시 $($reload.Cached)"
""

$hard = @()   # 지금 통과해야 하는 항목
$known = @()  # P2까지 실패가 예상된 항목

if ($before.Registered -ne 0) { $hard += "시작 등록이 0이 아니다($($before.Registered)) — 앞선 씬의 인스턴스가 남았다" }
if ($after.Scene -lt 2)       { $hard += "소환 후 씬 인스턴스가 2개 미만($($after.Scene))" }
if ($after.Registered -lt 2)  { $hard += "소환 후 등록이 2개 미만($($after.Registered))" }
if ($reload.Scene -lt 2)      { $hard += "재로드 후 씬 인스턴스가 2개 미만($($reload.Scene)) — m_prefabFileGuid가 직렬화를 못 건넜다" }

if ($reload.Registered -lt 2) { $known += "재로드 후 등록이 2개 미만($($reload.Registered)) — 왕복에서 연결이 끊긴다(P-a)" }

foreach ($h in $hard)  { "실패: $h" }
foreach ($k in $known) { "예상된 실패(P2에서 해소): $k" }

if ($hard.Count -gt 0) { exit 1 }

if ($known.Count -gt 0) {
    if ($Strict) { "엄격 모드 — 예상된 실패도 실패로 센다"; exit 1 }
    # P0~P1 동안에는 여기로 온다. 세트를 빨갛게 만들지 않되 침묵하지도 않는다 —
    # 침묵하면 P2에서 고쳐야 할 것이 있다는 사실 자체가 잊힌다.
    "부분 통과 — 왕복 전 연결은 정상이고, 왕복 후 복원만 미구현이다(트랙 P의 P2)"
    exit 0
}

"전체 통과 — 프리팹 연결이 저장·로드를 건넜다"
exit 0
