# 프리팹 왕복 검증 (트랙 P · P0~P2, P2에서 완료).
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
#   5  재로드 후 등록 2개        — ★ 연결이 왕복을 건넜다
#
# 5번이 이 검사의 존재 이유다. P0~P1 시점에는 SceneManager의 재연결 경로가 통째로
# 주석 처리돼 있어 실패하는 것이 정상이었다 — 그때는 이 스크립트가 5번 실패를
# "예상된 실패"로 따로 구분해, 세트를 빨갛게 만들지 않으면서도 침묵하지는 않는
# 절충으로 버텼다. P2가 EntityHandle 기반 재연결(ReconnectPrefabInstance,
# SceneManager.cpp)을 배선하면서 그 절충이 필요 없어졌다 — 이제 등록 복원은 다른
# 4개 항목과 같은 기본(하드) 판정이다.
#
# 실행: pwsh -NoProfile -File Tools\regression\verify-prefab-roundtrip.ps1
param(
    [string]$Exe = "C:\Users\lance\source\CreatorEngine\x64\Debug\Academy_4Q.exe",
    [string]$Work = $env:TEMP,
    [int]$TimeoutSeconds = 300
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

$hard = @()   # 지금 통과해야 하는 항목 — P2부터는 5개 전부 하드 판정이다

if ($before.Registered -ne 0) { $hard += "시작 등록이 0이 아니다($($before.Registered)) — 앞선 씬의 인스턴스가 남았다" }
if ($after.Scene -lt 2)       { $hard += "소환 후 씬 인스턴스가 2개 미만($($after.Scene))" }
if ($after.Registered -lt 2)  { $hard += "소환 후 등록이 2개 미만($($after.Registered))" }
if ($reload.Scene -lt 2)      { $hard += "재로드 후 씬 인스턴스가 2개 미만($($reload.Scene)) — m_prefabFileGuid가 직렬화를 못 건넜다" }
if ($reload.Registered -lt 2) { $hard += "재로드 후 등록이 2개 미만($($reload.Registered)) — 왕복에서 연결이 끊긴다" }

foreach ($h in $hard) { "실패: $h" }

if ($hard.Count -gt 0) { exit 1 }

"전체 통과 — 프리팹 연결이 저장·로드를 건넜다"
exit 0
